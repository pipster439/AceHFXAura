#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <cstdint>
#include <functional>

#include "aura/aura_types.h"
#include "aura/keymap.h"
#include "engine/effect.h"
#include "engine/plugin_interface.h"

namespace aura {

class GsiState;

/**
 * @brief Represents an active transient event pulse overlay.
 */
struct ActiveOverlay {
    std::string binding_id;
    bool persistent{false};
    bool pending_start{false};
    std::string event_name;
    std::shared_ptr<Effect> effect;
    uint64_t start_ms{0};
    uint64_t attack_ms{0};           // Ramp-up duration (ms)
    uint64_t duration_ms{1200};      // Total lifetime duration (ms)
    uint64_t fade_out_ms{400};       // Linear crossfade restoration duration (ms)
    std::string blend_mode{"blend"}; // "blend", "replace", "add"
    int priority{10};

    // Computes dynamic weight w in [0.0, 1.0] at current_ms
    double ComputeWeight(uint64_t current_ms) const;
    bool IsExpired(uint64_t current_ms) const;
};

/**
 * @brief Configured event-to-overlay binding definition.
 */
struct OverlayBinding {
    std::string id;
    std::string trigger{"event"};
    int priority{10};
    std::function<bool(const GsiState*, const std::string&)> condition;
    std::function<std::shared_ptr<Effect>()> make_effect;
    std::string event_name;          // e.g. "event.kill", "event.flash"
    std::string effect_name;         // Effect or plugin identifier
    std::shared_ptr<Effect> effect;  // Resolved effect instance
    uint64_t duration_ms{1200};
    uint64_t fade_out_ms{400};
    uint64_t attack_ms{0};
    std::string blend_mode{"blend"};
};

/**
 * @brief CS2 Transient Event Overlay Manager.
 * 
 * Tracks game events (kill, headshot, bomb, flash, MVP) and applies non-blocking
 * transient pulse overlays with attack, sustain, and smooth linear crossfade restoration.
 */
class OverlayManager {
public:
    OverlayManager();
    ~OverlayManager() = default;

    // Triggers an overlay effect for an event
    void TriggerOverlay(const std::string& event_name,
                        std::shared_ptr<Effect> effect,
                        uint64_t duration_ms = 1200,
                        uint64_t fade_out_ms = 400,
                        const std::string& blend_mode = "blend",
                        uint64_t attack_ms = 0);

    // Registers a declarative event binding
    void RegisterBinding(const OverlayBinding& binding);
    void ClearBindings();

    // Checks GSI state for event rising edges and triggers registered overlays
    void UpdateBindingsFromGsi(const GsiState* gsi, uint64_t current_ms, const std::string& foreground = "cs2.exe");

    // Renders active overlays on top of in_out_frame with zero heap allocation
    void ApplyOverlays(uint64_t current_ms,
                       FrameBuffer& in_out_frame,
                       const Keymap& keymap,
                       const IGsiReader* gsi = nullptr);

    // Clears all currently active overlays
    void ClearActiveOverlays();

    // Returns count of active overlays
    size_t GetActiveOverlayCount() const;

private:
    mutable std::mutex mutex_;
    std::vector<ActiveOverlay> active_overlays_;
    std::vector<OverlayBinding> bindings_;
    std::unordered_map<std::string, bool> prev_event_states_;
    std::unordered_map<std::string, double> prev_event_sequences_;
    bool in_game_{false};
    FrameBuffer temp_overlay_buf_; // Preallocated frame buffer to guarantee 0 heap allocation
};

} // namespace aura
