#include "config/device_profile_binding_engine.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <limits>
#include <regex>
#include <stdexcept>
#include <windows.h>

namespace aura {
namespace {
using Json = nlohmann::json;
std::string BasenameLower(const std::string& input) {
    if (input.empty() || input.size() > 32768 || input.find('\0') != std::string::npos) return {};
    const auto pos = input.find_last_of("\\/");
    const std::string name = pos == std::string::npos ? input : input.substr(pos + 1);
    if (name.empty() || name.size() > 255 || name == "." || name == ".." ||
        name.front() == ' ' || name.back() == ' ' || name.back() == '.' ||
        name.find_first_of("<>:\"/\\|?*") != std::string::npos ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32; })) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()), nullptr, 0);
    if (count == 0) return {};
    std::wstring wide(count, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()), wide.data(), count)) return {};
    const int lowered_count = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, wide.data(), count, nullptr, 0, nullptr, nullptr, 0);
    if (lowered_count == 0) return {};
    std::wstring lower(lowered_count, L'\0');
    if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, wide.data(), count, lower.data(), lowered_count, nullptr, nullptr, 0)) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, lower.data(), lowered_count, nullptr, 0, nullptr, nullptr);
    if (bytes == 0) return {};
    std::string output(bytes, '\0');
    if (!WideCharToMultiByte(CP_UTF8, 0, lower.data(), lowered_count, output.data(), bytes, nullptr, nullptr)) return {};
    return output;
}
void Preserve(Json& next, const Json& prior, const std::set<std::string>& known) {
    if (!prior.is_object()) return;
    for (auto it = prior.begin(); it != prior.end(); ++it)
        if (!known.count(it.key()) && !next.contains(it.key())) next[it.key()] = it.value();
}
const char* Kind(DeviceProfileDecisionKind kind) {
    const char* names[] = {"Match", "Fallback", "NoDecision", "InvalidDecision"};
    return names[static_cast<size_t>(kind)];
}
const char* Reason(DeviceProfileDecisionReason reason) {
    const char* names[] = {"NotInitialized", "MatchedRule", "Fallback", "NoMatchingRule", "AutomationDisabled",
        "ManualHold", "InvalidTargetProfile", "NoForeground", "ProfileDocumentUnavailable", "AutomationConfigurationUnavailable"};
    return names[static_cast<size_t>(reason)];
}
template<class T> Json Optional(const std::optional<T>& value) { return value ? Json(*value) : Json(nullptr); }
bool ControlSurface(const std::optional<std::string>& process) { return process && *process == "aura.exe"; }
}

