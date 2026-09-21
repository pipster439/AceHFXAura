#include "engine/plugin_manager.h"
#include "utils/logger.h"

#include <iostream>
#include <atomic>
#include <chrono>
#include <set>

namespace aura {

PluginAbiVersion NormalizePluginApiVersion(std::optional<uint32_t> raw_version) noexcept {
    if (!raw_version || *raw_version == 1 || *raw_version == 0x00010000) {
        return PluginAbiVersion::V1_0;
    }
    return PluginAbiVersion::Unsupported;
}

namespace {
std::filesystem::path PluginSourcePath(const std::filesystem::path& path) {
    // Freeze an absolute source identity so later CWD changes cannot redirect reload.
    return std::filesystem::weakly_canonical(std::filesystem::absolute(path));
}

bool SameSource(const std::filesystem::path& a, const std::filesystem::path& b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}
} // namespace

PluginManager& PluginManager::Instance() {
    static PluginManager instance;
    return instance;
}

PluginManager::PluginManager() {
    std::error_code ec;
    std::filesystem::create_directories(plugins_dir_, ec);
    std::filesystem::create_directories(cache_dir_, ec);
    CleanupShadowCache();
}

PluginManager::~PluginManager() {
    std::unordered_map<std::string, std::shared_ptr<const PluginEntry>> retired;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        retired.swap(plugins_);
    }
    // FreeLibrary may execute DLL teardown; do not hold the registry mutex there.
}

std::filesystem::path PluginManager::ResolvePluginPath(const std::string& name_or_path, const std::filesystem::path& base_dir) {
    std::filesystem::path p(name_or_path);
    std::error_code ec;

    if (std::filesystem::exists(p, ec) && !std::filesystem::is_directory(p, ec)) {
        return p;
    }

    if (p.extension() == ".dll") {
        std::filesystem::path in_base = base_dir / p;
        if (std::filesystem::exists(in_base, ec)) {
            return in_base;
        }
        return in_base;
    }

    // Try base_dir / ("effect_" + name_or_path + ".dll")
    std::filesystem::path cand1 = base_dir / ("effect_" + name_or_path + ".dll");
    if (std::filesystem::exists(cand1, ec)) {
        return cand1;
    }

    // Try base_dir / (name_or_path + ".dll")
    std::filesystem::path cand2 = base_dir / (name_or_path + ".dll");
    if (std::filesystem::exists(cand2, ec)) {
        return cand2;
    }

    // Fallback default
    return cand1;
}

