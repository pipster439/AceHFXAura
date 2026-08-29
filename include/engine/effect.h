#pragma once

#include "aura/aura_types.h"
#include "aura/keymap.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace aura {

class Effect {
public:
    virtual ~Effect() = default;
    virtual void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) = 0;
};

struct KeyOverride {
    std::string key_spec;
    ColorRGB color;
};

struct Profile {
    std::string name;
    std::shared_ptr<Effect> base_effect;
    std::vector<KeyOverride> key_overrides;

    void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) const {
        if (base_effect) {
            base_effect->Render(elapsed_ms, out_frame, keymap);
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
    }
};

} // namespace aura
