#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <filesystem>
#include <chrono>
#include <optional>

#include "engine/effect.h"
#include "engine/plugin_interface.h"
#include "engine/effect_program.h"
#include <unordered_set>

namespace aura {

enum class PluginAbiVersion { Unsupported, V1_0 };

// Only the two existing spellings (and historical absence) are supported.
PluginAbiVersion NormalizePluginApiVersion(std::optional<uint32_t> raw_version) noexcept;

struct EffectLifecycleExports {
    std::optional<uint32_t> version;
    bool has_finished_export{false};
    bool has_opacity_export{false};
    // Effective capabilities: unknown revision or opacity-only => both null.
    // Finished-only is usable with host opacity=1; no timing is executed here.
    PfnAuraIsEffectFinished is_finished{nullptr};
    PfnAuraGetEffectOpacity get_opacity{nullptr};
};

struct LogicalKeyboardExports {
    std::optional<uint32_t> version;
    PfnAuraBindLogicalKeyboard bind{nullptr};
};

/**
 * @brief Manages the lifecycle of a loaded DLL module and its temporary shadow file.
 * 
 * Safe teardown: FreeLibrary is only invoked when this handle's reference count drops to 0,
 * after all Effect instances and prepared sources referencing it have been released.
 */
class PluginHandle {
public:
    PluginHandle(HMODULE module, std::filesystem::path shadow_path, PfnAuraDestroyEffect destroy_fn)
        : module_(module), shadow_path_(std::move(shadow_path)), destroy_fn_(destroy_fn) {}

    ~PluginHandle() {
        if (module_) {
            FreeLibrary(module_);
            module_ = nullptr;
        }
        if (!shadow_path_.empty()) {
            std::error_code ec;
            std::filesystem::remove(shadow_path_, ec);
        }
    }

    HMODULE GetModule() const { return module_; }
    const std::filesystem::path& GetShadowPath() const { return shadow_path_; }
    PfnAuraDestroyEffect GetDestroyFn() const { return destroy_fn_; }

    PluginHandle(const PluginHandle&) = delete;
    PluginHandle& operator=(const PluginHandle&) = delete;

private:
    friend class PluginManager;
    HMODULE module_{nullptr};
    std::filesystem::path shadow_path_;
    PfnAuraDestroyEffect destroy_fn_{nullptr};
};

/**
 * @brief Metadata and function pointers for a loaded plugin.
 */
struct HallAnalogExports {
    std::optional<uint32_t> version;
    PfnAuraBindHallAnalog bind{nullptr};
};

struct PluginSummary {
    std::string effect_name;
    std::string kind; // "native_dll" or "program"
    uint64_t generation_id{0};
    std::filesystem::path original_path;
    bool has_lifecycle{false};
    bool has_logical_keyboard{false};
    bool has_hall_analog{false};
};

struct PluginEntry {
    uint64_t generation_id{0};
    std::string effect_name;
    std::filesystem::path original_path;
    uint32_t api_version{0}; // Raw export value, or historical default 1 if absent.
    bool version_export_present{false};
    PluginAbiVersion normalized_version{PluginAbiVersion::Unsupported};
    EffectLifecycleExports lifecycle;
    LogicalKeyboardExports logical_keyboard;
    HallAnalogExports hall_analog;
    std::shared_ptr<const EffectProgram> program; // restricted data tier; no DLL callbacks.
    bool HasLifecycle() const { return bool(program) || (lifecycle.version == 1 && lifecycle.is_finished); }
    std::shared_ptr<PluginHandle> handle;
    PfnAuraCreateEffect create_fn{nullptr};
    PfnAuraDestroyEffect destroy_fn{nullptr};
    PfnAuraGetEffectName name_fn{nullptr};
    PfnAuraGetPluginApiVersion version_fn{nullptr};
};

/** Host-only factory result. No trigger, timing, composition or rule execution.
 * Effect and immutable metadata are captured from the same DLL generation.
 * The Effect's deleter also owns that generation, so copying out the Effect is safe.
 * Consumers must never combine these callbacks with an object from another result.
 */
class TriggeredEffectInstance {
public:
    TriggeredEffectInstance() = default;
    explicit operator bool() const { return effect_ != nullptr; }
    const std::shared_ptr<Effect>& GetEffect() const { return effect_; }
    const std::shared_ptr<const PluginEntry>& GetGeneration() const { return generation_; }
    bool HasLifecycle() const;
    uint32_t Finished(uint64_t elapsed_ms) const;
    double Opacity(uint64_t elapsed_ms) const;
    const std::bitset<TOTAL_LEDS>* Coverage() const;
    bool Faulted() const;
    // Builtins have no DLL callbacks; only host-owned effects may use this path.
    static TriggeredEffectInstance FromHostEffect(std::shared_ptr<Effect> effect) {
        return TriggeredEffectInstance({}, std::move(effect));
    }

private:
    friend class PluginManager;
    TriggeredEffectInstance(std::shared_ptr<const PluginEntry> generation, std::shared_ptr<Effect> effect)
        : generation_(std::move(generation)), effect_(std::move(effect)) {}
    // Reverse member destruction order: object first, generation last.
    std::shared_ptr<const PluginEntry> generation_;
    std::shared_ptr<Effect> effect_;
};

/**
 * @brief Shadow-copy loading with transactional, mutex-protected generation publication.
 */
class PluginManager {
public:
    struct PreparedReload {
        std::shared_ptr<const PluginEntry> candidate, previous;
        std::string alias;
        bool previous_disabled{false};
        explicit operator bool() const { return candidate != nullptr; }
    };
    struct ProgramRemoval {
        std::shared_ptr<const PluginEntry> generation;
        std::vector<std::string> aliases;
        explicit operator bool() const { return generation && generation->program && !aliases.empty(); }
    };
    static PluginManager& Instance();

