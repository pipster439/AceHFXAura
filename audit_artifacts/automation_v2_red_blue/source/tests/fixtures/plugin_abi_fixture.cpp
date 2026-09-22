// Synthetic regression fixture built during Stage 1; never historical evidence.
#include "engine/effect.h"
#include <windows.h>
#include <stdexcept>

#ifndef FIXTURE_NAME
#define FIXTURE_NAME "abi_fixture"
#endif
#ifndef FIXTURE_VALUE
#define FIXTURE_VALUE 17
#endif
#ifndef FIXTURE_ABI
#define FIXTURE_ABI 1
#endif
#ifndef FIXTURE_LIFECYCLE
#define FIXTURE_LIFECYCLE 1
#endif

namespace {
unsigned* destroyed = nullptr;
unsigned* unloaded = nullptr;
unsigned factory_calls = 0;
void record(const char* event) {
    char path[MAX_PATH];
    const DWORD length = GetEnvironmentVariableA("AURA_PLUGIN_FIXTURE_TRACE", path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return;
    HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, event, static_cast<DWORD>(strlen(event)), &written, nullptr);
    CloseHandle(file);
}
class Fixture final : public aura::Effect {
public:
    const unsigned identity = FIXTURE_VALUE;
    void Render(uint64_t, aura::FrameBuffer& frame, const aura::Keymap&) override {
        frame.Clear();
        frame.buffer[0] = FIXTURE_VALUE;
    }
};
}

#ifdef FIXTURE_UNPREFIXED
#define EXPORT_VERSION GetPluginApiVersion
#define EXPORT_NAME GetEffectName
#define EXPORT_CREATE CreateEffect
#define EXPORT_DESTROY DestroyEffect
#else
#define EXPORT_VERSION AuraGetPluginApiVersion
#define EXPORT_NAME AuraGetEffectName
#define EXPORT_CREATE AuraCreateEffect
#define EXPORT_DESTROY AuraDestroyEffect
#endif

#ifndef FIXTURE_NO_VERSION
AURA_PLUGIN_EXPORT uint32_t EXPORT_VERSION() {
    record("version\n");
#ifdef FIXTURE_VERSION_THROWS
    throw std::runtime_error("fixture version exception");
#else
    return FIXTURE_ABI;
#endif
}
#endif
AURA_PLUGIN_EXPORT const char* EXPORT_NAME() { record("name\n"); return FIXTURE_NAME; }
AURA_PLUGIN_EXPORT aura::Effect* EXPORT_CREATE() {
    record("create\n");
    ++factory_calls;
#ifdef FIXTURE_FACTORY_NULL
    return nullptr;
#elif defined(FIXTURE_FACTORY_THROWS)
    throw std::runtime_error("fixture factory exception");
#else
    return new Fixture;
#endif
}
#ifndef FIXTURE_NO_DESTROY
AURA_PLUGIN_EXPORT void EXPORT_DESTROY(aura::Effect* effect) {
    delete effect;
    if (destroyed) ++*destroyed;
#ifdef FIXTURE_DESTROY_THROWS
    throw std::runtime_error("fixture destroy exception after delete");
#endif
}
#endif

#ifndef FIXTURE_NO_LIFECYCLE_VERSION
AURA_PLUGIN_EXPORT uint32_t AURA_PLUGIN_CALL AuraGetEffectLifecycleVersion() {
    record("lifecycle\n");
    return FIXTURE_LIFECYCLE;
}
#endif
#ifndef FIXTURE_NO_FINISHED
AURA_PLUGIN_EXPORT uint32_t AURA_PLUGIN_CALL AuraIsEffectFinished(const aura::Effect* effect, uint64_t elapsed) {
    // Different generations must not be paired even though they share C++ layout.
    if (static_cast<const Fixture*>(effect)->identity != FIXTURE_VALUE) return 99;
    return elapsed >= FIXTURE_VALUE ? 1 : 0;
}
#endif
#ifndef FIXTURE_NO_OPACITY
AURA_PLUGIN_EXPORT float AURA_PLUGIN_CALL AuraGetEffectOpacity(const aura::Effect* effect, uint64_t) {
    if (static_cast<const Fixture*>(effect)->identity != FIXTURE_VALUE) return -1.0f;
    return static_cast<float>(FIXTURE_VALUE) / 100.0f;
}
#endif

AURA_PLUGIN_EXPORT void FixtureObserve(unsigned* destroy_counter, unsigned* unload_counter) {
    destroyed = destroy_counter;
    unloaded = unload_counter;
}
AURA_PLUGIN_EXPORT unsigned FixtureFactoryCalls() { return factory_calls; }
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH && unloaded) ++*unloaded;
    return TRUE;
}
