// Standalone passive research program. No HID handle, hook, query, or write.
#include <windows.h>
#include <hidsdi.h>
#include <wtsapi32.h>
#include <bcrypt.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include <cwctype>
#include <limits>
#include "input_delivery.h"
#include "startup_checks.h"

namespace {
using namespace stage_b;
constexpr UINT_PTR timer_id = 1;
constexpr int start_id = 101, stop_id = 102;
uint64_t now_ms() { return GetTickCount64(); }
int initialization_failed(const char* stage, DWORD error, const char* why = "initialization_failed") {
    std::cout << "{\"classification\":\"INITIALIZATION_METADATA\",\"stage\":\"" << stage
              << "\",\"blocked_stage\":\"" << stage << "\",\"reason\":\"" << why << "\",\"win32_error\":" << error
              << ",\"error\":" << error << ",\"capture_started\":false,\"physical_test\":\"NOT RUN\"}" << std::endl;
    return 2;
}
std::string case_name(const Case& test_case) {
    std::string name;
    for (const wchar_t* p = test_case.name; *p; ++p) name.push_back(static_cast<char>(*p));
    return name; // Only canonical predefined ASCII names, never user text.
}
std::string fresh_session() {
    std::array<unsigned char, 16> random{};
    if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("session identity unavailable");
    const char hex[] = "0123456789abcdef"; std::string value;
    for (const auto byte : random) { value.push_back(hex[byte >> 4]); value.push_back(hex[byte & 15]); }
    return value; // Random run identity, not a serial number or device fingerprint.
}

struct Handle {
    HANDLE value{};
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct Desktop {
    HDESK value{};
    ~Desktop() { if (value) CloseDesktop(value); }
};
struct WtsMemory {
    LPWSTR value{};
    DWORD bytes{};
    ~WtsMemory() { if (value) WTSFreeMemory(value); }
    bool get(DWORD session, WTS_INFO_CLASS kind) {
        return WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session, kind, &value, &bytes) != FALSE;
    }
};
StartupCheck object_check(HANDLE object, const wchar_t* expected, const char* role) {
    if (!object) return {"desktop", role, "GetUserObjectInformationW", 0};
    wchar_t name[256]{}; DWORD needed{};
    if (!GetUserObjectInformationW(object, UOI_NAME, name, sizeof(name), &needed))
        return {"desktop", role, "GetUserObjectInformationW", GetLastError()};
    if (needed > sizeof(name) || wcscmp(name, expected) != 0)
        return {"desktop", role, "none", 0};
    return {};
}
// Same Windows 10/11 lock and desktop restrictions; expose the first failure.
StartupCheck desktop_check() {
    DWORD session{};
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session))
        return {"session", "process_session_query_failed", "ProcessIdToSessionId", GetLastError()};
    if (session != WTSGetActiveConsoleSessionId())
        return {"session", "not_active_local_console", "WTSGetActiveConsoleSessionId", 0};
    WtsMemory state, protocol, extended;
    if (!state.get(session, WTSConnectState))
        return {"session", "connection_query_failed", "WTSQuerySessionInformationW", GetLastError()};
    if (!state.value || state.bytes < sizeof(WTS_CONNECTSTATE_CLASS))
        return {"session", "connection_data_invalid", "none", 0};
    if (*reinterpret_cast<WTS_CONNECTSTATE_CLASS*>(state.value) != WTSActive)
        return {"session", "session_not_active", "none", 0};
    if (!protocol.get(session, WTSClientProtocolType))
        return {"session", "protocol_query_failed", "WTSQuerySessionInformationW", GetLastError()};
    if (!protocol.value || protocol.bytes < sizeof(USHORT))
        return {"session", "protocol_data_invalid", "none", 0};
    if (*reinterpret_cast<USHORT*>(protocol.value) != 0)
        return {"session", "remote_protocol_rejected", "none", 0};
    if (!extended.get(session, WTSSessionInfoEx))
        return {"session", "lock_query_failed", "WTSQuerySessionInformationW", GetLastError()};
    if (!extended.value || extended.bytes < sizeof(WTSINFOEXW))
        return {"session", "lock_data_invalid", "none", 0};
    const auto* info = reinterpret_cast<const WTSINFOEXW*>(extended.value);
    if (info->Level != 1 || info->Data.WTSInfoExLevel1.SessionState != WTSActive)
        return {"session", "extended_session_not_active", "none", 0};
    if (info->Data.WTSInfoExLevel1.SessionFlags != WTS_SESSIONSTATE_UNLOCK)
        return {"session", "session_locked_or_unknown", "none", 0};
    Desktop input{OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS)};
    if (!input.value) return {"desktop", "input_desktop_open_failed", "OpenInputDesktop", GetLastError()};
    const auto station = GetProcessWindowStation();
    if (!station) return {"desktop", "window_station_query_failed", "GetProcessWindowStation", GetLastError()};
    auto check = object_check(station, L"WinSta0", "interactive_window_station_required");
    if (!check.ready()) return check;
    const auto thread = GetThreadDesktop(GetCurrentThreadId());
    if (!thread) return {"desktop", "thread_desktop_query_failed", "GetThreadDesktop", GetLastError()};
    check = object_check(thread, L"Default", "default_thread_desktop_required");
    if (!check.ready()) return check;
    return object_check(input.value, L"Default", "default_input_desktop_required");
}
bool desktop_safe() { return desktop_check().ready(); }

