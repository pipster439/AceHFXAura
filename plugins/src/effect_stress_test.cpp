#include "engine/effect.h"
#include "engine/plugin_interface.h"
#include <atomic>

static std::atomic<int> g_instance_count{0};

class StressTestEffect : public aura::Effect {
public:
    StressTestEffect() {
        g_instance_count++;
    }
    ~StressTestEffect() override {
        g_instance_count--;
    }

    void Render(uint64_t elapsed_ms, aura::FrameBuffer& out_frame, const aura::Keymap& keymap) override {
        for (size_t i = 0; i < aura::FRAME_BUFFER_SIZE; i += 3) {
            out_frame.buffer[i]     = static_cast<uint8_t>((elapsed_ms + i) % 256);
            out_frame.buffer[i + 1] = 100;
            out_frame.buffer[i + 2] = 200;
        }
    }

    void RenderWithContext(const aura::EffectContext& ctx, aura::FrameBuffer& out_frame) override {
        double boost = 0.0;
        if (ctx.gsi) {
            boost = ctx.gsi->GetNumber("player.state.health", 100.0);
        }
        for (size_t i = 0; i < aura::FRAME_BUFFER_SIZE; i += 3) {
            out_frame.buffer[i]     = static_cast<uint8_t>((ctx.elapsed_ms + static_cast<uint64_t>(boost) + i) % 256);
            out_frame.buffer[i + 1] = 100;
            out_frame.buffer[i + 2] = 200;
        }
    }
};

AURA_PLUGIN_EXPORT uint32_t AuraGetPluginApiVersion() {
    return AURA_PLUGIN_API_VERSION;
}

AURA_PLUGIN_EXPORT const char* AuraGetEffectName() {
    return "stress_test";
}

AURA_PLUGIN_EXPORT aura::Effect* AuraCreateEffect() {
    return new StressTestEffect();
}

AURA_PLUGIN_EXPORT void AuraDestroyEffect(aura::Effect* effect) {
    delete effect;
}

