#include "config/rule_engine.h"
#include "gsi/gsi_adapter.h"
#include "utils/logger.h"
#include "engine/automation_effect_runtime.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace aura {
namespace {
bool ProcessField(const std::string& field) {
    const auto s = RuleEngine::ToLower(field);
    return s == "process" || s == "process.name" || s == "process_name";
}
bool ProcessMatches(const std::string& process, const std::string& target, bool symmetric = true) {
    auto strip = [](std::string s) {
        if (s.size() > 4 && s.substr(s.size() - 4) == ".exe") s.resize(s.size() - 4);
        return s;
    };
    return process == target || process == strip(target) || (symmetric && strip(process) == target);
}
ConditionResult Result(bool v, bool witness = false) {
    return {v ? ConditionTruth::True : ConditionTruth::False, v && witness};
}
void Require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
ConditionNode ParseCondition(const nlohmann::json& j, bool events, unsigned depth, size_t& nodes) {
    // Roots are depth 1. Count each explicit AST node, sharing the budget across
    // scope and when.condition; an omitted scope contributes no configured node.
    Require(depth <= 32, "v2 AST depth exceeds 32");
    Require(++nodes <= 256, "v2 rule AST node count exceeds 256");
    Require(j.is_object() && !j.empty(), "v2 condition must be a nonempty AST");
    if (j.contains("event")) {
        Require(events && j.size() == 1 && j["event"].is_string(), "occurrence leaf not legal here");
        const auto event = j["event"].get<std::string>();
        ConditionNode n; n.event = CanonicalAutomationEvent(event);
        Require(event.rfind("event.", 0) == 0 && !n.event.empty(), "unknown canonical occurrence");
        return n;
    }
    std::string logic = j.value("type", "");
    nlohmann::json children;
    if (j.contains("and")) { logic = "and"; children = j["and"]; }
    else if (j.contains("or")) { logic = "or"; children = j["or"]; }
    else if (j.contains("not")) { logic = "not"; children = nlohmann::json::array({j["not"]}); }
    else if (j.contains("conditions")) { logic = j.value("type", j.value("op", "")); children = j["conditions"]; }
    else if (j.contains("condition")) { logic = j.value("type", j.value("op", "")); children = nlohmann::json::array({j["condition"]}); }
    if (!logic.empty()) {
        ConditionNode n; n.logic_op = ConditionNode::StringToLogicOp(logic);
        Require(n.logic_op != LogicOp::None && children.is_array() && !children.empty(), "invalid v2 logical node");
        Require(n.logic_op != LogicOp::Not || children.size() == 1, "NOT requires one operand");
        for (const auto& child : children) n.children.push_back(ParseCondition(child, events, depth + 1, nodes));
        return n;
    }
    Require(j.contains("field") && j["field"].is_string() && j.contains("value"), "invalid v2 comparison");
    ConditionNode n; n.field = j["field"].get<std::string>(); n.target_value = j["value"];
    Require(!n.field.empty() && n.field.rfind("event.", 0) != 0 && n.field.rfind("event_sequence.", 0) != 0,
            "pulse fields are not v2 state/occurrence inputs");
    const auto op = j.value("op", "==");
    static const std::set<std::string> ops = {"==", "!=", "<", "<=", ">", ">=", "contains"};
    Require(ops.count(op) != 0, "unknown v2 comparison operator");
    n.comp_op = ConditionNode::StringToCompareOp(op);
    Require(n.target_value.is_string() || n.target_value.is_boolean() || n.target_value.is_number(), "invalid comparison target");
    if (ProcessField(n.field)) Require(n.target_value.is_string() && (op == "==" || op == "!="), "invalid process predicate");
    return n;
}
// NOT discards occurrence witnesses at evaluation time, including nested NOTs.
bool CanSupplyPositiveOccurrence(const ConditionNode& node) {
    if (node.logic_op == LogicOp::Not) return false;
    if (node.logic_op == LogicOp::And || node.logic_op == LogicOp::Or)
        return std::any_of(node.children.begin(), node.children.end(), CanSupplyPositiveOccurrence);
    return !node.event.empty();
}
struct ScopedCondition {
    bool in_scope;
    ConditionResult value;
};
ScopedCondition EvaluateRule(const AutomationRule& rule, const AutomationInputSnapshot& input) {
    if (!rule.enabled) return {false, Result(false)};
    const auto scope = rule.scope.Evaluate(input);
    if (scope.truth == ConditionTruth::False) return {false, Result(false)};
    const auto condition = rule.condition.Evaluate(input);
    if (scope.truth == ConditionTruth::Unknown)
        return {false, condition.truth == ConditionTruth::False ? Result(false) : ConditionResult{}};
    return {true, condition};
}
} // namespace

