#pragma once
#include <cstdint>
#include <cstring>

namespace stage_b {
// Metadata only; no OS calls, window creation, registration or input acquisition.
struct StartupCheck {
    const char* blocked_stage = "none";
    const char* reason = "ready";
    const char* api = "none";
    uint32_t win32_error{};
    bool ready() const { return std::strcmp(blocked_stage, "none") == 0; }
};
struct RegistrationRead {
    uint32_t returned{}, capacity_out{}, win32_error{};
};
inline StartupCheck empty_registration_check(const RegistrationRead& read) {
    if (read.returned == 0xffffffffu)
        return {"registration_table", "enumeration_failed", "GetRegisteredRawInputDevices", read.win32_error};
    // Success returns the number WRITTEN. capacity_out is not a guaranteed
    // success-path count: Windows can leave our input capacity (1) unchanged.
    if (read.returned != 0)
        return {"registration_table", "existing_process_registration", "GetRegisteredRawInputDevices", 0};
    return {};
}
inline StartupCheck startup_check(const StartupCheck& desktop, unsigned matches, const RegistrationRead& read) {
    if (!desktop.ready()) return desktop;
    if (matches != 1)
        return {"target_collection", matches == 0 ? "no_matching_collection" : "ambiguous_matching_collections", "none", 0};
    return empty_registration_check(read);
}
} // namespace stage_b
