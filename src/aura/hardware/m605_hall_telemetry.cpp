#include "aura/hardware/m605_hall_telemetry.h"
#include "aura/hardware/m605_key_mapping.h"
#include "aura/native_hid_backend.h"
#include "third_party/json.hpp"
#include "utils/logger.h"

#include <windows.h>
#include <setupapi.h>

extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <fstream>
#include <sstream>

namespace aura::m605::hall {
struct HallCtrlWriter {
    static bool Write(HANDLE handle, const std::array<uint8_t, kHallReportBytes>& report,
                      DWORD timeout_ms = 300) {
        if (handle == INVALID_HANDLE_VALUE) return false;
        // Hard safety guard: never allow bare 04 04 00 (FA_HA_MFG(0) sets mfg_block_keys=1).
        if (report[1] == 0x04 && report[2] == 0x04 && report[3] != 0x01) {
            LOG_ERROR("[M605Hall] Blocked unsafe 04 04 report without 0x01 unblock byte");
            return false;
        }

        std::lock_guard<std::mutex> dev_lock(NativeHidBackend::DeviceWriteMutex());

        HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ev) return false;

        OVERLAPPED ov{};
        ov.hEvent = ev;
        DWORD written = 0;
        BOOL ok = WriteFile(handle, report.data(), static_cast<DWORD>(report.size()), &written, &ov);
        bool success = false;
        if (ok) {
            success = (written == report.size());
        } else if (GetLastError() == ERROR_IO_PENDING) {
            if (WaitForSingleObject(ev, timeout_ms) == WAIT_OBJECT_0) {
                success = GetOverlappedResult(handle, &ov, &written, FALSE) && (written == report.size());
            } else {
                CancelIoEx(handle, &ov);
                WaitForSingleObject(ev, 30);
                GetOverlappedResult(handle, &ov, &written, TRUE);
            }
        }
        CloseHandle(ev);
        return success;
    }
};

namespace {

constexpr bool VerifyMatchesPhysicalKeys() {
    if (kHallKeyTable.size() != aura::m605::detail::kVerifiedPhysicalKeys.size()) return false;
    for (size_t i = 0; i < kHallKeyTable.size(); ++i) {
        if (kHallKeyTable[i].logical_id != aura::m605::detail::kVerifiedPhysicalKeys[i].logical_id ||
            kHallKeyTable[i].wire_id != aura::m605::detail::kVerifiedPhysicalKeys[i].wire_id) {
            return false;
        }
    }
    return true;
}
static_assert(VerifyMatchesPhysicalKeys(), "kHallKeyTable must match kVerifiedPhysicalKeys order and IDs");

double QpcSeconds() {
    static const double kFreqInv = [] {
        LARGE_INTEGER freq{};
        QueryPerformanceFrequency(&freq);
        return freq.QuadPart > 0 ? 1.0 / static_cast<double>(freq.QuadPart) : 0.0;
    }();
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) * kFreqInv;
}

uint64_t QpcMicroseconds() {
    static const LARGE_INTEGER kFreq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    if (kFreq.QuadPart <= 0) return 0;
    return static_cast<uint64_t>((now.QuadPart * 1000000LL) / kFreq.QuadPart);
}

std::wstring ToLowerW(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}

struct HidEndpointMatch {
    HANDLE handle = INVALID_HANDLE_VALUE;
    uint16_t in_len = 0;
    uint16_t out_len = 0;
    std::wstring path;
};

HidEndpointMatch OpenM605Endpoint(uint16_t target_usage_page, uint16_t required_in_len,
                                  uint16_t required_out_len, bool need_write,
                                  const wchar_t* path_substring = nullptr) {
    HidEndpointMatch result;
    GUID hid_guid{};
    HidD_GetHidGuid(&hid_guid);

    HDEVINFO dev_info = SetupDiGetClassDevsW(&hid_guid, nullptr, nullptr,
                                             DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (dev_info == INVALID_HANDLE_VALUE) return result;

    SP_DEVICE_INTERFACE_DATA if_data{};
    if_data.cbSize = sizeof(if_data);
    for (DWORD idx = 0; SetupDiEnumDeviceInterfaces(dev_info, nullptr, &hid_guid, idx, &if_data); ++idx) {
        DWORD req_size = 0;
        SetupDiGetDeviceInterfaceDetailW(dev_info, &if_data, nullptr, 0, &req_size, nullptr);
        if (req_size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || req_size > 4096) continue;

        std::vector<uint8_t> detail_buf(req_size);
        auto* detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detail_buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(dev_info, &if_data, detail, req_size, nullptr, nullptr)) {
            continue;
        }
        std::wstring path(detail->DevicePath);
        if (path_substring && ToLowerW(path).find(path_substring) == std::wstring::npos) {
            continue;
        }

        const DWORD access = GENERIC_READ | (need_write ? GENERIC_WRITE : 0);
        HANDLE h = CreateFileW(path.c_str(), access,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;

        HIDD_ATTRIBUTES attrs{};
        attrs.Size = sizeof(attrs);
        bool ok = HidD_GetAttributes(h, &attrs) &&
                  attrs.VendorID == kM605Vid && attrs.ProductID == kM605Pid;

        PHIDP_PREPARSED_DATA preparsed = nullptr;
        HIDP_CAPS caps{};
        if (ok && HidD_GetPreparsedData(h, &preparsed)) {
            ok = (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) &&
                 caps.UsagePage == target_usage_page &&
                 (required_in_len == 0 || caps.InputReportByteLength == required_in_len) &&
                 (required_out_len == 0 || caps.OutputReportByteLength == required_out_len);
            HidD_FreePreparsedData(preparsed);
        } else {
            ok = false;
        }

        if (ok) {
            result.handle = h;
            result.in_len = caps.InputReportByteLength;
            result.out_len = caps.OutputReportByteLength;
            result.path = std::move(path);
            break;
        }
        CloseHandle(h);
    }

    SetupDiDestroyDeviceInfoList(dev_info);
    return result;
}

inline bool WriteOverlappedReport(HANDLE handle, const std::array<uint8_t, kHallReportBytes>& report,
                                  DWORD timeout_ms = 300) {
    return HallCtrlWriter::Write(handle, report, timeout_ms);
}

std::array<uint8_t, kHallReportBytes> MakeCtrlReport(std::initializer_list<uint8_t> payload) {
    std::array<uint8_t, kHallReportBytes> rep{};
    rep[0] = 0x00;
    size_t idx = 1;
    for (uint8_t b : payload) {
        if (idx >= rep.size()) break;
        rep[idx++] = b;
    }
    return rep;
}

double Round3(double v) {
    return std::round(v * 1000.0) / 1000.0;
}

std::string FormatMm(double v) {
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "%.2f mm", v);
    return buf;
}

} // namespace

std::optional<size_t> FindKeyIndexByLogicalId(uint16_t logical_id) {
    for (size_t i = 0; i < kHallKeyTable.size(); ++i) {
        if (kHallKeyTable[i].logical_id == logical_id) return i;
    }
    return std::nullopt;
}

std::optional<size_t> FindKeyIndexByAnalogId(uint8_t analog_id) {
    for (size_t i = 0; i < kHallKeyTable.size(); ++i) {
        if (kHallKeyTable[i].analog_id == analog_id) return i;
    }
    return std::nullopt;
}

