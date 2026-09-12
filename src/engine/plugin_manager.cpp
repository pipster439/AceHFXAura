#include "engine/plugin_manager.h"
#include "utils/logger.h"

#include <iostream>
#include <atomic>
#include <chrono>

namespace aura {

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
    std::lock_guard<std::mutex> lock(mutex_);
    plugins_.clear();
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

std::shared_ptr<PluginEntry> PluginManager::LoadPluginInternal(const std::filesystem::path& dll_path) {
    std::error_code ec;
    if (!std::filesystem::exists(dll_path, ec) || std::filesystem::is_directory(dll_path, ec)) {
        LOG_ERROR("[PluginManager] 无法找到插件 DLL 文件: " << dll_path.string());
        return nullptr;
    }

    std::filesystem::create_directories(cache_dir_, ec);

    // 1. 生成唯一影子副本路径，规避 Windows LNK1104 独占锁
    static std::atomic<uint64_t> s_counter{0};
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string stem = dll_path.stem().string();
    std::filesystem::path shadow_path = cache_dir_ / (stem + "_" + std::to_string(now_ms) + "_" + std::to_string(++s_counter) + ".dll");

    // 拷贝至影子路径
    std::filesystem::copy_file(dll_path, shadow_path, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        LOG_ERROR("[PluginManager] 拷贝影子文件失败: " << ec.message() << " -> " << shadow_path.string());
        return nullptr;
    }

    // 2. 加载影子副本
    HMODULE hMod = LoadLibraryW(shadow_path.c_str());
    if (!hMod) {
        DWORD err = GetLastError();
        LOG_ERROR("[PluginManager] LoadLibraryW 失败 (错误码: " << err << ") 路径: " << shadow_path.string());
        std::filesystem::remove(shadow_path, ec);
        return nullptr;
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
        LOG_ERROR("[PluginManager] 插件缺失必备工厂导出函数 (CreateEffect / DestroyEffect): " << dll_path.string());
        FreeLibrary(hMod);
        std::filesystem::remove(shadow_path, ec);
        return nullptr;
    }

    uint32_t ver = pfn_version ? pfn_version() : 1;
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

    auto handle = std::make_shared<PluginHandle>(hMod, shadow_path, pfn_destroy);
    auto entry = std::make_shared<PluginEntry>();
    entry->effect_name = effect_name;
    entry->original_path = dll_path;
    entry->api_version = ver;
    entry->handle = handle;
    entry->create_fn = pfn_create;
    entry->destroy_fn = pfn_destroy;
    entry->name_fn = pfn_name;
    entry->version_fn = pfn_version;

    LOG_INFO("[PluginManager] 成功加载插件: '" << effect_name << "' (ABI v" << ver << ") 来自 " << dll_path.string());
    return entry;
}

std::shared_ptr<Effect> PluginManager::LoadPlugin(const std::string& name_or_path) {
    std::filesystem::path path = ResolvePluginPath(name_or_path, plugins_dir_);
    auto entry = LoadPluginInternal(path);
    if (!entry) return nullptr;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        plugins_[entry->effect_name] = entry;
        std::string stem = path.stem().string();
        if (stem != entry->effect_name) {
            plugins_[stem] = entry;
        }
        if (stem.rfind("effect_", 0) == 0) {
            std::string short_stem = stem.substr(7);
            if (short_stem != entry->effect_name) {
                plugins_[short_stem] = entry;
            }
        }
    }

    return CreateEffect(entry->effect_name);
}

bool PluginManager::ReloadPlugin(const std::string& effect_name) {
    std::filesystem::path path;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = plugins_.find(effect_name);
        if (it != plugins_.end()) {
            path = it->second->original_path;
        }
    }
    if (path.empty()) {
        path = ResolvePluginPath(effect_name, plugins_dir_);
    }

    auto new_entry = LoadPluginInternal(path);
    if (!new_entry) {
        LOG_WARN("[PluginManager] 重载插件失败: " << effect_name);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        plugins_[new_entry->effect_name] = new_entry;
        plugins_[effect_name] = new_entry;
        std::string stem = path.stem().string();
        plugins_[stem] = new_entry;
        if (stem.rfind("effect_", 0) == 0) {
            plugins_[stem.substr(7)] = new_entry;
        }
    }

    LOG_INFO("[PluginManager] 插件已完成热重载并注册: " << effect_name);
    return true;
}

std::shared_ptr<Effect> PluginManager::CreateEffect(const std::string& effect_name) {
    std::shared_ptr<PluginEntry> entry;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = plugins_.find(effect_name);
        if (it != plugins_.end()) {
            entry = it->second;
        }
    }

    if (!entry) {
        // Attempt lazy load
        std::filesystem::path cand = ResolvePluginPath(effect_name, plugins_dir_);
        std::error_code ec;
        if (std::filesystem::exists(cand, ec)) {
            entry = LoadPluginInternal(cand);
            if (entry) {
                std::lock_guard<std::mutex> lock(mutex_);
                plugins_[entry->effect_name] = entry;
                plugins_[effect_name] = entry;
            }
        }
    }

    if (!entry || !entry->create_fn || !entry->destroy_fn) {
        return nullptr;
    }

    Effect* raw_effect = entry->create_fn();
    if (!raw_effect) return nullptr;

    // 内存安全与 DLL 句柄生命周期守护：deleter 持有 PluginHandle 强引用
    auto handle = entry->handle;
    auto destroy_fn = entry->destroy_fn;

    return std::shared_ptr<Effect>(raw_effect, [handle, destroy_fn](Effect* p) {
        if (p && destroy_fn) {
            destroy_fn(p);
        }
    });
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
