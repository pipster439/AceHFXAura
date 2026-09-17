#include "aura/aura_adapter.h"
#include "aura/hal_compat.h"
#include "utils/logger.h"
#include "utils/system_info.h"
#include <filesystem>
#include <thread>
#include <sstream>
#include <iomanip>

namespace aura {

namespace {
std::string FormatHex(HRESULT hr) {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << static_cast<unsigned long>(hr);
    return oss.str();
}
} // namespace

AuraAdapter::AuraAdapter(bool dry_run)
    : dry_run_(dry_run),
      state_(AdapterState::Uninitialized),
      hHalMod_(nullptr),
      pFactory_(nullptr),
      pHal_(nullptr),
      pDev_(nullptr),
      fn_set_single_(nullptr),
      last_reconnect_attempt_(std::chrono::steady_clock::now()),
      failed_push_count_(0),
      current_reconnect_interval_ms_(1500),
      reconnect_attempts_(0) {}

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

bool ApplyAacDriverPatch(HMODULE hHalMod) {
    if (!hHalMod) {
        hHalMod = GetModuleHandleW(L"AacKbHal_x64.dll");
    }
    if (!hHalMod) {
        return false;
    }

    // 严禁对未识别的 DLL 盲目打补丁（遵守 Gate 安全基准：禁止散落硬编码 offset，禁止模糊签名搜索）
    const HalVersionInfo* matched_version = nullptr;
    wchar_t mod_path[MAX_PATH] = {0};
    if (GetModuleFileNameW(hHalMod, mod_path, MAX_PATH)) {
        HalGateResult file_gate = ValidateHalFileGate(mod_path);
        if (file_gate.IsSupported()) {
            matched_version = file_gate.matched_version;
        }
    }

    if (!matched_version) {
        // 若无法从文件确认，匹配集中管理的已验证版本表中的内存特征
        for (const auto& v : GetVerifiedHalVersions()) {
            HalGateResult mod_gate = ValidateHalModuleGate(hHalMod, &v);
            if (mod_gate.IsSupported()) {
                matched_version = &v;
                break;
            }
        }
    }

    if (!matched_version) {
        LOG_ERROR("ApplyAacDriverPatch: 拒绝为未通过兼容性 Gate 校验的模块应用内存补丁 (Fail-closed)");
        return false;
    }

    uint8_t* base = reinterpret_cast<uint8_t*>(hHalMod);
    DWORD rva_logger = matched_version->rva_logger;
    DWORD rva_enable = matched_version->rva_enable;

    // Fast-path: Check if already patched
    if (*(base + rva_logger) == 0xC3 && *reinterpret_cast<const uint32_t*>(base + rva_enable) == 0) {
        return true;
    }

    bool patched_any = false;

    // 1. Zero data segment flag: *reinterpret_cast<uint32_t*>(base + rva_enable) = 0;
    DWORD old_protect_data = 0;
    if (VirtualProtect(base + rva_enable, sizeof(uint32_t), PAGE_READWRITE, &old_protect_data)) {
        *reinterpret_cast<uint32_t*>(base + rva_enable) = 0;
        VirtualProtect(base + rva_enable, sizeof(uint32_t), old_protect_data, &old_protect_data);
        patched_any = true;
    } else {
        LOG_WARN("ApplyAacDriverPatch: VirtualProtect(EnableLog) 失败，错误码: " + std::to_string(GetLastError()));
    }

    // 2. Short-circuit function entry point: Write 0xC3 (ret) at Logger::Log via VirtualProtect + FlushInstructionCache
    DWORD old_protect_code = 0;
    if (VirtualProtect(base + rva_logger, 1, PAGE_EXECUTE_READWRITE, &old_protect_code)) {
        *(base + rva_logger) = 0xC3; // ret
        VirtualProtect(base + rva_logger, 1, old_protect_code, &old_protect_code);
        FlushInstructionCache(GetCurrentProcess(), base + rva_logger, 1);
        patched_any = true;
    } else {
        LOG_WARN("ApplyAacDriverPatch: VirtualProtect(Logger::Log) 失败，错误码: " + std::to_string(GetLastError()));
    }

    if (patched_any) {
        LOG_INFO("ApplyAacDriverPatch: 已成功对 AacKbHal_x64.dll 应用内存防崩补丁 (Logger::Log RVA " + 
                 FormatHex(rva_logger) + ", EnableLog RVA " + FormatHex(rva_enable) + ")");
    }
    return patched_any;
}

std::wstring ResolveInprocServerDllPath(
    const std::wstring& clsid_text,
    RegistryQueryFn query_fn)
{
    if (clsid_text.empty()) return L"";

    if (!query_fn) {
        query_fn = [](HKEY root, const std::wstring& subkey) -> std::wstring {
            wchar_t path[MAX_PATH] = {0};
            DWORD pathSize = sizeof(path);
            HKEY hKey = nullptr;
            if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                RegQueryValueExW(hKey, nullptr, nullptr, nullptr, reinterpret_cast<LPBYTE>(path), &pathSize);
                RegCloseKey(hKey);
            }
            return std::wstring(path);
        };
    }

