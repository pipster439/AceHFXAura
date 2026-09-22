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

    std::string event; // V2 occurrence leaf
    std::string field;
    CompareOp comp_op{CompareOp::Eq};
    nlohmann::json target_value;

    // Evaluates AST against GSI telemetry state and current foreground process

    ConditionResult Evaluate(const AutomationInputSnapshot& snapshot) const;
    static ConditionNode FromAutomationJson(const nlohmann::json& j, bool allow_events);
    bool UsesGsi() const;
    nlohmann::json ToJson() const;

    static std::string CompareOpToString(CompareOp op);
    static CompareOp StringToCompareOp(const std::string& op_str);
    static std::string LogicOpToString(LogicOp op);
    static LogicOp StringToLogicOp(const std::string& op_str);
};

struct AutomationRule {
    std::string id, mode;
    bool enabled{true}, dnd{false};
    ConditionNode scope, condition;
    nlohmann::json action;
    std::string fingerprint;
};
struct RulePlanEntry {
    size_t config_order{0};
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
    bool source_changed{false};
    // Diagnostic projection of the exact snapshot used for current reconciliation.
    bool automation_fresh{false};
    std::optional<uint64_t> telemetry_age_ms;
    uint64_t freshness_threshold_ms{0}, evaluated_at_ms{0};
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
    void RebaseAutomationSource();
    // One owning decision consumer; callback called once per immutable input batch.
    AutomationEvaluation EvaluateAutomation(GsiState& gsi,
        const std::function<std::string()>& foreground, std::optional<uint64_t> admitted_at_ms = std::nullopt);
    std::vector<RulePlanEntry> GetEvaluationPlan() const;
    uint64_t GetAutomationFreshnessMs() const;


    // Checks file modification time and reloads if changed
    bool CheckAndReload();

    // Read-only V2 profile selection; does not consume event observations.
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
    // Shared runtime/authoring validation; no effect construction or publication.
    static void ValidateAuthoringReferences(const AutomationRule& rule, const nlohmann::json& profiles);
    static AutomationRule ParseAutomationRule(const nlohmann::json& item);
    static void ValidateAutomationReferences(const AutomationRule& rule,
        const std::function<bool(const std::string&)>& profile_exists);

private:
    static FILETIME GetConfigFileTime(const std::string& path);
    FILETIME GetConfigFileTime() const;

    std::string config_path_;
    FILETIME last_write_time_{0, 0};

    std::string default_profile_name_;
    std::string fallback_profile_;
    int target_fps_{25};
    HardwareBackend hardware_backend_{HardwareBackend::Auto};
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
    AutomationEvaluation EvaluateProfilesLocked(const AutomationInputSnapshot& input) const;
    mutable std::mutex mutex_;
};

} // namespace aura