std::shared_ptr<const PluginEntry> PluginManager::LoadPluginInternal(const std::filesystem::path& dll_path, bool quiet) {
    // Keep the module alive through catch handlers and destruction of any exception
    // object whose type_info/vtable/destructor may itself live inside the DLL.
    std::shared_ptr<PluginHandle> handle;
    try {
        std::error_code ec;
        if (!std::filesystem::exists(dll_path, ec) || std::filesystem::is_directory(dll_path, ec)) {
            if (!quiet) LOG_ERROR("[PluginManager] 无法找到插件 DLL 文件: " << dll_path.string());
            return nullptr;
        }

        std::filesystem::create_directories(cache_dir_, ec);

        // 1. 生成唯一影子副本路径，规避 Windows LNK1104 独占锁
        static std::atomic<uint64_t> s_counter{0};
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::string stem = dll_path.stem().string();
        const uint64_t generation_id = ++s_counter;
        std::filesystem::path shadow_path = std::filesystem::absolute(cache_dir_ /
            (stem + "_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(now_ms) + "_" +
             std::to_string(generation_id) + ".dll"));

        // 拷贝至影子路径
        std::filesystem::copy_file(dll_path, shadow_path, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            if (!quiet) LOG_ERROR("[PluginManager] 拷贝影子文件失败: " << ec.message() << " -> " << shadow_path.string());
            return nullptr;
        }

        // 2. 加载影子副本
        // LoadLibraryW may search DLL directories instead of resolving a relative
        // subpath against CWD after the ASUS HAL changes the process DLL directory.
        const auto absolute_shadow_path = std::filesystem::absolute(shadow_path);
        HMODULE hMod = LoadLibraryW(absolute_shadow_path.c_str());
        if (!hMod) {
            DWORD err = GetLastError();
            if (!quiet) LOG_ERROR("[PluginManager] LoadLibraryW 失败 (错误码: " << err << ") 路径: " << shadow_path.string());
            std::filesystem::remove(shadow_path, ec);
            return nullptr;
        }

        // Own the module before calling any plugin export. Rejection/throws clean up
        // the candidate only; the published generation is not touched.
        try {
            handle = std::make_shared<PluginHandle>(hMod, shadow_path, nullptr);
        } catch (...) {
            FreeLibrary(hMod);
            std::filesystem::remove(shadow_path, ec);
            throw;
        }

        // 3. 提取符号（兼顾 Aura 前缀与标准符号名）
        auto pfn_create = reinterpret_cast<PfnAuraCreateEffect>(GetProcAddress(hMod, "AuraCreateEffect"));
        if (!pfn_create) {
            pfn_create = reinterpret_cast<PfnAuraCreateEffect>(GetProcAddress(hMod, "CreateEffect"));
        }

        auto pfn_destroy = reinterpret_cast<PfnAuraDestroyEffect>(GetProcAddress(hMod, "AuraDestroyEffect"));
        if (!pfn_destroy) {
            pfn_destroy = reinterpret_cast<PfnAuraDestroyEffect>(GetProcAddress(hMod, "DestroyEffect"));
        }

        auto pfn_version = reinterpret_cast<PfnAuraGetPluginApiVersion>(GetProcAddress(hMod, "AuraGetPluginApiVersion"));
        if (!pfn_version) {
            pfn_version = reinterpret_cast<PfnAuraGetPluginApiVersion>(GetProcAddress(hMod, "GetPluginApiVersion"));
        }

        auto pfn_name = reinterpret_cast<PfnAuraGetEffectName>(GetProcAddress(hMod, "AuraGetEffectName"));
        if (!pfn_name) {
            pfn_name = reinterpret_cast<PfnAuraGetEffectName>(GetProcAddress(hMod, "GetEffectName"));
        }

        if (!pfn_create || !pfn_destroy) {
            if (!quiet) LOG_ERROR("[PluginManager] 插件缺失必备工厂导出函数 (CreateEffect / DestroyEffect): " << dll_path.string());
            return nullptr;
        }
        handle->destroy_fn_ = pfn_destroy;

        const std::optional<uint32_t> raw_version = pfn_version ? std::optional<uint32_t>(pfn_version()) : std::nullopt;
        const auto normalized = NormalizePluginApiVersion(raw_version);
        if (normalized == PluginAbiVersion::Unsupported) {
            if (!quiet) LOG_ERROR("[PluginManager] Unsupported plugin ABI raw=" << *raw_version << " in " << dll_path.string()
                      << "; accepted: absent, 1, 0x00010000 (v1.0). Previous generation retained.");
            return nullptr;
        }
        if (!raw_version) {
            if (!quiet) LOG_WARN("[PluginManager] Missing version export; using historical ABI v1.0 for " << dll_path.string());
        }

        // Version validation precedes name, optional lifecycle negotiation and factory calls.
        EffectLifecycleExports lifecycle;
        auto lifecycle_version = reinterpret_cast<PfnAuraGetEffectLifecycleVersion>(GetProcAddress(hMod, "AuraGetEffectLifecycleVersion"));
        auto finished = reinterpret_cast<PfnAuraIsEffectFinished>(GetProcAddress(hMod, "AuraIsEffectFinished"));
        auto opacity = reinterpret_cast<PfnAuraGetEffectOpacity>(GetProcAddress(hMod, "AuraGetEffectOpacity"));
        lifecycle.has_finished_export = finished != nullptr;
        lifecycle.has_opacity_export = opacity != nullptr;
        if (lifecycle_version) lifecycle.version = lifecycle_version();
        if (lifecycle.version == AURA_EFFECT_LIFECYCLE_VERSION && finished) {
            lifecycle.is_finished = finished;
            lifecycle.get_opacity = opacity;
        } else if (lifecycle_version || finished || opacity) {
            if (!quiet) LOG_WARN("[PluginManager] Unsupported/partial lifecycle capability in " << dll_path.string()
                     << "; legacy envelope required (no lifecycle callbacks enabled).");
        }

        std::string effect_name;
        if (pfn_name) {
            const char* name_str = pfn_name();
            if (name_str && *name_str) {
                effect_name = name_str;
            }
        }
        if (effect_name.empty()) {
            effect_name = stem;
            if (effect_name.rfind("effect_", 0) == 0) {
                effect_name = effect_name.substr(7);
            }
        }

        auto entry = std::make_shared<PluginEntry>();
        entry->generation_id = generation_id;
        entry->effect_name = effect_name;
        entry->original_path = dll_path;
        entry->api_version = raw_version.value_or(1);
        entry->version_export_present = raw_version.has_value();
        entry->normalized_version = normalized;
        entry->lifecycle = lifecycle;
        entry->handle = handle;
        entry->create_fn = pfn_create;
        entry->destroy_fn = pfn_destroy;
        entry->name_fn = pfn_name;
        entry->version_fn = pfn_version;

        if (!quiet) LOG_INFO("[PluginManager] Prepared '" << effect_name << "' generation " << generation_id
                 << " (raw ABI " << (raw_version ? std::to_string(*raw_version) : "absent")
                 << " -> v1.0) from " << dll_path.string());
        return entry;
    } catch (const std::exception& error) {
        if (!quiet) LOG_ERROR("[PluginManager] Candidate load failed: " << dll_path.string() << ": " << error.what());
    } catch (...) {
        if (!quiet) LOG_ERROR("[PluginManager] Candidate export threw: " << dll_path.string());
    }
    return nullptr;
}

