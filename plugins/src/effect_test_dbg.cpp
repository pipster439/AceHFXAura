#include "engine/effect.h"
#include "engine/plugin_interface.h"

class TestEffect : public aura::Effect {
public:
    TestEffect() : aura::Effect("test") {}
    void Render(uint64_t, aura::FrameBuffer&, const aura::Keymap&) override {}
};

extern "C" {
    __declspec(dllexport) uint32_t AuraGetPluginApiVersion() { return 1; }
    __declspec(dllexport) const char* AuraGetEffectName() { return "test"; }
    __declspec(dllexport) aura::Effect* AuraCreateEffect() { return new TestEffect(); }
    __declspec(dllexport) void AuraDestroyEffect(aura::Effect* e) { delete e; }
}
