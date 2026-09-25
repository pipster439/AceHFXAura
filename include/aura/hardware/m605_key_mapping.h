#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace aura::m605 {

namespace detail {
struct VerifiedPhysicalKey {
    uint16_t logical_id;
    uint16_t wire_id;
};

// Explicit 68-key Stage 7A audit of AacKbHal_x64.dll v1.3.46.0,
// DAT_1801caef0 (RVA 0x1caef0). Do not index the vendor's 600 slots with
// arbitrary logical IDs. These are magnetic Wire IDs, not LEDs or HID usages.
inline constexpr std::array<VerifiedPhysicalKey, 68> kVerifiedPhysicalKeys{{
    {0x0100, 0x0001}, // Esc
    {0x0600, 0x0002}, // 1
    {0x0700, 0x0003}, // 2
    {0x0101, 0x0004}, // 3
    {0x0102, 0x0005}, // 4
    {0x0103, 0x0006}, // 5
    {0x0104, 0x0007}, // 6
    {0x0105, 0x0008}, // 7
    {0x0106, 0x0009}, // 8
    {0x0107, 0x000a}, // 9
    {0x0108, 0x000b}, // 0
    {0x0109, 0x000c}, // Minus
    {0x010a, 0x000d}, // Equal
    {0x0706, 0x000f}, // Backspace
    {0x0608, 0x004b}, // Insert

    {0x0200, 0x0010}, // Tab
    {0x0601, 0x0011}, // Q
    {0x0701, 0x0012}, // W
    {0x0201, 0x0013}, // E
    {0x0202, 0x0014}, // R
    {0x0203, 0x0015}, // T
    {0x0204, 0x0016}, // Y
    {0x0205, 0x0017}, // U
    {0x0206, 0x0018}, // I
    {0x0207, 0x0019}, // O
    {0x0208, 0x001a}, // P
    {0x0209, 0x001b}, // LeftBracket
    {0x020a, 0x001c}, // RightBracket
    {0x0607, 0x001d}, // Backslash
    {0x0708, 0x004c}, // Delete

    {0x0300, 0x001e}, // CapsLock
    {0x0602, 0x001f}, // A
    {0x0702, 0x0020}, // S
    {0x0301, 0x0021}, // D
    {0x0302, 0x0022}, // F
    {0x0303, 0x0023}, // G
    {0x0304, 0x0024}, // H
    {0x0305, 0x0025}, // J
    {0x0306, 0x0026}, // K
    {0x0307, 0x0027}, // L
    {0x0308, 0x0028}, // Semicolon
    {0x0309, 0x0029}, // Quote
    {0x0707, 0x002b}, // Enter
    {0x060b, 0x0055}, // PageUp

    {0x0400, 0x002c}, // LeftShift
    {0x0703, 0x002e}, // Z
    {0x0401, 0x002f}, // X
    {0x0501, 0x0030}, // C
    {0x0402, 0x0031}, // V
    {0x0403, 0x0032}, // B
    {0x0404, 0x0033}, // N
    {0x0405, 0x0034}, // M
    {0x0406, 0x0035}, // Comma
    {0x0407, 0x0036}, // Period
    {0x0408, 0x0037}, // Slash
    {0x040a, 0x0039}, // RightShift
    {0x070a, 0x0053}, // UpArrow
    {0x070b, 0x0056}, // PageDown

    {0x0500, 0x003a}, // LeftCtrl
    {0x0604, 0x003b}, // LeftWin
    {0x0704, 0x003c}, // LeftAlt
    {0x0503, 0x003d}, // Space
    {0x0507, 0x003e}, // RightAlt
    {0x0508, 0x009f}, // Fn: audited outlier, not a numeric-range exception
    {0x050a, 0x0040}, // RightCtrl/Copilot physical key
    {0x060a, 0x004f}, // LeftArrow
    {0x050b, 0x0054}, // DownArrow
    {0x040b, 0x0059}, // RightArrow
}};

constexpr bool UniqueVerifiedPhysicalKeys() {
    for (size_t i = 0; i < kVerifiedPhysicalKeys.size(); ++i) {
        if (kVerifiedPhysicalKeys[i].logical_id == 0 ||
            kVerifiedPhysicalKeys[i].wire_id == 0) return false;
        for (size_t j = i + 1; j < kVerifiedPhysicalKeys.size(); ++j) {
            if (kVerifiedPhysicalKeys[i].logical_id == kVerifiedPhysicalKeys[j].logical_id ||
                kVerifiedPhysicalKeys[i].wire_id == kVerifiedPhysicalKeys[j].wire_id) return false;
        }
    }
    return true;
}
static_assert(UniqueVerifiedPhysicalKeys(), "M605 audited keys must be unique");
} // namespace detail

inline constexpr size_t VerifiedM605PhysicalKeyCount() {
    return detail::kVerifiedPhysicalKeys.size();
}

inline constexpr bool IsVerifiedM605WireId(uint16_t wire_id) {
    for (const auto& entry : detail::kVerifiedPhysicalKeys) {
        if (entry.wire_id == wire_id) return true;
    }
    return false;
}

// Logical IDs encode (row << 8) | col, but only the explicit audited keys
// above are accepted. No HID Usage, LED ID or physical-order derivation.
std::optional<uint16_t> WireIdForLogicalKey(uint16_t logical_key_id);

// Compatibility entrypoint retained for Phase 3 callers. Uses the same
// canonical mapping; no second SpeedTap-specific table exists.
std::optional<uint16_t> SpeedTapWireIdForLogicalKey(uint16_t logical_key_id);

} // namespace aura::m605
