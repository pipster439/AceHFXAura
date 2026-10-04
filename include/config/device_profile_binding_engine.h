#pragma once

#include "third_party/json.hpp"
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace aura {

struct DeviceProfileBinding {
    std::string rule_id, process_name, profile_id;
    bool enabled = true;
    int32_t priority = 0;
};
struct DeviceProfileAutomationConfig {
    bool enabled = false;
    std::optional<std::string> fallback_profile_id;
    std::vector<DeviceProfileBinding> bindings;
    // JSON transport retains unknown fields; evaluator uses only typed fields.
    static nlohmann::json DefaultJson();
    static DeviceProfileAutomationConfig Parse(const nlohmann::json& json);
    static nlohmann::json NormalizeAndPreserve(nlohmann::json next, const nlohmann::json& prior);
};
struct DeviceProfileDecisionContext {
    bool profile_document_available = false;
    std::optional<bool> keyboard_available; // unknown unless authoritative coordinator supplies it
    bool safety_quarantine = false;
    std::string runtime_health = "Unknown";
    bool session_reconciliation_pending = false;
};
enum class DeviceProfileDecisionKind { Match, Fallback, NoDecision, InvalidDecision };
enum class DeviceProfileDecisionReason {
    NotInitialized, MatchedRule, Fallback, NoMatchingRule, AutomationDisabled,
    ManualHold, InvalidTargetProfile, NoForeground, ProfileDocumentUnavailable, AutomationConfigurationUnavailable
};
struct DeviceProfileAutomationDecision {
    DeviceProfileDecisionKind kind = DeviceProfileDecisionKind::NoDecision;
    DeviceProfileDecisionReason reason = DeviceProfileDecisionReason::NotInitialized;
    std::optional<std::string> profile_id, rule_id, foreground;
    uint64_t sequence = 0;
    std::optional<int64_t> timestamp_ms;
};

// Decision-only subsystem: deliberately has NO DeviceProfileRuntime/M605/HID
// dependency, activation callback, filesystem writer, or activation enable flag.
class DeviceProfileBindingEngine {
public:
    using Json = nlohmann::json;
    using Clock = std::function<int64_t()>; // monotonic milliseconds; injectable fake clock
    explicit DeviceProfileBindingEngine(Clock clock = {});
    static std::optional<std::string> NormalizeForeground(const std::string& name_or_path);
    static std::string CanonicalGuid(const std::string& value);
    void Configure(const Json& config, const std::set<std::string>& profile_ids,
        bool document_available, int64_t document_revision, bool configuration_available = true);
    void SetContext(const DeviceProfileDecisionContext& context);
    void Observe(const std::string& name_or_path);
    bool Advance(); // commits a stable decision; true only for a new semantic decision
    void NotifyManualProfileAction(); // accepted explicit Manual selection ONLY
    Json Snapshot() const; // pure read: does not advance clock/debounce or evaluate
    static constexpr int64_t StabilityMs = 500;
    // Candidate freshness at acceptance only; an established hold NEVER expires.
    static constexpr int64_t ControlSurfaceAnchorFreshnessMs = 30000;

private:
    DeviceProfileAutomationDecision ResolveLocked() const;
    bool CommitLocked(DeviceProfileAutomationDecision decision, int64_t now);
    Clock clock_;
    mutable std::mutex mutex_;
    DeviceProfileAutomationConfig config_;
    std::set<std::string> profile_ids_;
    bool document_available_ = false;
    bool configuration_available_ = true;
    uint64_t manual_action_sequence_ = 0;
    uint64_t foreground_observation_sequence_ = 0; // freshness epoch, not semantic target sequence
    int64_t document_revision_ = 0;
    DeviceProfileDecisionContext context_;
    std::optional<int64_t> context_timestamp_ms_;
    std::optional<std::string> observed_, committed_foreground_, hold_foreground_;
    std::optional<std::string> last_external_foreground_, control_surface_anchor_;
    std::optional<int64_t> control_surface_entered_at_;
    bool hold_active_ = false;
    std::string hold_anchor_source_;
    std::optional<int64_t> observed_at_, stable_at_;
    bool pending_ = false, reevaluate_ = true;
    DeviceProfileAutomationDecision decision_;
};
} // namespace aura
