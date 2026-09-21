#include "engine/automation_effect_runtime.h"
#include "engine/overlay_manager.h"
#include "utils/logger.h"
#include <algorithm>
#include <cmath>

namespace aura {
uint64_t AutomationEffectRuntime::Revision() const { std::lock_guard<std::mutex> lock(mutex_); return revision_; }
size_t AutomationEffectRuntime::ActiveCount() const { std::lock_guard<std::mutex> lock(mutex_); return layers_.size(); }
size_t AutomationEffectRuntime::DiagnosticCount() const { std::lock_guard<std::mutex> lock(mutex_); return diagnostics_.size(); }
void AutomationEffectRuntime::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++revision_; layers_.clear(); true_intervals_.clear(); diagnostics_.clear();
}
void AutomationEffectRuntime::Diagnose(const std::string& id, const char* reason) {
    if (diagnostics_.size() >= 64) return;
    if (diagnostics_.insert(id + ":" + reason).second)
        LOG_WARN("[Automation effect] " << id << ": " << reason);
}
void AutomationEffectRuntime::Consume(const AutomationEvaluation& evaluation, const Resolver& resolve,
                                      uint64_t now, uint64_t expected_revision) {
    std::lock_guard<std::mutex> lock(mutex_);
    // A rebuild completed after the caller began evaluating: consume no old work.
    if (expected_revision != revision_ || evaluation.config_generation < config_generation_) return;
    if (evaluation.config_generation != config_generation_) {
        layers_.clear(); true_intervals_.clear(); diagnostics_.clear();
        config_generation_ = evaluation.config_generation;
    }
    for (const auto& decision : evaluation.decisions) {
        const auto& action = decision.action;
        if (action.at("type") != "trigger_effect") continue;
        const bool persistent = action.at("lifetime") == "while_true";
        auto existing = std::find_if(layers_.begin(), layers_.end(), [&](const Layer& layer) { return layer.id == decision.rule_id; });
        if (persistent) {
            if (decision.eligibility != ConditionTruth::True) {
                true_intervals_.erase(decision.rule_id);
                if (existing != layers_.end()) layers_.erase(existing);
                continue;
            }
            // Includes completed or failed instances for this True interval.
            if (!true_intervals_.insert(decision.rule_id).second) continue;
        } else {
            if (!decision.admitted) continue;
            if (existing != layers_.end() && action.value("retrigger", "restart") == "ignore_while_active") continue;
        }
        if (existing == layers_.end() && layers_.size() >= 32) {
            Diagnose(decision.rule_id, "instance limit reached; admission consumed"); continue;
        }
        try {
            auto instance = resolve(action.at("effect"));
            if (!instance) { Diagnose(decision.rule_id, "effect unavailable; admission consumed"); continue; }
            Layer layer;
            layer.id = decision.rule_id; layer.instance = std::move(instance);
            layer.persistent = persistent; layer.started = now;
            layer.replace = action.value("composition", "overlay") == "replace";
            layer.additive = action.value("blend", "alpha") == "additive";
            layer.order = {action.value("priority", 10), decision.rule_order, ++sequence_};
            const auto& generation = layer.instance.GetGeneration();
            layer.capable = generation && generation->lifecycle.version == 1 && generation->lifecycle.is_finished;
            const auto envelope = action.value("compatibility", nlohmann::json::object());
            layer.duration = envelope.value("duration_ms", uint64_t{1200});
            layer.fade = envelope.value("fade_out_ms", uint64_t{400});
            layer.attack = envelope.value("attack_ms", uint64_t{0});
            // Capability effects own their curve; compatibility values never multiply it.
            // A legacy envelope must not be shortened by the implicit 5-second cap.
            if (!persistent) layer.watchdog = action.value("watchdog_ms", layer.capable ? uint64_t{5000} : std::max(uint64_t{5000}, layer.duration));
            if (existing == layers_.end()) layers_.push_back(std::move(layer));
            else *existing = std::move(layer); // construct successfully before retiring old object
        } catch (...) { Diagnose(decision.rule_id, "effect construction threw; previous instance retained"); }
    }
    std::sort(layers_.begin(), layers_.end(), [](const Layer& a, const Layer& b) { return a.order < b.order; });
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
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = layers_.begin(); it != layers_.end();) {
        if (it->persistent == persistent && !Render(*it, now, lower, keymap, gsi)) it = layers_.erase(it);
        else ++it;
    }
}
} // namespace aura
