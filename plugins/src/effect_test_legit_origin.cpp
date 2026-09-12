
        #include "engine/effect.h"
        #include "engine/plugin_interface.h"

        class TestValidAdversarialEffect : public aura::Effect {
        public:
            TestValidAdversarialEffect() : aura::Effect("adv_valid_effect") {}
            void Render(uint64_t elapsed_ms, aura::FrameBuffer& out_frame, const aura::Keymap& keymap) override {
                out_frame.Fill(aura::ColorRGB(255, 128, 64));
            }
        };

        extern "C" {
            __declspec(dllexport) uint32_t AuraGetPluginApiVersion() { return 1; }
            __declspec(dllexport) const char* AuraGetEffectName() { return "adv_valid_effect"; }
            __declspec(dllexport) aura::Effect* AuraCreateEffect() { return new TestValidAdversarialEffect(); }
            __declspec(dllexport) void AuraDestroyEffect(aura::Effect* effect) { delete effect; }
        }
        