#include "engine/effect.h"
#include "engine/plugin_interface.h"

class TestEffect : public aura::Effect {
public:
    void Render(uint64_t ms, aura::FrameBuffer& fb, const aura::Keymap& km) override {
        fb.Fill(aura::ColorRGB(255, 0, 0));
    }
};

extern "C" {
    __declspec(dllexport) uint32_t AuraGetPluginApiVersion() { return 1; }
    __declspec(dllexport) const char* AuraGetEffectName() { return "test_valid"; }
    __declspec(dllexport) aura::Effect* AuraCreateEffect() { return new TestEffect(); }
    __declspec(dllexport) void AuraDestroyEffect(aura::Effect* e) { delete e; }
}
