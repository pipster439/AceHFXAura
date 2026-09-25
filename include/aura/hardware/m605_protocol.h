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
Report BuildRuntimeApply();

} // namespace aura::m605
