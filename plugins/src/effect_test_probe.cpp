
#include "engine/effect.h"
#include "engine/plugin_interface.h"
class MyTestEffect : public aura::Effect {
public:
    MyTestEffect() = default;
    void Render(uint64_t elapsed_ms, aura::FrameBuffer& out_frame, const aura::Keymap& keymap) override {
        out_frame.Fill(255, 128, 64);
    }
};
extern "C" {
    __declspec(dllexport) uint32_t AuraGetPluginApiVersion() { return 1; }
    __declspec(dllexport) const char* AuraGetEffectName() { return "my_test"; }
    __declspec(dllexport) aura::Effect* AuraCreateEffect() { return new MyTestEffect(); }
    __declspec(dllexport) void AuraDestroyEffect(aura::Effect* effect) { delete effect; }
}
