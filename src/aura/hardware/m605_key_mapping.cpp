#include "aura/hardware/m605_key_mapping.h"
#include <array>
#include <cstddef>

namespace aura::m605 {

namespace {
constexpr size_t kRows = 8;
constexpr size_t kCols = 75;
}

std::optional<uint16_t> WireIdForLogicalKey(uint16_t logical_key_id) {
    const size_t row = logical_key_id >> 8;
    const size_t col = logical_key_id & 0xff;
    if (row >= kRows || col >= kCols) return std::nullopt;

    // M605 lookup index is row + col * 8. Populate only keys in the
    // verified runtime contract; zero means unknown, not Wire ID zero.
    static const std::array<uint16_t, kRows * kCols> table = [] {
        std::array<uint16_t, kRows * kCols> values{};
        values[5 + 1 * kRows] = 0x0030; // C: logical 0x0501
        values[4 + 2 * kRows] = 0x0031; // V: logical 0x0402
        return values;
    }();
    const uint16_t wire_id = table[row + col * kRows];
    if (wire_id == 0) return std::nullopt;
    return wire_id;
}

std::optional<uint16_t> SpeedTapWireIdForLogicalKey(uint16_t logical_key_id) {
    switch (logical_key_id) {
    case 0x0602: return 0x001f; // A: logical 1538
    case 0x0301: return 0x0021; // D: logical 769
    case 0x0701: return 0x0012; // W: logical 1793
    case 0x0702: return 0x0020; // S: logical 1794
    default: return WireIdForLogicalKey(logical_key_id); // existing V/C mappings
    }
}

} // namespace aura::m605
