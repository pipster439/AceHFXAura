#pragma once

#include "engine/effect.h"
#include "engine/automation_effect_runtime.h"
#include "engine/layer_compositor.h"
#include "aura/aura_types.h"
#include "aura/keymap.h"
#include "config/config_contract.h"
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
    bool ReconcileProfile(std::shared_ptr<const Profile> profile);
    std::shared_ptr<const Profile> GetActiveProfile() const { return GetActiveProfileCopy(); }

    // Zero-allocation render tick for the current elapsed time with optional GSI reader and overlays
    void Tick(FrameBuffer& out_frame, const Keymap& keymap, const IGsiReader* gsi = nullptr);
    // Explicit clock entry point for deterministic frame tests; same production path.
    void TickAt(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap, const IGsiReader* gsi = nullptr);
    void TickAt(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap, const EffectContext& context);
    void SetInputSources(const AudioFeatureSource* audio, const ScreenColorSource* screen);
    const AudioFeatureSource* GetAudioSource() const { return audio_source_; }
    const ScreenColorSource* GetScreenSource() const { return screen_source_; }
    void SetHallAnalogHost(const AuraHallAnalogHostV1* host);
    void SetHallLightingConfig(const HallLightingConfig& config);
    HallLightingConfig GetHallLightingConfig() const;
    bool ActiveEffectUsesHall() const;
    const LayerCompositor& GetCompositor() const { return compositor_; }
    const FrameBuffer& GetLastFrame() const { return last_frame_; }
    struct RenderDiagnostics {
        bool active_program{false}, program_faulted{false};
        bool audio_used{false}, audio_available{false};
        bool screen_used{false}, screen_available{false};
        uint64_t rendered_frames{0}, program_generation{0}, last_render_us{0};
        std::string program_error;
        uint64_t compositor_calls{0}, composited_layers{0};
    };
    RenderDiagnostics GetRenderDiagnostics() const;
    AutomationEffectRuntime& GetAutomationEffects() { return automation_effects_; }

    uint64_t GetElapsedMs() const;

    // CS2 Transient Event Overlay Manager

    // Edit-time preview frame hardware push
    void SetPreviewFrame(const FrameBuffer& frame, uint64_t duration_ms = 300);
    void ClearPreview();
    bool HasActivePreview() const;

private:
    std::shared_ptr<const Profile> GetActiveProfileCopy() const;

    mutable std::mutex profile_mutex_;
    std::shared_ptr<const Profile> active_profile_;
    uint64_t profile_started_ms_{0};
    std::string base_identity_, attempted_base_identity_;
    std::chrono::steady_clock::time_point start_time_;

    AutomationEffectRuntime automation_effects_;
    LayerCompositor compositor_;
    const AudioFeatureSource* audio_source_{nullptr};
    const ScreenColorSource* screen_source_{nullptr};
    AuraHallAnalogHostV1 hall_host_{};
    mutable std::mutex hall_config_mutex_;
    HallLightingConfig hall_lighting_{};
    FrameBuffer last_frame_{};
    mutable std::mutex render_diagnostics_mutex_;
    RenderDiagnostics render_diagnostics_{};

    mutable std::mutex preview_mutex_;
    FrameBuffer preview_frame_;
    bool preview_active_{false};
    std::chrono::steady_clock::time_point preview_expiry_;
};

} // namespace aura