    // 1. HKLM 注册表路径优先尝试
    std::wstring subkey_hklm = L"SOFTWARE\\Classes\\CLSID\\" + clsid_text + L"\\InprocServer32";
    std::wstring path = query_fn(HKEY_LOCAL_MACHINE, subkey_hklm);
    if (!path.empty()) {
        return path;
    }

    // 2. HKLM miss 时回退至 HKCR (使用同一份安全深拷贝的 clsid_text)
    std::wstring subkey_hkcr = L"CLSID\\" + clsid_text + L"\\InprocServer32";
    return query_fn(HKEY_CLASSES_ROOT, subkey_hkcr);
}

static std::wstring GetComServerDllPath(REFCLSID clsid) {
    LPOLESTR clsidStr = nullptr;
    if (FAILED(StringFromCLSID(clsid, &clsidStr)) || !clsidStr) {
        return L"";
    }
    // 关键修复：在释放 COM 分配的内存前显式深拷贝至 std::wstring，
    // 后续无论是 HKLM 还是 HKCR fallback 均统一使用 clsid_text，彻底消除 UAF。
    std::wstring clsid_text(clsidStr);
    CoTaskMemFree(clsidStr);
    clsidStr = nullptr;

    return aura::ResolveInprocServerDllPath(clsid_text);
}

static LONG CallCreateLedDeviceSafe(PFN_CreateLedDevice fn, void* pHal, FakeVector* pVec) {
    if (!fn || !pHal || !pVec) return -1;
    __try {
        return fn(pHal, pVec);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -2;
    }
}

