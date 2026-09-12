#pragma once

#include <cstdint>
#include <string>
#include "aura/aura_types.h"
#include "aura/keymap.h"

namespace aura {

class Effect;


/**
 * @brief Pure virtual interface for querying CS2 Game State Integration telemetry.
 * 
 * Provides decoupled, zero-link-dependency access to numeric, boolean, and text
 * game state fields across DLL boundaries via C++ virtual method table (vtable).
 */
class IGsiReader {
public:
    virtual ~IGsiReader() = default;

    virtual double GetNumber(const char* field, double def_val = 0.0) const = 0;
    virtual bool GetBool(const char* field, bool def_val = false) const = 0;
    virtual const char* GetString(const char* field, const char* def_val = "") const = 0;
    virtual bool IsActive() const { return true; }
};

/**
 * @brief Rendering context passed to modern context-aware custom effects.
 */
struct EffectContext {
    uint64_t elapsed_ms{0};
    const Keymap& keymap;
    const IGsiReader* gsi{nullptr};

    EffectContext(uint64_t ms, const Keymap& km, const IGsiReader* reader = nullptr)
        : elapsed_ms(ms), keymap(km), gsi(reader) {}
};

} // namespace aura

#ifdef _WIN32
  #define AURA_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
  #define AURA_PLUGIN_EXPORT extern "C"
#endif

// Canonical Plugin API version
constexpr uint32_t AURA_PLUGIN_API_VERSION = 1;

extern "C" {
    // Canonical Plugin C Export Function Prototypes
    typedef uint32_t (*PfnAuraGetPluginApiVersion)();
    typedef const char* (*PfnAuraGetEffectName)();
    typedef aura::Effect* (*PfnAuraCreateEffect)();
    typedef void (*PfnAuraDestroyEffect)(aura::Effect*);
    typedef void (*PfnAuraRenderEffectWithContext)(aura::Effect*, const aura::EffectContext*, aura::FrameBuffer*);

    // Aliases without Aura prefix
    typedef uint32_t (*PfnGetPluginApiVersion)();
    typedef const char* (*PfnGetEffectName)();
    typedef aura::Effect* (*PfnCreateEffect)();
    typedef void (*PfnDestroyEffect)(aura::Effect*);
}
