#include "aura/hardware/m605_key_mapping.h"

namespace aura::m605 {

std::optional<uint16_t> WireIdForLogicalKey(uint16_t logical_key_id) {
    for (const auto& entry : detail::kVerifiedPhysicalKeys) {
        if (entry.logical_id == logical_key_id) return entry.wire_id;
    }
    return std::nullopt;
}

std::optional<uint16_t> SpeedTapWireIdForLogicalKey(uint16_t logical_key_id) {
    return WireIdForLogicalKey(logical_key_id);
}

} // namespace aura::m605