ConditionNode ConditionNode::FromAutomationJson(const nlohmann::json& j, bool allow_events) {
    size_t nodes = 0;
    return ParseCondition(j, allow_events, 1, nodes);
}
bool ConditionNode::UsesGsi() const {
    if (!event.empty() || (!field.empty() && !ProcessField(field))) return true;
    return std::any_of(children.begin(), children.end(), [](const auto& c) { return c.UsesGsi(); });
}
ConditionResult ConditionNode::Evaluate(const AutomationInputSnapshot& s) const {
    if (logic_op == LogicOp::Not) {
        if (children.size() != 1) return {};
        const auto r = children.front().Evaluate(s);
        if (r.truth == ConditionTruth::Unknown) return {};
        return Result(r.truth == ConditionTruth::False); // NOT never supplies a positive witness
    }
    if (logic_op == LogicOp::And || logic_op == LogicOp::Or) {
        bool unknown = false, witness = false, any_true = false, any_false = false;
        for (const auto& child : children) {
            const auto r = child.Evaluate(s);
            unknown |= r.truth == ConditionTruth::Unknown;
            any_false |= r.truth == ConditionTruth::False;
            any_true |= r.truth == ConditionTruth::True;
            witness |= r.truth == ConditionTruth::True && r.positive_occurrence;
        }
        if (logic_op == LogicOp::And) {
            if (any_false) return Result(false);
            if (unknown) return {};
            return Result(true, witness);
        }
        if (any_true) return Result(true, witness);
        return unknown ? ConditionResult{} : Result(false);
    }
    if (!event.empty()) {
        if (!s.automation_fresh) return {};
        return Result(std::find(s.occurrences.begin(), s.occurrences.end(), event) != s.occurrences.end(), true);
    }
    if (field.empty()) return Result(true); // implicit, omitted scope only
    if (ProcessField(field)) {
        if (!target_value.is_string()) return {};
        const bool match = ProcessMatches(RuleEngine::ToLower(s.foreground_process), RuleEngine::ToLower(target_value.get<std::string>()));
        return Result(comp_op == CompareOp::Ne ? !match : match);
    }
    if (!s.automation_fresh || !s.telemetry) return {};
    const auto it = s.telemetry->fields.find(field);
    if (it == s.telemetry->fields.end()) return {};
    const auto& value = *it;
    if (value.is_number() && target_value.is_number()) {
        const double a = value.get<double>(), b = target_value.get<double>();
        if (!std::isfinite(a) || !std::isfinite(b)) return {};
        switch (comp_op) {
            case CompareOp::Eq: return Result(std::abs(a - b) < 1e-4);
            case CompareOp::Ne: return Result(std::abs(a - b) >= 1e-4);
            case CompareOp::Lt: return Result(a < b); case CompareOp::Le: return Result(a <= b);
            case CompareOp::Gt: return Result(a > b); case CompareOp::Ge: return Result(a >= b);
            default: return {};
        }
    }
    if (value.is_string() && target_value.is_string()) {
        const auto a = RuleEngine::ToLower(value.get<std::string>());
        const auto b = RuleEngine::ToLower(target_value.get<std::string>());
        if (comp_op == CompareOp::Contains) return Result(a.find(b) != std::string::npos);
        if (comp_op == CompareOp::Eq) return Result(a == b);
        if (comp_op == CompareOp::Ne) return Result(a != b);
    }
    if (value.is_boolean() && target_value.is_boolean()) {
        if (comp_op == CompareOp::Eq) return Result(value == target_value);
        if (comp_op == CompareOp::Ne) return Result(value != target_value);
    }
    return {};
}