std::shared_ptr<const PluginEntry> PluginManager::FindByPath(const std::filesystem::path& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& item : plugins_) {
        if (SameSource(item.second->original_path, path)) return item.second;
    }
    return nullptr;
}

bool PluginManager::PublishGeneration(const std::shared_ptr<const PluginEntry>& candidate,
                                     const std::string& requested_alias,
                                     const std::shared_ptr<const PluginEntry>& expected_previous, bool quiet) {
    // Preflight the complete alias set, then swap a prepared registry. A collision,
    // concurrent publication or allocation failure cannot leave half-updated aliases.
    std::unordered_map<std::string, std::shared_ptr<const PluginEntry>> next;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::set<std::string> aliases{candidate->effect_name, candidate->original_path.stem().string(), requested_alias};
        const auto stem = candidate->original_path.stem().string();
        if (stem.rfind("effect_", 0) == 0) aliases.insert(stem.substr(7));
        std::shared_ptr<const PluginEntry> current;
        for (const auto& item : plugins_) {
            if (SameSource(item.second->original_path, candidate->original_path)) {
                current = item.second;
                aliases.insert(item.first);
            }
        }
        if (current != expected_previous) {
            if (!quiet) LOG_WARN("[PluginManager] Concurrent generation publication for " << candidate->original_path.string()
                     << "; rejecting stale candidate.");
            return false;
        }
        for (const auto& alias : aliases) {
            auto found = plugins_.find(alias);
            if (found != plugins_.end() && !SameSource(found->second->original_path, candidate->original_path)) {
                if (!quiet) LOG_ERROR("[PluginManager] Plugin alias collision '" << alias << "': "
                          << found->second->original_path.string() << " vs " << candidate->original_path.string()
                          << "; candidate rejected, existing aliases retained.");
                return false;
            }
        }
        if (current && current->effect_name != candidate->effect_name) {
            if (!quiet) LOG_WARN("[PluginManager] Export name changed '" << current->effect_name << "' -> '"
                     << candidate->effect_name << "'; preserving prior aliases for this source only.");
        }
        next = plugins_;
        for (const auto& alias : aliases) if (!alias.empty()) next[alias] = candidate;
        plugins_.swap(next);
    }
    // Old generations can unload here, outside the registry mutex.
    return true;
}

