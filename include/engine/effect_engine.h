#pragma once

#include "engine/effect.h"
#include "engine/overlay_manager.h"
#include "engine/automation_effect_runtime.h"
#include "aura/aura_types.h"
#include "aura/keymap.h"
#include <memory>
#include <mutex>
#include <chrono>

namespace aura {

class EffectEngine {
public:
    EffectEngine();

    // Holds a shared_ptr copy so a concurrent config hot reload can never
    // invalidate the profile currently being rendered (no dangling pointers).
    void SetActiveProfile(std::shared_ptr<const Profile> profile);

    // Zero-allocation render tick for the current elapsed time with optional GSI reader and overlays
    void Tick(FrameBuffer& out_frame, const Keymap& keymap, const IGsiReader* gsi = nullptr);
    // Explicit clock entry point for deterministic frame tests; same production path.
    void TickAt(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap, const IGsiReader* gsi = nullptr);
    AutomationEffectRuntime& GetAutomationEffects() { return automation_effects_; }

    uint64_t GetElapsedMs() const;

    // CS2 Transient Event Overlay Manager
    OverlayManager& GetOverlayManager() { return overlay_manager_; }
    const OverlayManager& GetOverlayManager() const { return overlay_manager_; }

    // Edit-time preview frame hardware push
    void SetPreviewFrame(const FrameBuffer& frame, uint64_t duration_ms = 300);
    void ClearPreview();
    bool HasActivePreview() const;

private:
    std::shared_ptr<const Profile> GetActiveProfileCopy() const;

    mutable std::mutex profile_mutex_;
    std::shared_ptr<const Profile> active_profile_;
    uint64_t profile_started_ms_{0};
    std::chrono::steady_clock::time_point start_time_;

    OverlayManager overlay_manager_;
    AutomationEffectRuntime automation_effects_;

    mutable std::mutex preview_mutex_;
    FrameBuffer preview_frame_;
    bool preview_active_{false};
    std::chrono::steady_clock::time_point preview_expiry_;
};

} // namespace aura
