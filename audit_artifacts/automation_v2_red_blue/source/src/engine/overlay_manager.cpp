#include "engine/overlay_manager.h"
#include "gsi/gsi_adapter.h"
#include "utils/logger.h"

#include <algorithm>
#include <cmath>

namespace aura {

double ActiveOverlay::ComputeWeight(uint64_t current_ms) const {
    if (current_ms < start_ms) return 0.0;
    if (persistent) return 1.0;
    uint64_t delta_t = current_ms - start_ms;
    if (delta_t >= duration_ms) return 0.0;

    // 1. Attack phase (fast ramp-up)
    if (attack_ms > 0 && delta_t < attack_ms) {
        return std::clamp(static_cast<double>(delta_t) / static_cast<double>(attack_ms), 0.0, 1.0);
    }

    // 2. Fade out linear crossfade restoration phase
    if (fade_out_ms > 0 && duration_ms > fade_out_ms) {
        uint64_t fade_start = duration_ms - fade_out_ms;
        if (delta_t >= fade_start) {
            uint64_t fade_elapsed = delta_t - fade_start;
            double w = 1.0 - (static_cast<double>(fade_elapsed) / static_cast<double>(fade_out_ms));
            return std::clamp(w, 0.0, 1.0);
        }
    }

    // 3. Sustain phase
    return 1.0;
}

bool ActiveOverlay::IsExpired(uint64_t current_ms) const {
    return !persistent && (current_ms >= start_ms && (current_ms - start_ms) >= duration_ms);
}

OverlayManager::OverlayManager() {
    temp_overlay_buf_.Clear();
}

void OverlayManager::TriggerOverlay(const std::string& event_name,
                                    std::shared_ptr<Effect> effect,
                                    uint64_t duration_ms,
                                    uint64_t fade_out_ms,
                                    const std::string& blend_mode,
                                    uint64_t attack_ms) {
    if (!effect) return;

    std::lock_guard<std::mutex> lock(mutex_);

    // Check if an overlay for this same event already exists; if so, refresh/extend it
    for (auto& existing : active_overlays_) {
        if (existing.event_name == event_name) {
            existing.pending_start = true;
            existing.duration_ms = duration_ms;
            existing.fade_out_ms = fade_out_ms;
            existing.blend_mode = blend_mode;
            existing.effect = effect;
            existing.attack_ms = attack_ms;
            return;
        }
    }

    ActiveOverlay overlay;
    overlay.event_name = event_name;
    overlay.effect = effect;
    overlay.pending_start = true;
    overlay.duration_ms = duration_ms;
    overlay.fade_out_ms = fade_out_ms;
    overlay.attack_ms = attack_ms;
    overlay.blend_mode = blend_mode.empty() ? "blend" : blend_mode;

    active_overlays_.push_back(std::move(overlay));
    LOG_INFO("[OverlayManager] 触发瞬态光效覆盖: " << event_name << " (时长: " << duration_ms << "ms, 淡出: " << fade_out_ms << "ms)");
}

void OverlayManager::RegisterBinding(const OverlayBinding& binding) {
    std::lock_guard<std::mutex> lock(mutex_);
    bindings_.push_back(binding);
}

void OverlayManager::ClearBindings() {
    std::lock_guard<std::mutex> lock(mutex_);
    bindings_.clear();
    prev_event_states_.clear();
    prev_event_sequences_.clear();
    active_overlays_.clear();
    in_game_ = false;
}

void OverlayManager::UpdateBindingsFromGsi(const GsiState* gsi, uint64_t current_ms,
                                           const std::string& foreground) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool enabled = gsi && gsi->IsActive() &&
        (foreground == "cs2.exe" || foreground == "cs2" || foreground == "csgo.exe" || foreground == "csgo");
    const bool entering = enabled && !in_game_;
    in_game_ = enabled;
    if (!enabled) active_overlays_.clear();
    for (size_t i = 0; i < bindings_.size(); ++i) {
        const auto& binding = bindings_[i];
        const std::string id = binding.id.empty() ? "binding_" + std::to_string(i) : binding.id;
        const bool event_active = gsi && gsi->GetBool(binding.event_name.c_str(), false);
        const double sequence = gsi ? gsi->GetNumber(("event_sequence." + binding.event_name).c_str(), 0.0) : 0.0;
        const bool occurred = !entering && ((sequence > 0 && sequence != prev_event_sequences_[id]) ||
                              (sequence == 0 && event_active && !prev_event_states_[id]));
        prev_event_sequences_[id] = sequence;
        prev_event_states_[id] = event_active;
        const bool matches = enabled && (!binding.condition || binding.condition(gsi, foreground));
        auto existing = std::find_if(active_overlays_.begin(), active_overlays_.end(),
            [&id](const ActiveOverlay& o) { return o.binding_id == id; });
        if (binding.trigger == "state") {
            if (!matches) {
                if (existing != active_overlays_.end()) active_overlays_.erase(existing);
                continue;
            }
            if (existing != active_overlays_.end()) continue;
        } else if (!matches || !occurred) continue;

        ActiveOverlay overlay;
        overlay.binding_id = id;
        overlay.event_name = binding.event_name;
        overlay.effect = binding.make_effect ? binding.make_effect() : binding.effect;
        if (!overlay.effect) continue;
        overlay.start_ms = current_ms;
        overlay.duration_ms = binding.duration_ms;
        overlay.fade_out_ms = binding.fade_out_ms;
        overlay.attack_ms = binding.attack_ms;
        overlay.blend_mode = binding.blend_mode;
        overlay.priority = binding.priority;
        overlay.persistent = binding.trigger == "state";
        // Repeated events restart their own layer, even within the GSI pulse window.
        if (existing != active_overlays_.end()) *existing = std::move(overlay);
        else active_overlays_.push_back(std::move(overlay));
    }
    std::stable_sort(active_overlays_.begin(), active_overlays_.end(),
        [](const ActiveOverlay& a, const ActiveOverlay& b) { return a.priority < b.priority; });
}

