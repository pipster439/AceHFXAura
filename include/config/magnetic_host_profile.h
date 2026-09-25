#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace aura {

// Saved ASUS host intent only. No field in this structure is device readback.
struct MagneticHostProfile {
    std::optional<int> active_profile_id;
    std::optional<uint8_t> global_actuation_raw;
    std::optional<uint8_t> global_rt_press_raw;
    std::optional<uint8_t> global_rt_release_raw;
    bool per_key_rt_list_known = false;
    std::map<uint16_t, bool> per_key_rt_enabled;
    std::optional<uint8_t> global_deadzone_top_raw;
    std::optional<uint8_t> global_deadzone_bottom_raw;
    std::map<std::pair<uint16_t, uint16_t>, bool> saved_speedtap_pairs;
    bool saved_speedtap_pairs_known = false;
    std::optional<bool> static_analog_effect;
    // Per-key/DKS fields without an independently established schema remain Unknown.
};

class MagneticHostProfileProvider {
public:
    // Discovery is read-only and fail-closed if more than one device/profile
    // candidate exists or the active-profile identity is not trustworthy.
    static MagneticHostProfile LoadFromDirectory(const std::filesystem::path& directory);
    static MagneticHostProfile LoadProduction();
    // Pure parser for deterministic fixtures; the two XML files must identify
    // the same serial and the selected active profile.
    static MagneticHostProfile Parse(const std::string& config_xml,
                                     const std::string& profile_xml,
                                     const std::string& serial);
};

} // namespace aura
