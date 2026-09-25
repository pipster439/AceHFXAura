#include "aura/hardware/m605_protocol.h"
#include "aura/hardware/m605_key_mapping.h"
#include <cmath>

namespace aura::m605 {

namespace {
std::optional<uint8_t> ScaleMillimeters(double millimeters, double min_mm,
                                         double max_mm, int min_raw, int max_raw) {
    if (!std::isfinite(millimeters) || millimeters < min_mm || millimeters > max_mm) {
        return std::nullopt;
    }
    const auto raw = static_cast<int>(std::lround(millimeters * 10.0));
    if (raw < min_raw || raw > max_raw) return std::nullopt;
    return static_cast<uint8_t>(raw);
}
} // namespace

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

std::optional<std::array<Report, 2>> BuildPerKeyRapidTriggerStages(
    uint16_t logical_key_id, double press_mm, double release_mm, bool enabled) {
    const auto wire_id = WireIdForLogicalKey(logical_key_id);
    const auto press = ScaleMillimeters(press_mm, 0.1, 2.5, 1, 25);
    const auto release = ScaleMillimeters(release_mm, 0.1, 2.5, 1, 25);
    if (!wire_id || !press || !release) return std::nullopt;

    std::array<Report, 2> stages{};
    for (size_t i = 0; i < stages.size(); ++i) {
        Report& report = stages[i];
        report[1] = 0x51;
        report[2] = 0x54;
        report[3] = static_cast<uint8_t>(i + 1); // Press, then Release.
        report[5] = static_cast<uint8_t>(*wire_id & 0xff);
        report[6] = static_cast<uint8_t>(*wire_id >> 8);
        report[7] = i == 0 ? *press : *release;
        report[9] = enabled ? 1 : 0;
    }
    return stages;
}

std::optional<Report> BuildPerKeyDeadzone(
    uint16_t logical_key_id, double top_mm, double bottom_mm) {
    const auto wire_id = WireIdForLogicalKey(logical_key_id);
    const auto top = ScaleMillimeters(top_mm, 0.0, 0.5, 0, 5);
    const auto bottom = ScaleMillimeters(bottom_mm, 0.0, 0.5, 0, 5);
    if (!wire_id || !top || !bottom) return std::nullopt;

    Report report{};
    report[1] = 0x51;
    report[2] = 0x59;
    report[5] = static_cast<uint8_t>(*wire_id & 0xff);
    report[6] = static_cast<uint8_t>(*wire_id >> 8);
    report[7] = *bottom; // Verified order: Bottom before Top.
    report[8] = *top;
    return report;
}

std::optional<Report> BuildResetAllPerKeyDeadzoneOverrides(
    uint8_t global_bottom_raw, uint8_t global_top_raw, uint8_t layer) {
    if (global_bottom_raw > 5 || global_top_raw > 5 || layer != 0) return std::nullopt;

    Report report{};
    report[1] = 0x51;
    report[2] = 0x52;
    report[3] = 0x04; // RESET ALL per-key Deadzone overrides.
    report[5] = global_bottom_raw;
    report[6] = layer;
    report[7] = global_top_raw;
    return report;
}

std::optional<Report> BuildSpeedTapPair(
    uint16_t logical_key_1, uint16_t logical_key_2, uint8_t enabled) {
    if (logical_key_1 == logical_key_2 || enabled > 1) return std::nullopt;
    const auto wire_1 = SpeedTapWireIdForLogicalKey(logical_key_1);
    const auto wire_2 = SpeedTapWireIdForLogicalKey(logical_key_2);
    if (!wire_1 || !wire_2 || *wire_1 == *wire_2) return std::nullopt;

    Report report{};
    report[1] = 0x51;
    report[2] = 0x55;
    report[5] = static_cast<uint8_t>(*wire_1 & 0xff);
    report[6] = static_cast<uint8_t>(*wire_1 >> 8);
    report[7] = static_cast<uint8_t>(*wire_2 & 0xff);
    report[8] = static_cast<uint8_t>(*wire_2 >> 8);
    report[9] = enabled;
    return report;
}

std::optional<Report> BuildSpeedTapMaster(uint8_t enabled) {
    if (enabled > 1) return std::nullopt;
    Report report{};
    report[1] = 0x51;
    report[2] = 0x57;
    report[5] = enabled;
    return report;
}

Report BuildResetSpeedTapRuntimeToProfile() {
    Report report{};
    report[1] = 0x51;
    report[2] = 0x56;
    return report;
}

} // namespace aura::m605