std::wstring path_upper(HANDLE device) {
    UINT characters{};
    if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, nullptr, &characters) != 0 ||
        characters == 0 || characters > 4096) return {};
    std::vector<wchar_t> name(size_t(characters) + 1, 0);
    if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, name.data(), &characters) == UINT(-1)) return {};
    std::wstring result(name.data());
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t c) { return wchar_t(towupper(c)); });
    return result; // Only in RAM. Device path/serial is never printed.
}
bool caps_match(HANDLE device, const Contract& spec) {
    UINT bytes{};
    if (GetRawInputDeviceInfoW(device, RIDI_PREPARSEDDATA, nullptr, &bytes) != 0 || !bytes || bytes > 65536) return false;
    std::vector<unsigned long long> data((bytes + 7u) / 8u);
    if (GetRawInputDeviceInfoW(device, RIDI_PREPARSEDDATA, data.data(), &bytes) == UINT(-1)) return false;
    auto* parsed = reinterpret_cast<PHIDP_PREPARSED_DATA>(data.data());
    HIDP_CAPS caps{};
    if (HidP_GetCaps(parsed, &caps) != HIDP_STATUS_SUCCESS || caps.UsagePage != spec.page ||
        caps.Usage != spec.usage || caps.InputReportByteLength != spec.bytes) return false;
    bool found = false;
    if (caps.NumberInputButtonCaps) {
        USHORT count = caps.NumberInputButtonCaps;
        std::vector<HIDP_BUTTON_CAPS> buttons(count);
        if (HidP_GetButtonCaps(HidP_Input, buttons.data(), &count, parsed) != HIDP_STATUS_SUCCESS) return false;
        for (USHORT i = 0; i < count; ++i) {
            if (buttons[i].ReportID != spec.report_id) return false;
            found = true;
        }
    }
    if (caps.NumberInputValueCaps) {
        USHORT count = caps.NumberInputValueCaps;
        std::vector<HIDP_VALUE_CAPS> values(count);
        if (HidP_GetValueCaps(HidP_Input, values.data(), &count, parsed) != HIDP_STATUS_SUCCESS) return false;
        for (USHORT i = 0; i < count; ++i) {
            if (values[i].ReportID != spec.report_id) return false;
            found = true;
        }
    }
    return found;
}
struct Target { HANDLE handle{}; unsigned matches{}; };
Target find_target(Scope scope) {
    UINT count{};
    if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) != 0 || count > 4096) return {};
    std::vector<RAWINPUTDEVICELIST> devices(count);
    if (GetRawInputDeviceList(devices.data(), &count, sizeof(RAWINPUTDEVICELIST)) == UINT(-1)) return {};
    const auto spec = contract(scope);
    Target result;
    for (UINT i = 0; i < count; ++i) {
        if (devices[i].dwType != RIM_TYPEHID) continue;
        RID_DEVICE_INFO info{}; info.cbSize = sizeof(info); UINT bytes = sizeof(info);
        if (GetRawInputDeviceInfoW(devices[i].hDevice, RIDI_DEVICEINFO, &info, &bytes) == UINT(-1) ||
            info.dwType != RIM_TYPEHID || info.hid.dwVendorId != 0x0b05 || info.hid.dwProductId != 0x1b7e ||
            info.hid.usUsagePage != spec.page || info.hid.usUsage != spec.usage) continue;
        const auto path = path_upper(devices[i].hDevice);
        const auto expected = scope == Scope::Control ? L"&MI_01#" : L"&MI_02&COL03#";
        if (path.find(L"VID_0B05&PID_1B7E") == std::wstring::npos ||
            path.find(expected) == std::wstring::npos || !caps_match(devices[i].hDevice, spec)) continue;
        result.handle = devices[i].hDevice; ++result.matches;
    }
    return result;
}
RegistrationRead registration_read() {
    UINT capacity = 1; RAWINPUTDEVICE item{};
    const UINT returned = GetRegisteredRawInputDevices(&item, &capacity, sizeof(item));
    const DWORD error = returned == UINT(-1) ? GetLastError() : 0;
    return {returned, capacity, error};
}
void check_fields(const StartupCheck& check) {
    std::cout << "\"blocked_stage\":\"" << check.blocked_stage << "\",\"reason\":\"" << check.reason
              << "\",\"api\":\"" << check.api << "\",\"win32_error\":" << check.win32_error;
}
bool no_existing_registration() {
    const auto read = registration_read(); const auto check = empty_registration_check(read);
    if (!check.ready()) {
        std::cout << "{\"classification\":\"REGISTRATION_TABLE_BLOCKED\","; check_fields(check);
        std::cout << ",\"returned\":" << read.returned << ",\"capacity_out\":" << read.capacity_out << "}" << std::endl;
    }
    return check.ready();
}

