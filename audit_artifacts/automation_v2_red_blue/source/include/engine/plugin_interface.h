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

    /**
     * @brief 获取 CS2 GSI 文本字符串遥测。
     * 
     * @param field 遥测键名（如 "player.team", "round.phase"）。
     * @param def_val 键名不存在时的默认回退字符串。
     * @return const char* 指向以 null 结尾的 UTF-8 字符串指针。
     * 
     * @note 生命周期契约 (Lifetime Contract):
     * 返回的指针底层基于 thread_local 静态存储区，只保证有效到【同一线程下一次 GetString() 调用之前】。
     * 调用方/插件若需要跨调用或跨帧长期保存字符串内容，必须在下次调用前立即执行深拷贝 (如赋值给 std::string)。
     * （若未来演进至 API v2，可考虑 caller-owned buffer 方案；当前严格保持跨 DLL ABI 稳定不变）。
     */
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
  #define AURA_PLUGIN_CALL __cdecl
#else
  #define AURA_PLUGIN_EXPORT extern "C"
  #define AURA_PLUGIN_CALL
#endif

// Canonical Plugin API version
constexpr uint32_t AURA_PLUGIN_API_VERSION = 1;

// Optional C exports; independent of the unchanged Plugin ABI v1 class layouts.
// All callbacks are serial, nonblocking and must not throw across the DLL boundary.
constexpr uint32_t AURA_EFFECT_LIFECYCLE_VERSION = 1;

extern "C" {
    // Canonical Plugin C Export Function Prototypes
    typedef uint32_t (*PfnAuraGetPluginApiVersion)();
    typedef const char* (*PfnAuraGetEffectName)();
    typedef aura::Effect* (*PfnAuraCreateEffect)();
    typedef void (*PfnAuraDestroyEffect)(aura::Effect*);
    typedef void (*PfnAuraRenderEffectWithContext)(aura::Effect*, const aura::EffectContext*, aura::FrameBuffer*);

    typedef uint32_t (AURA_PLUGIN_CALL *PfnAuraGetEffectLifecycleVersion)();
    // Query the exact object returned by this generation's factory, after rendering
    // with the same elapsed_ms. Finished returns 0/1; opacity must be finite [0,1].
    typedef uint32_t (AURA_PLUGIN_CALL *PfnAuraIsEffectFinished)(const aura::Effect*, uint64_t elapsed_ms);
    typedef float (AURA_PLUGIN_CALL *PfnAuraGetEffectOpacity)(const aura::Effect*, uint64_t elapsed_ms);

    // Aliases without Aura prefix
    typedef uint32_t (*PfnGetPluginApiVersion)();
    typedef const char* (*PfnGetEffectName)();
    typedef aura::Effect* (*PfnCreateEffect)();
    typedef void (*PfnDestroyEffect)(aura::Effect*);
}
