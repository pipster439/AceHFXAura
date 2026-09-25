#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace aura::m605 {

using Report = std::array<uint8_t, 65>;

enum class DksTriggerState : uint8_t {
    Inactive = 0, Tap = 1, Release = 2, Hold = 3
};

struct DksTarget {
    enum class Kind : uint8_t { LogicalKey, DefaultSentinel };
    Kind kind = Kind::DefaultSentinel;
    uint16_t logical_key_id = 0;
    static DksTarget LogicalKey(uint16_t id) { return {Kind::LogicalKey, id}; }
    static DksTarget DefaultSentinel() { return {Kind::DefaultSentinel, 0}; }
};

struct DksSlot {
    DksTarget target;
    DksTriggerState down_start = DksTriggerState::Inactive;
    DksTriggerState down_end = DksTriggerState::Inactive;
    DksTriggerState up_start = DksTriggerState::Inactive;
    DksTriggerState up_end = DksTriggerState::Inactive;
};

struct DksConfig {
    uint16_t source_logical_key_id = 0;
    double start_mm = 0;
    double end_mm = 0;
    std::array<DksSlot, 4> slots{};
};

std::optional<uint8_t> EncodeDksTriggerMask(const DksSlot& slot);
std::optional<Report> BuildPerKeyDksSlot(const DksConfig& config, uint8_t slot_index);
std::optional<std::array<Report, 4>> BuildPerKeyDksStages(const DksConfig& config);
// Exact four-slot ASUS-style rewrite physically verified in Stage 8B.
DksConfig StandardDksConfiguration(uint16_t source_logical_key_id);

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
