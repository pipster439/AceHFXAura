#include "aura/aura_adapter.h"
#include "utils/logger.h"
#include "utils/system_info.h"
#include <thread>

namespace aura {



AuraAdapter::AuraAdapter(bool dry_run)
    : dry_run_(dry_run),
      state_(AdapterState::Uninitialized),
      hHalMod_(nullptr),
      pFactory_(nullptr),
      pHal_(nullptr),
      pDev_(nullptr),
      fn_set_single_(nullptr),
      last_reconnect_attempt_(std::chrono::steady_clock::now()),
      failed_push_count_(0) {}

AuraAdapter::~AuraAdapter() {
    Shutdown();
}

bool AuraAdapter::BuildPaddedHardwareTable(const Keymap* keymap) {
    std::vector<uint8_t> calibrated_ids;
    if (keymap) {
        for (const auto& [name, info] : keymap->GetAllKeys()) {
            if (info.led_id >= 0 && info.led_id < static_cast<int>(TOTAL_LEDS)) {
                calibrated_ids.push_back(static_cast<uint8_t>(info.led_id));
            }
        }
    }

    if (calibrated_ids.empty()) {
        Keymap fallback;
        if (fallback.LoadFromJson("calibrated_keymap.json")) {
            for (const auto& [name, info] : fallback.GetAllKeys()) {
                if (info.led_id >= 0 && info.led_id < static_cast<int>(TOTAL_LEDS)) {
                    calibrated_ids.push_back(static_cast<uint8_t>(info.led_id));
                }
            }
        }
    }

    std::sort(calibrated_ids.begin(), calibrated_ids.end());
    calibrated_ids.erase(std::unique(calibrated_ids.begin(), calibrated_ids.end()), calibrated_ids.end());

    padded_hardware_table_.clear();
    for (uint8_t kid : calibrated_ids) {
        // USB 64-byte HID packet boundary protection:
        // A 64-byte HID report packs 15 keys (4 bytes each + 4-byte header).
        // Slot 14 (the 15th key) occupies bytes 60..63, which sits on the USB transfer boundary
        // and causes the Blue subpixel (byte 63) to flicker due to hardware FIFO delimiting.
        // We isolate slot 14 with a dummy padding key (0xFF) so no real physical key ever touches byte 63!
        if (padded_hardware_table_.size() % 15 == 14) {
            padded_hardware_table_.push_back(DUMMY_PADDING_LED_ID);
        }
        padded_hardware_table_.push_back(kid);
    }

    // 上界校验：这是防止 PushFrame 与驱动表写入越界的关键闸门。
    // 原实现直接按表长写入 216 字节的 stream_buffer_，68 键时恰好占满、零余量，
    // 只要键位表出现第 69 个唯一 led_id 就会越界 3 字节。
    if (padded_hardware_table_.size() > MAX_HARDWARE_STREAM_KEYS) {
        LOG_ERROR("FATAL: 隔离寻址表条目 " + std::to_string(padded_hardware_table_.size()) +
                  " 超过安全上界 " + std::to_string(MAX_HARDWARE_STREAM_KEYS) +
                  "（原始键位数 " + std::to_string(calibrated_ids.size()) + "）。"
                  "拒绝继续，避免越界写坏推流缓冲与驱动对象内存。");
        padded_hardware_table_.clear();
        return false;
    }

    LOG_INFO("构建安全隔离硬件寻址表完成: 总条目 " + std::to_string(padded_hardware_table_.size()) + 
             " (含 " + std::to_string(calibrated_ids.size()) + 
             " 物理键位 + USB 64字节边界隔离槽；安全上界 " + std::to_string(MAX_HARDWARE_STREAM_KEYS) + ")");
    return true;
}

bool AuraAdapter::Initialize(const Keymap* keymap) {
    LOG_INFO("AuraAdapter::Initialize: 前置契约——调用方已完成 COM 初始化");
    keymap_ = keymap;
    if (!BuildPaddedHardwareTable(keymap_)) {
        // 表长超上界：必须以失败告终，不能用可能越界的表去驱动硬件
        state_ = AdapterState::Error;
        return false;
    }

    if (dry_run_) {
        LOG_INFO("[Dry-Run] AuraAdapter 初始化完成 (虚拟硬件模式，不挂载实际 DLL)");
        state_ = AdapterState::Connected;
        return true;
    }

    return ConnectHardwareInternal();
}

bool AuraAdapter::ConnectHardwareInternal() {
    state_ = AdapterState::Connecting;
    ReleaseHardwareInternal();

    GUID clsid_hal{}, iid_hal{};
    CLSIDFromString(L"{AE9DB4C8-4F2A-4756-9B11-2F6D78C61F1A}", &clsid_hal);
    CLSIDFromString(L"{F2C8D5B4-3854-4325-8A4F-FD7C5072E3BA}", &iid_hal);

    LOG_INFO("正在尝试创建 CLSID_ClaymoreHal COM 实例...");

    void* hal_ptr = nullptr;
    HRESULT hr = CoCreateInstance(
        clsid_hal,
        nullptr,
        CLSCTX_INPROC_SERVER,
        iid_hal,
        &hal_ptr
    );

    if (FAILED(hr) || !hal_ptr) {
        LOG_WARN("CoCreateInstance(CLSID_ClaymoreHal) 失败: 0x" + std::to_string(hr));
        state_ = AdapterState::Disconnected;
        return false;
    }

    pHal_ = hal_ptr;
    void** hal_vtable = *reinterpret_cast<void***>(pHal_);
    if (!hal_vtable) {
        LOG_ERROR("HAL 虚表指针为空");
        ReleaseHardwareInternal();
        state_ = AdapterState::Disconnected;
        return false;
    }

    // 1. CreateLedDevice() with strictly validated preallocated storage
    LOG_INFO("[Connect Step 6] Calling CreateLedDevice...");
    constexpr size_t PREALLOC_CAPACITY = 64;
    void* dev_storage[PREALLOC_CAPACITY] = {nullptr};

    FakeVector vec;
    vec.first = dev_storage;
    vec.last = dev_storage;
    vec.end = dev_storage + PREALLOC_CAPACITY;

    PFN_CreateLedDevice fn_create_dev = reinterpret_cast<PFN_CreateLedDevice>(hal_vtable[VTABLE_HAL_CREATE_LED_DEVICE]);
    LONG create_res = fn_create_dev(pHal_, &vec);

    // Memory safety validation
    if (vec.first != dev_storage || vec.last < vec.first || vec.last > vec.end) {
        LOG_ERROR("FATAL: CreateLedDevice 破坏了预分配向量边界！假设不成立，立即中止。");
        ReleaseHardwareInternal();
        state_ = AdapterState::Error;
        return false;
    }

    size_t dev_count = static_cast<size_t>(vec.last - vec.first);
    LOG_INFO("CreateLedDevice 执行完成，检测到设备数量: " + std::to_string(dev_count));

    if (dev_count == 0 || dev_storage[0] == nullptr) {
        LOG_WARN("未在 HAL 中枚举到 ROG FALCHION ACE HFX 硬件 (可能未插拔或拨码切至另一台PC)");
        ReleaseHardwareInternal();
        state_ = AdapterState::Disconnected;
        return false;
    }

    pDev_ = dev_storage[0];
    void** dev_vtable = *reinterpret_cast<void***>(pDev_);
    if (!dev_vtable) {
        LOG_ERROR("设备对象虚表指针为空");
        ReleaseHardwareInternal();
        state_ = AdapterState::Disconnected;
        return false;
    }

    // 2. 挂载安全隔离硬件寻址表
    if (padded_hardware_table_.empty()) {
        if (!BuildPaddedHardwareTable(keymap_)) {
            ReleaseHardwareInternal();
            state_ = AdapterState::Error;
            return false;
        }
    }

    // 上界保护：底层闭源 COM 组件为设备预留的真实缓冲区长度不可知，
    // 故以上界校验替代"盲目信任表长"，避免把表写穿对象内部内存。
    const size_t table_entries = padded_hardware_table_.size();
    if (table_entries > MAX_HARDWARE_STREAM_KEYS) {
        LOG_ERROR("FATAL: 硬件寻址表条目 " + std::to_string(table_entries) +
                  " 超过安全上界 " + std::to_string(MAX_HARDWARE_STREAM_KEYS) + "，拒绝写入驱动");
        ReleaseHardwareInternal();
        state_ = AdapterState::Error;
        return false;
    }

    *reinterpret_cast<DWORD*>(reinterpret_cast<uint8_t*>(pDev_) + 0x6C) = static_cast<DWORD>(table_entries);
    uint8_t* led_table = reinterpret_cast<uint8_t*>(pDev_) + 0x74;
    for (size_t i = 0; i < table_entries; ++i) {
        led_table[i] = padded_hardware_table_[i];
    }

    // 3. 提取 Set_L_STD_SINGLE_XY (VTable[19])
    fn_set_single_ = reinterpret_cast<PFN_SetSingle>(dev_vtable[VTABLE_DEV_SET_SINGLE]);
    if (!fn_set_single_) {
        LOG_ERROR("未找到 Set_L_STD_SINGLE_XY 虚函数 (VTable[19])");
        ReleaseHardwareInternal();
        state_ = AdapterState::Error;
        return false;
    }

    state_ = AdapterState::Connected;
    failed_push_count_ = 0;
    LOG_INFO("[+] 成功获取硬件控制权，ROG FALCHION ACE HFX 安全隔离驱动通道已就绪！");
    return true;
}

LONG AuraAdapter::CallSetSingleSafe(void* pDev, void* buffer) {
    if (!fn_set_single_ || !pDev) return -1;
    __try {
        return fn_set_single_(pDev, buffer);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

bool AuraAdapter::PushFrame(const FrameBuffer& frame) {
    if (dry_run_) {
        dry_run_frame_count_++;
        if (dry_run_frame_count_ % 25 == 1) { // 约每秒打印一次当前计算出的帧特征
            std::ostringstream oss;
            size_t non_zero_count = 0;
            for (size_t i = 0; i < TOTAL_LEDS; ++i) {
                if (frame.buffer[i * 3 + 0] > 0 || frame.buffer[i * 3 + 1] > 0 || frame.buffer[i * 3 + 2] > 0) {
                    non_zero_count++;
                }
            }
            oss << "[Dry-Run 效果帧 #" << dry_run_frame_count_ << "] 活跃通道数: " 
                << non_zero_count << "/" << TOTAL_LEDS << " | 典型通道色彩: ";
            int shown = 0;
            for (size_t i = 0; i < TOTAL_LEDS && shown < 4; ++i) {
                int r = frame.buffer[i * 3 + 0];
                int g = frame.buffer[i * 3 + 1];
                int b = frame.buffer[i * 3 + 2];
                if (r > 0 || g > 0 || b > 0) {
                    oss << "ID " << i << "->RGB(" << r << "," << g << "," << b << ") ";
                    shown++;
                }
            }
            LOG_INFO(oss.str());
        }
        return true;
    }

    if (state_ != AdapterState::Connected || !pDev_ || !fn_set_single_) {
        return false;
    }

    // Translate frame buffer (indexed by led_id) to hardware stream buffer (padded isolated slots)
    // 纵深防御：正常路径下 BuildPaddedHardwareTable 已拒绝超长表，此处再兜一次，
    // 确保 stream_buffer_ 的索引永远落在 sizeof(stream_buffer_) 之内。
    if (padded_hardware_table_.size() * RGB_CHANNELS > sizeof(stream_buffer_)) {
        LOG_ERROR("FATAL: 硬件寻址表长度 " + std::to_string(padded_hardware_table_.size()) +
                  " 超出推流缓冲区容量 " + std::to_string(sizeof(stream_buffer_) / RGB_CHANNELS) +
                  " 槽，已丢弃本帧以避免越界写");
        return false;
    }
    std::memset(stream_buffer_, 0, sizeof(stream_buffer_));
    for (size_t i = 0; i < padded_hardware_table_.size(); ++i) {
        uint8_t lid = padded_hardware_table_[i];
        if (lid != DUMMY_PADDING_LED_ID && lid < TOTAL_LEDS) {
            stream_buffer_[i * 3 + 0] = frame.buffer[lid * 3 + 0];
            stream_buffer_[i * 3 + 1] = frame.buffer[lid * 3 + 1];
            stream_buffer_[i * 3 + 2] = frame.buffer[lid * 3 + 2];
        }
    }

    LONG res = CallSetSingleSafe(pDev_, stream_buffer_);
    if (res < 0) {
        failed_push_count_++;
        if (failed_push_count_ >= 3) {
            LOG_WARN("Set_L_STD_SINGLE_XY 连续推流异常 (" + std::to_string(failed_push_count_) + 
                     " 次，错误码: " + std::to_string(res) + ")，判定硬件连接断开");
            state_ = AdapterState::Disconnected;
            ReleaseHardwareInternal();
            last_reconnect_attempt_ = std::chrono::steady_clock::now();
            return false;
        }
    } else {
        failed_push_count_ = 0;
    }

    return true;
}

bool AuraAdapter::ForceReset() {
    LOG_INFO("正在执行硬件状态强制复位...");
    if (dry_run_) {
        LOG_INFO("[Dry-Run] 强制复位完成 (全黑底色)");
        return true;
    }

    if (state_ != AdapterState::Connected || !pDev_) {
        LOG_WARN("硬件尚未连接，无法执行强制复位");
        return false;
    }

    FrameBuffer black;
    black.Clear();

    // 推送数帧全黑确保硬件彻底消光
    for (int f = 0; f < 5; ++f) {
        LONG res = CallSetSingleSafe(pDev_, const_cast<uint8_t*>(black.Data()));
        if (res < 0) {
            LOG_WARN("硬件状态重置推流失败，错误码: " + std::to_string(res));
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }

    LOG_INFO("[+] 硬件强制复位完成，残余光效已清理");
    return true;
}

bool AuraAdapter::CheckReconnect() {
    if (dry_run_ || state_ == AdapterState::Connected) {
        return true;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_reconnect_attempt_).count();
    
    // 退避重试周期: 1.5 秒
    if (elapsed >= 1500) {
        last_reconnect_attempt_ = now;
        LOG_INFO("正在尝试重新连接 ROG FALCHION ACE HFX 硬件...");
        return ConnectHardwareInternal();
    }

    return false;
}

void AuraAdapter::ReleaseHardwareInternal() {
    if (pDev_) {
        void** dev_vtable = *reinterpret_cast<void***>(pDev_);
        if (dev_vtable) {
            PFN_Release fn_rel = reinterpret_cast<PFN_Release>(dev_vtable[VTABLE_DEV_RELEASE]);
            fn_rel(pDev_);
        }
        pDev_ = nullptr;
    }

    if (pHal_) {
        void** hal_vtable = *reinterpret_cast<void***>(pHal_);
        if (hal_vtable) {
            PFN_Release fn_rel = reinterpret_cast<PFN_Release>(hal_vtable[VTABLE_HAL_RELEASE]);
            fn_rel(pHal_);
        }
        pHal_ = nullptr;
    }

    if (pFactory_) {
        pFactory_->Release();
        pFactory_ = nullptr;
    }

    fn_set_single_ = nullptr;
}

void AuraAdapter::Shutdown() {
    if (state_ == AdapterState::Uninitialized) {
        return;
    }

    if (dry_run_) {
        state_ = AdapterState::Uninitialized;
        return;
    }

    if (state_ == AdapterState::Connected && pDev_) {
        // Clean blackout before disconnecting
        FrameBuffer black;
        black.Clear();
        PushFrame(black);
    }

    ReleaseHardwareInternal();

    state_ = AdapterState::Uninitialized;
    LOG_INFO("[+] AuraAdapter 已安全关闭并释放所有 COM 资源");
}

bool AuraAdapter::RunInitStressTest(size_t iterations) {
    LOG_INFO("==========================================================");
    LOG_INFO("开始执行 CreateLedDevice 内存安全与初始化循环压力测试");
    LOG_INFO("测试目标: 连续执行 " + std::to_string(iterations) + " 次完整的 COM 加载/建构/释放循环");
    LOG_INFO("==========================================================");

    size_t success_count = 0;
    size_t failure_count = 0;

    ProcessMemoryStats mem_start{};
    GetCurrentProcessMemory(mem_start);
    LOG_INFO("初始内存占用 - WorkingSet: " + FormatBytes(mem_start.working_set_bytes) + 
             ", PrivateBytes: " + FormatBytes(mem_start.private_bytes));

    for (size_t i = 1; i <= iterations; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();
        bool ok = ConnectHardwareInternal();
        auto t1 = std::chrono::high_resolution_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

        if (ok && pDev_ != nullptr) {
            success_count++;
            if (i % 10 == 0 || i == iterations || i == 1) {
                ProcessMemoryStats mem_cur{};
                GetCurrentProcessMemory(mem_cur);
                LOG_INFO("[Cycle " + std::to_string(i) + "/" + std::to_string(iterations) + "] 成功! " +
                         "耗时: " + std::to_string(us) + " us, WorkingSet: " + FormatBytes(mem_cur.working_set_bytes) +
                         ", PrivateBytes: " + FormatBytes(mem_cur.private_bytes));
            }
        } else {
            failure_count++;
            LOG_ERROR("[Cycle " + std::to_string(i) + "] 失败!");
        }

        ReleaseHardwareInternal();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    ProcessMemoryStats mem_end{};
    GetCurrentProcessMemory(mem_end);
    LOG_INFO("==========================================================");
    LOG_INFO("压测完成结果: 成功 " + std::to_string(success_count) + " 次, 失败 " + std::to_string(failure_count) + " 次");
    LOG_INFO("最终内存占用 - WorkingSet: " + FormatBytes(mem_end.working_set_bytes) + 
             ", PrivateBytes: " + FormatBytes(mem_end.private_bytes));
    LOG_INFO("==========================================================");

    return failure_count == 0;
}

} // namespace aura
