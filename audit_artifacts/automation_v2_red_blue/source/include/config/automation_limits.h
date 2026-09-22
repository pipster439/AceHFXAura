#pragma once
#include <cstddef>
#include <cstdint>
namespace aura {
struct AutomationRetriggerLimits {
    static constexpr size_t stack_active = 4, active_global = 32;
    static constexpr size_t pending_per_rule = 4, pending_global = 64;
    static constexpr uint64_t pending_ttl_ms = 2000;
    static constexpr size_t pending_attempts_per_frame = 4;
};
}