float ConvertHallRawToDepthMm(uint16_t raw, float idle, float span) {
    if (span <= 1.0f || !std::isfinite(idle) || !std::isfinite(span)) return 0.0f;
    const float mm = (idle - static_cast<float>(raw)) / span * kTravelMm;
    if (mm < 0.0f) return 0.0f;
    if (mm > kTravelMm) return kTravelMm;
    return mm;
}

std::optional<double> MatchHallDepthAtTimestamp(const std::vector<HallSample>& samples,
                                                double edge_ts_sec, double max_delta_sec) {
    if (samples.empty()) return std::nullopt;
    double best_dt = 1e9;
    double best_depth = 0.0;
    for (const auto& s : samples) {
        const double dt = std::abs(s.ts_sec - edge_ts_sec);
        if (dt < best_dt) {
            best_dt = dt;
            best_depth = static_cast<double>(s.depth_mm);
        }
    }
    if (best_dt > max_delta_sec) return std::nullopt;
    return best_depth;
}

VerifyTrialResult EvaluateActuationTrial(double target_mm, double measured_mm, double tolerance_mm) {
    const double err = measured_mm - target_mm;
    const bool ok = std::abs(err) <= tolerance_mm;
    VerifyTrialResult res;
    res.kind = "actuation";
    res.label = "触发点 " + FormatMm(target_mm);
    res.target_mm = Round3(target_mm);
    res.measured_mm = Round3(measured_mm);
    res.error_mm = Round3(err);
    res.passed = ok;
    char buf[96]{};
    std::snprintf(buf, sizeof(buf), "目标 %.1fmm / 实测 %.2fmm (偏差 %+.2fmm)",
                  target_mm, measured_mm, err);
    res.detail = buf;
    return res;
}

std::pair<VerifyTrialResult, VerifyTrialResult> EvaluateRapidTriggerTrial(
    double target_rt_mm, double peak_mm, double rel_mm, double valley_mm, double repress_mm,
    double tolerance_mm) {
    const double rel_delta = peak_mm - rel_mm;
    const double rep_delta = repress_mm - valley_mm;

    VerifyTrialResult rel;
    rel.kind = "rt_release";
    rel.label = "RT 抬起断开 " + FormatMm(target_rt_mm);
    rel.target_mm = Round3(target_rt_mm);
    rel.measured_mm = Round3(rel_delta);
    rel.error_mm = Round3(rel_delta - target_rt_mm);
    rel.passed = std::abs(rel.error_mm) <= tolerance_mm;
    {
        char buf[128]{};
        std::snprintf(buf, sizeof(buf), "峰值 %.2f -> 断开 %.2f = %.2fmm (偏差 %+.2fmm)",
                      peak_mm, rel_mm, rel_delta, rel.error_mm);
        rel.detail = buf;
    }

    VerifyTrialResult rep;
    rep.kind = "rt_repress";
    rep.label = "RT 再压触发 " + FormatMm(target_rt_mm);
    rep.target_mm = Round3(target_rt_mm);
    rep.measured_mm = Round3(rep_delta);
    rep.error_mm = Round3(rep_delta - target_rt_mm);
    rep.passed = std::abs(rep.error_mm) <= tolerance_mm;
    {
        char buf[128]{};
        std::snprintf(buf, sizeof(buf), "谷值 %.2f -> 重触发 %.2f = %.2fmm (偏差 %+.2fmm)",
                      valley_mm, repress_mm, rep_delta, rep.error_mm);
        rep.detail = buf;
    }
    return {rel, rep};
}

VerifyTrialResult EvaluateDeadzoneTopTrial(double expected_mm, double measured_mm, double tolerance_mm) {
    const double err = measured_mm - expected_mm;
    VerifyTrialResult res;
    res.kind = "deadzone_top";
    res.label = "顶部死区";
    res.target_mm = Round3(expected_mm);
    res.measured_mm = Round3(measured_mm);
    res.error_mm = Round3(err);
    res.passed = std::abs(err) <= tolerance_mm;
    char buf[112]{};
    std::snprintf(buf, sizeof(buf), "期望屏蔽区边界 ~%.2fmm / 实测触发 %.2fmm (偏差 %+.2fmm)",
                  expected_mm, measured_mm, err);
    res.detail = buf;
    return res;
}

VerifyTrialResult EvaluateDeadzoneBottomTrial(double bottom_mm, double peak_mm, double rel_mm,
                                              double rt_mm, double tolerance_mm) {
    const double release_boundary = static_cast<double>(kTravelMm) - bottom_mm;
    const double err = rel_mm - release_boundary;
    const bool ok = (rel_mm <= release_boundary + tolerance_mm) && ((peak_mm - rel_mm) >= rt_mm * 0.5);
    VerifyTrialResult res;
    res.kind = "deadzone_bottom";
    res.label = "底部死区";
    res.target_mm = Round3(release_boundary);
    res.measured_mm = Round3(rel_mm);
    res.error_mm = Round3(err);
    res.passed = ok;
    char buf[128]{};
    std::snprintf(buf, sizeof(buf), "底死区 %.1fmm (边界<=%.2fmm) / 峰值 %.2f 断开于 %.2fmm",
                  bottom_mm, release_boundary, peak_mm, rel_mm);
    res.detail = buf;
    return res;
}

void PublishSharedMemoryLocked(AuraM605HallSharedMemory* shm, bool active, uint64_t timestamp_us,
                               uint32_t packet_count,
                               const std::array<AuraM605HallSharedKey, kHallKeyCount>& keys) {
    if (!shm) return;
    shm->magic = kHallShmMagic;
    shm->version = kHallShmVersion;
    const uint32_t cur = shm->seq.load(std::memory_order_relaxed);
    const uint32_t odd = (cur & 1u) ? cur : (cur + 1u);
    shm->seq.store(odd, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);

    shm->active = active ? 1u : 0u;
    shm->timestamp_us = timestamp_us;
    shm->packet_count = packet_count;
    shm->key_count = static_cast<uint32_t>(kHallKeyCount);
    shm->vid = kM605Vid;
    shm->pid = kM605Pid;
    shm->reserved = 0;
    for (size_t i = 0; i < kHallKeyCount; ++i) {
        shm->keys[i] = keys[i];
    }

    std::atomic_thread_fence(std::memory_order_release);
    shm->seq.store(odd + 1u, std::memory_order_release);
}

bool ReadSharedMemorySnapshot(const AuraM605HallSharedMemory* shm,
                              AuraM605HallSharedMemory& out_copy) {
    if (!shm) return false;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const uint32_t s1 = shm->seq.load(std::memory_order_acquire);
        if (s1 & 1u) continue;
        std::atomic_thread_fence(std::memory_order_acquire);

        out_copy.magic = shm->magic;
        out_copy.version = shm->version;
        out_copy.active = shm->active;
        out_copy.timestamp_us = shm->timestamp_us;
        out_copy.packet_count = shm->packet_count;
        out_copy.key_count = shm->key_count;
        out_copy.vid = shm->vid;
        out_copy.pid = shm->pid;
        out_copy.reserved = shm->reserved;
        for (size_t i = 0; i < kHallKeyCount; ++i) {
            out_copy.keys[i] = shm->keys[i];
        }

        std::atomic_thread_fence(std::memory_order_acquire);
        const uint32_t s2 = shm->seq.load(std::memory_order_acquire);
        if (s1 == s2 && out_copy.magic == kHallShmMagic && out_copy.version == kHallShmVersion) {
            out_copy.seq.store(s2, std::memory_order_relaxed);
            return true;
        }
    }
    return false;
}

