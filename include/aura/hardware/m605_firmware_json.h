#pragma once
#include "aura/hardware/m605_protocol.h"
#include "third_party/json.hpp"

namespace aura::m605 {
inline nlohmann::json FirmwareCompatibilityJson(const FirmwareCompatibility& value) {
    return {{"state", FirmwareStateName(value.state)},
        {"version", value.version.empty() ? nlohmann::json(nullptr) : nlohmann::json(value.version)},
        {"reason", value.reason}, {"session_generation", value.session_generation},
        {"source", "current opened M605 USB handle / BasicInfo; cached observation, no wire nonce"}};
}
}
