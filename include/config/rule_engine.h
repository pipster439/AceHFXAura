#pragma once

#include "engine/effect.h"
#include "gsi/automation_input.h"
#include <functional>
#include <optional>
#include "third_party/json.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <windows.h>

namespace aura {

class GsiState;

// 1. Process rule entry (legacy & simplified)
struct RuleEntry {
    std::string process_name;
    std::string profile_name;
    bool suppress_web_ui{false};
};

// 2. GSI binding entry (legacy flat)
struct GsiBinding {
    std::string field;
    std::string op{"=="};
    nlohmann::json target_value;
    std::string profile_name;
};

// 3. ConditionNode AST for recursive continuous GSI state tree
enum class LogicOp { None, And, Or, Not };
enum class CompareOp { Eq, Ne, Lt, Le, Gt, Ge, Contains };

enum class ConditionTruth { False, True, Unknown };
struct ConditionResult {
    ConditionTruth truth{ConditionTruth::Unknown};
    bool positive_occurrence{false};
};

struct ConditionNode {
    LogicOp logic_op{LogicOp::None};
    std::vector<ConditionNode> children;

    std::string event; // v2 occurrence leaf only; legacy parser never sets this
    std::string field;
    CompareOp comp_op{CompareOp::Eq};
    nlohmann::json target_value;

    // Evaluates AST against GSI telemetry state and current foreground process
    bool Evaluate(const GsiState* gsi, const std::string& foreground_proc) const;

    ConditionResult Evaluate(const AutomationInputSnapshot& snapshot) const;
    static ConditionNode FromAutomationJson(const nlohmann::json& j, bool allow_events);
    bool UsesGsi() const;
    static ConditionNode FromJson(const nlohmann::json& j);
    nlohmann::json ToJson() const;

    static std::string CompareOpToString(CompareOp op);
    static CompareOp StringToCompareOp(const std::string& op_str);
    static std::string LogicOpToString(LogicOp op);
    static LogicOp StringToLogicOp(const std::string& op_str);
};

// 4. Orchestration rule entry (modern AST-driven)
struct OrchestrationRule {
    std::string id;
    std::string name;
    std::string process;               // Optional foreground process filter
    bool dnd{false};                   // Web UI suppression / Do Not Disturb
    ConditionNode condition;           // Multi-condition AST
    std::string target_profile;
};

// 5. CS2 Transient event overlay definition
struct EventOverlayRule {
    std::string id;
    ConditionNode condition;
    std::string trigger{"event"}; // event: pulse; state: active while condition holds
    int priority{10};
    std::string event;                 // e.g. "event.kill", "event.flash"
    std::string name;
    std::string effect;                // Profile or plugin effect name
    uint64_t duration_ms{1200};
    uint64_t fade_out_ms{400};
    uint64_t attack_ms{0};
    std::string blend_mode{"blend"};   // "blend", "replace", "add"
};

struct OrchestrationConfig {
    std::vector<OrchestrationRule> rules;
    std::vector<EventOverlayRule> event_overlays;
    std::string fallback_profile;
};

enum class RuleProvenance { Application, GsiBinding, Orchestration, EventOverlay, AutomationV2 };
enum class CompatibilityPolicy { LegacyBoolean, LegacyCs2Binding, LegacyApplication, LegacyOverlayExecutor, V2Snapshot };
struct AutomationRule {
    std::string id, mode;
    bool enabled{true}, dnd{false};
    ConditionNode scope, condition;
    nlohmann::json action;
    std::string fingerprint;
};
struct RulePlanEntry {
    RuleProvenance provenance{RuleProvenance::Application};
    CompatibilityPolicy compatibility{CompatibilityPolicy::LegacyApplication};
    size_t config_order{0};
    RuleEntry application;
    GsiBinding binding;
    OrchestrationRule orchestration;
    EventOverlayRule overlay;
    AutomationRule automation;
};
struct AutomationDecision {
    std::string rule_id;
    nlohmann::json action;
    ConditionTruth eligibility{ConditionTruth::Unknown};
    bool admitted{false}; // one_shot only; while_true uses eligibility
    uint64_t source_epoch{0}, packet_sequence{0};
    size_t rule_order{0};
};
struct AutomationEvaluation {
    uint64_t config_generation{0};
    std::string foreground_process;
    std::shared_ptr<const Profile> profile;
    bool suppress_web_ui{false};
    std::vector<AutomationDecision> decisions;
    uint64_t dropped_input_batches{0};
    bool rebased{false};
    size_t profile_plan_entries_evaluated{0};
    struct RuleStatus {
        std::string id, semantic_identity, action_identity, recipe_identity;
        uint64_t plugin_generation{0};
        size_t rule_order{0};
        bool enabled{false}, persistent{false};
        ConditionTruth scope{ConditionTruth::Unknown}, continuation{ConditionTruth::Unknown};
    };
    bool reconciliation_complete{false};
    bool has_v2_rules{false};
    std::vector<RuleStatus> rule_status;
};

class RuleEngine {
public:
    RuleEngine();