Json DeviceProfileAutomationConfig::DefaultJson() {
    return {{"schema_version", 1}, {"enabled", false}, {"fallback_profile_id", nullptr}, {"bindings", Json::array()}};
}
std::string DeviceProfileBindingEngine::CanonicalGuid(const std::string& value) {
    static const std::regex guid(R"(^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$)");
    if (!std::regex_match(value, guid) || value == "00000000-0000-0000-0000-000000000000")
        throw std::invalid_argument("Invalid device automation GUID");
    auto canonical = value;
    std::transform(canonical.begin(), canonical.end(), canonical.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return canonical;
}
DeviceProfileAutomationConfig DeviceProfileAutomationConfig::Parse(const Json& json) {
    if (!json.is_object() || !json.contains("schema_version") || !json.at("schema_version").is_number_integer() ||
        json.at("schema_version") != 1 || !json.contains("enabled") || !json.at("enabled").is_boolean() ||
        !json.contains("fallback_profile_id") || !json.contains("bindings") || !json.at("bindings").is_array() ||
        json.at("bindings").size() > 1024) throw std::invalid_argument("Invalid device automation schema");
    DeviceProfileAutomationConfig result;
    result.enabled = json.at("enabled").get<bool>();
    if (!json.at("fallback_profile_id").is_null()) {
        if (!json.at("fallback_profile_id").is_string()) throw std::invalid_argument("Invalid device automation fallback GUID");
        result.fallback_profile_id = DeviceProfileBindingEngine::CanonicalGuid(json.at("fallback_profile_id").get<std::string>());
    }
    std::set<std::string> ids;
    for (const auto& rule : json.at("bindings")) {
        if (!rule.is_object() || !rule.contains("rule_id") || !rule.at("rule_id").is_string() ||
            !rule.contains("profile_id") || !rule.at("profile_id").is_string() ||
            !rule.contains("enabled") || !rule.at("enabled").is_boolean() ||
            !rule.contains("process_name") || !rule.at("process_name").is_string() ||
            !rule.contains("priority") || !rule.at("priority").is_number_integer())
            throw std::invalid_argument("Invalid device automation binding");
        if (rule.at("priority").is_number_unsigned() &&
            rule.at("priority").get<uint64_t>() > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))
            throw std::invalid_argument("Device automation priority outside int32 range");
        const auto priority = rule.at("priority").get<int64_t>();
        if (priority < std::numeric_limits<int32_t>::min() || priority > std::numeric_limits<int32_t>::max())
            throw std::invalid_argument("Device automation priority outside int32 range");
        const auto name = rule.at("process_name").get<std::string>();
        // Stored rules accept a basename only; observed identities may be paths.
        if (name.find_first_of("/\\") != std::string::npos || BasenameLower(name).empty())
            throw std::invalid_argument("Device automation requires a valid process basename");
        DeviceProfileBinding binding{DeviceProfileBindingEngine::CanonicalGuid(rule.at("rule_id").get<std::string>()),
            BasenameLower(name), DeviceProfileBindingEngine::CanonicalGuid(rule.at("profile_id").get<std::string>()),
            rule.at("enabled").get<bool>(), static_cast<int32_t>(priority)};
        if (!ids.insert(binding.rule_id).second) throw std::invalid_argument("Duplicate device automation rule GUID");
        result.bindings.push_back(std::move(binding));
    }
    std::sort(result.bindings.begin(), result.bindings.end(), [](const auto& a, const auto& b) {
        return a.priority != b.priority ? a.priority > b.priority : a.rule_id < b.rule_id;
    });
    return result;
}
Json DeviceProfileAutomationConfig::NormalizeAndPreserve(Json next, const Json& prior) {
    Parse(next); // validate before touching durable state
    Preserve(next, prior, {"schema_version", "enabled", "fallback_profile_id", "bindings"});
    if (!next.at("fallback_profile_id").is_null())
        next["fallback_profile_id"] = DeviceProfileBindingEngine::CanonicalGuid(next.at("fallback_profile_id").get<std::string>());
    for (auto& rule : next["bindings"]) {
        rule["rule_id"] = DeviceProfileBindingEngine::CanonicalGuid(rule.at("rule_id").get<std::string>());
        rule["profile_id"] = DeviceProfileBindingEngine::CanonicalGuid(rule.at("profile_id").get<std::string>());
        rule["process_name"] = BasenameLower(rule.at("process_name").get<std::string>());
        if (prior.is_object() && prior.contains("bindings") && prior.at("bindings").is_array())
            for (const auto& old : prior.at("bindings"))
                if (DeviceProfileBindingEngine::CanonicalGuid(old.at("rule_id").get<std::string>()) == rule.at("rule_id")) {
                    Preserve(rule, old, {"rule_id", "enabled", "process_name", "profile_id", "priority"}); break;
                }
    }
    return next;
}