    PluginManager();
    ~PluginManager();

    // Loads a plugin DLL (using shadow copy into plugins/.cache to prevent LNK1104 locks)
    std::shared_ptr<Effect> LoadPlugin(const std::string& name_or_path);

    // Forces a reload of a plugin from disk
    bool ReloadPlugin(const std::string& effect_name);
    PreparedReload PrepareReload(const std::string& name, bool require_lifecycle = false);
    PreparedReload PrepareProgram(const std::string& name, std::shared_ptr<const EffectProgram> program,
                                  const std::filesystem::path& immutable_source);
    bool UnregisterProgram(const std::string& name, uint64_t expected_generation);
    ProgramRemoval PrepareProgramRemoval(const std::string& name, uint64_t expected_generation) const;
    bool UnregisterProgram(const ProgramRemoval& removal);
    bool RestoreProgram(const ProgramRemoval& removal);
    // Compensate only this exact publication receipt; refuse a newer generation.
    bool RollbackProgram(const PreparedReload& published);
    bool PublishReload(const PreparedReload& prepared);
    std::shared_ptr<const PluginEntry> GetGeneration(const std::string& name) const;

    // Creates an instance of an effect managed by a plugin
    std::shared_ptr<Effect> CreateEffect(const std::string& effect_name);

    // Generation-bound equivalents. Legacy entry points delegate to these.
    // Automation uses quiet=true and supplies bounded rule-level failure diagnostics.
    // Destruction exception containment/diagnostics remain owned by the deleter.
    TriggeredEffectInstance LoadPluginInstance(const std::string& name_or_path, bool quiet = false);
    TriggeredEffectInstance CreateEffectInstance(const std::string& effect_name, bool quiet = false);
    // Pin/load validated exports without invoking CreateEffect. Unpublished cold
    // generations are owned by the returned source, never an unbounded cache.
    std::shared_ptr<const PluginEntry> PrepareEffectGeneration(const std::string& name);
    static TriggeredEffectInstance CreateFromGeneration(std::shared_ptr<const PluginEntry> generation) {
        return Instantiate(std::move(generation), {}, true);
    }

    // Checks if a plugin with the given effect name is loaded
    bool HasPlugin(const std::string& effect_name) const;

    // Returns a list of loaded effect names
    std::vector<std::string> GetLoadedPluginNames() const;
    std::vector<PluginSummary> GetPluginSummaries() const;

    // Discovers and loads all plugins from plugins directory
    void LoadAllFromDirectory(const std::filesystem::path& dir = "plugins");

    // Clean up temporary shadow cache files
    void CleanupShadowCache(const std::filesystem::path& cache_dir = "plugins/.cache");

    // Resolves file path for an effect name: "foo" -> "plugins/effect_foo.dll"
    static std::filesystem::path ResolvePluginPath(const std::string& name_or_path, const std::filesystem::path& base_dir = "plugins");

private:
    std::shared_ptr<const PluginEntry> LoadPluginInternal(const std::filesystem::path& dll_path, bool quiet = false);
    static TriggeredEffectInstance Instantiate(std::shared_ptr<const PluginEntry> generation,
                                              std::shared_ptr<bool> destruction_failed = {}, bool quiet = false);
    std::shared_ptr<const PluginEntry> FindByPath(const std::filesystem::path& path) const;
    bool PublishGeneration(const std::shared_ptr<const PluginEntry>& candidate,
                           const std::string& requested_alias,
                           const std::shared_ptr<const PluginEntry>& expected_previous, bool quiet = false,
                           std::optional<bool> expected_disabled = std::nullopt);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<const PluginEntry>> plugins_;
    std::unordered_set<std::string> disabled_program_aliases_;
    std::unordered_map<std::string,uint64_t> disabled_program_generations_;
    std::filesystem::path plugins_dir_{"plugins"};
    std::filesystem::path cache_dir_{"plugins/.cache"};
};

} // namespace aura