TriggeredEffectInstance PluginManager::Instantiate(std::shared_ptr<const PluginEntry> generation,
                                                   std::shared_ptr<bool> destruction_failed, bool quiet) {
    if (!generation || !generation->create_fn || !generation->destroy_fn) return {};
    try {
        Effect* raw = generation->create_fn();
        if (!raw) {
            if (!quiet) LOG_ERROR("[PluginManager] Factory returned null for " << generation->effect_name);
            return {};
        }
        auto effect = std::shared_ptr<Effect>(raw, [owner = generation, destruction_failed](Effect* p) mutable noexcept {
            try {
                owner->destroy_fn(p);
            } catch (...) {
                if (destruction_failed) *destruction_failed = true;
                // A shared_ptr deleter must never propagate, including when
                // formatting or writing the diagnostic itself fails.
                try {
                    LOG_ERROR("[PluginManager] Destroy export threw for " << owner->effect_name);
                } catch (...) {
                }
            }
            // Release after DestroyEffect returns, even if weak_ptr<Effect> survives.
            owner.reset();
        });
        return TriggeredEffectInstance(std::move(generation), std::move(effect));
    } catch (const std::exception& error) {
        if (!quiet) LOG_ERROR("[PluginManager] Effect construction failed: " << error.what());
    } catch (...) {
        if (!quiet) LOG_ERROR("[PluginManager] Effect factory threw.");
    }
    return {};
}

TriggeredEffectInstance PluginManager::LoadPluginInstance(const std::string& name_or_path, bool quiet) {
    try {
        const auto path = PluginSourcePath(ResolvePluginPath(name_or_path, plugins_dir_));
        const auto previous = FindByPath(path);
        auto entry = LoadPluginInternal(path, quiet);
        auto instance = Instantiate(entry, {}, quiet);
        if (!instance || !PublishGeneration(entry, name_or_path, previous, quiet)) return {};
        return instance;
    } catch (const std::exception& error) {
        if (!quiet) LOG_ERROR("[PluginManager] Plugin publication failed: " << error.what());
    }
    return {};
}

std::shared_ptr<Effect> PluginManager::LoadPlugin(const std::string& name_or_path) {
    return LoadPluginInstance(name_or_path).GetEffect();
}

bool PluginManager::ReloadPlugin(const std::string& effect_name) {
    try {
        std::filesystem::path path;
        std::shared_ptr<const PluginEntry> previous;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = plugins_.find(effect_name);
            if (it != plugins_.end()) {
                previous = it->second;
                path = previous->original_path;
            }
        }
        if (path.empty()) {
            path = PluginSourcePath(ResolvePluginPath(effect_name, plugins_dir_));
            previous = FindByPath(path);
        }
        auto entry = LoadPluginInternal(path);
        // Validate one factory/destructor roundtrip before publishing a reload.
        // No Render or lifecycle animation callback is executed by this substrate.
        auto destruction_failed = std::make_shared<bool>(false);
        {
            auto probe = Instantiate(entry, destruction_failed);
            if (!probe) return false;
        }
        if (*destruction_failed) return false;
        if (!PublishGeneration(entry, effect_name, previous)) return false;
        LOG_INFO("[PluginManager] Published reload '" << effect_name << "' generation " << entry->generation_id);
        return true;
    } catch (const std::exception& error) {
        LOG_ERROR("[PluginManager] Reload failed; previous generation retained: " << error.what());
    }
    return false;
}

TriggeredEffectInstance PluginManager::CreateEffectInstance(const std::string& effect_name, bool quiet) {
    std::shared_ptr<const PluginEntry> entry;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = plugins_.find(effect_name);
        if (it != plugins_.end()) {
            entry = it->second;
        }
    }

    return entry ? Instantiate(std::move(entry), {}, quiet) : LoadPluginInstance(effect_name, quiet);
}

std::shared_ptr<Effect> PluginManager::CreateEffect(const std::string& effect_name) {
    return CreateEffectInstance(effect_name).GetEffect();
}

bool PluginManager::HasPlugin(const std::string& effect_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return plugins_.find(effect_name) != plugins_.end();
}

std::vector<std::string> PluginManager::GetLoadedPluginNames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    names.reserve(plugins_.size());
    for (const auto& [k, v] : plugins_) {
        names.push_back(k);
    }
    return names;
}

void PluginManager::LoadAllFromDirectory(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (entry.is_regular_file(ec) && entry.path().extension() == ".dll") {
            LoadPlugin(entry.path().string());
        }
    }
}

void PluginManager::CleanupShadowCache(const std::filesystem::path& cache_dir) {
    std::error_code ec;
    if (!std::filesystem::exists(cache_dir, ec)) return;

    for (const auto& entry : std::filesystem::directory_iterator(cache_dir, ec)) {
        if (ec) break;
        if (entry.is_regular_file(ec) && entry.path().extension() == ".dll") {
            // Silently ignore files still locked by active processes
            std::filesystem::remove(entry.path(), ec);
        }
    }
}

} // namespace aura
