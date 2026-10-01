#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace aura {
// Device observation, deliberately separate from Profile intent/SessionApplied.
enum class HardwareRtGateState { Unknown, Off, On };
inline const char* HardwareRtGateName(HardwareRtGateState value) {
    return value == HardwareRtGateState::On ? "on" : value == HardwareRtGateState::Off ? "off" : "unknown";
}
struct HardwareRtGateObservation {
    HardwareRtGateState state = HardwareRtGateState::Unknown;
    uint64_t observation_sequence = 0;
    uint64_t input_session_generation = 0; // MI_02 collection lifetime, not M605 writer session
    bool input_collection_connected = false;
    std::string observed_at_utc;
    std::string unavailable_reason = "no_status_notification_received";
};
// Actual USB/Raw Input report includes Report ID 03; no dummy 00 prefix.
// nullopt: unrelated input; Unknown: recognized but malformed RT notification.
inline std::optional<HardwareRtGateState> ParseHardwareRtGateReport(const uint8_t* bytes, size_t length) {
    if (!bytes || length < 2 || bytes[0] != 3 || bytes[1] != 0x76) return std::nullopt;
    if (length != 21 || bytes[2] || bytes[3] || bytes[4] > 1) return HardwareRtGateState::Unknown;
    for (size_t i = 5; i < length; ++i) if (bytes[i]) return HardwareRtGateState::Unknown;
    return bytes[4] ? HardwareRtGateState::On : HardwareRtGateState::Off;
}
// This describes the gate/host-intent combination, not full firmware readback.
inline std::optional<bool> DeriveEffectiveRt(HardwareRtGateState gate, std::optional<bool> enabled) {
    if (gate == HardwareRtGateState::Off || enabled == false) return false;
    if (gate == HardwareRtGateState::Unknown || !enabled) return std::nullopt;
    return true;
}
}