void OverlayManager::ApplyOverlays(uint64_t current_ms,
                                  FrameBuffer& in_out_frame,
                                  const Keymap& keymap,
                                  const IGsiReader* gsi) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 1. Initialize newly triggered overlays (start_ms == 0)
    for (auto& overlay : active_overlays_) {
        if (overlay.pending_start) {
            overlay.pending_start = false;
            overlay.start_ms = current_ms;
        }
    }

    // 2. Remove expired overlays
    active_overlays_.erase(
        std::remove_if(active_overlays_.begin(), active_overlays_.end(),
            [current_ms](const ActiveOverlay& o) { return o.IsExpired(current_ms); }),
        active_overlays_.end());

    if (active_overlays_.empty()) return;

    // 3. Render and blend each active overlay (0 heap allocation)
    for (const auto& overlay : active_overlays_) {
        if (!overlay.effect) continue;

        double w = overlay.ComputeWeight(current_ms);
        if (w <= 0.0001) continue;

        temp_overlay_buf_.Clear();
        EffectContext ctx(current_ms - overlay.start_ms, keymap, gsi);
        overlay.effect->RenderWithContext(ctx, temp_overlay_buf_);

        const std::string& mode = overlay.blend_mode;

        if (mode == "replace") {
            // 全盘强制置换（适用于致盲全白屏等高侵入事件）
            for (size_t i = 0; i < FRAME_BUFFER_SIZE; ++i) {
                double base_val = static_cast<double>(in_out_frame.buffer[i]);
                double ov_val = static_cast<double>(temp_overlay_buf_.buffer[i]);
                double blended = ov_val * w + base_val * (1.0 - w);
                in_out_frame.buffer[i] = static_cast<uint8_t>(std::clamp(blended, 0.0, 255.0));
            }
        } else if (mode == "add") {
            // 增量光能脉冲（适用于爆头高能闪光等）
            for (size_t i = 0; i < FRAME_BUFFER_SIZE; ++i) {
                double base_val = static_cast<double>(in_out_frame.buffer[i]);
                double ov_val = static_cast<double>(temp_overlay_buf_.buffer[i]);
                double blended = base_val + ov_val * w;
                in_out_frame.buffer[i] = static_cast<uint8_t>(std::min(255.0, blended));
            }
        } else {
            // 线性平滑混合（RGB 三通道感知：非覆盖背景保持原有方案色彩）
            for (size_t i = 0; i + 2 < FRAME_BUFFER_SIZE; i += 3) {
                uint8_t or_r = temp_overlay_buf_.buffer[i];
                uint8_t or_g = temp_overlay_buf_.buffer[i + 1];
                uint8_t or_b = temp_overlay_buf_.buffer[i + 2];

                if (or_r == 0 && or_g == 0 && or_b == 0) {
                    continue; // 覆盖层此键为暗色时，透传底层光效
                }

                double b_r = static_cast<double>(in_out_frame.buffer[i]);
                double b_g = static_cast<double>(in_out_frame.buffer[i + 1]);
                double b_b = static_cast<double>(in_out_frame.buffer[i + 2]);

                in_out_frame.buffer[i]     = static_cast<uint8_t>(std::clamp(b_r * (1.0 - w) + static_cast<double>(or_r) * w, 0.0, 255.0));
                in_out_frame.buffer[i + 1] = static_cast<uint8_t>(std::clamp(b_g * (1.0 - w) + static_cast<double>(or_g) * w, 0.0, 255.0));
                in_out_frame.buffer[i + 2] = static_cast<uint8_t>(std::clamp(b_b * (1.0 - w) + static_cast<double>(or_b) * w, 0.0, 255.0));
            }
        }
    }
}

void OverlayManager::ClearActiveOverlays() {
    std::lock_guard<std::mutex> lock(mutex_);
    active_overlays_.clear();
}

size_t OverlayManager::GetActiveOverlayCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_overlays_.size();
}

} // namespace aura