M605HallTelemetry::ScopedPause::ScopedPause(M605HallTelemetry* owner) : owner_(owner) {
    if (!owner_) return;
    std::lock_guard<std::mutex> lock(owner_->ctrl_mutex_);
    ++owner_->pause_depth_;
    if (owner_->pause_depth_ == 1 && owner_->stream_running_ && !owner_->paused_while_active_) {
        HANDLE h = static_cast<HANDLE>(owner_->h_hall_);
        if (h != INVALID_HANDLE_VALUE) {
            WriteOverlappedReport(h, kStopStreamReport, 300);
            Sleep(25);
        }
        owner_->paused_while_active_ = true;
        was_paused_ = true;
    }
}

M605HallTelemetry::ScopedPause::ScopedPause(ScopedPause&& other) noexcept
    : owner_(other.owner_), was_paused_(other.was_paused_) {
    other.owner_ = nullptr;
    other.was_paused_ = false;
}

M605HallTelemetry::ScopedPause::~ScopedPause() {
    if (!owner_) return;
    std::lock_guard<std::mutex> lock(owner_->ctrl_mutex_);
    if (owner_->pause_depth_ > 0) --owner_->pause_depth_;
    if (owner_->pause_depth_ == 0 && owner_->paused_while_active_) {
        owner_->paused_while_active_ = false;
        if (owner_->stream_running_) {
            HANDLE h = static_cast<HANDLE>(owner_->h_hall_);
            if (h != INVALID_HANDLE_VALUE) {
                if (owner_->unlock_fa_) {
                    WriteOverlappedReport(h, MakeCtrlReport({0xFA, 0x00, 0xD3, 0xA5}), 300);
                    Sleep(20);
                    WriteOverlappedReport(h, MakeCtrlReport({0xFA, 0x10, 0x00, 0x00, 0x01}), 300);
                    Sleep(20);
                }
                WriteOverlappedReport(h, MakeCtrlReport({0x04, 0x17, 0, 0, 0, 0, 0, 0, 0, 0x00}), 300);
                Sleep(10);
            }
        }
    }
}

M605HallTelemetry::M605HallTelemetry(std::filesystem::path calibration_path)
    : calibration_path_(std::move(calibration_path)) {
    for (size_t i = 0; i < kHallKeyCount; ++i) {
        key_idle_[i] = kHallKeyTable[i].default_idle;
        key_span_[i] = kHallKeyTable[i].default_span;
        shared_keys_[i].hid_usage = kHallKeyTable[i].analog_id;
        shared_keys_[i].logical_id = kHallKeyTable[i].logical_id;
        shared_keys_[i].normalized = 0.0f;
        shared_keys_[i].depth_mm = 0.0f;
        shared_keys_[i].raw = static_cast<uint16_t>(kHallKeyTable[i].default_idle);
        shared_keys_[i].digital_down = 0;
        shared_keys_[i].reserved = 0;
    }
    LoadCalibrationOverrides();
    OpenSharedMemory();
    SyncSharedMemory(false, QpcMicroseconds(), 0);
}

M605HallTelemetry::~M605HallTelemetry() {
    Stop();
    CloseSharedMemory();
}

void M605HallTelemetry::LoadCalibrationOverrides() {
    std::vector<std::filesystem::path> candidates;
    if (!calibration_path_.empty()) {
        candidates.push_back(calibration_path_);
    } else {
        candidates.push_back(std::filesystem::current_path() / "hall_calibration.json");
        wchar_t exe_buf[MAX_PATH]{};
        if (GetModuleFileNameW(nullptr, exe_buf, MAX_PATH) > 0) {
            candidates.push_back(std::filesystem::path(exe_buf).parent_path() / "hall_calibration.json");
        }
    }

    for (const auto& path : candidates) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || !std::filesystem::is_regular_file(path, ec)) {
            continue;
        }
        std::ifstream ifs(path);
        if (!ifs) continue;
        nlohmann::json root = nlohmann::json::parse(ifs, nullptr, false);
        if (!root.is_object()) continue;
        std::lock_guard<std::mutex> lock(data_mutex_);
        for (size_t i = 0; i < kHallKeyCount; ++i) {
            const auto it = root.find(kHallKeyTable[i].name);
            if (it == root.end() || !it->is_object()) continue;
            if (it->contains("idle") && it->at("idle").is_number()) {
                const float idle = it->at("idle").get<float>();
                if (idle > 1000.0f && idle < 10000.0f) key_idle_[i] = idle;
            }
            if (it->contains("span") && it->at("span").is_number()) {
                const float span = it->at("span").get<float>();
                if (span > 2000.0f && span < 10000.0f) key_span_[i] = span;
            }
        }
        LOG_INFO("[M605Hall] Loaded Hall calibration overrides from " + path.string());
        break;
    }
}

bool M605HallTelemetry::OpenSharedMemory() {
    if (shm_view_) return true;
    HANDLE h_map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                      static_cast<DWORD>(sizeof(AuraM605HallSharedMemory)),
                                      kHallShmName);
    if (!h_map) {
        std::lock_guard<std::mutex> lock(data_mutex_);
        last_error_ = "CreateFileMappingW failed: " + std::to_string(GetLastError());
        return false;
    }
    const bool already_exists = (GetLastError() == ERROR_ALREADY_EXISTS);
    void* view = MapViewOfFile(h_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(AuraM605HallSharedMemory));
    if (!view) {
        std::lock_guard<std::mutex> lock(data_mutex_);
        last_error_ = "MapViewOfFile failed: " + std::to_string(GetLastError());
        CloseHandle(h_map);
        return false;
    }
    shm_handle_ = h_map;
    shm_view_ = static_cast<AuraM605HallSharedMemory*>(view);
    if (!already_exists) {
        shm_view_->magic = kHallShmMagic;
        shm_view_->version = kHallShmVersion;
        shm_view_->seq.store(0, std::memory_order_relaxed);
    }
    return true;
}

void M605HallTelemetry::CloseSharedMemory() {
    if (shm_view_) {
        std::array<AuraM605HallSharedKey, kHallKeyCount> cleared;
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            cleared = shared_keys_;
        }
        for (auto& k : cleared) {
            k.normalized = 0.0f;
            k.depth_mm = 0.0f;
            k.digital_down = 0;
        }
        PublishSharedMemoryLocked(shm_view_, false, QpcMicroseconds(), 0, cleared);
        UnmapViewOfFile(shm_view_);
        shm_view_ = nullptr;
    }
    if (shm_handle_) {
        CloseHandle(static_cast<HANDLE>(shm_handle_));
        shm_handle_ = nullptr;
    }
}

