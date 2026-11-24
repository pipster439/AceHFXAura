// Synthetic native DLL: no hooks, device I/O, or private configuration.
#include "engine/effect.h"
#include <cstring>
#include <stdexcept>

#ifndef KEYBOARD_FIXTURE_VERSION
#define KEYBOARD_FIXTURE_VERSION 1
#endif
#ifndef KEYBOARD_FIXTURE_VALUE
#define KEYBOARD_FIXTURE_VALUE 17
#endif

namespace {
unsigned factories = 0, destroyed = 0, reads = 0;
aura::Effect* last_created = nullptr;
class KeyboardFixture final : public aura::Effect {
public:
    AuraLogicalKeyboardHostV1 host{};
    void Render(uint64_t, aura::FrameBuffer& frame, const aura::Keymap&) override {
        frame.Clear();
        AuraLogicalKeyboardSnapshotV1 snapshot{};
        if (!host.read_snapshot || host.read_snapshot(host.context, &snapshot, sizeof(snapshot)) != 1) return;
        ++reads;
        frame.buffer[2] = KEYBOARD_FIXTURE_VALUE;
        if (!snapshot.source_available) return;
        frame.buffer[1] = 1;
        for (uint32_t i = 0; i < snapshot.count; ++i) {
            const auto& key = snapshot.keys[i];
            if (std::strcmp(key.name, "W") == 0) {
                frame.buffer[0] = key.held ? 200 : key.has_release ? 80 : 0;
                if (key.has_release && key.last_up_ms <= snapshot.observed_at_ms && key.up_sequence > key.down_sequence)
                    frame.buffer[3] = 1;
            }
        }
    }
};
}

AURA_PLUGIN_EXPORT uint32_t AuraGetPluginApiVersion() { return AURA_PLUGIN_API_VERSION; }
AURA_PLUGIN_EXPORT const char* AuraGetEffectName() { return "keyboard_fixture"; }
AURA_PLUGIN_EXPORT aura::Effect* AuraCreateEffect() { ++factories; return last_created = new KeyboardFixture; }
AURA_PLUGIN_EXPORT void AuraDestroyEffect(aura::Effect* effect) { ++destroyed; delete effect; }
AURA_PLUGIN_EXPORT uint32_t AURA_PLUGIN_CALL AuraGetEffectLifecycleVersion() { return 1; }
AURA_PLUGIN_EXPORT uint32_t AURA_PLUGIN_CALL AuraIsEffectFinished(const aura::Effect* effect, uint64_t) {
    const auto* instance = dynamic_cast<const KeyboardFixture*>(effect);
    return instance && instance->host.read_snapshot ? 0u : 2u;
}
#ifndef KEYBOARD_FIXTURE_NO_VERSION
AURA_PLUGIN_EXPORT uint32_t AURA_PLUGIN_CALL AuraGetLogicalKeyboardVersion() {
#ifdef KEYBOARD_FIXTURE_VERSION_THROW
    throw std::runtime_error("synthetic keyboard version failure");
#else
    return KEYBOARD_FIXTURE_VERSION;
#endif
}
#endif
#ifndef KEYBOARD_FIXTURE_NO_BIND
AURA_PLUGIN_EXPORT uint32_t AURA_PLUGIN_CALL AuraBindLogicalKeyboard(aura::Effect* effect, const AuraLogicalKeyboardHostV1* host) {
#ifdef KEYBOARD_FIXTURE_BIND_THROW
    (void)effect; (void)host;
    throw std::runtime_error("synthetic keyboard binder failure");
#elif defined(KEYBOARD_FIXTURE_BIND_REJECT)
    (void)effect; (void)host;
    return 0;
#else
    if (effect != last_created || !host || host->struct_size != sizeof(*host) ||
        host->version != AURA_LOGICAL_KEYBOARD_VERSION || !host->read_snapshot) return 0;
    AuraLogicalKeyboardSnapshotV1 sample{};
    if (host->read_snapshot(host->context, nullptr, sizeof(sample)) != 0 ||
        host->read_snapshot(host->context, &sample, sizeof(sample) - 1) != 0) return 0;
    static_cast<KeyboardFixture*>(effect)->host = *host;
    return 1;
#endif
}
#endif
AURA_PLUGIN_EXPORT void KeyboardFixtureCounts(unsigned* create_count, unsigned* destroy_count, unsigned* read_count) {
    *create_count = factories; *destroy_count = destroyed; *read_count = reads;
}