static void CallReleaseSafe(PFN_Release fn, void* ptr) {
    if (!fn || !ptr) return;
    __try {
        fn(ptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // 捕获硬件释放时的底层访问异常，避免析构时二次崩溃
    }
}

bool AuraAdapter::ConnectHardwareInternal() {
    state_ = AdapterState::Connecting;
    ReleaseHardwareInternal();

    GUID clsid_hal{}, iid_hal{};
    CLSIDFromString(L"{AE9DB4C8-4F2A-4756-9B11-2F6D78C61F1A}", &clsid_hal);
    CLSIDFromString(L"{F2C8D5B4-3854-4325-8A4F-FD7C5072E3BA}", &iid_hal);

    void* hal_ptr = nullptr;

    // 1. 优先尝试从本地路径/驱动目录加载 AacKbHal_x64.dll 并免注册表调用 DllGetClassObject
    if (hHalMod_) {
        // 如果外部已传递模块句柄，验证其文件与内存兼容性 Gate
        wchar_t mod_path[MAX_PATH] = {0};
        if (GetModuleFileNameW(hHalMod_, mod_path, MAX_PATH)) {
            HalGateResult gate = ValidateHalFileGate(mod_path);
            if (!gate.IsSupported()) {
                LOG_ERROR("ASUS HAL 既有模块文件 Gate 拦截:\n" + FormatHalGateError(gate));
                hHalMod_ = nullptr;
            } else {
                HalGateResult mod_gate = ValidateHalModuleGate(hHalMod_, gate.matched_version);
                if (!mod_gate.IsSupported()) {
                    LOG_ERROR("ASUS HAL 既有模块内存签名 Gate 拦截:\n" + FormatHalGateError(mod_gate));
                    hHalMod_ = nullptr;
                }
            }
        }
    }

    if (!hHalMod_) {
        std::vector<std::filesystem::path> candidates;
        wchar_t mod_path[MAX_PATH];
        if (GetModuleFileNameW(nullptr, mod_path, MAX_PATH)) {
            std::filesystem::path exe_dir = std::filesystem::path(mod_path).parent_path();
            candidates.push_back(exe_dir / "AacKbHal_x64.dll");
            candidates.push_back(exe_dir / "drivers" / "AacKbHal_x64.dll");
            candidates.push_back(exe_dir / ".." / "drivers" / "AacKbHal_x64.dll");
            candidates.push_back(exe_dir / ".." / "AacKbHal_x64.dll");
        }
        candidates.push_back(std::filesystem::current_path() / "AacKbHal_x64.dll");
        candidates.push_back(std::filesystem::current_path() / "drivers" / "AacKbHal_x64.dll");
        candidates.push_back(L"C:\\Program Files\\ASUS\\Aac_Keyboard\\AacKbHal_x64.dll");

        std::error_code ec;
        for (const auto& p : candidates) {
            if (std::filesystem::exists(p, ec) && !std::filesystem::is_directory(p, ec)) {
                // 兼容性 Gate 校验第 1 阶段：文件 SHA-256 与版本号必须匹配已验证列表
                HalGateResult gate = ValidateHalFileGate(p.wstring());
                if (!gate.IsSupported()) {
                    LOG_ERROR("ASUS HAL 文件兼容性 Gate 拦截: " + p.string() + "\n" + FormatHalGateError(gate));
                    continue;
                }

                SetDllDirectoryW(p.parent_path().c_str());
                hHalMod_ = LoadLibraryW(p.c_str());
                if (hHalMod_) {
                    // 兼容性 Gate 校验第 2 阶段：内存中关键 runtime signature 二次校验
                    HalGateResult mod_gate = ValidateHalModuleGate(hHalMod_, gate.matched_version);
                    if (!mod_gate.IsSupported()) {
                        LOG_ERROR("ASUS HAL 模块内存签名 Gate 拦截: " + p.string() + "\n" + FormatHalGateError(mod_gate));
                        FreeLibrary(hHalMod_);
                        hHalMod_ = nullptr;
                        continue;
                    }
                    LOG_INFO("成功加载并通过兼容性 Gate 验证底层驱动库: " + p.string() + " (v" + gate.file_version + ")");
                    break;
                }
            }
        }
    }

    if (hHalMod_) {
        ApplyAacDriverPatch(hHalMod_);
        using PFN_DllGetClassObject = HRESULT (__stdcall *)(REFCLSID, REFIID, LPVOID*);
        PFN_DllGetClassObject fn_get_class_obj = reinterpret_cast<PFN_DllGetClassObject>(
            GetProcAddress(hHalMod_, "DllGetClassObject")
        );
        if (fn_get_class_obj) {
            if (pFactory_) {
                pFactory_->Release();
                pFactory_ = nullptr;
            }
            HRESULT hr_fac = fn_get_class_obj(clsid_hal, IID_IClassFactory, reinterpret_cast<void**>(&pFactory_));
            if (SUCCEEDED(hr_fac) && pFactory_) {
                HRESULT hr_inst = pFactory_->CreateInstance(nullptr, iid_hal, &hal_ptr);
                if (SUCCEEDED(hr_inst) && hal_ptr) {
                    LOG_INFO("成功通过免注册 COM (DllGetClassObject) 实例化 CLSID_ClaymoreHal");
                } else {
                    LOG_WARN("IClassFactory::CreateInstance 失败: " + FormatHex(hr_inst));
                }
            } else {
                LOG_WARN("DllGetClassObject 获取工厂失败: " + FormatHex(hr_fac));
            }
        }
    }

    // 2. 若免注册加载未成功，回退至系统 COM 注册表解析 (兼容已安装奥创的标准环境)
    if (!hal_ptr) {
        if (hHalMod_) {
            FreeLibrary(hHalMod_);
            hHalMod_ = nullptr;
        }
        LOG_INFO("尝试通过系统注册表 CoCreateInstance 创建 CLSID_ClaymoreHal 实例...");
        std::wstring regDllPath = GetComServerDllPath(clsid_hal);
        if (regDllPath.empty()) {
            regDllPath = L"C:\\Program Files\\ASUS\\Aac_Keyboard\\AacKbHal_x64.dll";
        }
        HMODULE hSysPre = nullptr;
        if (!regDllPath.empty() && std::filesystem::exists(regDllPath)) {
            HalGateResult gate = ValidateHalFileGate(regDllPath);
            if (!gate.IsSupported()) {
                LOG_ERROR("ASUS HAL 注册表目标 DLL 兼容性 Gate 拦截:\n" + FormatHalGateError(gate));
                state_ = AdapterState::Disconnected;
                return false;
            }
            hSysPre = LoadLibraryW(regDllPath.c_str());
            if (hSysPre) {
                HalGateResult mod_gate = ValidateHalModuleGate(hSysPre, gate.matched_version);
                if (!mod_gate.IsSupported()) {
                    LOG_ERROR("ASUS HAL 注册表目标模块签名 Gate 拦截:\n" + FormatHalGateError(mod_gate));
                    FreeLibrary(hSysPre);
                    state_ = AdapterState::Disconnected;
                    return false;
                }
                ApplyAacDriverPatch(hSysPre);
                hHalMod_ = hSysPre;
            }
        } else {
            LOG_ERROR("未找到任何受支持的 ASUS HAL 驱动组件，启动硬件控制中止 (Fail-closed)");
            state_ = AdapterState::Disconnected;
            return false;
        }

        HRESULT hr = CoCreateInstance(
            clsid_hal,
            nullptr,
            CLSCTX_INPROC_SERVER,
            iid_hal,
            &hal_ptr
        );
        if (hHalMod_) {
            ApplyAacDriverPatch(hHalMod_);
        }
        if (FAILED(hr) || !hal_ptr) {
            LOG_WARN("CoCreateInstance(CLSID_ClaymoreHal) 失败: " + FormatHex(hr) + " (驱动未就绪或未找到硬件组件)");
            ReleaseHardwareInternal();
            if (hHalMod_) {
                FreeLibrary(hHalMod_);
                hHalMod_ = nullptr;
            }
            state_ = AdapterState::Disconnected;
            return false;
        }
        LOG_INFO("成功通过系统注册表 CoCreateInstance 实例化 CLSID_ClaymoreHal");
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
    LONG create_res = CallCreateLedDeviceSafe(fn_create_dev, pHal_, &vec);
    if (create_res == -2) {
        LOG_ERROR("FATAL: CreateLedDevice 执行时触发底层硬件访问异常 (SEH)，已安全拦截！");
        ReleaseHardwareInternal();
        state_ = AdapterState::Error;
        return false;
    }
    (void)create_res;

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
            current_reconnect_interval_ms_ = 1500;
            reconnect_attempts_ = 0;
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

    // 推送数帧全黑确保硬件彻底消光 (通过 PushFrame 映射安全硬件隔离寻址表)
    for (int f = 0; f < 5; ++f) {
        if (!PushFrame(black)) {
            LOG_WARN("硬件状态重置推流失败");
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
    
    // 指数退避重试：初始 1.5 秒，连续失败则按 1.5s -> 3s -> 6s -> 12s -> 24s -> 60s 递增
    if (static_cast<uint64_t>(elapsed) >= current_reconnect_interval_ms_) {
        last_reconnect_attempt_ = now;
        ++reconnect_attempts_;
        LOG_INFO("正在尝试重新连接 ROG FALCHION ACE HFX 硬件 (第 " + std::to_string(reconnect_attempts_) + 
                 " 次重试，当前退避间隔: " + std::to_string(current_reconnect_interval_ms_) + "ms)...");
        bool ok = ConnectHardwareInternal();
        if (ok) {
            LOG_INFO("[+] ROG FALCHION ACE HFX 硬件重新连接成功！");
            current_reconnect_interval_ms_ = 1500;
            reconnect_attempts_ = 0;
            return true;
        } else {
            current_reconnect_interval_ms_ = std::min<uint64_t>(current_reconnect_interval_ms_ * 2, 60000);
            return false;
        }
    }

    return false;
}

void AuraAdapter::ReleaseHardwareInternal() {
    if (pDev_) {
        void** dev_vtable = *reinterpret_cast<void***>(pDev_);
        if (dev_vtable) {
            PFN_Release fn_rel = reinterpret_cast<PFN_Release>(dev_vtable[VTABLE_DEV_RELEASE]);
            CallReleaseSafe(fn_rel, pDev_);
        }
        pDev_ = nullptr;
    }

    if (pHal_) {
        void** hal_vtable = *reinterpret_cast<void***>(pHal_);
        if (hal_vtable) {
            PFN_Release fn_rel = reinterpret_cast<PFN_Release>(hal_vtable[VTABLE_HAL_RELEASE]);
            CallReleaseSafe(fn_rel, pHal_);
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
    if (hHalMod_) {
        FreeLibrary(hHalMod_);
        hHalMod_ = nullptr;
    }

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