void M605HallTelemetry::SyncSharedMemory(bool active, uint64_t timestamp_us, uint32_t packet_count) {
    if (!shm_view_) return;
    std::array<AuraM605HallSharedKey, kHallKeyCount> copy;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        copy = shared_keys_;
    }
    PublishSharedMemoryLocked(shm_view_, active, timestamp_us, packet_count, copy);
}

M605HallTelemetry::ScopedPause M605HallTelemetry::PauseForTransaction() {
    return ScopedPause(this);
}

bool M605HallTelemetry::Start(bool unlock_fa) {
    std::lock_guard<std::mutex> lock(ctrl_mutex_);
    if (stream_running_) return true;
    return StartStreamInternal(unlock_fa, false);
}

bool M605HallTelemetry::StartStreamInternal(bool unlock_fa, bool /*from_resume*/) {
    OpenSharedMemory();

    auto hall = OpenM605Endpoint(kHallUsagePage, kHallReportBytes, kHallReportBytes, true, L"&mi_01");
    if (hall.handle == INVALID_HANDLE_VALUE) {
        std::lock_guard<std::mutex> dlock(data_mutex_);
        last_error_ = "无法打开 M605 0xFF00 霍尔通道 (err=" + std::to_string(GetLastError()) + ")";
        return false;
    }

    auto evt = OpenM605Endpoint(kEventUsagePage, kEventReportBytes, 0, false, nullptr);

    if (unlock_fa) {
        if (!WriteOverlappedReport(hall.handle, MakeCtrlReport({0xFA, 0x00, 0xD3, 0xA5}), 300)) {
            CloseHandle(hall.handle);
            if (evt.handle != INVALID_HANDLE_VALUE) CloseHandle(evt.handle);
            std::lock_guard<std::mutex> dlock(data_mutex_);
            last_error_ = "发送原厂 FA 00 D3 A5 解锁包失败";
            return false;
        }
        Sleep(20);
        if (!WriteOverlappedReport(hall.handle, MakeCtrlReport({0xFA, 0x10, 0x00, 0x00, 0x01}), 300)) {
            CloseHandle(hall.handle);
            if (evt.handle != INVALID_HANDLE_VALUE) CloseHandle(evt.handle);
            std::lock_guard<std::mutex> dlock(data_mutex_);
            last_error_ = "发送原厂 FA 10 00 00 01 解锁包失败";
            return false;
        }
        Sleep(20);
    }

    if (!WriteOverlappedReport(hall.handle,
                               MakeCtrlReport({0x04, 0x17, 0, 0, 0, 0, 0, 0, 0, 0x00}), 300)) {
        CloseHandle(hall.handle);
        if (evt.handle != INVALID_HANDLE_VALUE) CloseHandle(evt.handle);
        std::lock_guard<std::mutex> dlock(data_mutex_);
        last_error_ = "发送 04 17 00 启动霍尔流失败";
        return false;
    }

    h_hall_ = hall.handle;
    h_event_ = evt.handle;
    event_channel_ok_ = (evt.handle != INVALID_HANDLE_VALUE);
    unlock_fa_ = unlock_fa;
    paused_while_active_ = false;
    pause_depth_ = 0;
    stop_threads_.store(false, std::memory_order_release);

    {
        std::lock_guard<std::mutex> dlock(data_mutex_);
        last_error_.clear();
        packet_count_ = 0;
        packet_rate_hz_ = 0.0;
        rate_window_packets_ = 0;
        rate_window_start_ = std::chrono::steady_clock::now();
        bitmap_.fill(0);
        edges_.clear();
    }

    stream_running_ = true;
    SyncSharedMemory(true, QpcMicroseconds(), 0);

    hall_thread_ = std::thread(&M605HallTelemetry::HallThreadProc, this);
    if (event_channel_ok_) {
        event_thread_ = std::thread(&M605HallTelemetry::EventThreadProc, this);
    }
    LOG_INFO("[M605Hall] Hall telemetry stream started (event_channel=" +
             std::string(event_channel_ok_ ? "ok" : "unavailable") + ")");
    return true;
}

void M605HallTelemetry::Stop() {
    StopVerify();
    std::lock_guard<std::mutex> lock(ctrl_mutex_);
    StopStreamInternal(true);
}

void M605HallTelemetry::StopStreamInternal(bool keep_shared_mapping) {
    if (!stream_running_ && h_hall_ == INVALID_HANDLE_VALUE && h_event_ == INVALID_HANDLE_VALUE) {
        return;
    }
    stop_threads_.store(true, std::memory_order_release);

    if (hall_thread_.joinable()) hall_thread_.join();
    if (event_thread_.joinable()) event_thread_.join();

    if (h_hall_ != INVALID_HANDLE_VALUE) {
        // ALWAYS stop stream with 04 04 01 so firmware mfg_block_keys stays 0!
        WriteOverlappedReport(static_cast<HANDLE>(h_hall_), kStopStreamReport, 300);
        CloseHandle(static_cast<HANDLE>(h_hall_));
        h_hall_ = INVALID_HANDLE_VALUE;
    }
    if (h_event_ != INVALID_HANDLE_VALUE) {
        CloseHandle(static_cast<HANDLE>(h_event_));
        h_event_ = INVALID_HANDLE_VALUE;
    }

    stream_running_ = false;
    paused_while_active_ = false;
    pause_depth_ = 0;
    event_channel_ok_ = false;

    if (keep_shared_mapping) {
        {
            std::lock_guard<std::mutex> dlock(data_mutex_);
            packet_rate_hz_ = 0.0;
            for (size_t i = 0; i < kHallKeyCount; ++i) {
                shared_keys_[i].normalized = 0.0f;
                shared_keys_[i].depth_mm = 0.0f;
                shared_keys_[i].digital_down = 0;
            }
        }
        SyncSharedMemory(false, QpcMicroseconds(), 0);
    }
    LOG_INFO("[M605Hall] Hall telemetry stream stopped safely (04 04 01)");
}

bool M605HallTelemetry::IsActive() const {
    std::lock_guard<std::mutex> lock(ctrl_mutex_);
    return stream_running_;
}

