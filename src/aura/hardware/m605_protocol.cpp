#include "aura/hardware/m605_protocol.h"
#include "aura/hardware/m605_key_mapping.h"
#include <cmath>

namespace aura::m605 {

std::optional<Report> BuildPerKeyActuation(uint16_t logical_key_id, double millimeters) {
    const auto wire_id = WireIdForLogicalKey(logical_key_id);
    if (!wire_id || !std::isfinite(millimeters) || millimeters < 0.1 || millimeters > 4.0) {
        return std::nullopt;
    }
    const auto raw = static_cast<int>(std::lround(millimeters * 10.0));
    if (raw < 1 || raw > 40) return std::nullopt;

    Report report{};
    report[1] = 0x51;
    report[2] = 0x4f;
    report[5] = static_cast<uint8_t>(*wire_id & 0xff);
    report[6] = static_cast<uint8_t>(*wire_id >> 8);
    report[7] = static_cast<uint8_t>(raw);
    return report;
}

std::optional<Report> BuildAnalogEffect(uint8_t effect_id, uint8_t enabled) {
    // Only Static/恒亮 (effect 0) has a physical ON/OFF closed loop.
    if (effect_id != 0 || enabled > 1) return std::nullopt;
    Report report{};
    report[1] = 0x51;
    report[2] = 0x2d;
    report[5] = effect_id;
    report[6] = enabled;
    return report;
}

Report BuildRuntimeApply() {
    Report report{};
    report[1] = 0x50;
    report[2] = 0x55;
    return report;
}

} // namespace aura::m605
