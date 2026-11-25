#include "engine/plugin_manager.h"
#include "utils/logger.h"
#include "monitor/key_input_hub.h"

#include <iostream>
#include <atomic>
#include <chrono>
#include <set>
#include <fstream>

namespace aura {

PluginAbiVersion NormalizePluginApiVersion(std::optional<uint32_t> raw_version) noexcept {
    if (!raw_version || *raw_version == 1 || *raw_version == 0x00010000) {
        return PluginAbiVersion::V1_0;
    }
    return PluginAbiVersion::Unsupported;
}

namespace {
std::atomic<uint64_t> generation_sequence{0};
bool LogicalProgramName(const std::string& name) {
    return !name.empty() && name.size() <= 64 && std::all_of(name.begin(),name.end(),[](unsigned char c){
        return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';});
}
std::filesystem::path PluginSourcePath(const std::filesystem::path& path) {
    // Freeze an absolute source identity so later CWD changes cannot redirect reload.
    return std::filesystem::weakly_canonical(std::filesystem::absolute(path));
}

bool SameSource(const std::filesystem::path& a, const std::filesystem::path& b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

uint32_t AURA_PLUGIN_CALL ReadLogicalKeyboard(void*, AuraLogicalKeyboardSnapshotV1* output, uint32_t bytes) {
    if (!output || bytes != sizeof(AuraLogicalKeyboardSnapshotV1)) return 0;
    try { KeyInputHub::Instance().ReadLogicalSnapshot(*output); return 1; }
    catch (...) { *output = {}; return 0; }
}
// Process-lifetime table/context; no DLL generation or temporary factory stack
// address escapes. Effects own their DLL generation through the existing deleter.
const AuraLogicalKeyboardHostV1 logical_keyboard_host{
    sizeof(AuraLogicalKeyboardHostV1), AURA_LOGICAL_KEYBOARD_VERSION, nullptr, &ReadLogicalKeyboard};

struct HallShmMemoryV1 {
    uint32_t magic;
    uint32_t version;
    std::atomic<uint32_t> seq;
    uint32_t active;
    uint64_t timestamp_us;
    uint32_t packet_count;
    uint32_t key_count;
    uint16_t vid;
    uint16_t pid;
    uint32_t reserved;
    AuraHallKeyStateV1 keys[AURA_HALL_ANALOG_MAX_KEYS];
};
static_assert(sizeof(HallShmMemoryV1) == 40 + AURA_HALL_ANALOG_MAX_KEYS * 16);

uint32_t AURA_PLUGIN_CALL ReadHallAnalog(void*, AuraHallAnalogSnapshotV1* output, uint32_t bytes) {
    if (!output || bytes != sizeof(AuraHallAnalogSnapshotV1)) return 0;
    *output = {};
    output->struct_size = sizeof(AuraHallAnalogSnapshotV1);
    output->version = AURA_HALL_ANALOG_VERSION;

    HANDLE h_map = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Local\\AuraM605HallAnalogV1");
    if (!h_map) return 1; // Valid call, Hall telemetry not active yet
    void* view = MapViewOfFile(h_map, FILE_MAP_READ, 0, 0, sizeof(HallShmMemoryV1));
    if (!view) {
        CloseHandle(h_map);
        return 1;
    }

    const auto* shm = static_cast<const HallShmMemoryV1*>(view);
    for (int attempt = 0; attempt < 8; ++attempt) {
        const uint32_t s1 = shm->seq.load(std::memory_order_acquire);
        if (s1 & 1u) continue;
        std::atomic_thread_fence(std::memory_order_acquire);
        const uint32_t magic = shm->magic;
        const uint32_t version = shm->version;
        const uint32_t active = shm->active;
        const uint64_t ts = shm->timestamp_us;
        const uint32_t pkts = shm->packet_count;
        const uint32_t count = std::min<uint32_t>(shm->key_count, AURA_HALL_ANALOG_MAX_KEYS);
        AuraHallKeyStateV1 local_keys[AURA_HALL_ANALOG_MAX_KEYS]{};
        for (uint32_t i = 0; i < count; ++i) {
            local_keys[i] = shm->keys[i];
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        const uint32_t s2 = shm->seq.load(std::memory_order_acquire);
        if (s1 == s2 && magic == 0x414D3635u && version == 1u) {
            output->active = active;
            output->count = count;
            output->timestamp_us = ts;
            output->packet_count = pkts;
            for (uint32_t i = 0; i < count; ++i) {
                output->keys[i] = local_keys[i];
            }
            break;
        }
    }

    UnmapViewOfFile(view);
    CloseHandle(h_map);
    return 1;
}

const AuraHallAnalogHostV1 hall_analog_host{
    sizeof(AuraHallAnalogHostV1), AURA_HALL_ANALOG_VERSION, nullptr, &ReadHallAnalog};
} // namespace

bool TriggeredEffectInstance::HasLifecycle() const { return generation_ && generation_->HasLifecycle(); }
uint32_t TriggeredEffectInstance::Finished(uint64_t elapsed) const {
    if(generation_&&generation_->program) return static_cast<ProgramEffect*>(effect_.get())->Finished()?1u:0u;
    return HasLifecycle()?generation_->lifecycle.is_finished(effect_.get(),elapsed):0u;
}
double TriggeredEffectInstance::Opacity(uint64_t elapsed) const {
    if(generation_&&generation_->program)return static_cast<ProgramEffect*>(effect_.get())->Opacity();
    return HasLifecycle()&&generation_->lifecycle.get_opacity?generation_->lifecycle.get_opacity(effect_.get(),elapsed):1.0;
}
const std::bitset<TOTAL_LEDS>* TriggeredEffectInstance::Coverage() const {
    return generation_&&generation_->program?&static_cast<ProgramEffect*>(effect_.get())->Coverage():nullptr;
}
bool TriggeredEffectInstance::Faulted() const {
    return generation_&&generation_->program&&static_cast<ProgramEffect*>(effect_.get())->Status().faulted;
}

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

    if (p.extension() == ".dll" || p.extension() == ".aurair") {
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

    for(const auto& candidate : {base_dir / ("effect_" + name_or_path + ".aurair"), base_dir / (name_or_path + ".aurair")})
        if(std::filesystem::is_regular_file(candidate,ec))return candidate;

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

        if(dll_path.extension()==".aurair") {
            const auto size=std::filesystem::file_size(dll_path,ec);
            if(ec||size>EffectProgram::MaxJsonBytes)return nullptr;
            std::ifstream stream(dll_path,std::ios::binary);std::string data(static_cast<size_t>(size),'\0');
            if(!stream.read(data.data(),static_cast<std::streamsize>(size))||stream.peek()!=std::char_traits<char>::eof())return nullptr;
            auto program=EffectProgram::Parse(data);auto entry=std::make_shared<PluginEntry>();
            entry->generation_id=++generation_sequence;entry->original_path=dll_path;
            entry->effect_name=dll_path.stem().string();if(entry->effect_name.rfind("effect_",0)==0)entry->effect_name.erase(0,7);
            if(!LogicalProgramName(entry->effect_name))return nullptr;
            entry->program=std::move(program);return entry;
        }

        std::filesystem::create_directories(cache_dir_, ec);

        // 1. 生成唯一影子副本路径，规避 Windows LNK1104 独占锁
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::string stem = dll_path.stem().string();
        const uint64_t generation_id = ++generation_sequence;
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

        LogicalKeyboardExports keyboard;
        auto keyboard_version = reinterpret_cast<PfnAuraGetLogicalKeyboardVersion>(GetProcAddress(hMod, "AuraGetLogicalKeyboardVersion"));
        auto keyboard_bind = reinterpret_cast<PfnAuraBindLogicalKeyboard>(GetProcAddress(hMod, "AuraBindLogicalKeyboard"));
        if (keyboard_version || keyboard_bind) {
            if (!keyboard_version || !keyboard_bind || keyboard_version() != AURA_LOGICAL_KEYBOARD_VERSION) {
                if (!quiet) LOG_ERROR("[PluginManager] Unsupported/partial logical keyboard contract; previous generation retained.");
                return nullptr;
            }
            keyboard.version = AURA_LOGICAL_KEYBOARD_VERSION;
            keyboard.bind = keyboard_bind;
        }

        HallAnalogExports hall_analog;
        auto hall_version = reinterpret_cast<PfnAuraGetHallAnalogVersion>(GetProcAddress(hMod, "AuraGetHallAnalogVersion"));
        auto hall_bind = reinterpret_cast<PfnAuraBindHallAnalog>(GetProcAddress(hMod, "AuraBindHallAnalog"));
        if (hall_version || hall_bind) {
            if (!hall_version || !hall_bind || hall_version() != AURA_HALL_ANALOG_VERSION) {
                if (!quiet) LOG_ERROR("[PluginManager] Unsupported/partial Hall analog contract; previous generation retained.");
                return nullptr;
            }
            hall_analog.version = AURA_HALL_ANALOG_VERSION;
            hall_analog.bind = hall_bind;
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
        entry->logical_keyboard = keyboard;
        entry->hall_analog = hall_analog;
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
                                     const std::shared_ptr<const PluginEntry>& expected_previous, bool quiet,
                                     std::optional<bool> expected_disabled) {
    // Preflight the complete alias set, then swap a prepared registry. A collision,
    // concurrent publication or allocation failure cannot leave half-updated aliases.
    std::unordered_map<std::string, std::shared_ptr<const PluginEntry>> next;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(candidate->program) {
            const bool disabled=disabled_program_aliases_.count(requested_alias)!=0;
            if(expected_disabled ? disabled!=*expected_disabled : disabled)return false;
            const auto found=plugins_.find(requested_alias);
            const auto current=found==plugins_.end()?nullptr:found->second;
            if(current!=expected_previous||(current&&!current->program))return false;
            std::set<std::string> aliases{requested_alias,candidate->effect_name};
            if(current)for(const auto& item:plugins_)if(item.second==current)aliases.insert(item.first);
            for(const auto& alias:aliases){auto item=plugins_.find(alias);if(item!=plugins_.end()&&item->second!=current)return false;}
            next=plugins_;for(const auto& alias:aliases)if(!alias.empty())next[alias]=candidate;
            plugins_.swap(next);for(const auto& alias:aliases){disabled_program_aliases_.erase(alias);disabled_program_generations_.erase(alias);}
            return true;
        }
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
    if(generation&&generation->program) {
        try {
            auto effect=std::shared_ptr<Effect>(new ProgramEffect(generation->program,&logical_keyboard_host,&hall_analog_host),
                [owner=generation](Effect* pointer){delete pointer;});
            static_cast<ProgramEffect*>(effect.get())->SetGenerationId(generation->generation_id);
            return TriggeredEffectInstance(std::move(generation),std::move(effect));
        } catch(...) {return {};}
    }
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
        if (generation->logical_keyboard.bind && generation->logical_keyboard.bind(raw, &logical_keyboard_host) != 1) {
            if (!quiet) LOG_ERROR("[PluginManager] Logical keyboard bind rejected; candidate instance destroyed.");
            return {};
        }
        if (generation->hall_analog.bind && generation->hall_analog.bind(raw, &hall_analog_host) != 1) {
            if (!quiet) LOG_ERROR("[PluginManager] Hall analog bind rejected; candidate instance destroyed.");
            return {};
        }
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
        {std::lock_guard<std::mutex> lock(mutex_);if(disabled_program_aliases_.count(name_or_path))return {};}
        const auto path = PluginSourcePath(ResolvePluginPath(name_or_path, plugins_dir_));
        auto previous = GetGeneration(name_or_path);
        if(!previous)previous=FindByPath(path);
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

PluginManager::PreparedReload PluginManager::PrepareReload(const std::string& effect_name, bool require_lifecycle) {
    try {
        {std::lock_guard<std::mutex> lock(mutex_);if(disabled_program_aliases_.count(effect_name))return {};}
        auto previous = GetGeneration(effect_name);
        auto path = previous ? previous->original_path : PluginSourcePath(ResolvePluginPath(effect_name, plugins_dir_));
        if (!previous) previous = FindByPath(path);
        auto entry = LoadPluginInternal(path);
        if(previous&&previous->program&&entry&&entry->program) {
            auto renamed=std::make_shared<PluginEntry>(*entry);renamed->effect_name=previous->effect_name;entry=std::move(renamed);
        }
        if (!entry || (require_lifecycle && !entry->HasLifecycle())) return {};
        auto destruction_failed = std::make_shared<bool>(false);
        { auto probe = Instantiate(entry, destruction_failed); if (!probe) return {}; }
        if (*destruction_failed) return {};
        return {entry, previous, effect_name};
    } catch (...) { LOG_ERROR("[PluginManager] Prepare failed; old generation retained"); return {}; }
}
PluginManager::PreparedReload PluginManager::PrepareProgram(const std::string& name,
    std::shared_ptr<const EffectProgram> program,const std::filesystem::path& immutable_source) {
    if(!program||!LogicalProgramName(name)||immutable_source.extension()!=".aurair")return {};
    try {
        std::shared_ptr<const PluginEntry> previous;bool disabled=false;
        {std::lock_guard<std::mutex> lock(mutex_);auto found=plugins_.find(name);if(found!=plugins_.end())previous=found->second;
            disabled=disabled_program_aliases_.count(name)!=0;}
        if(previous&&!previous->program)return {};
        auto entry=std::make_shared<PluginEntry>();entry->generation_id=++generation_sequence;
        entry->effect_name=name;entry->original_path=PluginSourcePath(immutable_source);entry->program=std::move(program);
        if(!Instantiate(entry,{},true))return {};
        return {entry,previous,name,disabled};
    }catch(...){return {};}
}
bool PluginManager::UnregisterProgram(const std::string& name,uint64_t expected_generation) {
    return UnregisterProgram(PrepareProgramRemoval(name,expected_generation));
}
PluginManager::ProgramRemoval PluginManager::PrepareProgramRemoval(const std::string& name,uint64_t expected_generation) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found=plugins_.find(name);if(found==plugins_.end()||!found->second->program||found->second->generation_id!=expected_generation)return {};
    ProgramRemoval removal;removal.generation=found->second;
    for(const auto& item:plugins_)if(item.second==removal.generation)removal.aliases.push_back(item.first);
    return removal;
}
bool PluginManager::UnregisterProgram(const ProgramRemoval& removal) {
    if(!removal)return false;
    std::unordered_map<std::string,std::shared_ptr<const PluginEntry>> next;
    std::unordered_set<std::string> disabled;
    std::unordered_map<std::string,uint64_t> generations;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t aliases=0;for(const auto& item:plugins_)if(item.second==removal.generation)++aliases;
        if(aliases!=removal.aliases.size())return false;
        for(const auto& alias:removal.aliases){auto found=plugins_.find(alias);if(found==plugins_.end()||found->second!=removal.generation)return false;}
        next=plugins_;disabled=disabled_program_aliases_;generations=disabled_program_generations_;
        for(auto it=next.begin();it!=next.end();) {
            if(it->second==removal.generation){disabled.insert(it->first);generations[it->first]=removal.generation->generation_id;it=next.erase(it);}else ++it;
        }
        plugins_.swap(next);disabled_program_aliases_.swap(disabled);disabled_program_generations_.swap(generations);
    }
    return true;
}
bool PluginManager::RestoreProgram(const ProgramRemoval& removal) {
    if(!removal)return false;
    std::unordered_map<std::string,std::shared_ptr<const PluginEntry>> next;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for(const auto& alias:removal.aliases){auto disabled=disabled_program_generations_.find(alias);
            if(plugins_.count(alias)||disabled==disabled_program_generations_.end()||disabled->second!=removal.generation->generation_id)return false;}
        next=plugins_;for(const auto& alias:removal.aliases)next[alias]=removal.generation;
        plugins_.swap(next);for(const auto& alias:removal.aliases){disabled_program_aliases_.erase(alias);disabled_program_generations_.erase(alias);}
    }
    return true;
}
bool PluginManager::RollbackProgram(const PreparedReload& published) {
    if(!published.candidate||!published.candidate->program || (published.previous&&!published.previous->program))return false;
    std::unordered_map<std::string,std::shared_ptr<const PluginEntry>> next;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto found=plugins_.find(published.alias);if(found==plugins_.end()||found->second!=published.candidate)return false;
        next=plugins_;
        for(auto it=next.begin();it!=next.end();) {
            if(it->second!=published.candidate){++it;continue;}
            if(published.previous){it->second=published.previous;++it;}else it=next.erase(it);
        }
        if(published.previous_disabled)disabled_program_aliases_.insert(published.alias);
        plugins_.swap(next);
    }
    return true;
}
bool PluginManager::PublishReload(const PreparedReload& prepared) {
    if (!prepared) return false;
    try { return PublishGeneration(prepared.candidate, prepared.alias, prepared.previous, false,
                                  prepared.candidate->program?std::optional<bool>(prepared.previous_disabled):std::nullopt); }
    catch (...) { LOG_ERROR("[PluginManager] Publication failed; old generation retained"); return false; }
}
bool PluginManager::ReloadPlugin(const std::string& name) {
    return PublishReload(PrepareReload(name));
}
std::shared_ptr<const PluginEntry> PluginManager::GetGeneration(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = plugins_.find(name);
    return found == plugins_.end() ? nullptr : found->second;
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
std::shared_ptr<const PluginEntry> PluginManager::PrepareEffectGeneration(const std::string& name) {
    {std::lock_guard<std::mutex> lock(mutex_);if(disabled_program_aliases_.count(name))return {};}
    if (auto current=GetGeneration(name)) return current;
    try {
        const auto path=PluginSourcePath(ResolvePluginPath(name,plugins_dir_));
        if (auto current=FindByPath(path)) return current;
        return LoadPluginInternal(path,true);
    } catch (...) { return {}; }
}

bool PluginManager::HasPlugin(const std::string& effect_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return plugins_.find(effect_name) != plugins_.end();
}

bool PluginManager::PluginUsesHallAnalog(const std::string& effect_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = plugins_.find(effect_name);
    if (it == plugins_.end() || !it->second) return false;
    const auto& entry = *it->second;
    if (entry.hall_analog.version == AURA_HALL_ANALOG_VERSION && entry.hall_analog.bind != nullptr) return true;
    return entry.program && entry.program->UsesHall();
}

const AuraHallAnalogHostV1* PluginManager::GetHallAnalogHost() {
    return &hall_analog_host;
}

std::vector<PluginSummary> PluginManager::GetPluginSummaries() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PluginSummary> out;
    std::set<const PluginEntry*> seen;
    for (const auto& [alias, entry] : plugins_) {
        if (!entry || !seen.insert(entry.get()).second) continue;
        PluginSummary summary;
        summary.effect_name = entry->effect_name.empty() ? alias : entry->effect_name;
        summary.kind = entry->program ? "program" : "native_dll";
        summary.generation_id = entry->generation_id;
        summary.original_path = entry->original_path;
        summary.has_lifecycle = entry->HasLifecycle();
        summary.has_logical_keyboard = (entry->logical_keyboard.version == AURA_LOGICAL_KEYBOARD_VERSION &&
                                        entry->logical_keyboard.bind != nullptr);
        summary.has_hall_analog = (entry->hall_analog.version == AURA_HALL_ANALOG_VERSION &&
                                   entry->hall_analog.bind != nullptr);
        out.push_back(std::move(summary));
    }
    std::sort(out.begin(), out.end(), [](const PluginSummary& a, const PluginSummary& b) {
        return a.effect_name < b.effect_name;
    });
    return out;
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
        if (entry.is_regular_file(ec) && (entry.path().extension() == ".dll" || entry.path().extension() == ".aurair")) {
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
