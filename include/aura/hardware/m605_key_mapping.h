#pragma once

#include <cstdint>
#include <optional>

namespace aura::m605 {

// Logical IDs encode (row << 8) | col. Unknown table entries fail closed.
// This table is for magnetic switch Wire IDs, never lighting LEDs or HID usages.
std::optional<uint16_t> WireIdForLogicalKey(uint16_t logical_key_id);

// SpeedTap additionally verified A, D, W and S. Keep this command-scoped
// lookup separate so adding SpeedTap evidence does not widen the frozen
// Actuation, Rapid Trigger or Deadzone setters.
std::optional<uint16_t> SpeedTapWireIdForLogicalKey(uint16_t logical_key_id);

} // namespace aura::m605