AutomationRule RuleEngine::ParseAutomationRule(const nlohmann::json& item) {
    AutomationRule r;
    r.id = item.at("id").get<std::string>(); Require(!r.id.empty(), "v2 rule requires stable id");
    r.enabled = item.value("enabled", true); r.dnd = item.value("dnd", false);
    const auto& when = item.at("when"); const auto& action = item.at("action");
    Require(when.is_object() && action.is_object(), "v2 requires WHEN and action");
    r.mode = when.at("mode").get<std::string>();
    Require(r.mode == "state" || r.mode == "rising" || r.mode == "event", "unsupported WHEN mode");
    size_t nodes = 0;
    r.condition = ParseCondition(when.at("condition"), r.mode == "event", 1, nodes);
    Require(r.mode != "event" || CanSupplyPositiveOccurrence(r.condition),
            "event WHEN condition requires a positive occurrence leaf outside NOT");
    if (item.contains("scope")) r.scope = ParseCondition(item["scope"], false, 1, nodes);
    const auto type = action.at("type").get<std::string>();
    Require(!item.contains("latch") && !action.contains("latch") && !action.contains("activation") &&
            !action.contains("dnd"), "unsupported action policy");
    if (type == "activate_profile") {
        Require(r.mode == "state" && action.at("profile").is_string() && !action.at("profile").get<std::string>().empty(),
                "only state can activate profile");
        Require(!action.contains("lifetime") && !action.contains("retrigger"), "profile action has no lifecycle/retrigger");
    } else {
        Require(type == "trigger_effect", "unsupported action type");
        Require(!action.contains("event") && !action.contains("condition") && !action.contains("process"),
                "Trigger predicates belong in WHEN, not Play Effect");
        Require(!item.contains("dnd"), "dnd is supported only for state activate_profile rules");
        const auto lifetime = action.at("lifetime").get<std::string>();
        Require(lifetime == (r.mode == "state" ? "while_true" : "one_shot"), "unsupported mode/lifetime pairing");
        const auto retrigger = action.value("retrigger", "restart");
        Require(retrigger == "restart" || retrigger == "ignore_while_active" || retrigger == "stack" || retrigger == "queue", "unknown retrigger policy");
        if (retrigger=="stack" || retrigger=="queue") {
            for (const auto* setting:{"stack","queue","limits","pending_ttl_ms","max_pending_per_rule","max_pending_global","max_active_per_rule","max_active_v2"})
                Require(!action.contains(setting),"advanced retrigger limits are fixed, not configurable");
        }
        Require(r.mode != "state" || !action.contains("retrigger"), "while_true has no retrigger policy");
        const auto& effect = action.at("effect");
        const auto kind = effect.at("kind").get<std::string>();
        Require((kind == "profile_effect" || kind == "plugin") && !effect.at("name").get<std::string>().empty(), "invalid effect reference");
        const auto composition = action.value("composition", "overlay"), blend = action.value("blend", "alpha");
        Require((composition == "overlay" || composition == "replace") && (blend == "alpha" || blend == "additive"), "invalid composition metadata");
        if (action.contains("priority")) {
            const auto& priority = action["priority"];
            Require(priority.is_number_integer() && priority >= INT32_MIN && priority <= INT32_MAX, "invalid layer priority");
        }
        if (action.contains("watchdog_ms")) {
            const auto& watchdog = action["watchdog_ms"];
            Require(r.mode != "state" && watchdog.is_number_integer() && watchdog >= 1 && watchdog <= 60000,
                    "one-shot watchdog_ms must be 1..60000");
        }
        if (action.contains("compatibility")) {
            const auto& envelope = action["compatibility"];
            Require(envelope.is_object() && envelope.value("lifecycle", "legacy_envelope") == "legacy_envelope", "invalid compatibility lifecycle");
            for (const auto* key : {"duration_ms", "fade_out_ms", "attack_ms"}) {
                if (!envelope.contains(key)) continue;
                const auto& time = envelope[key];
                Require(time.is_number_integer() && time >= 0 && (std::string(key) != "duration_ms" || time > 0), "invalid compatibility envelope time");
            }
        }
    }
    r.action = action;
    auto semantic_action = action;
    if (type == "trigger_effect") {
        semantic_action["composition"] = action.value("composition", "overlay");
        semantic_action["blend"] = action.value("blend", "alpha");
        semantic_action["priority"] = action.value("priority", 10);
        if (r.mode != "state") semantic_action["retrigger"] = action.value("retrigger", "restart");
    }
    const nlohmann::json semantic = {{"mode", r.mode}, {"enabled", r.enabled}, {"dnd", r.dnd},
        {"scope", r.scope.ToJson()}, {"condition", r.condition.ToJson()}, {"action", semantic_action}};
    r.fingerprint = semantic.dump();
    return r;
}