void M605HallTelemetry::HallThreadProc() {
    HANDLE h = static_cast<HANDLE>(h_hall_);
    HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ev) return;

    std::array<uint8_t, kHallReportBytes> buf{};
    while (!stop_threads_.load(std::memory_order_acquire)) {
        ResetEvent(ev);
        OVERLAPPED ov{};
        ov.hEvent = ev;
        DWORD received = 0;
        BOOL ok = ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &received, &ov);
        if (!ok) {
            const DWORD err = GetLastError();
            if (err != ERROR_IO_PENDING) {
                std::lock_guard<std::mutex> dlock(data_mutex_);
                last_error_ = "0xFF00 ReadFile error: " + std::to_string(err);
                break;
            }
            const DWORD wait = WaitForSingleObject(ev, 100);
            if (wait != WAIT_OBJECT_0) {
                CancelIoEx(h, &ov);
                WaitForSingleObject(ev, 30);
                GetOverlappedResult(h, &ov, &received, TRUE);
                continue;
            }
            if (!GetOverlappedResult(h, &ov, &received, FALSE)) continue;
        }

        if (received < 7 || buf[0] != 0x00 || buf[1] != 0x04 || buf[2] != 0x20 || buf[3] != 0x00) {
            continue;
        }

        const double ts_sec = QpcSeconds();
        const uint64_t ts_us = QpcMicroseconds();
        uint32_t total_pkts = 0;

        std::array<AuraM605HallSharedKey, kHallKeyCount> shm_snapshot;
        {
            std::lock_guard<std::mutex> dlock(data_mutex_);
            ++packet_count_;
            total_pkts = static_cast<uint32_t>(packet_count_);
            ++rate_window_packets_;
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - rate_window_start_).count();
            if (elapsed >= 0.5) {
                packet_rate_hz_ = static_cast<double>(rate_window_packets_) / elapsed;
                rate_window_packets_ = 0;
                rate_window_start_ = now;
            }

            const size_t limit = std::min(static_cast<size_t>(received), kHallReportBytes);
            for (size_t pos = 4; pos + 2 < limit; pos += 3) {
                const uint8_t aid = buf[pos];
                const uint16_t raw = static_cast<uint16_t>(buf[pos + 1] | (buf[pos + 2] << 8));
                if (raw <= 100) continue;

                const auto key_idx = FindKeyIndexByAnalogId(aid);
                if (!key_idx) continue;

                const size_t i = *key_idx;
                const float idle = key_idle_[i];
                const float span = key_span_[i];
                const float depth = ConvertHallRawToDepthMm(raw, idle, span);
                shared_keys_[i].raw = raw;
                shared_keys_[i].depth_mm = depth;
                shared_keys_[i].normalized = depth / kTravelMm;

                if (record_samples_ && aid == record_analog_id_) {
                    const float rec_idle = target_override_idle_ > 100.0
                        ? static_cast<float>(target_override_idle_) : idle;
                    const float rec_span = target_override_span_ > 100.0
                        ? static_cast<float>(target_override_span_) : span;
                    const float rec_depth = ConvertHallRawToDepthMm(raw, rec_idle, rec_span);
                    recorded_samples_.push_back({ts_sec, raw, rec_depth});
                    while (recorded_samples_.size() > 4096) recorded_samples_.pop_front();
                }
            }
            shm_snapshot = shared_keys_;
        }

        if (shm_view_) {
            PublishSharedMemoryLocked(shm_view_, true, ts_us, total_pkts, shm_snapshot);
        }
    }

    CloseHandle(ev);
}

void M605HallTelemetry::EventThreadProc() {
    HANDLE h = static_cast<HANDLE>(h_event_);
    HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ev) return;

    std::array<uint8_t, kEventReportBytes> buf{};
    while (!stop_threads_.load(std::memory_order_acquire)) {
        ResetEvent(ev);
        OVERLAPPED ov{};
        ov.hEvent = ev;
        DWORD received = 0;
        BOOL ok = ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &received, &ov);
        if (!ok) {
            const DWORD err = GetLastError();
            if (err != ERROR_IO_PENDING) break;
            const DWORD wait = WaitForSingleObject(ev, 100);
            if (wait != WAIT_OBJECT_0) {
                CancelIoEx(h, &ov);
                WaitForSingleObject(ev, 30);
                GetOverlappedResult(h, &ov, &received, TRUE);
                continue;
            }
            if (!GetOverlappedResult(h, &ov, &received, FALSE)) continue;
        }

        if (received < 19 || buf[1] != 0x71) continue;
        const double ts_sec = QpcSeconds();

        std::lock_guard<std::mutex> dlock(data_mutex_);
        for (size_t byte_idx = 0; byte_idx < 17; ++byte_idx) {
            const uint8_t cur = buf[2 + byte_idx];
            const uint8_t diff = cur ^ bitmap_[byte_idx];
            if (diff == 0) continue;
            for (uint8_t bit_idx = 0; bit_idx < 8; ++bit_idx) {
                const uint8_t mask = static_cast<uint8_t>(1u << (7u - bit_idx));
                if (diff & mask) {
                    const uint8_t state = (cur & mask) ? 1u : 0u;
                    edges_.push_back({ts_sec, static_cast<uint8_t>(byte_idx), bit_idx, state});
                    while (edges_.size() > 512) edges_.pop_front();
                }
            }
            bitmap_[byte_idx] = cur;
        }
    }

    CloseHandle(ev);
}

HallTelemetrySnapshot M605HallTelemetry::GetSnapshot() const {
    HallTelemetrySnapshot snap;
    {
        std::lock_guard<std::mutex> lock(ctrl_mutex_);
        snap.active = stream_running_;
        snap.event_channel_ok = event_channel_ok_;
        snap.shared_memory_ok = (shm_view_ != nullptr);
    }
    std::lock_guard<std::mutex> dlock(data_mutex_);
    snap.packet_count = packet_count_;
    snap.packet_rate_hz = packet_rate_hz_;
    snap.last_error = last_error_;
    snap.keys.reserve(kHallKeyCount);
    for (size_t i = 0; i < kHallKeyCount; ++i) {
        HallKeySnapshot ks;
        ks.logical_id = kHallKeyTable[i].logical_id;
        ks.hid_usage = kHallKeyTable[i].analog_id;
        ks.name = kHallKeyTable[i].name;
        ks.raw = shared_keys_[i].raw;
        ks.depth_mm = shared_keys_[i].depth_mm;
        ks.normalized = shared_keys_[i].normalized;
        ks.digital_down = (shared_keys_[i].digital_down != 0);
        snap.keys.push_back(std::move(ks));
    }
    return snap;
}

void M605HallTelemetry::ClearEdges() {
    std::lock_guard<std::mutex> dlock(data_mutex_);
    edges_.clear();
}

std::vector<BitmapEdge> M605HallTelemetry::PopAllEdges() {
    std::lock_guard<std::mutex> dlock(data_mutex_);
    std::vector<BitmapEdge> out(edges_.begin(), edges_.end());
    edges_.clear();
    return out;
}

bool M605HallTelemetry::WaitForEdge(uint8_t byte_idx, uint8_t bit_idx, uint8_t desired_state,
                                     double timeout_sec, BitmapEdge& out_edge) {
    const double deadline = QpcSeconds() + timeout_sec;
    while (!verify_cancel_.load(std::memory_order_acquire) && QpcSeconds() < deadline) {
        uint16_t live_raw = 0;
        double live_depth = 0.0;
        bool live_down = false;
        bool update_live = false;
        {
            std::lock_guard<std::mutex> dlock(data_mutex_);
            for (auto it = edges_.begin(); it != edges_.end(); ++it) {
                if (it->byte_idx == byte_idx && it->bit_idx == bit_idx &&
                    it->state == desired_state) {
                    out_edge = *it;
                    edges_.erase(edges_.begin(), std::next(it));
                    return true;
                }
            }
            const auto key_idx = FindKeyIndexByAnalogId(record_analog_id_);
            if (key_idx) {
                const float idle = target_override_idle_ > 100.0
                    ? static_cast<float>(target_override_idle_) : key_idle_[*key_idx];
                const float span = target_override_span_ > 100.0
                    ? static_cast<float>(target_override_span_) : key_span_[*key_idx];
                live_raw = shared_keys_[*key_idx].raw;
                live_depth = ConvertHallRawToDepthMm(live_raw, idle, span);
                live_down = ((bitmap_[byte_idx] >> (7u - bit_idx)) & 1u) != 0;
                shared_keys_[*key_idx].digital_down = live_down ? 1 : 0;
                update_live = true;
            }
        }
        if (update_live) {
            std::lock_guard<std::mutex> vlock(verify_mutex_);
            verify_status_.live_raw = live_raw;
            verify_status_.live_depth_mm = Round3(live_depth);
            verify_status_.digital_down = live_down;
        }
        Sleep(10);
    }
    return false;
}

