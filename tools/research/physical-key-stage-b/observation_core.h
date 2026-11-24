#pragma once

// Structural observation only. Markers and numeric-field probes are NOT
// verified key identities, Press/Release semantics, or a physical provider.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include "../../../include/aura/hardware/m605_rt_gate.h" // Pure existing RT parser; no monitor or device I/O.

namespace stage_b {
enum class Scope { Control, Notifications };
struct Contract {
    uint16_t page, usage;
    size_t bytes;
    uint8_t report_id;
    const char* collection;
};
inline Contract contract(Scope scope) {
    return scope == Scope::Control ? Contract{0xff00, 1, 65, 0, "MI_01"}
                                  : Contract{0xffc0, 1, 21, 3, "MI_02_Col03"};
}
// Windows registration metadata, not a device-report parser. On this Windows
// host the snapshot omits requested DEVNOTIFY. Both forms remain foreground;
// no INPUTSINK/NOLEGACY/PAGEONLY or other flags are accepted.
inline bool registration_snapshot_matches(Scope scope, uint32_t returned, uint32_t count,
                                         uint16_t page, uint16_t usage, uint32_t flags,
                                         uintptr_t actual_window, uintptr_t expected_window) {
    const auto spec = contract(scope);
    return returned == 1 && count == 1 && page == spec.page && usage == spec.usage &&
           (flags == 0 || flags == 0x2000) && expected_window != 0 && actual_window == expected_window;
}
enum class Stop {
    None, Timeout, Cancel, FocusLost, Locked, Disconnected, DesktopUnavailable,
    Malformed, ClockError, Budget, RegistrationFailed, Exception, Ambiguous,
    IdleTimeout, CleanupFailed, MessageLoopError, MessageLoopExit
};
inline const char* reason(Stop value) {
    switch (value) {
    case Stop::None: return "none";
    case Stop::Timeout: return "trial_timeout";
    case Stop::Cancel: return "owner_cancel";
    case Stop::FocusLost: return "focus_lost";
    case Stop::Locked: return "session_locked";
    case Stop::Disconnected: return "target_disconnected";
    case Stop::DesktopUnavailable: return "local_desktop_unavailable";
    case Stop::Malformed: return "malformed_input";
    case Stop::ClockError: return "monotonic_clock_error";
    case Stop::Budget: return "report_budget_reached";
    case Stop::RegistrationFailed: return "registration_failed";
    case Stop::Exception: return "exception";
    case Stop::Ambiguous: return "ambiguous_target";
    case Stop::IdleTimeout: return "idle_timeout";
    case Stop::CleanupFailed: return "cleanup_failed";
    case Stop::MessageLoopError: return "message_loop_error";
    case Stop::MessageLoopExit: return "unexpected_message_loop_exit";
    }
    return "unknown";
}

struct Case {
    const wchar_t* name;
    const wchar_t* instruction;
    std::array<uint16_t, 4> configuration_wire_ids;
    size_t count;
};
// Stage 7A CONFIGURATION mapping only. The bytes 2..3 probe remains a static
// layout hypothesis; a match is never promoted to physical identity evidence.
inline constexpr std::array<Case, 12> cases{{
    {L"A", L"A only: press, hold, release once", {0x001f}, 1},
    {L"W", L"W only: press, hold, release once", {0x0012}, 1},
    {L"S", L"S only: press, hold, release once", {0x0020}, 1},
    {L"D", L"D only: press, hold, release once", {0x0021}, 1},
    {L"LWIN", L"Left Win only: press, hold, release once", {0x003b}, 1},
    {L"LSHIFT", L"Left Shift only: press, hold, release once", {0x002c}, 1},
    {L"RSHIFT", L"Right Shift only: press, hold, release once", {0x0039}, 1},
    {L"COPILOT", L"Copilot only: press, hold, release once", {0x0040}, 1},
    {L"COPILOT_LSHIFT", L"Hold real Left Shift; press/release Copilot; release Shift", {0x0040, 0x002c}, 2},
    {L"COPILOT_LWIN", L"Hold real Left Win; press/release Copilot; release Win", {0x0040, 0x003b}, 2},
    {L"AWSD", L"Hold A/W/S/D together; release each independently", {0x001f, 0x0012, 0x0020, 0x0021}, 4},
    {L"RTGATE", L"Physical RT switch: toggle once, wait 2 seconds, toggle back. No keys.", {}, 0}
}};

struct Counts {
    uint64_t reports{}, marker73{}, marker74{}, other{}, consecutive_duplicates{};
    uint64_t repeated73{}, repeated74{}, marker74_before73{};
    uint64_t field_hypothesis_selected{}, field_hypothesis_other{};
    std::array<uint64_t, 6> parameter_hypothesis{}; // 0..4, then other; not DKS attribution.
    uint64_t first_ms{}, last_ms{}, interval_sum_ms{}, interval_max_ms{}, intervals{};
    uint32_t last_batch_report_bytes{}, last_batch_report_count{};
    std::array<uint64_t, 256> report_ids{};
    std::array<uint64_t, 67> report_lengths{}; // 0..65, then larger.
    uint64_t invalid_length{}, invalid_report_id{};
    uint64_t rt_on{}, rt_off{}, rt_invalid{}, rt_changes{}, rt_repeats{};
    uint8_t rt_last_valid_state{0xff}; // One RT switch state, never a key identity or history.
};
enum class FreshState { Unknown, Off, On, Stale };
inline const char* state_name(FreshState state) {
    switch (state) {
    case FreshState::Unknown: return "unknown";
    case FreshState::Off: return "off";
    case FreshState::On: return "on";
    case FreshState::Stale: return "stale";
    }
    return "unknown";
}
// GetMessage result classification only. This does not emulate OS delivery.
inline Stop message_loop_end(int returned, bool finished) {
    return returned < 0 ? Stop::MessageLoopError : !finished ? Stop::MessageLoopExit : Stop::None;
}
inline bool advance_disconnected_session(uintptr_t target, uintptr_t removed, uint64_t& generation) {
    if (!target || target != removed) return false;
    ++generation; return true; // Caller stops/clears the binding; never auto-reconnect.
}
inline bool note_input_entry(bool active, uint64_t& total, uint64_t& inactive, uint64_t& trial) {
    ++total;
    if (!active) { ++inactive; return false; }
    ++trial; return true;
}

class Observation {
public:
    void arm(Scope scope, const Case& test_case, uintptr_t device, uint64_t generation,
             uint64_t now, uint64_t duration_ms) {
        clear();
        scope_ = scope; test_case_ = test_case; device_ = device; generation_ = generation;
        started_ = now; duration_ = duration_ms; active_ = true; counts_ = {};
        stop_ = Stop::None; watermark_ = now; rt_state_ = FreshState::Unknown;
    }
    void finish(Stop why) {
        active_ = false; stop_ = why; rt_state_ = FreshState::Stale; clear();
    }
    bool tick(uint64_t now) {
        if (!active_) return false;
        if (now < watermark_) finish(Stop::ClockError);
        else if (now - started_ >= duration_) finish(Stop::Timeout);
        else watermark_ = now;
        return active_;
    }
    // Identity is checked before any payload dereference. This is also used by
    // the native adapter AFTER its header-only target filter.
    bool batch(uintptr_t device, uint64_t generation, const uint8_t* bytes, size_t available,
               uint32_t report_size, uint32_t count, uint64_t now) {
        if (!active_ || device != device_ || generation != generation_) return false;
        if (!tick(now)) return false;
        counts_.last_batch_report_bytes = report_size;
        counts_.last_batch_report_count = count;
        const auto spec = contract(scope_);
        ++counts_.report_lengths[report_size <= 65 ? report_size : 66];
        if (!bytes || report_size != spec.bytes || count == 0 || available > 65536 ||
            uint64_t(report_size) * count != available || count > 4096) {
            ++counts_.invalid_length;
            finish(Stop::Malformed); return false;
        }
        // Validate all framing first: do not partially count a malformed batch.
        for (uint32_t i = 0; i < count; ++i) {
            ++counts_.report_ids[bytes[size_t(i) * report_size]];
            if (bytes[size_t(i) * report_size] != spec.report_id) {
                ++counts_.invalid_report_id;
                finish(Stop::Malformed); return false;
            }
        }
        if (count > 200000 - counts_.reports) { finish(Stop::Budget); return false; }
        for (uint32_t i = 0; i < count; ++i) {
            const auto* report = bytes + size_t(i) * report_size;
            if (previous_size_ == report_size && std::memcmp(previous_.data(), report, report_size) == 0)
                ++counts_.consecutive_duplicates;
            if (have_previous_time_) {
                const auto interval = now - previous_time_;
                counts_.interval_sum_ms += interval;
                counts_.interval_max_ms = std::max(counts_.interval_max_ms, interval);
                ++counts_.intervals;
            } else counts_.first_ms = now - started_;
            counts_.last_ms = now - started_;
            previous_time_ = now; have_previous_time_ = true;
            ++counts_.reports;
            // Header-position probe only; never sweep a payload for 73/74.
            const uint8_t marker = report[1];
            if (marker == 0x73 || marker == 0x74) {
                if (marker == 0x73) { ++counts_.marker73; if (previous_marker_ == marker) ++counts_.repeated73; }
                else { ++counts_.marker74; if (!counts_.marker73) ++counts_.marker74_before73;
                       if (previous_marker_ == marker) ++counts_.repeated74; }
                const uint16_t probe = uint16_t(report[2]) | uint16_t(uint16_t(report[3]) << 8);
                const auto end = test_case_.configuration_wire_ids.begin() + test_case_.count;
                if (std::find(test_case_.configuration_wire_ids.begin(), end, probe) != end)
                    ++counts_.field_hypothesis_selected;
                else ++counts_.field_hypothesis_other;
                ++counts_.parameter_hypothesis[report[4] <= 4 ? report[4] : 5];
                previous_marker_ = marker;
            } else { ++counts_.other; previous_marker_ = 0; }
            if (scope_ == Scope::Notifications) {
                const auto gate = aura::ParseHardwareRtGateReport(report, report_size);
                if (gate) {
                    if (*gate == aura::HardwareRtGateState::Unknown) {
                        ++counts_.rt_invalid; rt_state_ = FreshState::Unknown;
                    } else {
                        const auto next = *gate == aura::HardwareRtGateState::On ? FreshState::On : FreshState::Off;
                        if (next == FreshState::On) ++counts_.rt_on; else ++counts_.rt_off;
                        counts_.rt_last_valid_state = *gate == aura::HardwareRtGateState::On ? 1 : 0;
                        if (rt_state_ == next) ++counts_.rt_repeats;
                        else if (rt_state_ == FreshState::On || rt_state_ == FreshState::Off) ++counts_.rt_changes;
                        rt_state_ = next;
                    }
                }
            }
            std::copy_n(report, report_size, previous_.begin()); previous_size_ = report_size;
        }
        return true;
    }
    bool active() const { return active_; }
    Stop stop() const { return stop_; }
    FreshState rt_state() const { return rt_state_; }
    bool accepts(uintptr_t device, uint64_t generation) const {
        return active_ && device == device_ && generation == generation_;
    }
    const Counts& counts() const { return counts_; }
    bool raw_memory_clear() const { return std::all_of(previous_.begin(), previous_.end(), [](uint8_t x) { return x == 0; }); }
private:
    void clear() {
        // Volatile stores avoid leaving the short duplicate-comparison buffer
        // resident on normal cleanup. No complete report history is retained.
        volatile uint8_t* p = previous_.data();
        for (size_t i = 0; i < previous_.size(); ++i) p[i] = 0;
        previous_size_ = 0; previous_marker_ = 0; previous_time_ = 0;
        have_previous_time_ = false; device_ = 0;
    }
    Scope scope_{Scope::Notifications};
    Case test_case_{cases[0]};
    uintptr_t device_{};
    uint64_t generation_{}, started_{}, duration_{}, previous_time_{}, watermark_{};
    FreshState rt_state_{FreshState::Unknown};
    bool active_{}, have_previous_time_{};
    Stop stop_{Stop::None};
    Counts counts_{};
    std::array<uint8_t, 65> previous_{};
    size_t previous_size_{};
    uint8_t previous_marker_{};
};

struct SubscriptionApi {
    virtual ~SubscriptionApi() = default;
    virtual bool add() = 0;
    virtual bool remove() = 0;
};
class Subscription {
public:
    explicit Subscription(SubscriptionApi& api) : api_(api) {}
    ~Subscription() { close(); }
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    bool start(bool safe) {
        if (!safe || registered_) return false;
        registered_ = api_.add(); return registered_;
    }
    bool close() {
        if (!registered_) return true;
        registered_ = false;
        return api_.remove();  // Report failure; process exit is the final cleanup boundary.
    }
    bool registered() const { return registered_; }
private:
    SubscriptionApi& api_;
    bool registered_{};
};
} // namespace stage_b