std::vector<RulePlanEntry> RuleEngine::GetEvaluationPlan() const {
    std::lock_guard<std::mutex> lock(mutex_); return plan_;
}
void RuleEngine::ValidateAutomationReferences(const AutomationRule& rule,
    const std::function<bool(const std::string&)>& profile_exists) {
    const auto& action = rule.action;
    std::string name;
    if (action.at("type") == "activate_profile") name = action.at("profile").get<std::string>();
    else if (action.at("effect").at("kind") == "profile_effect") name = action.at("effect").at("name").get<std::string>();
    else return; // Plugin resolution belongs to the effect runtime.
    if (!profile_exists(name)) throw std::runtime_error("Referenced profile '" + name + "' does not exist");
}
uint64_t RuleEngine::GetAutomationFreshnessMs() const {
    std::lock_guard<std::mutex> lock(mutex_); return automation_freshness_ms_;
}
AutomationEvaluation RuleEngine::EvaluateProfilesLocked(const AutomationInputSnapshot& input) const {
    AutomationEvaluation out;
    out.config_generation = config_generation_;
    for (const auto& p : plan_) {
        const auto& r = p.automation;
        if (r.action.at("type") != "activate_profile") continue;
        ++out.profile_plan_entries_evaluated;
        const auto truth = EvaluateRule(r, input).value.truth;
        const bool matches = truth == ConditionTruth::True;
        if (matches && r.dnd) out.suppress_web_ui = true;
        out.decisions.push_back({r.id, r.action, truth, false, input.source_epoch, input.packet_sequence, p.config_order});
        if (matches && !out.profile) {
            const auto it = profiles_.find(r.action.at("profile").get<std::string>());
            if (it != profiles_.end()) out.profile = it->second;
        }
    }
    if (!out.profile) {
        auto it = profiles_.find(fallback_profile_);
        if (it == profiles_.end()) it = profiles_.find(default_profile_name_);
        if (it != profiles_.end()) out.profile = it->second;
    }
    return out;
}

void RuleEngine::RebaseAutomationSource() {
    std::lock_guard<std::mutex> lock(mutex_); edge_memory_.clear();
}