    bool LoadConfig(const std::string& config_path);
    // One owning decision consumer; callback called once per immutable input batch.
    AutomationEvaluation EvaluateAutomation(GsiState& gsi,
        const std::function<std::string()>& foreground, std::optional<uint64_t> admitted_at_ms = std::nullopt);
    std::vector<RulePlanEntry> GetEvaluationPlan() const;
    uint64_t GetAutomationFreshnessMs() const;


    // Checks file modification time and reloads if changed
    bool CheckAndReload();

    // Matches process name against configured rules, falling back to default.
    // Order of precedence:
    // 1. Orchestration AST rules (matching process + multi-condition tree).
    // 2. If cs2.exe and GSI active: evaluate legacy gsi_bindings.
    // 3. Match foreground process rules.
    // 4. Fallback profile or default_profile.
    std::shared_ptr<const Profile> MatchProfile(const std::string& process_name, const GsiState* gsi_state = nullptr);

    // Checks if the matched rule requests suppressing the web UI service
    bool ShouldSuppressWebUi(const std::string& process_name, const GsiState* gsi = nullptr);

    std::string GetDefaultProfileName() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return default_profile_name_;
    }
    std::string GetConfigPath() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return config_path_;
    }
    std::vector<GsiBinding> GetGsiBindings() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return gsi_bindings_;
    }
    const OrchestrationConfig& GetOrchestration() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return orchestration_;
    }
    std::vector<EventOverlayRule> GetEventOverlayRules() const;
    int GetFps() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return target_fps_;
    }
    HardwareBackend GetHardwareBackend() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return hardware_backend_;
    }
    bool HasProfile(const std::string& name) const;
    std::shared_ptr<const Profile> GetProfile(const std::string& name) const;

    static std::string ToLower(const std::string& s);

private:
    static FILETIME GetConfigFileTime(const std::string& path);
    FILETIME GetConfigFileTime() const;

    std::string config_path_;
    FILETIME last_write_time_{0, 0};

    std::string default_profile_name_;
    int target_fps_{25};
    HardwareBackend hardware_backend_{HardwareBackend::Auto};
    std::vector<RuleEntry> rules_;
    std::vector<GsiBinding> gsi_bindings_;
    OrchestrationConfig orchestration_;
    std::unordered_map<std::string, std::shared_ptr<Profile>> profiles_;

    struct EdgeMemory {
        std::string fingerprint;
        ConditionTruth previous{ConditionTruth::Unknown};
        bool initialized{false}, in_scope{false};
        uint64_t epoch{0}, sequence{0}, receipt{0};
    };
    std::vector<RulePlanEntry> plan_;
    std::unordered_map<std::string, EdgeMemory> edge_memory_;
    uint64_t automation_freshness_ms_{3000};
    uint64_t config_generation_{0};
    static AutomationRule ParseAutomationRule(const nlohmann::json& item);
    AutomationEvaluation EvaluateProfilesLocked(const std::string& process, const GsiState* legacy,
        const AutomationInputSnapshot& input) const;
    mutable std::mutex mutex_;
};

} // namespace aura