DeviceProfileBindingEngine::DeviceProfileBindingEngine(Clock clock) : clock_(std::move(clock)) {
    if (!clock_) clock_ = [] { return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count(); };
}
std::optional<std::string> DeviceProfileBindingEngine::NormalizeForeground(const std::string& value) {
    auto normalized = BasenameLower(value);
    return normalized.empty() ? std::nullopt : std::optional<std::string>(std::move(normalized));
}
void DeviceProfileBindingEngine::Configure(const Json& json, const std::set<std::string>& ids,
    bool available, int64_t revision, bool configuration_available) {
    auto config = DeviceProfileAutomationConfig::Parse(json);
    std::set<std::string> canonical;
    for (const auto& id : ids) canonical.insert(CanonicalGuid(id));
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = std::move(config); profile_ids_ = std::move(canonical);
    document_available_ = available; document_revision_ = revision; reevaluate_ = true;
    configuration_available_ = configuration_available;
    context_.profile_document_available = available;
}
void DeviceProfileBindingEngine::SetContext(const DeviceProfileDecisionContext& context) {
    std::lock_guard<std::mutex> lock(mutex_);
    context_ = context; context_timestamp_ms_ = clock_();
}
void DeviceProfileBindingEngine::Observe(const std::string& value) {
    const auto process = NormalizeForeground(value);
    std::lock_guard<std::mutex> lock(mutex_);
    if (observed_at_ && process == observed_) return;
    // Only the immediately preceding stable external context can be carried
    // through Aura. Unknown/transient intervening input never licenses history.
    if (ControlSurface(process)) {
        control_surface_anchor_ = observed_ == committed_foreground_ && committed_foreground_ &&
            !ControlSurface(committed_foreground_) ? committed_foreground_ : std::nullopt;
        control_surface_entered_at_ = clock_();
    } else {
        control_surface_anchor_.reset(); control_surface_entered_at_.reset();
    }
    ++foreground_observation_sequence_;
    observed_ = process; observed_at_ = clock_(); pending_ = true;
}
DeviceProfileAutomationDecision DeviceProfileBindingEngine::ResolveLocked() const {
    DeviceProfileAutomationDecision decision; decision.foreground = committed_foreground_;
    if (!document_available_) { decision.reason = DeviceProfileDecisionReason::ProfileDocumentUnavailable; return decision; }
    if (!configuration_available_) { decision.reason = DeviceProfileDecisionReason::AutomationConfigurationUnavailable; return decision; }
    if (!config_.enabled) { decision.reason = DeviceProfileDecisionReason::AutomationDisabled; return decision; }
    if (hold_active_) {
        decision.reason = DeviceProfileDecisionReason::ManualHold; return decision;
    }
    if (!committed_foreground_) { decision.reason = DeviceProfileDecisionReason::NoForeground; return decision; }
    for (const auto& rule : config_.bindings) if (rule.enabled && rule.process_name == *committed_foreground_) {
        decision.profile_id = rule.profile_id; decision.rule_id = rule.rule_id;
        decision.kind = profile_ids_.count(rule.profile_id) ? DeviceProfileDecisionKind::Match : DeviceProfileDecisionKind::InvalidDecision;
        decision.reason = decision.kind == DeviceProfileDecisionKind::Match ? DeviceProfileDecisionReason::MatchedRule : DeviceProfileDecisionReason::InvalidTargetProfile;
        return decision; // invalid winner is visible; never fall through to another rule/fallback
    }
    if (config_.fallback_profile_id) {
        decision.profile_id = config_.fallback_profile_id;
        decision.kind = profile_ids_.count(*decision.profile_id) ? DeviceProfileDecisionKind::Fallback : DeviceProfileDecisionKind::InvalidDecision;
        decision.reason = decision.kind == DeviceProfileDecisionKind::Fallback ? DeviceProfileDecisionReason::Fallback : DeviceProfileDecisionReason::InvalidTargetProfile;
    } else decision.reason = DeviceProfileDecisionReason::NoMatchingRule;
    return decision;
}
bool DeviceProfileBindingEngine::CommitLocked(DeviceProfileAutomationDecision next, int64_t now) {
    const auto valid = [](const auto& d) { return d.kind == DeviceProfileDecisionKind::Match || d.kind == DeviceProfileDecisionKind::Fallback; };
    const bool same = valid(next) && valid(decision_) ? next.profile_id == decision_.profile_id :
        next.kind == decision_.kind && next.reason == decision_.reason && next.profile_id == decision_.profile_id && next.rule_id == decision_.rule_id;
    next.sequence = decision_.sequence + (same ? 0 : 1);
    next.timestamp_ms = same ? decision_.timestamp_ms : std::optional<int64_t>(now);
    decision_ = std::move(next); return !same;
}
bool DeviceProfileBindingEngine::Advance() {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = clock_();
    if (!observed_at_ || (pending_ && now - *observed_at_ < StabilityMs)) return false;
    if (!pending_ && !reevaluate_) return false;
    committed_foreground_ = observed_; stable_at_ = now; pending_ = false; reevaluate_ = false;
    // Aura is transparent for manual override lifecycle, but ordinary matching
    // (including explicit aura.exe bindings) is otherwise unchanged.
    if (committed_foreground_ && !ControlSurface(committed_foreground_)) {
        last_external_foreground_ = committed_foreground_;
        if (hold_active_) {
            if (!hold_foreground_) {
                hold_foreground_ = committed_foreground_; hold_anchor_source_ = "FirstStableExternal";
            } else if (hold_foreground_ != committed_foreground_) {
                hold_active_ = false; hold_foreground_.reset(); hold_anchor_source_.clear();
            }
        }
    }
    return CommitLocked(ResolveLocked(), now);
}
void DeviceProfileBindingEngine::NotifyManualProfileAction() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++manual_action_sequence_;
    hold_active_ = true; hold_foreground_.reset(); hold_anchor_source_ = "AwaitingStableExternal";
    const auto now = clock_();
    if (observed_ && !ControlSurface(observed_) && !pending_ && observed_ == committed_foreground_) {
        hold_foreground_ = committed_foreground_; hold_anchor_source_ = "CurrentStableExternal";
    } else if (ControlSurface(observed_) && control_surface_anchor_ && control_surface_entered_at_ &&
        now - *control_surface_entered_at_ <= ControlSurfaceAnchorFreshnessMs) {
        hold_foreground_ = control_surface_anchor_; hold_anchor_source_ = "ControlSurfacePreviousStableExternal";
    }
    DeviceProfileAutomationDecision hold; hold.foreground = observed_;
    hold.reason = DeviceProfileDecisionReason::ManualHold;
    CommitLocked(std::move(hold), now);
}
Json DeviceProfileBindingEngine::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Json blockers = Json::array({"PhaseHardwareActivationDisabled"});
    if (!configuration_available_) blockers.push_back("AutomationConfigurationUnavailable");
    if (!document_available_ || !context_.profile_document_available) blockers.push_back("ProfileDocumentUnavailable");
    if (!context_.keyboard_available) blockers.push_back("KeyboardAvailabilityUnknown");
    else if (!*context_.keyboard_available) blockers.push_back("KeyboardUnavailable");
    if (context_.safety_quarantine) blockers.push_back("SafetyQuarantine");
    if (context_.runtime_health != "Clean") blockers.push_back("RuntimeUnhealthyOrUnknown");
    if (context_.session_reconciliation_pending) blockers.push_back("SessionReconciliationPending");
    return {{"schema_version", 1}, {"enabled", config_.enabled}, {"binding_count", config_.bindings.size()},
        {"configuration_available", configuration_available_}, {"manual_action_sequence", manual_action_sequence_},
        {"configuration_document_revision", document_revision_}, {"clock_source", "monotonic milliseconds; process-local"},
        {"foreground_process", Optional(observed_)}, {"foreground_observed_at", Optional(observed_at_)},
        {"foreground_observation_sequence", foreground_observation_sequence_},
        {"committed_foreground_process", Optional(committed_foreground_)}, {"foreground_stable_at", Optional(stable_at_)},
        {"debounce_pending", pending_}, {"debounce_ms", StabilityMs}, {"evaluation_state", pending_ ? "Debouncing" : Reason(decision_.reason)},
        {"manual_hold", hold_active_}, {"manual_hold_foreground", Optional(hold_foreground_)},
        {"manual_hold_anchor", Optional(hold_foreground_)}, {"manual_hold_pending_anchor", hold_active_ && !hold_foreground_},
        {"manual_hold_source", hold_active_ ? Json("ManualProfileActivation") : Json(nullptr)},
        {"manual_hold_anchor_source", hold_active_ ? Json(hold_anchor_source_) : Json(nullptr)},
        {"last_external_foreground", Optional(last_external_foreground_)},
        {"control_surface_foreground", ControlSurface(observed_) ? Optional(observed_) : Json(nullptr)},
        {"resolved_profile_id", Optional(decision_.profile_id)}, {"matched_rule_id", Optional(decision_.rule_id)},
        {"decision_kind", Kind(decision_.kind)}, {"decision_reason", Reason(decision_.reason)},
        {"suppression_reason", decision_.reason == DeviceProfileDecisionReason::ManualHold ? Json("ManualHold") : Json(nullptr)},
        {"decision_sequence", decision_.sequence}, {"decision_timestamp", Optional(decision_.timestamp_ms)},
        {"decision_foreground_process", Optional(decision_.foreground)},
        {"hardware_activation_allowed", false}, {"hardware_block_reason", "PhaseHardwareActivationDisabled"},
        {"hardware_block_reasons", blockers}, {"authority_context", {{"profile_document_available", context_.profile_document_available},
            {"keyboard_available", Optional(context_.keyboard_available)}, {"runtime_health", context_.runtime_health},
            {"persistent_safety_quarantine", context_.safety_quarantine}, {"session_reconciliation_pending", context_.session_reconciliation_pending},
            {"observed_at", Optional(context_timestamp_ms_)}}}};
}
} // namespace aura
