#include "engine/overlay_manager.h"
#include "gsi/gsi_adapter.h"
#include "utils/logger.h"

#include <algorithm>
#include <cmath>

namespace aura {

double ActiveOverlay::ComputeWeight(uint64_t current_ms) const {
    if (current_ms < start_ms) return 0.0;
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
    return (current_ms >= start_ms && (current_ms - start_ms) >= duration_ms);
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
            existing.start_ms = 0; // Will be set on next render or current clock
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
    overlay.start_ms = 0; // 0 indicates needs initialization to current_ms on first tick
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
}

void OverlayManager::UpdateBindingsFromGsi(const GsiState* gsi, uint64_t current_ms) {
    if (!gsi) return;

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& binding : bindings_) {
        bool active = gsi->GetBool(binding.event_name.c_str(), false);
        bool prev = prev_event_states_[binding.event_name];

        // Rising edge detection (false -> true)
        if (active && !prev && binding.effect) {
            // Trigger overlay
            ActiveOverlay overlay;
            overlay.event_name = binding.event_name;
            overlay.effect = binding.effect;
            overlay.start_ms = current_ms;
            overlay.duration_ms = binding.duration_ms;
            overlay.fade_out_ms = binding.fade_out_ms;
            overlay.attack_ms = binding.attack_ms;
            overlay.blend_mode = binding.blend_mode.empty() ? "blend" : binding.blend_mode;

            // Remove any existing active overlay for this event
            active_overlays_.erase(
                std::remove_if(active_overlays_.begin(), active_overlays_.end(),
                    [&binding](const ActiveOverlay& o) { return o.event_name == binding.event_name; }),
                active_overlays_.end());

            active_overlays_.push_back(std::move(overlay));
            LOG_INFO("[OverlayManager] GSI 事件跃迁触发覆盖: " << binding.event_name);
        }

        prev_event_states_[binding.event_name] = active;
    }
}

void OverlayManager::ApplyOverlays(uint64_t current_ms,
                                  FrameBuffer& in_out_frame,
                                  const Keymap& keymap,
                                  const IGsiReader* gsi) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 1. Initialize newly triggered overlays (start_ms == 0)
    for (auto& overlay : active_overlays_) {
        if (overlay.start_ms == 0) {
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
        EffectContext ctx(current_ms, keymap, gsi);
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
