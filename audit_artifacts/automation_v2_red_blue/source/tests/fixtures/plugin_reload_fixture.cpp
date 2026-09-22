#include "engine/effect.h"
#include "engine/plugin_interface.h"

class ReloadFixture : public aura::Effect {
public:
    void Render(uint64_t, aura::FrameBuffer& frame, const aura::Keymap&) override {
        frame.Clear();
        frame.buffer[0] = FIXTURE_VALUE;
    }
};

AURA_PLUGIN_EXPORT uint32_t AuraGetPluginApiVersion() { return AURA_PLUGIN_API_VERSION; }
AURA_PLUGIN_EXPORT const char* AuraGetEffectName() { return "reload_fixture"; }
AURA_PLUGIN_EXPORT aura::Effect* AuraCreateEffect() { return new ReloadFixture; }
AURA_PLUGIN_EXPORT void AuraDestroyEffect(aura::Effect* effect) { delete effect; }