AutomationEvaluation RuleEngine::EvaluateAutomation(GsiState& gsi,
    const std::function<std::string()>& foreground, std::optional<uint64_t> admitted_at_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto input = gsi.DrainAutomationInputs();
    // One admission context for the entire drain, including reconciliation.
    // Capture after draining so receipt timestamps cannot be newer than admission.
    const auto process = foreground();
    const auto now = admitted_at_ms ? *admitted_at_ms : AutomationMonotonicMs();
    auto capture = [&](std::shared_ptr<const AutomationTelemetry> telemetry, std::vector<std::string> events = {}) {
        return AutomationInputSnapshot::Capture(process, std::move(telemetry), now,
            automation_freshness_ms_, std::move(events));
    };
    std::vector<AutomationInputSnapshot> batches;
    for (const auto& b : input.batches)
        batches.push_back(capture(b->telemetry, b->occurrences));
    const auto current = capture(input.latest);
    auto out = EvaluateProfilesLocked(current);
    out.foreground_process = current.foreground_process;
    out.automation_fresh = current.automation_fresh;
    if (current.telemetry) out.telemetry_age_ms = current.telemetry_age_ms;
    out.freshness_threshold_ms = automation_freshness_ms_;
    out.evaluated_at_ms = current.admitted_at_ms;
    out.reconciliation_complete = true;
    out.has_v2_rules = !plan_.empty();
    for (const auto& entry : plan_) {
        if (entry.automation.action.at("type") != "trigger_effect") continue;
        const auto& rule = entry.automation;
        AutomationEvaluation::RuleStatus status;
        status.id = rule.id; status.semantic_identity = rule.fingerprint;
        const bool uses_gsi = rule.scope.UsesGsi() || rule.condition.UsesGsi();
        if (uses_gsi) status.semantic_identity += ":freshness=" + std::to_string(automation_freshness_ms_);
        status.enabled = rule.enabled; status.persistent = rule.mode == "state"; status.rule_order = entry.config_order;
        status.scope = rule.scope.Evaluate(current).truth;
        status.continuation = status.scope;
        // Event-time false is not a continuing predicate. Only explicit scope and
        // required telemetry validity cancel; independent true process branches survive.
        if (!rule.enabled) status.continuation = ConditionTruth::False;
        else if (status.continuation == ConditionTruth::True && uses_gsi && !current.automation_fresh &&
                 rule.condition.Evaluate(current).truth != ConditionTruth::True)
            status.continuation = ConditionTruth::Unknown;
        const auto& effect = rule.action.at("effect");
        status.action_identity = effect.dump();
        std::string plugin;
        if (effect.at("kind") == "profile_effect") {
            const auto profile = profiles_.find(effect.at("name").get<std::string>());
            if (profile != profiles_.end()) { status.recipe_identity = ProfileEffectIdentity(*profile->second); plugin = ProfilePluginName(*profile->second); }
        } else { plugin = effect.at("name").get<std::string>(); status.recipe_identity = effect.dump(); }
        if (!plugin.empty()) {
            const auto generation = PluginManager::Instance().GetGeneration(plugin);
            if (generation) status.plugin_generation = generation->generation_id;
        }
        out.rule_status.push_back(std::move(status));
    }
    out.dropped_input_batches = input.dropped_batches; out.rebased = input.overflowed;
    if (input.overflowed) LOG_WARN("[Automation] Input overflow; dropped=" << input.dropped_batches << "; rebasing at latest snapshot");
    for (const auto& p : plan_) {
        const auto& r = p.automation;
        if (r.action.at("type") == "activate_profile") continue; // already evaluated once in arbitration
        if (r.mode == "state") {
            const auto truth = EvaluateRule(r, current).value.truth;
            out.decisions.push_back({r.id, r.action, truth, false, current.source_epoch, current.packet_sequence, p.config_order});
            continue;
        }
        auto& mem = edge_memory_[r.id];
        const bool uses_gsi = r.condition.UsesGsi() || r.scope.UsesGsi();
        const bool reset = !mem.initialized || mem.fingerprint != r.fingerprint || input.overflowed ||
            (uses_gsi && mem.epoch != current.source_epoch) || !mem.in_scope;
        auto evaluate = [&](const AutomationInputSnapshot& s, bool seed) {
            const auto evaluated = EvaluateRule(r, s);
            const bool scoped = evaluated.in_scope;
            const auto value = evaluated.value;
            const bool gap = mem.sequence != 0 && s.received_at_ms > mem.receipt &&
                s.received_at_ms - mem.receipt > automation_freshness_ms_;
            if (seed || !scoped || !mem.in_scope || (gap && (r.condition.UsesGsi() || r.scope.UsesGsi())))
                mem.previous = ConditionTruth::Unknown;
            bool admitted = false;
            if (r.mode == "rising") admitted = mem.previous == ConditionTruth::False && value.truth == ConditionTruth::True;
            if (r.mode == "event") admitted = !seed && scoped && mem.in_scope && !gap &&
                s.automation_fresh && value.truth == ConditionTruth::True && value.positive_occurrence;
            if (admitted) out.decisions.push_back({r.id, r.action, value.truth, true, s.source_epoch, s.packet_sequence, p.config_order});
            mem.previous = value.truth; mem.in_scope = scoped;
            mem.receipt = s.received_at_ms; mem.epoch = s.source_epoch; mem.sequence = s.packet_sequence;
        };
        if (reset) {
            evaluate(current, true); // discard pre-start/edit/scope-entry/overflow inputs
        } else {
            for (const auto& batch : batches) {
                if (batch.source_epoch == mem.epoch && batch.packet_sequence <= mem.sequence) continue;
                evaluate(batch, uses_gsi && batch.source_epoch != mem.epoch);
            }
            // Reconcile expiry/process changes even without any newly received packet.
            evaluate(current, false);
        }
        mem.initialized = true; mem.fingerprint = r.fingerprint;

    }
    // Rule memory is independent; present admissions in packet order, then config
    // order within a packet. State reconciliation decisions precede admissions.
    const auto admissions = std::stable_partition(out.decisions.begin(), out.decisions.end(),
        [](const auto& d) { return !d.admitted; });
    std::stable_sort(admissions, out.decisions.end(), [](const auto& a, const auto& b) {
        if (a.source_epoch != b.source_epoch) return a.source_epoch < b.source_epoch;
        return a.packet_sequence < b.packet_sequence;
    });
    return out;
}
} // namespace aura
