// Synthetic Stage 3 DLL. Not historical ABI evidence or a release payload.
#include "engine/effect.h"
#include <stdexcept>
#include <limits>
#ifndef LIFECYCLE_MARKER
#define LIFECYCLE_MARKER 200
#endif
namespace {
int mode = 0;
uint64_t finish_at = 100;
float opacity = 0.5f;
unsigned created = 0, destroyed = 0, renders = 0, finished_calls = 0, opacity_calls = 0;
void Trace(const char* text) {
    char path[MAX_PATH]; const auto length=GetEnvironmentVariableA("AURA_LIFECYCLE_FIXTURE_TRACE",path,MAX_PATH);
    if (!length || length>=MAX_PATH) return;
    HANDLE file=CreateFileA(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return;
    DWORD written; WriteFile(file,text,static_cast<DWORD>(strlen(text)),&written,nullptr); CloseHandle(file);
}
class Fixture final : public aura::Effect {
public:
    const int marker = LIFECYCLE_MARKER, behavior = mode;
    const uint64_t completion = finish_at;
    const float weight = opacity;
    uint64_t last_render = UINT64_MAX;
    mutable uint64_t last_finished = UINT64_MAX;
    unsigned ticks = 0;
    void Render(uint64_t elapsed, aura::FrameBuffer& out, const aura::Keymap&) override {
        ++renders; ++ticks; last_render = elapsed;
        Trace("render\n");
        out.buffer[0] = marker; out.buffer[1] = marker; out.buffer[2] = marker;
        if (behavior == 1) throw std::runtime_error("render failure after buffer write");
    }
};
}
AURA_PLUGIN_EXPORT uint32_t AuraGetPluginApiVersion() { return 1; }
AURA_PLUGIN_EXPORT const char* AuraGetEffectName() { return "automation_lifecycle"; }
AURA_PLUGIN_EXPORT aura::Effect* AuraCreateEffect() {
    if (mode == 11) return nullptr;
    if (mode == 12) throw std::runtime_error("factory failure");
    ++created; return new Fixture;
}
AURA_PLUGIN_EXPORT void AuraDestroyEffect(aura::Effect* effect) {
    const int behavior = static_cast<Fixture*>(effect)->behavior;
    delete effect; ++destroyed;
    if (behavior == 10) throw std::runtime_error("destroy failure after delete");
}
AURA_PLUGIN_EXPORT uint32_t AURA_PLUGIN_CALL AuraGetEffectLifecycleVersion() { return 1; }
AURA_PLUGIN_EXPORT uint32_t AURA_PLUGIN_CALL AuraIsEffectFinished(const aura::Effect* effect, uint64_t elapsed) {
    ++finished_calls;
    const auto& instance = *static_cast<const Fixture*>(effect);
    if (instance.behavior == 2) throw std::runtime_error("finished failure");
    if (instance.marker != LIFECYCLE_MARKER || instance.last_render != elapsed || instance.behavior == 4) return 99;
    instance.last_finished = elapsed;
    if (elapsed >= instance.completion) Trace("finished\n");
    return elapsed >= instance.completion ? 1 : 0;
}
AURA_PLUGIN_EXPORT float AURA_PLUGIN_CALL AuraGetEffectOpacity(const aura::Effect* effect, uint64_t elapsed) {
    ++opacity_calls;
    const auto& instance = *static_cast<const Fixture*>(effect);
    if (instance.behavior == 3 || instance.last_finished != elapsed) throw std::runtime_error("opacity/order failure");
    if (instance.behavior == 5) return std::numeric_limits<float>::quiet_NaN();
    if (instance.behavior == 6) return std::numeric_limits<float>::infinity();
    if (instance.behavior == 7) return instance.ticks == 1 ? 0.f : 1.f;
    return instance.weight;
}
AURA_PLUGIN_EXPORT void FixtureConfigure(int behavior, uint64_t end, float weight) { mode=behavior; finish_at=end; opacity=weight; }
AURA_PLUGIN_EXPORT unsigned FixtureCount(unsigned what) {
    switch (what) { case 0: return created; case 1: return destroyed; case 2: return renders; case 3: return finished_calls; default: return opacity_calls; }
}
