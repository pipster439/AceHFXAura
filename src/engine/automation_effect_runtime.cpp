#include "engine/automation_effect_runtime.h"
#include "engine/overlay_manager.h"
#include "utils/logger.h"
#include <algorithm>
#include <cmath>

namespace aura {
namespace { void Count(uint64_t& counter) { if (counter!=UINT64_MAX) ++counter; } }
size_t AutomationEffectRuntime::PendingCount() const { std::lock_guard<std::mutex> lock(mutex_); return pending_.size(); }
AutomationEffectRuntime::Counters AutomationEffectRuntime::GetCounters() const { std::lock_guard<std::mutex> lock(mutex_); return counters_; }

uint64_t AutomationEffectRuntime::Revision() const { std::lock_guard<std::mutex> lock(mutex_); return revision_; }
size_t AutomationEffectRuntime::ActiveCount() const { std::lock_guard<std::mutex> lock(mutex_); return layers_.size(); }
size_t AutomationEffectRuntime::DiagnosticCount() const { std::lock_guard<std::mutex> lock(mutex_); return diagnostics_.size(); }
void AutomationEffectRuntime::Clear() {
    std::vector<Layer> retired_layers;
    std::deque<Token> retired_tokens;
    std::lock_guard<std::mutex> lock(mutex_);
    ++revision_; retired_layers.swap(layers_); true_intervals_.clear(); interval_semantics_.clear(); interval_recipes_.clear(); diagnostics_.clear(); retired_tokens.swap(pending_);
}
void AutomationEffectRuntime::Diagnose(const std::string& id, const char* reason) {
    if (diagnostics_.size() >= 64) return;
    if (diagnostics_.insert(id + ":" + reason).second)
        LOG_WARN("[Automation effect] " << id << ": " << reason);
}
void AutomationEffectRuntime::Consume(const AutomationEvaluation& evaluation, const Resolver& resolve,
                                      uint64_t now, uint64_t expected_revision, const Preparer& prepare) {
    // Declared before the lock: even exception/early-return unwinding unlocks
    // before releasing any retired or temporary generation ownership.
    std::vector<Layer> retired_layers;
    retired_layers.reserve(AutomationRetriggerLimits::active_global + evaluation.decisions.size());
    std::deque<Token> retired_tokens;
    std::deque<PreparedEffectSource> prepared_sources;
    std::deque<TriggeredEffectInstance> constructed_instances;
    std::lock_guard<std::mutex> lock(mutex_);
    // A rebuild completed after the caller began evaluating: consume no old work.
    if (expected_revision != revision_ || evaluation.config_generation < config_generation_) return;
    if (evaluation.config_generation != config_generation_ && !evaluation.reconciliation_complete) {
        retired_layers.swap(layers_); true_intervals_.clear(); interval_semantics_.clear(); interval_recipes_.clear(); diagnostics_.clear(); retired_tokens.swap(pending_);
    }
    config_generation_ = evaluation.config_generation;
    auto status_for = [&](const std::string& id) -> const AutomationEvaluation::RuleStatus* {
        for (const auto& status : evaluation.rule_status) if (status.id == id) return &status;
        return nullptr;
    };
    if (evaluation.reconciliation_complete) {
        for (auto it = layers_.begin(); it != layers_.end();) {
            const auto* status = status_for(it->id);
            if (!status || !status->enabled || it->semantic_identity != status->semantic_identity ||
                (!it->persistent && status->continuation != ConditionTruth::True)) {
                retired_layers.push_back(std::move(*it)); it = layers_.erase(it);
            }
            else { std::get<1>(it->order) = status->rule_order; ++it; }
        }
        for (auto it = interval_semantics_.begin(); it != interval_semantics_.end();) {
            const auto* status = status_for(it->first);
            if (!status || !status->enabled || status->semantic_identity != it->second) {
                true_intervals_.erase(it->first); interval_recipes_.erase(it->first); it = interval_semantics_.erase(it);
            } else ++it;
        }
    }
    // Reconcile/expire all tokens before admitting or constructing more work.
    for (auto it=pending_.begin();it!=pending_.end();) {
        const auto* status=status_for(it->id);
        if (evaluation.reconciliation_complete && (!status || !status->enabled ||
            status->semantic_identity!=it->semantic_identity || status->continuation!=ConditionTruth::True)) {
            Count(counters_.cancelled); retired_tokens.push_back(std::move(*it)); it=pending_.erase(it);
        } else if (now>=it->admitted && now-it->admitted>AutomationRetriggerLimits::pending_ttl_ms) {
            Count(counters_.expired); Diagnose(it->id,"pending token expired"); retired_tokens.push_back(std::move(*it)); it=pending_.erase(it);
        } else { if(status) it->rule_order=status->rule_order; ++it; }
    }
    // At most four pending factory attempts, at most one per rule, per frame.
    // Blocked rule heads do not prevent another rule using available capacity.
    size_t attempts=0; std::set<std::string> visited;
    for (auto it=pending_.begin();it!=pending_.end() && attempts<AutomationRetriggerLimits::pending_attempts_per_frame && layers_.size()<AutomationRetriggerLimits::active_global;) {
        if (!visited.insert(it->id).second || std::any_of(layers_.begin(),layers_.end(),[&](const Layer& layer){return layer.id==it->id;})) { ++it; continue; }
        retired_tokens.push_back(std::move(*it)); it=pending_.erase(it); ++attempts;
        auto& token=retired_tokens.back();
        try {
            constructed_instances.emplace_back();
            auto& instance=constructed_instances.back();
            instance=token.source.Create();
            if (!instance) { Count(counters_.factory_failures); Diagnose(token.id,"queued factory failed; token consumed"); continue; }
            auto layer=MakeLayer(token.id,token.action,token.rule_order,instance,now);
            layer.semantic_identity=token.semantic_identity; layers_.push_back(std::move(layer));
        } catch (...) { Count(counters_.factory_failures); Diagnose(token.id,"queued factory threw; token consumed"); }
    }
    for (const auto& decision : evaluation.decisions) {
        const auto& action = decision.action;
        if (action.at("type") != "trigger_effect") continue;
        const bool persistent = action.at("lifetime") == "while_true";
        const auto* status = status_for(decision.rule_id);
        if (evaluation.reconciliation_complete && (!status || !status->enabled ||
            (!persistent && status->continuation != ConditionTruth::True))) continue;
        auto existing = std::find_if(layers_.begin(), layers_.end(), [&](const Layer& layer) { return layer.id == decision.rule_id; });
        const std::string target_identity = status ? status->recipe_identity + ":generation=" + std::to_string(status->plugin_generation) : "";
        if (persistent) {
            if (decision.eligibility != ConditionTruth::True) {
                true_intervals_.erase(decision.rule_id);
                interval_recipes_.erase(decision.rule_id);
                if (existing != layers_.end()) { retired_layers.push_back(std::move(*existing)); layers_.erase(existing); }
                continue;
            }
            // Includes completed or failed instances for this True interval.
            const bool entered = true_intervals_.insert(decision.rule_id).second;
            if (status) interval_semantics_[decision.rule_id] = status->semantic_identity;
            if (!entered) {
                if (!status) continue;
                if (existing == layers_.end()) {
                    if (interval_recipes_[decision.rule_id] == target_identity) continue;
                } else {
                const auto generation = existing->instance.GetGeneration();
                const bool changed = existing->recipe_identity != status->recipe_identity ||
                    (status->plugin_generation && (!generation || generation->generation_id != status->plugin_generation));
                if (!changed || existing->attempted_identity == target_identity) continue;
                }
            }
        } else {
            if (!decision.admitted) continue;
            if (existing != layers_.end() && action.value("retrigger", "restart") == "ignore_while_active") continue;
        }
        const auto policy=action.value("retrigger","restart");
        if (!persistent && policy=="queue") {
            const auto waiting=std::count_if(pending_.begin(),pending_.end(),[&](const Token& token){return token.id==decision.rule_id;});
            const bool immediate=existing==layers_.end() && waiting==0 && layers_.size()<AutomationRetriggerLimits::active_global;
            if (!immediate && (waiting>=AutomationRetriggerLimits::pending_per_rule || pending_.size()>=AutomationRetriggerLimits::pending_global)) {
                Count(counters_.capacity_drops); Diagnose(decision.rule_id,"pending limit reached; newest admission dropped"); continue;
            }
            try {
                prepared_sources.emplace_back();
                auto& source=prepared_sources.back();
                source=prepare?prepare(action.at("effect")):PreparedEffectSource{};
                if (!source) { Count(counters_.unavailable); Diagnose(decision.rule_id,"source unavailable at admission; consumed"); continue; }
                if (immediate) {
                    constructed_instances.emplace_back();
                    auto& instance=constructed_instances.back();
                    instance=source.Create();
                    if (!instance) { Count(counters_.factory_failures); Diagnose(decision.rule_id,"queue initial factory failed; admission consumed"); continue; }
                    auto layer=MakeLayer(decision.rule_id,action,decision.rule_order,instance,now);
                    if(status) layer.semantic_identity=status->semantic_identity;
                    layers_.push_back(std::move(layer));
                } else pending_.push_back({decision.rule_id,status?status->semantic_identity:"",action,decision.rule_order,now,source});
            } catch (...) { Count(counters_.unavailable); Diagnose(decision.rule_id,"queue source preparation/construction failed; admission consumed"); }
            continue;
        }
        if (!persistent && policy=="stack") {
            const auto active=std::count_if(layers_.begin(),layers_.end(),[&](const Layer& layer){return layer.id==decision.rule_id;});
            if(active>=AutomationRetriggerLimits::stack_active || layers_.size()>=AutomationRetriggerLimits::active_global) { Count(counters_.capacity_drops); Diagnose(decision.rule_id,"stack limit reached; newest admission dropped"); continue; }
            existing=layers_.end(); // each admission owns a fresh independent layer
        }
        if (existing == layers_.end() && layers_.size() >= AutomationRetriggerLimits::active_global) {
            Diagnose(decision.rule_id, "instance limit reached; admission consumed"); continue;
        }
        try {
            if (persistent) interval_recipes_[decision.rule_id] = target_identity;
            if (persistent && existing != layers_.end()) existing->attempted_identity = target_identity;
            constructed_instances.emplace_back();
            auto& instance=constructed_instances.back();
            instance = resolve(action.at("effect"));
            if (!instance) { Diagnose(decision.rule_id, "effect unavailable; admission consumed"); continue; }
            Layer layer=MakeLayer(decision.rule_id,action,decision.rule_order,instance,now);
            if (status) { layer.semantic_identity=status->semantic_identity; layer.recipe_identity=status->recipe_identity; layer.attempted_identity=target_identity; }
            const auto& generation=layer.instance.GetGeneration();
            if (persistent && status) interval_recipes_[decision.rule_id]=status->recipe_identity+":generation="+std::to_string(generation?generation->generation_id:0);
            if (existing == layers_.end()) layers_.push_back(std::move(layer));
            else { retired_layers.push_back(std::move(*existing)); *existing = std::move(layer); } // retire after unlock
        } catch (...) { Diagnose(decision.rule_id, "effect construction threw; previous instance retained"); }
    }
    std::sort(layers_.begin(), layers_.end(), [](const Layer& a, const Layer& b) { return a.order < b.order; });
}
AutomationEffectRuntime::Layer AutomationEffectRuntime::MakeLayer(const std::string& id, const nlohmann::json& action,
    size_t order, TriggeredEffectInstance instance, uint64_t now) {
    Layer layer; layer.id=id; layer.instance=std::move(instance);
    layer.persistent = action.at("lifetime") == "while_true"; layer.started = now;
    layer.replace = action.value("composition", "overlay") == "replace";
    layer.additive = action.value("blend", "alpha") == "additive";
    layer.order = {action.value("priority", 10), order, ++sequence_};
    const auto& generation = layer.instance.GetGeneration();
    layer.capable = generation && generation->lifecycle.version == 1 && generation->lifecycle.is_finished;
    const auto envelope = action.value("compatibility", nlohmann::json::object());
    layer.duration = envelope.value("duration_ms", uint64_t{1200});
    layer.fade = envelope.value("fade_out_ms", uint64_t{400});
    layer.attack = envelope.value("attack_ms", uint64_t{0});
    // Capability effects own their curve; compatibility values never multiply it.
    // A legacy envelope must not be shortened by the implicit 5-second cap.
    if (!layer.persistent) layer.watchdog = action.value("watchdog_ms", layer.capable ? uint64_t{5000} : std::max(uint64_t{5000}, layer.duration));
    return layer;
}
bool AutomationEffectRuntime::Render(Layer& layer, uint64_t now, FrameBuffer& lower,
                                     const Keymap& keymap, const IGsiReader* gsi) {
    const uint64_t elapsed = now >= layer.started ? now - layer.started : 0;
    if (!layer.persistent && elapsed >= layer.watchdog) { Diagnose(layer.id, "watchdog expired"); return false; }
    if (!layer.capable && !layer.persistent && elapsed >= layer.duration) return false;
    // Checks between plugin calls include time spent in a returned slow callback.
    // This cannot interrupt hangs, terminate, or native access violations.
    const auto wall_start = std::chrono::steady_clock::now();
    auto overdue = [&] {
        if (layer.persistent) return false;
        const auto spent = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - wall_start).count());
        return spent >= layer.watchdog - elapsed;
    };
    try {
        temporary_.Clear();
        layer.instance.GetEffect()->RenderWithContext(EffectContext(elapsed, keymap, gsi), temporary_);
        if (overdue()) { Diagnose(layer.id, "watchdog expired"); return false; }
        double opacity = 1;
        if (layer.capable) {
            const auto& lifecycle = layer.instance.GetGeneration()->lifecycle;
            const auto finished = lifecycle.is_finished(layer.instance.GetEffect().get(), elapsed);
            if (finished > 1) { Diagnose(layer.id, "invalid finished status"); return false; }
            if (finished == 1) return false;
            if (overdue()) { Diagnose(layer.id, "watchdog expired"); return false; }
            if (lifecycle.get_opacity) opacity = lifecycle.get_opacity(layer.instance.GetEffect().get(), elapsed);
            if (!std::isfinite(opacity)) { Diagnose(layer.id, "non-finite opacity"); return false; }
            opacity = std::clamp(opacity, 0.0, 1.0);
            if (overdue()) { Diagnose(layer.id, "watchdog expired"); return false; }
        } else if (!layer.persistent) {
            // Reuse the exact legacy envelope math; legacy executor remains unchanged.
            ActiveOverlay envelope;
            envelope.duration_ms = layer.duration; envelope.fade_out_ms = layer.fade; envelope.attack_ms = layer.attack;
            opacity = envelope.ComputeWeight(elapsed);
        }
        for (size_t led = 0; led < TOTAL_LEDS; ++led) {
            const size_t offset = led * RGB_CHANNELS;
            const bool covered = layer.replace || temporary_.buffer[offset] || temporary_.buffer[offset+1] || temporary_.buffer[offset+2];
            if (!covered) continue;
            for (size_t channel = 0; channel < RGB_CHANNELS; ++channel) {
                const size_t i = offset + channel;
                const double value = (layer.additive ? lower.buffer[i] : (1-opacity)*lower.buffer[i]) + opacity*temporary_.buffer[i];
                lower.buffer[i] = static_cast<uint8_t>(std::clamp(value, 0.0, 255.0));
            }
        }
        return true;
    } catch (...) { Diagnose(layer.id, "effect render/lifecycle callback threw; instance retired"); return false; }
}
void AutomationEffectRuntime::Apply(bool persistent, uint64_t now, FrameBuffer& lower,
                                    const Keymap& keymap, const IGsiReader* gsi) {
    std::vector<Layer> retired_layers;
    retired_layers.reserve(AutomationRetriggerLimits::active_global);
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = layers_.begin(); it != layers_.end();) {
        if (it->persistent == persistent && !Render(*it, now, lower, keymap, gsi)) {
            retired_layers.push_back(std::move(*it)); it = layers_.erase(it);
        }
        else ++it;
    }
}
} // namespace aura