void M605HallTelemetry::StartRecordingSamples(uint8_t analog_id) {
    std::lock_guard<std::mutex> dlock(data_mutex_);
    record_analog_id_ = analog_id;
    record_samples_ = true;
    recorded_samples_.clear();
}

void M605HallTelemetry::StopRecordingSamples() {
    std::lock_guard<std::mutex> dlock(data_mutex_);
    record_samples_ = false;
    recorded_samples_.clear();
}

std::optional<double> M605HallTelemetry::DepthAtEdge(double edge_ts_sec) const {
    std::lock_guard<std::mutex> dlock(data_mutex_);
    std::vector<HallSample> copy(recorded_samples_.begin(), recorded_samples_.end());
    return MatchHallDepthAtTimestamp(copy, edge_ts_sec, 0.05);
}

void M605HallTelemetry::GetLatestTargetState(uint8_t analog_id, double& depth_mm, uint16_t& raw) const {
    std::lock_guard<std::mutex> dlock(data_mutex_);
    const auto idx = FindKeyIndexByAnalogId(analog_id);
    if (!idx) {
        depth_mm = 0.0;
        raw = 0;
        return;
    }
    raw = shared_keys_[*idx].raw;
    const float idle = target_override_idle_ > 100.0
        ? static_cast<float>(target_override_idle_) : key_idle_[*idx];
    const float span = target_override_span_ > 100.0
        ? static_cast<float>(target_override_span_) : key_span_[*idx];
    depth_mm = ConvertHallRawToDepthMm(raw, idle, span);
}

bool M605HallTelemetry::StartVerify(const VerifyRequest& req, VerifyHardwareWriter writer) {
    if (!FindKeyIndexByLogicalId(req.logical_id)) {
        std::lock_guard<std::mutex> vlock(verify_mutex_);
        verify_status_ = {};
        verify_status_.state = "Failed";
        verify_status_.logical_id = req.logical_id;
        verify_status_.error = "未找到对应的 68 键物理键位";
        return false;
    }
    if (!writer.set_actuation || !writer.set_rt || !writer.disable_rt || !writer.set_deadzone) {
        std::lock_guard<std::mutex> vlock(verify_mutex_);
        verify_status_ = {};
        verify_status_.state = "Failed";
        verify_status_.logical_id = req.logical_id;
        verify_status_.error = "硬件写入回调未绑定";
        return false;
    }

    StopVerify();

    const size_t key_idx = *FindKeyIndexByLogicalId(req.logical_id);
    bool was_streaming = IsActive();
    if (!was_streaming) {
        if (!Start(req.unlock_fa)) {
            std::lock_guard<std::mutex> vlock(verify_mutex_);
            verify_status_ = {};
            verify_status_.state = "Failed";
            verify_status_.logical_id = req.logical_id;
            verify_status_.key_name = kHallKeyTable[key_idx].name;
            verify_status_.error = GetSnapshot().last_error;
            return false;
        }
    }

    if (!GetSnapshot().event_channel_ok) {
        if (!was_streaming) Stop();
        std::lock_guard<std::mutex> vlock(verify_mutex_);
        verify_status_ = {};
        verify_status_.state = "Failed";
        verify_status_.logical_id = req.logical_id;
        verify_status_.key_name = kHallKeyTable[key_idx].name;
        verify_status_.error = "无法打开 0xFFC0 事件通道以采集数字触发沿";
        return false;
    }

    int total_steps = 0;
    if (req.mode == "actuation") total_steps = static_cast<int>(req.actuation_points.size());
    else if (req.mode == "rt") total_steps = static_cast<int>(req.rt_points.size());
    else if (req.mode == "deadzone") total_steps = 2;

    {
        std::lock_guard<std::mutex> vlock(verify_mutex_);
        verify_cancel_.store(false, std::memory_order_release);
        verify_running_ = true;
        started_stream_for_verify_ = !was_streaming;
        verify_status_ = {};
        verify_status_.state = "SamplingIdle";
        verify_status_.logical_id = req.logical_id;
        verify_status_.key_name = kHallKeyTable[key_idx].name;
        verify_status_.mode = req.mode;
        verify_status_.step_index = 0;
        verify_status_.step_total = total_steps;
        verify_status_.instruction = "正在采样基准静态霍尔值，请暂勿触碰按键...";
    }

    verify_thread_ = std::thread(&M605HallTelemetry::VerifyThreadProc, this, req, std::move(writer));
    return true;
}

void M605HallTelemetry::StopVerify() {
    verify_cancel_.store(true, std::memory_order_release);
    if (verify_thread_.joinable()) {
        verify_thread_.join();
    }
    std::lock_guard<std::mutex> vlock(verify_mutex_);
    verify_running_ = false;
}

VerifyStatusSnapshot M605HallTelemetry::GetVerifyStatus() const {
    VerifyStatusSnapshot snap;
    {
        std::lock_guard<std::mutex> vlock(verify_mutex_);
        snap = verify_status_;
    }
    if (snap.logical_id != 0) {
        const auto idx = FindKeyIndexByLogicalId(snap.logical_id);
        if (idx) {
            double d = 0.0;
            uint16_t raw = 0;
            GetLatestTargetState(kHallKeyTable[*idx].analog_id, d, raw);
            if (raw > 0) {
                snap.live_depth_mm = Round3(d);
                snap.live_raw = raw;
            }
        }
    }
    return snap;
}

