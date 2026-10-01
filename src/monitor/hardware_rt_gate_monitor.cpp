#include "monitor/hardware_rt_gate_monitor.h"
#include "utils/logger.h"
#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <vector>
#include <hidsdi.h>

namespace aura {
namespace {
constexpr USHORT kPage = 0xffc0, kUsage = 1;
bool IsTarget(HANDLE device) {
    RID_DEVICE_INFO info{}; info.cbSize = sizeof(info); UINT size = sizeof(info);
    if (GetRawInputDeviceInfoW(device, RIDI_DEVICEINFO, &info, &size) == UINT(-1) ||
        info.dwType != RIM_TYPEHID || info.hid.dwVendorId != 0x0b05 || info.hid.dwProductId != 0x1b7e ||
        info.hid.usUsagePage != kPage || info.hid.usUsage != kUsage) return false;
    UINT chars = 0;
    if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, nullptr, &chars) == UINT(-1) || chars == 0 || chars > 32768) return false;
    std::wstring path(chars, L'\0');
    if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, path.data(), &chars) == UINT(-1)) return false;
    std::transform(path.begin(), path.end(), path.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    if (path.find(L"&mi_02&col03#") == std::wstring::npos) return false;
    // OS-cached report descriptor: no device handle or feature/query report.
    UINT data_size = 0;
    if (GetRawInputDeviceInfoW(device, RIDI_PREPARSEDDATA, nullptr, &data_size) == UINT(-1) ||
        data_size == 0 || data_size > 65536) return false;
    std::vector<uint8_t> data(data_size);
    if (GetRawInputDeviceInfoW(device, RIDI_PREPARSEDDATA, data.data(), &data_size) == UINT(-1)) return false;
    auto* preparsed = reinterpret_cast<PHIDP_PREPARSED_DATA>(data.data());
    HIDP_CAPS caps{};
    if (HidP_GetCaps(preparsed, &caps) != HIDP_STATUS_SUCCESS || caps.InputReportByteLength != 21 ||
        caps.NumberInputButtonCaps == 0 || caps.NumberInputButtonCaps > 64) return false;
    std::vector<HIDP_BUTTON_CAPS> buttons(caps.NumberInputButtonCaps);
    USHORT count = caps.NumberInputButtonCaps;
    if (HidP_GetButtonCaps(HidP_Input, buttons.data(), &count, preparsed) != HIDP_STATUS_SUCCESS) return false;
    return std::any_of(buttons.begin(), buttons.begin() + count,
        [](const auto& value) { return value.ReportID == 3; });
}
}
bool HardwareRtGateMonitor::Start() {
    if (thread_.joinable()) return false;
    thread_id_ = 0;
    thread_ = std::thread(&HardwareRtGateMonitor::Run, this);
    return true;
}
void HardwareRtGateMonitor::Stop() {
    if (!thread_.joinable()) return;
    // Run publishes its message queue before its ID. No polling delay.
    { std::unique_lock<std::mutex> lock(startup_mutex_);
      startup_ready_.wait(lock, [this] { return thread_id_.load() != 0; }); }
    PostThreadMessageW(thread_id_.load(), WM_QUIT, 0, 0);
    thread_.join();
}
void HardwareRtGateMonitor::Publish(HardwareRtGateState state, const char* reason) {
    observation_.state = state; observation_.unavailable_reason = reason;
    ++observation_.observation_sequence;
    if (state == HardwareRtGateState::Unknown) observation_.observed_at_utc.clear();
    else {
        SYSTEMTIME now{}; GetSystemTime(&now); char text[32]{};
        std::snprintf(text, sizeof(text), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
        observation_.observed_at_utc = text;
    }
    callback_(observation_);
}
void HardwareRtGateMonitor::ReconcileCollection() {
    UINT count = 0;
    if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) == UINT(-1) || count > 4096) {
        device_ = nullptr; observation_.input_collection_connected = false;
        Publish(HardwareRtGateState::Unknown, "input_enumeration_failed"); return;
    }
    std::vector<RAWINPUTDEVICELIST> devices(count);
    if (count && GetRawInputDeviceList(devices.data(), &count, sizeof(RAWINPUTDEVICELIST)) == UINT(-1)) {
        device_ = nullptr; observation_.input_collection_connected = false;
        Publish(HardwareRtGateState::Unknown, "input_enumeration_failed"); return;
    }
    HANDLE found = nullptr; size_t matches = 0;
    for (const auto& item : devices) if (item.dwType == RIM_TYPEHID && IsTarget(item.hDevice)) { found = item.hDevice; ++matches; }
    if (matches != 1) found = nullptr; // multiple keyboards: never merge status identities
    if (found == device_ && matches <= 1) return;
    device_ = found; ++observation_.input_session_generation;
    observation_.input_collection_connected = found != nullptr;
    Publish(HardwareRtGateState::Unknown, matches > 1 ? "ambiguous_input_collections" : found ?
        "awaiting_status_notification" : "input_collection_removed");
}
void HardwareRtGateMonitor::Receive(HRAWINPUT input) {
    UINT size = 0;
    if (GetRawInputData(input, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) == UINT(-1) ||
        size < sizeof(RAWINPUTHEADER) || size > 65536) return;
    std::vector<uint8_t> bytes(size);
    if (GetRawInputData(input, RID_INPUT, bytes.data(), &size, sizeof(RAWINPUTHEADER)) == UINT(-1)) {
        Publish(HardwareRtGateState::Unknown, "input_read_failed"); return;
    }
    if (size < sizeof(RAWINPUTHEADER)) return;
    const auto* raw = reinterpret_cast<const RAWINPUT*>(bytes.data());
    if (raw->header.dwType != RIM_TYPEHID || !device_ || raw->header.hDevice != device_) return;
    const size_t offset = offsetof(RAWINPUT, data.hid.bRawData);
    if (size < offset || raw->data.hid.dwSizeHid == 0 ||
        uint64_t(raw->data.hid.dwCount) * raw->data.hid.dwSizeHid > size - offset) {
        Publish(HardwareRtGateState::Unknown, "malformed_input_buffer"); return;
    }
    for (DWORD i = 0; i < raw->data.hid.dwCount; ++i) {
        const auto state = ParseHardwareRtGateReport(raw->data.hid.bRawData + size_t(i) * raw->data.hid.dwSizeHid,
            raw->data.hid.dwSizeHid);
        if (state) Publish(*state, *state == HardwareRtGateState::Unknown ? "malformed_status_notification" : "");
    }
}
LRESULT CALLBACK HardwareRtGateMonitor::WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<HardwareRtGateMonitor*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<HardwareRtGateMonitor*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self && message == WM_INPUT_DEVICE_CHANGE) {
        if (wp == GIDC_REMOVAL && reinterpret_cast<HANDLE>(lp) == self->device_) {
            self->device_ = nullptr; ++self->observation_.input_session_generation;
            self->observation_.input_collection_connected = false;
            self->Publish(HardwareRtGateState::Unknown, "input_collection_removed");
        }
        self->ReconcileCollection();
    }
    if (self && message == WM_INPUT) self->Receive(reinterpret_cast<HRAWINPUT>(lp));
    return DefWindowProcW(window, message, wp, lp); // foreground Raw Input cleanup
}
void HardwareRtGateMonitor::Run() {
    MSG message{}; PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    { std::lock_guard<std::mutex> lock(startup_mutex_); thread_id_ = GetCurrentThreadId(); }
    startup_ready_.notify_all();
    WNDCLASSW cls{}; cls.lpfnWndProc = WindowProc; cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"AuraHardwareRtGateInput";
    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        Publish(HardwareRtGateState::Unknown, "input_window_registration_failed"); return;
    }
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, cls.hInstance, this);
    RAWINPUTDEVICE input{kPage, kUsage, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, window};
    if (!window || !RegisterRawInputDevices(&input, 1, sizeof(input))) {
        LOG_WARN("[RT gate] Passive input registration unavailable");
        Publish(HardwareRtGateState::Unknown, "input_registration_failed");
        if (window) DestroyWindow(window); return;
    }
    ReconcileCollection();
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    input.dwFlags = RIDEV_REMOVE; input.hwndTarget = nullptr;
    RegisterRawInputDevices(&input, 1, sizeof(input)); DestroyWindow(window);
    device_ = nullptr; observation_.input_collection_connected = false;
    Publish(HardwareRtGateState::Unknown, "observer_stopped");
}
}
