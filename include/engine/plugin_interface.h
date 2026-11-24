#pragma once

#include <cstdint>
#include <type_traits>
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
class AudioFeatureSource;
class ScreenColorSource;

struct EffectContext {
    uint64_t elapsed_ms{0};
    const Keymap& keymap;
    const IGsiReader* gsi{nullptr};
    const AudioFeatureSource* audio{nullptr};
    const ScreenColorSource* screen{nullptr};
    uint64_t monotonic_ms{0};

    EffectContext(uint64_t ms, const Keymap& km, const IGsiReader* reader = nullptr,
                  const AudioFeatureSource* audio_source = nullptr,
                  const ScreenColorSource* screen_source = nullptr, uint64_t monotonic_time_ms = 0)
        : elapsed_ms(ms), keymap(km), gsi(reader), audio(audio_source), screen(screen_source), monotonic_ms(monotonic_time_ms ? monotonic_time_ms : ms) {}
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

// Optional logical keyboard input extension. These POD records and C exports do
// not change Effect, EffectContext, FrameBuffer, or their ABI-v1 vtables/layouts.
// Names identify logical OS keys, never magnetic travel or physical provenance.
constexpr uint32_t AURA_LOGICAL_KEYBOARD_VERSION = 1;
constexpr uint32_t AURA_LOGICAL_KEYBOARD_MAX_KEYS = 128;
constexpr uint32_t AURA_LOGICAL_KEYBOARD_NAME_BYTES = 16;
struct AuraLogicalKeyStateV1 {
    char name[AURA_LOGICAL_KEYBOARD_NAME_BYTES]; // NUL-terminated; no positional mapping.
    uint32_t held;
    uint32_t has_release;
    uint64_t last_down_ms;
    uint64_t last_up_ms;
    uint64_t down_sequence;
    uint64_t up_sequence;
};
struct AuraLogicalKeyboardSnapshotV1 {
    uint32_t struct_size;
    uint32_t version;
    uint32_t source_available; // 0 => all key states unknown; do not render stale input.
    uint32_t count;
    uint64_t observed_at_ms; // host steady-clock milliseconds, same clock as Down/Up.
    uint64_t sequence;
    uint64_t source_epoch; // changes on availability transitions/reset; no event replay.
    AuraLogicalKeyStateV1 keys[AURA_LOGICAL_KEYBOARD_MAX_KEYS];
};
struct AuraLogicalKeyboardHostV1 {
    uint32_t struct_size;
    uint32_t version;
    void* context;
    // Caller owns output; pass sizeof(AuraLogicalKeyboardSnapshotV1). Read once
    // per Render, then reuse the immutable copy for all keys. 0 means unavailable
    // contract/output; 1 means a valid snapshot (source_available may still be 0).
    uint32_t (AURA_PLUGIN_CALL *read_snapshot)(void* context, AuraLogicalKeyboardSnapshotV1* output,
                                            uint32_t output_bytes);
};
static_assert(std::is_standard_layout_v<AuraLogicalKeyboardSnapshotV1> &&
              std::is_trivially_copyable_v<AuraLogicalKeyboardSnapshotV1>);
static_assert(std::is_standard_layout_v<AuraLogicalKeyboardHostV1> &&
              std::is_trivially_copyable_v<AuraLogicalKeyboardHostV1>);

// Optional Hall analog depth extension (read-only snapshot of 68-key travel).
// Independent of ABI-v1 Effect, EffectContext, FrameBuffer, and LogicalKeyboard.
constexpr uint32_t AURA_HALL_ANALOG_VERSION = 1;
constexpr uint32_t AURA_HALL_ANALOG_MAX_KEYS = 68;
struct AuraHallKeyStateV1 {
    uint16_t hid_usage;
    uint16_t logical_id;
    float    normalized;
    float    depth_mm;
    uint16_t raw;
    uint8_t  digital_down;
    uint8_t  reserved;
};
struct AuraHallAnalogSnapshotV1 {
    uint32_t struct_size;
    uint32_t version;
    uint32_t active;
    uint32_t count;
    uint64_t timestamp_us;
    uint32_t packet_count;
    uint32_t reserved;
    AuraHallKeyStateV1 keys[AURA_HALL_ANALOG_MAX_KEYS];
};
struct AuraHallAnalogHostV1 {
    uint32_t struct_size;
    uint32_t version;
    void* context;
    uint32_t (AURA_PLUGIN_CALL *read_snapshot)(void* context, AuraHallAnalogSnapshotV1* output,
                                               uint32_t output_bytes);
};
static_assert(std::is_standard_layout_v<AuraHallAnalogSnapshotV1> &&
              std::is_trivially_copyable_v<AuraHallAnalogSnapshotV1>);
static_assert(std::is_standard_layout_v<AuraHallAnalogHostV1> &&
              std::is_trivially_copyable_v<AuraHallAnalogHostV1>);

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

    typedef uint32_t (AURA_PLUGIN_CALL *PfnAuraGetLogicalKeyboardVersion)();
    // Both exports are mandatory when either is declared. Unknown versions,
    // missing exports, throws or return != 1 reject the candidate generation.
    // Bind receives the exact factory object, before any Render/lifecycle call.
    // Copy the table; its callback/context remain valid through DestroyEffect.
    // No retained asynchronous calls are permitted after DestroyEffect returns.
    typedef uint32_t (AURA_PLUGIN_CALL *PfnAuraBindLogicalKeyboard)(
        aura::Effect*, const AuraLogicalKeyboardHostV1*);

    typedef uint32_t (AURA_PLUGIN_CALL *PfnAuraGetHallAnalogVersion)();
    typedef uint32_t (AURA_PLUGIN_CALL *PfnAuraBindHallAnalog)(
        aura::Effect*, const AuraHallAnalogHostV1*);

    // Aliases without Aura prefix
    typedef uint32_t (*PfnGetPluginApiVersion)();
    typedef const char* (*PfnGetEffectName)();
    typedef aura::Effect* (*PfnCreateEffect)();
    typedef void (*PfnDestroyEffect)(aura::Effect*);
}
