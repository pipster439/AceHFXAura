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

#include "engine/effect.h"
#include "engine/plugin_interface.h"

namespace aura {

/**
 * @brief Manages the lifecycle of a loaded DLL module and its temporary shadow file.
 * 
 * Safe teardown: FreeLibrary is only invoked when this handle's reference count drops to 0,
 * which happens only after all std::shared_ptr<Effect> instances referencing it have been destroyed.
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
    HMODULE module_{nullptr};
    std::filesystem::path shadow_path_;
    PfnAuraDestroyEffect destroy_fn_{nullptr};
};

/**
 * @brief Metadata and function pointers for a loaded plugin.
 */
struct PluginEntry {
    std::string effect_name;
    std::filesystem::path original_path;
    uint32_t api_version{0};
    std::shared_ptr<PluginHandle> handle;
    PfnAuraCreateEffect create_fn{nullptr};
    PfnAuraDestroyEffect destroy_fn{nullptr};
    PfnAuraGetEffectName name_fn{nullptr};
    PfnAuraGetPluginApiVersion version_fn{nullptr};
};

/**
 * @brief Dynamic Plugin Manager supporting shadow-copy loading and lock-free hot swapping.
 */
class PluginManager {
public:
    static PluginManager& Instance();

    PluginManager();
    ~PluginManager();

    // Loads a plugin DLL (using shadow copy into plugins/.cache to prevent LNK1104 locks)
    std::shared_ptr<Effect> LoadPlugin(const std::string& name_or_path);

    // Forces a reload of a plugin from disk
    bool ReloadPlugin(const std::string& effect_name);

    // Creates an instance of an effect managed by a plugin
    std::shared_ptr<Effect> CreateEffect(const std::string& effect_name);

    // Checks if a plugin with the given effect name is loaded
    bool HasPlugin(const std::string& effect_name) const;

    // Returns a list of loaded effect names
    std::vector<std::string> GetLoadedPluginNames() const;

    // Discovers and loads all plugins from plugins directory
    void LoadAllFromDirectory(const std::filesystem::path& dir = "plugins");

    // Clean up temporary shadow cache files
    void CleanupShadowCache(const std::filesystem::path& cache_dir = "plugins/.cache");

    // Resolves file path for an effect name: "foo" -> "plugins/effect_foo.dll"
    static std::filesystem::path ResolvePluginPath(const std::string& name_or_path, const std::filesystem::path& base_dir = "plugins");

private:
    std::shared_ptr<PluginEntry> LoadPluginInternal(const std::filesystem::path& dll_path);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<PluginEntry>> plugins_;
    std::filesystem::path plugins_dir_{"plugins"};
    std::filesystem::path cache_dir_{"plugins/.cache"};
};

} // namespace aura
