#pragma once
#include "third_party/json.hpp"
#include <memory>
#include <vector>
#include <string>
#include <cstdint>
#include <limits>
#include <chrono>

namespace aura {
// One v2 adapter mapping for detector names, canonical IDs and truthful public aliases.
// Deliberately excludes the legacy synthetic round_mvp alias.
std::string CanonicalAutomationEvent(const std::string& name);
inline uint64_t AutomationMonotonicMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct AutomationTelemetry {
    nlohmann::json fields = nlohmann::json::object();
    uint64_t source_epoch{0}, packet_sequence{0}, received_at_ms{0};
};
struct AutomationObservation {
    std::shared_ptr<const AutomationTelemetry> telemetry;
    // Vector index is the ordinal within this detector packet; canonical IDs.
    std::vector<std::string> occurrences;
};
struct AutomationInputDrain {
    std::shared_ptr<const AutomationTelemetry> latest;
    std::vector<std::shared_ptr<const AutomationObservation>> batches;
    uint64_t dropped_batches{0}; // cumulative diagnostic
    bool overflowed{false};
};
// Passed by const reference for a decision; no live GsiState reads in v2 leaves.
struct AutomationInputSnapshot {
    std::string foreground_process;
    std::shared_ptr<const AutomationTelemetry> telemetry;
    std::vector<std::string> occurrences;
    uint64_t source_epoch{0}, packet_sequence{0}, received_at_ms{0};
    uint64_t admitted_at_ms{0}, telemetry_age_ms{std::numeric_limits<uint64_t>::max()};
    bool automation_fresh{false};
    static AutomationInputSnapshot Capture(std::string process,
        std::shared_ptr<const AutomationTelemetry> data, uint64_t now, uint64_t freshness,
        std::vector<std::string> events = {}) {
        AutomationInputSnapshot s;
        s.foreground_process = std::move(process); s.telemetry = std::move(data);
        s.occurrences = std::move(events); s.admitted_at_ms = now;
        if (s.telemetry) {
            s.source_epoch = s.telemetry->source_epoch; s.packet_sequence = s.telemetry->packet_sequence;
            s.received_at_ms = s.telemetry->received_at_ms;
            if (now >= s.received_at_ms) s.telemetry_age_ms = now - s.received_at_ms;
            s.automation_fresh = s.telemetry_age_ms <= freshness;
        }
        return s;
    }
};
} // namespace aura