void M605HallTelemetry::VerifyThreadProc(VerifyRequest req, VerifyHardwareWriter writer) {
    const size_t key_idx = *FindKeyIndexByLogicalId(req.logical_id);
    const auto& spec = kHallKeyTable[key_idx];
    const uint8_t aid = spec.analog_id;

    {
        std::lock_guard<std::mutex> dlock(data_mutex_);
        target_override_idle_ = key_idle_[key_idx];
        target_override_span_ = key_span_[key_idx];
    }
    StartRecordingSamples(aid);

    auto set_state = [&](const std::string& st, const std::string& instr, int step_idx = -1) {
        std::lock_guard<std::mutex> vlock(verify_mutex_);
        verify_status_.state = st;
        verify_status_.instruction = instr;
        if (step_idx >= 0) verify_status_.step_index = step_idx;
    };

    auto fail_with = [&](const std::string& msg) {
        std::lock_guard<std::mutex> vlock(verify_mutex_);
        verify_status_.state = "Failed";
        verify_status_.error = msg;
        verify_status_.instruction = msg;
    };

    // Write helpers pause the high-rate 04 20 00 stream cleanly before calling M605Runtime.
    auto safe_set_act = [&](double mm) {
        ScopedPause pause(this);
        return writer.set_actuation(req.logical_id, mm);
    };
    auto safe_set_rt = [&](double p, double r) {
        ScopedPause pause(this);
        return writer.set_rt(req.logical_id, p, r);
    };
    auto safe_disable_rt = [&]() {
        ScopedPause pause(this);
        return writer.disable_rt(req.logical_id, req.inherited_rt_press_mm, req.inherited_rt_release_mm);
    };
    auto safe_set_dz = [&](double top, double bot) {
        ScopedPause pause(this);
        return writer.set_deadzone(req.logical_id, top, bot);
    };

    // 1. Sample live idle baseline (400ms)
    set_state("SamplingIdle", std::string("正在采集 ") + spec.name + " 静态基准，请勿按键...");
    std::vector<uint16_t> idle_samples;
    const double t_idle_end = QpcSeconds() + 0.40;
    while (!verify_cancel_.load(std::memory_order_acquire) && QpcSeconds() < t_idle_end) {
        double d = 0.0;
        uint16_t raw = 0;
        GetLatestTargetState(aid, d, raw);
        if (raw > 100) idle_samples.push_back(raw);
        Sleep(10);
    }
    if (!verify_cancel_.load(std::memory_order_acquire) && idle_samples.size() >= 10) {
        std::sort(idle_samples.begin(), idle_samples.end());
        const uint16_t median = idle_samples[idle_samples.size() / 2];
        std::lock_guard<std::mutex> dlock(data_mutex_);
        target_override_idle_ = static_cast<double>(median);
    }

    bool wrote_hardware = false;
    auto restore_hardware = [&]() {
        if (!wrote_hardware) return;
        set_state("Restoring", std::string("正在恢复 ") + spec.name + " 默认触发参数...");
        safe_disable_rt();
        safe_set_dz(0.0, 0.0);
        safe_set_act(req.restore_actuation_mm);
        wrote_hardware = false;
    };

    // 2. Bind 0x71 bitmap bit
    std::optional<std::pair<uint8_t, uint8_t>> bit;
    {
        std::lock_guard<std::mutex> dlock(data_mutex_);
        bit = bound_bit_;
    }

    if (!verify_cancel_.load(std::memory_order_acquire)) {
        set_state("WritingConfig", std::string("正在重置 ") + spec.name + " 为 1.0mm 以识别触发位...");
        if (!safe_disable_rt() || !safe_set_dz(0.0, 0.0) || !safe_set_act(1.0)) {
            fail_with("写入初始触发配置失败，请检查设备状态或固件版本。");
            StopRecordingSamples();
            return;
        }
        wrote_hardware = true;
    }

    if (!verify_cancel_.load(std::memory_order_acquire)) {
        ClearEdges();
        set_state("BindingKey", std::string("请轻按并松开 ") + spec.name + " 一次以同步数字触发位 (限 20 秒)...");
        const double deadline = QpcSeconds() + 20.0;
        std::optional<std::pair<uint8_t, uint8_t>> detected;
        while (!verify_cancel_.load(std::memory_order_acquire) && QpcSeconds() < deadline) {
            const auto edges = PopAllEdges();
            double d = 0.0;
            uint16_t raw = 0;
            GetLatestTargetState(aid, d, raw);
            for (const auto& e : edges) {
                if (e.state == 1 && d > 0.25) {
                    detected = {e.byte_idx, e.bit_idx};
                    break;
                }
            }
            if (detected) break;
            Sleep(10);
        }
        if (detected) {
            bit = detected;
            {
                std::lock_guard<std::mutex> dlock(data_mutex_);
                bound_bit_ = bit;
            }
            {
                std::lock_guard<std::mutex> vlock(verify_mutex_);
                char bbuf[32]{};
                std::snprintf(bbuf, sizeof(bbuf), "B%u:b%u", bit->first, bit->second);
                verify_status_.bound_bit = bbuf;
            }
            BitmapEdge rel{};
            WaitForEdge(bit->first, bit->second, 0, 5.0, rel);
            Sleep(150);
        } else if (!verify_cancel_.load(std::memory_order_acquire)) {
            restore_hardware();
            fail_with("等待按键同步超时，未检测到数字触发沿。");
            StopRecordingSamples();
            return;
        }
    }

    // 3. Execute tests
    if (!verify_cancel_.load(std::memory_order_acquire) && bit) {
        const uint8_t b_idx = bit->first;
        const uint8_t bit_idx = bit->second;

        if (req.mode == "actuation") {
            int step = 0;
            for (double target : req.actuation_points) {
                if (verify_cancel_.load(std::memory_order_acquire)) break;
                ++step;
                set_state("WritingConfig",
                          std::string("正在下发触发点 ") + FormatMm(target) + "...", step);
                if (!safe_disable_rt() || !safe_set_dz(0.0, 0.0) || !safe_set_act(target)) {
                    fail_with("下发触发点参数失败");
                    break;
                }
                ClearEdges();
                set_state("WaitingPress",
                          std::string("请缓慢按下 ") + spec.name + " 直至触发 (" + FormatMm(target) + ")，然后松开",
                          step);

                BitmapEdge press_edge{};
                if (!WaitForEdge(b_idx, bit_idx, 1, 20.0, press_edge)) {
                    if (!verify_cancel_.load(std::memory_order_acquire)) {
                        fail_with("等待触发超时");
                    }
                    break;
                }
                const auto measured = DepthAtEdge(press_edge.ts_sec);
                double m_val = measured.value_or(0.0);
                if (!measured) {
                    uint16_t raw = 0;
                    GetLatestTargetState(aid, m_val, raw);
                }
                auto trial = EvaluateActuationTrial(target, m_val);
                {
                    std::lock_guard<std::mutex> vlock(verify_mutex_);
                    verify_status_.results.push_back(std::move(trial));
                }
                BitmapEdge rel{};
                WaitForEdge(b_idx, bit_idx, 0, 10.0, rel);
                Sleep(150);
            }
        } else if (req.mode == "rt") {
            int step = 0;
            for (double rt_mm : req.rt_points) {
                if (verify_cancel_.load(std::memory_order_acquire)) break;
                ++step;
                set_state("WritingConfig",
                          std::string("正在下发 RT 灵敏度 ") + FormatMm(rt_mm) + "...", step);
                if (!safe_set_dz(0.0, 0.0) || !safe_set_act(1.0) || !safe_set_rt(rt_mm, rt_mm)) {
                    fail_with("下发 Rapid Trigger 参数失败");
                    break;
                }
                ClearEdges();
                set_state("WaitingPress",
                          std::string("RT 校验 (") + FormatMm(rt_mm) + ")：请缓慢按下 " + spec.name + " 并按到底",
                          step);

                BitmapEdge first_press{};
                if (!WaitForEdge(b_idx, bit_idx, 1, 20.0, first_press)) {
                    if (!verify_cancel_.load(std::memory_order_acquire)) fail_with("RT 首按超时");
                    break;
                }
                set_state("WaitingRtCycle",
                          std::string("保持按到底后缓慢回抬触发断开，未完全松开时再下压触发重连"), step);

                double d_peak = 0.0;
                const double rel_deadline = QpcSeconds() + 20.0;
                BitmapEdge rel_edge{};
                bool got_rel = false;
                while (!verify_cancel_.load(std::memory_order_acquire) && QpcSeconds() < rel_deadline) {
                    double cur_d = 0.0;
                    uint16_t raw = 0;
                    GetLatestTargetState(aid, cur_d, raw);
                    if (cur_d > d_peak) d_peak = cur_d;
                    const auto edges = PopAllEdges();
                    for (const auto& e : edges) {
                        if (e.byte_idx == b_idx && e.bit_idx == bit_idx && e.state == 0) {
                            rel_edge = e;
                            got_rel = true;
                            break;
                        }
                    }
                    if (got_rel) break;
                    Sleep(10);
                }
                if (!got_rel) {
                    if (!verify_cancel_.load(std::memory_order_acquire)) fail_with("RT 抬起断开超时");
                    break;
                }
                const double d_rel = DepthAtEdge(rel_edge.ts_sec).value_or(d_peak);

                double d_valley = d_rel;
                const double rep_deadline = QpcSeconds() + 15.0;
                BitmapEdge rep_edge{};
                bool got_rep = false;
                while (!verify_cancel_.load(std::memory_order_acquire) && QpcSeconds() < rep_deadline) {
                    double cur_d = 0.0;
                    uint16_t raw = 0;
                    GetLatestTargetState(aid, cur_d, raw);
                    if (cur_d < d_valley) d_valley = cur_d;
                    const auto edges = PopAllEdges();
                    for (const auto& e : edges) {
                        if (e.byte_idx == b_idx && e.bit_idx == bit_idx && e.state == 1) {
                            rep_edge = e;
                            got_rep = true;
                            break;
                        }
                    }
                    if (got_rep) break;
                    Sleep(10);
                }
                if (!got_rep) {
                    if (!verify_cancel_.load(std::memory_order_acquire)) {
                        // Release might have happened all the way at top if user let go
                        fail_with("未检测到半程再压重触发（注意抬起断开后不要完全松开）");
                    }
                    break;
                }
                double d_repress = DepthAtEdge(rep_edge.ts_sec).value_or(d_valley);
                auto [rel_res, rep_res] = EvaluateRapidTriggerTrial(
                    rt_mm, d_peak, d_rel, d_valley, d_repress);
                {
                    std::lock_guard<std::mutex> vlock(verify_mutex_);
                    verify_status_.results.push_back(std::move(rel_res));
                    verify_status_.results.push_back(std::move(rep_res));
                }
                BitmapEdge final_rel{};
                WaitForEdge(b_idx, bit_idx, 0, 10.0, final_rel);
                Sleep(150);
            }
        } else if (req.mode == "deadzone") {
            // Step 1: Top deadzone
            set_state("WritingConfig",
                      std::string("正在下发顶部死区 ") + FormatMm(req.dz_top_mm) + "...", 1);
            if (!safe_disable_rt() || !safe_set_act(0.1) || !safe_set_dz(req.dz_top_mm, 0.0)) {
                fail_with("下发顶部死区参数失败");
            } else {
                ClearEdges();
                const double expected = std::max(0.1, req.dz_top_mm);
                set_state("WaitingPress",
                          std::string("请极慢按下 ") + spec.name + "（验证顶部死区 " + FormatMm(req.dz_top_mm) + " 屏蔽区间），触发后松开",
                          1);
                BitmapEdge press_edge{};
                if (!WaitForEdge(b_idx, bit_idx, 1, 20.0, press_edge)) {
                    if (!verify_cancel_.load(std::memory_order_acquire)) fail_with("顶部死区校验超时");
                } else {
                    double m_val = 0.0;
                    uint16_t raw = 0;
                    GetLatestTargetState(aid, m_val, raw);
                    m_val = DepthAtEdge(press_edge.ts_sec).value_or(m_val);
                    auto top_res = EvaluateDeadzoneTopTrial(expected, m_val);
                    {
                        std::lock_guard<std::mutex> vlock(verify_mutex_);
                        verify_status_.results.push_back(std::move(top_res));
                    }
                    BitmapEdge rel{};
                    WaitForEdge(b_idx, bit_idx, 0, 10.0, rel);
                    Sleep(150);
                }
            }

            // Step 2: Bottom deadzone
            if (!verify_cancel_.load(std::memory_order_acquire) && verify_status_.state != "Failed") {
                set_state("WritingConfig",
                          std::string("正在下发底部死区 ") + FormatMm(req.dz_bottom_mm) + "...", 2);
                if (!safe_set_act(1.0) || !safe_set_dz(0.0, req.dz_bottom_mm) ||
                    !safe_set_rt(req.dz_rt_mm, req.dz_rt_mm)) {
                    fail_with("下发底部死区参数失败");
                } else {
                    ClearEdges();
                    set_state("WaitingPress",
                              std::string("请用力按到底 (") + spec.name + ")，保持顿一下后缓慢回抬直至断开", 2);
                    BitmapEdge press_edge{};
                    if (!WaitForEdge(b_idx, bit_idx, 1, 20.0, press_edge)) {
                        if (!verify_cancel_.load(std::memory_order_acquire)) fail_with("底部死区按下超时");
                    } else {
                        set_state("WaitingRelease", "已触发，请先按到底再缓慢回抬直到断开...", 2);
                        double d_peak = 0.0;
                        BitmapEdge rel_edge{};
                        bool got_rel = false;
                        const double rel_deadline = QpcSeconds() + 20.0;
                        while (!verify_cancel_.load(std::memory_order_acquire) && QpcSeconds() < rel_deadline) {
                            double cur_d = 0.0;
                            uint16_t raw = 0;
                            GetLatestTargetState(aid, cur_d, raw);
                            if (cur_d > d_peak) d_peak = cur_d;
                            const auto edges = PopAllEdges();
                            for (const auto& e : edges) {
                                if (e.byte_idx == b_idx && e.bit_idx == bit_idx && e.state == 0) {
                                    rel_edge = e;
                                    got_rel = true;
                                    break;
                                }
                            }
                            if (got_rel) break;
                            Sleep(10);
                        }
                        if (!got_rel) {
                            if (!verify_cancel_.load(std::memory_order_acquire)) fail_with("底部死区断开超时");
                        } else {
                            const double d_rel = DepthAtEdge(rel_edge.ts_sec).value_or(d_peak);
                            auto bot_res = EvaluateDeadzoneBottomTrial(
                                req.dz_bottom_mm, d_peak, d_rel, req.dz_rt_mm);
                            std::lock_guard<std::mutex> vlock(verify_mutex_);
                            verify_status_.results.push_back(std::move(bot_res));
                        }
                    }
                }
            }
        }
    }

    restore_hardware();
    StopRecordingSamples();

    std::lock_guard<std::mutex> vlock(verify_mutex_);
    if (verify_cancel_.load(std::memory_order_acquire)) {
        verify_status_.state = "Cancelled";
        verify_status_.instruction = "校验已停止，已恢复默认触发配置。";
    } else if (verify_status_.state != "Failed") {
        verify_status_.state = "Completed";
        size_t pass_cnt = 0;
        for (const auto& r : verify_status_.results) {
            if (r.passed) ++pass_cnt;
        }
        verify_status_.instruction = "校验完成：" + std::to_string(pass_cnt) + "/" +
            std::to_string(verify_status_.results.size()) + " 项通过";
    }
    verify_running_ = false;
}

} // namespace aura::m605::hall
