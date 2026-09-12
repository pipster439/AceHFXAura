
#include "engine/effect.h"
#include "engine/plugin_interface.h"

// Simulate Effect having explicit Effect(const std::string&) and FrameBuffer having Fill(ColorRGB)
namespace aura_test {
    inline void FillColor(aura::FrameBuffer& fb, const aura::ColorRGB& c) {
        fb.Fill(c.r, c.g, c.b);
    }
}

class TestValidAdversarialEffect : public aura::Effect {
public:
    TestValidAdversarialEffect() = default;
    void Render(uint64_t elapsed_ms, aura::FrameBuffer& out_frame, const aura::Keymap& keymap) override {
        out_frame.Fill(255, 128, 64);
    }
};

extern "C" {
    __declspec(dllexport) uint32_t AuraGetPluginApiVersion() { return 1; }
    __declspec(dllexport) const char* AuraGetEffectName() { return "adv_valid_effect"; }
    __declspec(dllexport) aura::Effect* AuraCreateEffect() { return new TestValidAdversarialEffect(); }
    __declspec(dllexport) void AuraDestroyEffect(aura::Effect* effect) { delete effect; }
}