bool registration_matches(Scope scope, HWND window, bool emit_snapshot = false, DWORD* observed_flags = nullptr,
                          RegistrationRead* snapshot = nullptr) {
    // Unlike GetRawInputDeviceList, a NULL buffer with registered entries returns
    // UINT(-1)/ERROR_INSUFFICIENT_BUFFER. Read one bounded entry directly; more
    // registrations also fail closed instead of treating a size probe as failure.
    UINT count = 1;
    RAWINPUTDEVICE item{};
    SetLastError(0);
    const UINT returned = GetRegisteredRawInputDevices(&item, &count, sizeof(item));
    const DWORD error = returned == UINT(-1) ? GetLastError() : 0;
    if (snapshot) *snapshot = {returned, count, error};
    if (observed_flags) *observed_flags = item.dwFlags;
    if (emit_snapshot) {
        std::cout << "{\"classification\":\"REGISTRATION_METADATA\",\"returned\":" << returned
                  << ",\"count\":" << count << ",\"error\":" << error << ",\"page\":" << item.usUsagePage
                  << ",\"usage\":" << item.usUsage << ",\"flags\":" << item.dwFlags
                  << ",\"expected_flags\":" << RIDEV_DEVNOTIFY << ",\"window_matches\":"
                  << (item.hwndTarget == window ? "true" : "false") << "}" << std::endl;
    }
    return registration_snapshot_matches(scope, returned, count, item.usUsagePage, item.usUsage, item.dwFlags,
                                         reinterpret_cast<uintptr_t>(item.hwndTarget), reinterpret_cast<uintptr_t>(window));
}
struct NativeSubscription : SubscriptionApi {
    Scope scope{Scope::Notifications}; HWND window{};
    DWORD add_error{}, remove_error{};
    bool add() override {
        const auto spec = contract(scope);
        RAWINPUTDEVICE device{spec.page, spec.usage, RIDEV_DEVNOTIFY, window};
        const bool ok = RegisterRawInputDevices(&device, 1, sizeof(device)) != FALSE;
        add_error = ok ? 0 : GetLastError(); return ok;
    }
    bool remove() override {
        const auto spec = contract(scope);
        RAWINPUTDEVICE device{spec.page, spec.usage, RIDEV_REMOVE, nullptr};
        const bool ok = RegisterRawInputDevices(&device, 1, sizeof(device)) != FALSE;
        remove_error = ok ? 0 : GetLastError(); return ok;
    }
};
const InputLayout native_layout{sizeof(RAWINPUTHEADER), offsetof(RAWINPUTHEADER, hDevice), sizeof(HANDLE),
    offsetof(RAWINPUT, data.hid.dwSizeHid), offsetof(RAWINPUT, data.hid.dwCount), offsetof(RAWINPUT, data.hid.bRawData)};
