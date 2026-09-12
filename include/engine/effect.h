#pragma once

#include "aura/aura_types.h"
#include "aura/keymap.h"
#include "engine/plugin_interface.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace aura {

class Effect {
public:
    Effect() = default;
    explicit Effect(const std::string& /*name*/) {}
    virtual ~Effect() = default;
    virtual void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) = 0;
    virtual void RenderWithContext(const EffectContext& ctx, FrameBuffer& out_frame) {
        Render(ctx.elapsed_ms, out_frame, ctx.keymap);
    }
};

struct KeyOverride {
    std::string key_spec;
    ColorRGB color;
};

struct Profile {
    std::string name;
    std::shared_ptr<Effect> base_effect;
    std::vector<KeyOverride> key_overrides;
    uint8_t brightness = 255;
    int fps = 25;

    void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap, const IGsiReader* gsi = nullptr) const {
        if (base_effect) {
            EffectContext ctx(elapsed_ms, keymap, gsi);
            base_effect->RenderWithContext(ctx, out_frame);
        } else {
            out_frame.Clear();
        }

        // Apply per-key overrides
        for (const auto& ko : key_overrides) {
            std::vector<int> led_ids;
            if (keymap.ResolveKeys(ko.key_spec, led_ids)) {
                for (int id : led_ids) {
                    out_frame.SetKey(id, ko.color);
                }
            }
        }

        // Apply uniform brightness scaling
        if (brightness == 0) {
            out_frame.Clear();
        } else if (brightness < 255) {
            for (size_t i = 0; i < FRAME_BUFFER_SIZE; ++i) {
                out_frame.buffer[i] = static_cast<uint8_t>(
                    (static_cast<uint32_t>(out_frame.buffer[i]) * brightness) / 255
                );
            }
        }
    }
};

} // namespace aura
