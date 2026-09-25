#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace aura::m605 {

using Report = std::array<uint8_t, 65>;

// Builders accept logical identities only. Invalid or unverified inputs
// produce no report, so callers cannot fall back to a guessed Wire ID.
std::optional<Report> BuildPerKeyActuation(uint16_t logical_key_id, double millimeters);
std::optional<Report> BuildAnalogEffect(uint8_t effect_id, uint8_t enabled);
// Fixed Press/Release pair; no caller-supplied vendor packet list.
std::optional<std::array<Report, 2>> BuildPerKeyRapidTriggerStages(
    uint16_t logical_key_id, double press_mm, double release_mm, bool enabled);
std::optional<Report> BuildPerKeyDeadzone(
    uint16_t logical_key_id, double top_mm, double bottom_mm);
// Destructive table reset. Values must come from authoritative application
// configuration, not an assumed hardware readback. Only layer 0 is supported.
std::optional<Report> BuildResetAllPerKeyDeadzoneOverrides(
    uint8_t global_bottom_raw, uint8_t global_top_raw, uint8_t layer = 0);
std::optional<Report> BuildSpeedTapPair(
    uint16_t logical_key_1, uint16_t logical_key_2, uint8_t enabled);
std::optional<Report> BuildSpeedTapMaster(uint8_t enabled);
// Restores runtime pair state toward the active-profile baseline; it does not
// prove the device's full pair table is empty.
Report BuildResetSpeedTapRuntimeToProfile();
Report BuildRuntimeApply();

} // namespace aura::m605