struct NativeInputSource : InputSource {
    HRAWINPUT raw;
    explicit NativeInputSource(HRAWINPUT value) : raw(value) {}
    ReadResult header(InputHeader& out) override {
        RAWINPUTHEADER h{}; UINT bytes = sizeof(h);
        const UINT returned = GetRawInputData(raw, RID_HEADER, &h, &bytes, sizeof(h));
        out = {h.dwType, h.dwSize, reinterpret_cast<uintptr_t>(h.hDevice)};
        return {returned, bytes, returned == UINT(-1) ? GetLastError() : 0};
    }
    ReadResult size() override {
        UINT bytes{}; const UINT returned = GetRawInputData(raw, RID_INPUT, nullptr, &bytes, sizeof(RAWINPUTHEADER));
        return {returned, bytes, returned == UINT(-1) ? GetLastError() : 0};
    }
    ReadResult copy(void* buffer, uint32_t capacity) override {
        UINT bytes = capacity; const UINT returned = GetRawInputData(raw, RID_INPUT, buffer, &bytes, sizeof(RAWINPUTHEADER));
        return {returned, bytes, returned == UINT(-1) ? GetLastError() : 0};
    }
};
template<size_t N> void histogram(const std::array<uint64_t, N>& values) {
    std::cout << '['; bool comma = false;
    for (size_t i = 0; i < N; ++i) if (values[i]) {
        if (comma) std::cout << ',';
        std::cout << '[' << i << ',' << values[i] << ']'; comma = true;
    }
    std::cout << ']';
}
struct App {
    Scope scope{Scope::Notifications}; Case test_case{cases[0]}; unsigned seconds{10};
    HWND window{}, start_button{}, status{};
    bool wts{}, fatal{}, finished{}, cleanup_ok{true};
    unsigned trials{}; uint64_t generation{1}, idle_since{};
    uint64_t trial_started{}, wm_input{}, wm_input_total{}, wm_input_inactive{}, safe_ticks{};
    uint64_t device_changes{}, device_arrivals{}, device_removals{}, target_removals{}, registration_checks{};
    uint64_t input_guard_rejected{}, input_deadline_rejected{};
    DWORD loop_error{};
    bool window_valid{}, window_thread_valid{};
    InputDelivery delivery;
    bool registration_verified{}, registration_removed_verified{}, end_target_verified{};
    Stop run_stop{Stop::None};
    DWORD registration_snapshot_flags{};
    Target target; Observation observation; NativeSubscription native; Subscription subscription{native};
    std::string session, case_label;
    std::wstring window_title;
    ~App() { cleanup(); }
    void cleanup() {
        if (observation.active()) { run_stop = Stop::MessageLoopExit; observation.finish(run_stop); }
        cleanup_ok = subscription.close() && cleanup_ok;
        if (window) {
            KillTimer(window, timer_id);
            if (wts) { cleanup_ok = WTSUnRegisterSessionNotification(window) != FALSE && cleanup_ok; wts = false; }
            DestroyWindow(window); window = nullptr;
        }
    }
    void stop(Stop why, bool abort) {
        run_stop = why;
        if (observation.active() || pending_core_end) {
            if (observation.active()) observation.finish(why);
            cleanup_ok = subscription.close() && cleanup_ok;
            registration_removed_verified = no_existing_registration();
            cleanup_ok = registration_removed_verified && cleanup_ok;
            const auto current = find_target(scope);
            end_target_verified = current.matches == 1 && current.handle == target.handle;
            const auto c = observation.counts(); const auto spec = contract(scope); const auto d = delivery.counts();
            ++trials;
            std::cout << "{\"classification\":\"PHYSICAL_OBSERVATION_UNVERIFIED\",\"device\":\"M605-target-1\","
                      << "\"collection\":\"" << spec.collection << "\",\"session\":\"" << session
                      << "\",\"generation\":" << generation
                      << ",\"trial\":" << trials << ",\"case\":\"" << case_label
                      << "\",\"expected_report_id\":" << unsigned(spec.report_id)
                      << ",\"expected_length\":" << spec.bytes
                      << ",\"last_batch_report_bytes\":" << c.last_batch_report_bytes
                      << ",\"last_batch_report_count\":" << c.last_batch_report_count << ",\"stop\":\"" << reason(why)
                      << "\",\"registration_verified\":" << (registration_verified ? "true" : "false")
                      << ",\"registration_requested_flags\":" << RIDEV_DEVNOTIFY
                      << ",\"registration_snapshot_flags\":" << registration_snapshot_flags
                      << ",\"registration_removed_verified\":" << (registration_removed_verified ? "true" : "false")
                      << ",\"end_target_verified\":" << (end_target_verified ? "true" : "false")
                      << ",\"observed_window_ms\":" << now_ms() - trial_started
                      << ",\"safe_timer_ticks\":" << safe_ticks << ",\"wm_input\":" << wm_input
                      << ",\"wm_input_total\":" << wm_input_total << ",\"wm_input_inactive\":" << wm_input_inactive
                      << ",\"input_guard_rejected\":" << input_guard_rejected << ",\"input_deadline_rejected\":" << input_deadline_rejected
                      << ",\"window_valid\":" << (window_valid ? "true" : "false")
                      << ",\"window_thread_valid\":" << (window_thread_valid ? "true" : "false")
                      << ",\"registration_checks\":" << registration_checks
                      << ",\"device_change_total\":" << device_changes << ",\"device_arrivals\":" << device_arrivals
                      << ",\"device_removals\":" << device_removals << ",\"target_removals\":" << target_removals
                      << ",\"header_target\":" << d.target_headers << ",\"header_other\":" << d.other_headers
                      << ",\"stale_session_filtered\":" << d.stale_session << ",\"payload_batches\":" << d.payload_batches
                      << ",\"padded_batches\":" << d.padded_batches << ",\"last_input_error\":" << d.last_error
                      << ",\"reports\":" << c.reports << ",\"marker_73\":" << c.marker73
                      << ",\"marker_74\":" << c.marker74 << ",\"other\":" << c.other
                      << ",\"consecutive_duplicates\":" << c.consecutive_duplicates
                      << ",\"repeated_73\":" << c.repeated73 << ",\"repeated_74\":" << c.repeated74
                      << ",\"marker_74_before_73\":" << c.marker74_before73
                      << ",\"field_hypothesis_selected\":" << c.field_hypothesis_selected
                      << ",\"field_hypothesis_other\":" << c.field_hypothesis_other
                      << ",\"parameter_hypothesis_0_to_4_other\":[" << c.parameter_hypothesis[0] << ','
                      << c.parameter_hypothesis[1] << ',' << c.parameter_hypothesis[2] << ','
                      << c.parameter_hypothesis[3] << ',' << c.parameter_hypothesis[4] << ',' << c.parameter_hypothesis[5] << ']'
                      << ",\"first_ms\":" << c.first_ms << ",\"last_ms\":" << c.last_ms
                      << ",\"interval_count\":" << c.intervals << ",\"interval_sum_ms\":" << c.interval_sum_ms
                      << ",\"interval_max_ms\":" << c.interval_max_ms
                      << ",\"rt_on\":" << c.rt_on << ",\"rt_off\":" << c.rt_off << ",\"rt_invalid\":" << c.rt_invalid
                      << ",\"rt_changes\":" << c.rt_changes << ",\"rt_repeats\":" << c.rt_repeats
                      << ",\"rt_current\":\"" << state_name(observation.rt_state()) << "\",\"invalid_length\":" << c.invalid_length
                      << ",\"rt_last_valid\":\"" << (c.rt_last_valid_state == 1 ? "on" : c.rt_last_valid_state == 0 ? "off" : "unknown") << '"'
                      << ",\"invalid_report_id\":" << c.invalid_report_id << ",\"read_failures\":";
            histogram(d.failures); std::cout << ",\"report_id_histogram\":"; histogram(c.report_ids);
            std::cout << ",\"report_length_batch_histogram\":"; histogram(c.report_lengths);
            std::cout << ",\"physical_identity\":\"UNKNOWN\",\"press_release\":\"UNKNOWN\",\"cleanup_ok\":"
                      << (cleanup_ok ? "true" : "false") << "}" << std::endl;
        } else cleanup_ok = subscription.close() && cleanup_ok;
        if (abort || !cleanup_ok || trials >= 3) {
            fatal = fatal || abort || !cleanup_ok; finished = true;
            SetWindowTextW(status, L"Stopped. Close this window. No listener is active.");
            EnableWindow(start_button, FALSE);
            PostMessageW(window, WM_CLOSE, 0, 0);
        } else {
            idle_since = now_ms();
            SetWindowTextW(status, L"Trial ended. Click Start only when ready for the next repeat.");
            EnableWindow(start_button, TRUE);
        }
    }
    void start() {
        if (observation.active() || finished) return;
        if (!desktop_safe() || GetForegroundWindow() != window) { stop(Stop::DesktopUnavailable, true); return; }
        DWORD process{};
        window_valid = IsWindow(window) != FALSE;
        window_thread_valid = GetWindowThreadProcessId(window, &process) == GetCurrentThreadId() && process == GetCurrentProcessId();
        if (!window_valid || !window_thread_valid) { stop(Stop::RegistrationFailed, true); return; }
        const auto current = find_target(scope);
        if (current.matches != 1 || current.handle != target.handle) { stop(Stop::Ambiguous, true); return; }
        if (!no_existing_registration() || !subscription.start(true)) { stop(Stop::RegistrationFailed, true); return; }
        registration_verified = registration_matches(scope, window, false, &registration_snapshot_flags);
        if (!registration_verified) { stop(Stop::RegistrationFailed, true); return; }
        wm_input = safe_ticks = input_guard_rejected = input_deadline_rejected = 0;
        registration_checks = 1; delivery.reset();
        registration_removed_verified = end_target_verified = false;
        trial_started = now_ms();
        observation.arm(scope, test_case, reinterpret_cast<uintptr_t>(target.handle), generation, now_ms(), seconds * 1000ull);
        EnableWindow(start_button, FALSE);
        SetWindowTextW(status, L"TEST ACTIVE: perform only the selected action once. Auto-stop at timeout.");
    }
    void input(HRAWINPUT raw) {
        if (!note_input_entry(observation.active(), wm_input_total, wm_input_inactive, wm_input)) return;
        if (!desktop_safe() || GetForegroundWindow() != window) { ++input_guard_rejected; stop(Stop::DesktopUnavailable, true); return; }
        if (!observation.tick(now_ms())) { ++input_deadline_rejected; stop_after_core(); return; }
        NativeInputSource source(raw);
        delivery.receive(source, native_layout, reinterpret_cast<uintptr_t>(target.handle), generation, now_ms(), observation);
        if (!observation.active()) stop_after_core();
    }
    // Core can stop before adapter cleanup. Temporarily re-arm is forbidden;
    // preserve counts and print the ended trial through the common path.
    void stop_after_core() {
        const Stop why = observation.stop();
        emit_core_end(why);
    }
    void emit_core_end(Stop why) {
        // stop() also needs to report an already-ended core. This flag allows
        // the same summary path without reopening capture or retaining bytes.
        pending_core_end = true;
        stop(why, why != Stop::Timeout);
        pending_core_end = false;
    }
    bool pending_core_end{};
    void timer() {
        if (!desktop_safe()) { stop(Stop::DesktopUnavailable, true); PostMessageW(window, WM_CLOSE, 0, 0); return; }
        if (observation.active()) {
            if (GetForegroundWindow() != window) { stop(Stop::FocusLost, true); return; }
            const auto current = find_target(scope); // Cached metadata only; no HID query.
            if (current.matches != 1 || current.handle != target.handle) {
                advance_disconnected_session(reinterpret_cast<uintptr_t>(target.handle), reinterpret_cast<uintptr_t>(target.handle), generation);
                stop(Stop::Disconnected, true); target.handle = nullptr; return;
            }
            ++registration_checks;
            if (!IsWindow(window) || !registration_matches(scope, window)) { stop(Stop::RegistrationFailed, true); return; }
            ++safe_ticks;
            wchar_t progress[160]{};
            const auto remaining_ms = seconds * 1000ull - std::min(seconds * 1000ull, now_ms() - trial_started);
            const auto gate = observation.rt_state();
            const wchar_t* gate_name = gate == FreshState::On ? L"ON" : gate == FreshState::Off ? L"OFF" : L"UNKNOWN";
            swprintf_s(progress, L"TEST ACTIVE - trial %u/3 - %llu seconds left. RT state: %ls. Only selected action.",
                       trials + 1, (remaining_ms + 999) / 1000, gate_name);
            SetWindowTextW(status, progress);
            if (!observation.tick(now_ms())) stop_after_core();
        } else if (now_ms() - idle_since >= 60000) {
            stop(Stop::IdleTimeout, true); PostMessageW(window, WM_CLOSE, 0, 0);
        }
    }
};

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        app->window = hwnd;
    }
    if (!app) return DefWindowProcW(hwnd, message, w, l);
    try {
        switch (message) {
        case WM_COMMAND:
            if (LOWORD(w) == start_id && HIWORD(w) == BN_CLICKED) app->start();
            if (LOWORD(w) == stop_id && HIWORD(w) == BN_CLICKED) app->stop(Stop::Cancel, true);
            return 0;
        case WM_INPUT:
            app->input(reinterpret_cast<HRAWINPUT>(l));
            // DefWindowProc must clean foreground WM_INPUT buffers.
            DefWindowProcW(hwnd, message, w, l); return 0;
        case WM_INPUT_DEVICE_CHANGE:
            ++app->device_changes;
            if (w == GIDC_REMOVAL) ++app->device_removals;
            if (w == GIDC_ARRIVAL) ++app->device_arrivals;
            if (w == GIDC_REMOVAL && advance_disconnected_session(reinterpret_cast<uintptr_t>(app->target.handle),
                static_cast<uintptr_t>(l), app->generation)) {
                ++app->target_removals;
                app->stop(Stop::Disconnected, true); app->target.handle = nullptr;
            } else if (w == GIDC_ARRIVAL) {
                const auto current = find_target(app->scope);
                if (current.matches != 1 || current.handle != app->target.handle) app->stop(Stop::Ambiguous, true);
            }
            return 0;
        case WM_WTSSESSION_CHANGE:
            if (w == WTS_SESSION_LOCK || w == WTS_SESSION_LOGOFF || w == WTS_CONSOLE_DISCONNECT || w == WTS_REMOTE_CONNECT)
                app->stop(Stop::Locked, true);
            return 0;
        case WM_ACTIVATEAPP:
            if (!w && app->observation.active()) app->stop(Stop::FocusLost, true);
            break;
        case WM_TIMER: app->timer(); return 0;
        case WM_CLOSE:
            if (!app->finished) app->stop(Stop::Cancel, true);
            DestroyWindow(hwnd); return 0;
        case WM_DESTROY:
            KillTimer(hwnd, timer_id);
            if (app->wts) { app->cleanup_ok = WTSUnRegisterSessionNotification(hwnd) != FALSE && app->cleanup_ok; app->wts = false; }
            app->window = nullptr; PostQuitMessage(0); return 0;
        }
    } catch (...) {
        // No allocation or formatting while unwinding a native callback.
        app->pending_core_end = app->observation.active(); app->run_stop = Stop::Exception;
        app->observation.finish(Stop::Exception);
        app->cleanup_ok = app->subscription.close() && app->cleanup_ok;
        app->fatal = true; app->finished = true;
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        if (message == WM_INPUT) { DefWindowProcW(hwnd, message, w, l); return 0; }
    }
    return DefWindowProcW(hwnd, message, w, l);
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        App app; bool preflight = false, registration_audit = false;
        try {
            for (int i = 1; i < argc; ++i) {
                const std::wstring option = argv[i];
                if (option == L"--preflight") preflight = true;
                else if (option == L"--registration-audit") registration_audit = true;
                else if (option == L"--collection" && i + 1 < argc) {
                    const std::wstring value = argv[++i];
                    if (value == L"control") app.scope = Scope::Control;
                    else if (value == L"notifications") app.scope = Scope::Notifications;
                    else throw std::runtime_error("invalid collection");
                } else if (option == L"--case" && i + 1 < argc) {
                    const std::wstring value = argv[++i]; bool found = false;
                    for (const auto& test_case : cases) if (value == test_case.name) { app.test_case = test_case; found = true; }
                    if (!found) throw std::runtime_error("invalid case");
                } else if (option == L"--timeout" && i + 1 < argc) {
                    const std::wstring value = argv[++i];
                    if (value.empty() || value.find_first_not_of(L"0123456789") != std::wstring::npos) throw std::runtime_error("invalid timeout");
                    const auto number = std::stoul(value);
                    if (number < 5 || number > 30) throw std::runtime_error("invalid timeout");
                    app.seconds = static_cast<unsigned>(number);
                } else throw std::runtime_error("invalid arguments");
            }
            if (preflight && registration_audit) throw std::runtime_error("incompatible modes");
            if (app.scope == Scope::Control && case_name(app.test_case) == "RTGATE") throw std::runtime_error("RTGATE requires notifications");
        } catch (...) { return initialization_failed("cli", ERROR_INVALID_PARAMETER, "invalid_arguments_or_incompatible_mode"); }
        const auto desktop = desktop_check();
        app.target = find_target(app.scope);
        const auto read = registration_read();
        const auto gate = startup_check(desktop, app.target.matches, read);
        const auto spec = contract(app.scope);
        // All three modes share the same process/session/collection gate. Window
        // validity and foreground are checked later, at explicit Start only.
        std::cout << "{\"classification\":\"" << (preflight ? "READ_ONLY_PREFLIGHT" : "STARTUP_PREFLIGHT")
                  << "\",\"mode\":\"" << (preflight ? "preflight" : registration_audit ? "registration_audit" : "interactive")
                  << "\",\"local_unlocked_console\":" << (desktop.ready() ? "true" : "false")
                  << ",\"collection\":\"" << spec.collection << "\",\"matching_collections\":" << app.target.matches << ',';
        check_fields(gate);
        std::cout << ",\"registration_returned\":" << read.returned << ",\"registration_capacity_out\":" << read.capacity_out
                  << ",\"registration_win32_error\":" << read.win32_error
                  << ",\"window_created\":false,\"foreground_check\":\""
                  << (registration_audit ? "not_applicable_metadata_only" : "deferred_until_explicit_start") << "\""
                  << ",\"capture_started\":false,\"raw_registration\":false,\"physical_test\":\"NOT RUN\",\"stop\":\""
                  << (gate.ready() ? "preflight_ready" : "preflight_blocked") << "\"}" << std::endl;
        if (preflight || !gate.ready()) return gate.ready() ? 0 : 2;
        app.session = fresh_session();
        app.case_label = case_name(app.test_case);
        app.window_title = L"M605 - ";
        app.window_title += app.scope == Scope::Control ? L"control / MI_01" : L"notifications / MI_02 Col03";
        app.window_title += L" - "; app.window_title += app.test_case.name;
        Handle single{CreateMutexW(nullptr, TRUE, L"Local\\AceHFXAuraStageBPassiveObserver")};
        if (!single.value || GetLastError() == ERROR_ALREADY_EXISTS) return initialization_failed("single_instance", GetLastError());
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSW cls{}; cls.lpfnWndProc = window_proc; cls.hInstance = instance;
        cls.lpszClassName = L"AceHFXStageBPassiveWindow"; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        if (!RegisterClassW(&cls)) return initialization_failed("window_class", GetLastError());
        struct ClassCleanup { HINSTANCE instance; App& app; ~ClassCleanup() { app.cleanup(); UnregisterClassW(L"AceHFXStageBPassiveWindow", instance); } } class_cleanup{instance, app};
        app.window = CreateWindowExW(0, cls.lpszClassName, app.window_title.c_str(), WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                     CW_USEDEFAULT, CW_USEDEFAULT, 710, 255, nullptr, nullptr, instance, &app);
        if (!app.window) return initialization_failed("window_create", GetLastError());
        if (registration_audit) {
            // No message pump and no Observation::arm: no report payload can be
            // fetched. Check reversible Windows registration metadata only.
            app.native.scope = app.scope; app.native.window = app.window;
            const bool added = app.subscription.start(no_existing_registration());
            RegistrationRead snapshot;
            const bool exact = added && registration_matches(app.scope, app.window, true, nullptr, &snapshot);
            const bool remove_ok = app.subscription.close();
            const auto after = registration_read();
            const bool removed = remove_ok && empty_registration_check(after).ready();
            StartupCheck audit;
            if (!added) audit = {"registration_add", "registration_not_added", "RegisterRawInputDevices", app.native.add_error};
            else if (!exact) audit = {"registration_snapshot", snapshot.returned == UINT(-1) ? "snapshot_query_failed" : "snapshot_mismatch",
                                      "GetRegisteredRawInputDevices", snapshot.win32_error};
            else if (!remove_ok) audit = {"registration_remove", "registration_not_removed", "RegisterRawInputDevices", app.native.remove_error};
            else if (!removed) audit = empty_registration_check(after);
            app.cleanup();
            std::cout << "{\"classification\":\"READ_ONLY_REGISTRATION_AUDIT\",\"added\":" << (added ? "true" : "false")
                      << ",\"exact_snapshot\":" << (exact ? "true" : "false") << ",\"removed\":" << (removed ? "true" : "false")
                      << ',';
            check_fields(audit);
            std::cout << ",\"after_remove_returned\":" << after.returned << ",\"after_remove_capacity_out\":" << after.capacity_out
                      << ",\"after_remove_win32_error\":" << after.win32_error
                      << ",\"registration_error\":" << app.native.add_error << ",\"removal_error\":" << app.native.remove_error
                      << ",\"capture_started\":false,\"payloads_read\":0,\"physical_test\":\"NOT RUN\"}" << std::endl;
            return exact && removed ? 0 : 2;
        }
        const HWND instruction = CreateWindowW(L"STATIC", app.test_case.instruction, WS_CHILD | WS_VISIBLE, 16, 12, 660, 35, app.window, nullptr, instance, nullptr);
        const HWND privacy = CreateWindowW(L"STATIC", L"Three separate trials. No typing elsewhere. Only aggregate marker counts are retained.\n73/74 semantics and numeric key fields remain UNKNOWN.", WS_CHILD | WS_VISIBLE,
                      16, 48, 660, 45, app.window, nullptr, instance, nullptr);
        app.status = CreateWindowW(L"STATIC", L"Not listening. Click Start trial only in a safe dedicated test session.", WS_CHILD | WS_VISIBLE,
                                    16, 101, 660, 35, app.window, nullptr, instance, nullptr);
        app.start_button = CreateWindowW(L"BUTTON", L"Start trial", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 16, 151, 180, 30,
                                         app.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(start_id)), instance, nullptr);
        const HWND stop_button = CreateWindowW(L"BUTTON", L"Stop", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 218, 151, 120, 30,
                                               app.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(stop_id)), instance, nullptr);
        app.native.scope = app.scope; app.native.window = app.window;
        if (!instruction || !privacy || !app.status || !app.start_button || !stop_button)
            return initialization_failed("controls_create", GetLastError());
        if (!WTSRegisterSessionNotification(app.window, NOTIFY_FOR_THIS_SESSION))
            return initialization_failed("session_notifications", GetLastError());
        app.wts = true; app.idle_since = now_ms();
        if (!SetTimer(app.window, timer_id, 100, nullptr)) return initialization_failed("safety_timer", GetLastError());
        ShowWindow(app.window, SW_SHOW); UpdateWindow(app.window);
        MSG message{}; BOOL fetched;
        while ((fetched = GetMessageW(&message, nullptr, 0, 0)) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
        const auto loop_stop = message_loop_end(fetched, app.finished);
        if (fetched == -1) app.loop_error = GetLastError();
        if (loop_stop != Stop::None) app.stop(loop_stop, true);
        else if (app.pending_core_end) app.stop(app.run_stop, true);
        app.cleanup();
        std::cout << "{\"classification\":\"CLEANUP\",\"trials\":" << app.trials << ",\"raw_memory_clear\":"
                  << (app.observation.raw_memory_clear() ? "true" : "false") << ",\"stop\":\"" << reason(app.run_stop)
                  << "\",\"message_loop_error\":" << app.loop_error << ",\"registration_error\":" << app.native.add_error
                  << ",\"removal_error\":" << app.native.remove_error
                  << ",\"wm_input_total\":" << app.wm_input_total << ",\"device_change_total\":" << app.device_changes
                  << ",\"cleanup_ok\":" << (app.cleanup_ok ? "true" : "false") << "}" << std::endl;
        return app.fatal || !app.cleanup_ok ? 2 : 0;
    } catch (...) {
        std::cout << "{\"classification\":\"UNHANDLED_EXCEPTION\",\"capture_started\":\"unknown\",\"stop\":\"unhandled_exception\",\"physical_test\":\"INCONCLUSIVE\"}" << std::endl;
        return 2;
    }
}
