#pragma once

#include "engine/effect.h"
#include "third_party/json.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <windows.h>

namespace aura {

class GsiState;

struct RuleEntry {
    std::string process_name;
    std::string profile_name;
    bool suppress_web_ui{false};
};

struct GsiBinding {
    std::string field;
    std::string op{"=="};
    nlohmann::json target_value;
    std::string profile_name;
};

class RuleEngine {
public:
    RuleEngine();

    bool LoadConfig(const std::string& config_path);

    // Checks file modification time and reloads if changed
    bool CheckAndReload();

    // Matches process name against configured rules, falling back to default.
    // If GSI state is provided, evaluates GSI bindings according to priority discipline:
    // 1. If process is cs2.exe: evaluate gsi_bindings first (earlier index = higher priority).
    //    If matched, returns the binding profile; if none match, falls back to cs2.exe rule.
    // 2. If process is empty/desktop and GSI has active data: evaluate gsi_bindings.
    // 3. Otherwise, matches process_name against normal rules, then default_profile.
    std::shared_ptr<const Profile> MatchProfile(const std::string& process_name, const GsiState* gsi_state = nullptr);

    // Checks if the matched rule requests suppressing the web UI service
    bool ShouldSuppressWebUi(const std::string& process_name);

    std::string GetDefaultProfileName() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return default_profile_name_;
    }
    std::string GetConfigPath() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return config_path_;
    }
    std::vector<GsiBinding> GetGsiBindings() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return gsi_bindings_;
    }
    int GetFps() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return target_fps_;
    }
    bool HasProfile(const std::string& name) const;
    std::shared_ptr<const Profile> GetProfile(const std::string& name) const;

private:
    static std::string ToLower(const std::string& s);
    static FILETIME GetConfigFileTime(const std::string& path);
    FILETIME GetConfigFileTime() const;

    std::string config_path_;
    FILETIME last_write_time_{0, 0};

    std::string default_profile_name_;
    int target_fps_{25};
    std::vector<RuleEntry> rules_;
    std::vector<GsiBinding> gsi_bindings_;
    std::unordered_map<std::string, std::shared_ptr<Profile>> profiles_;

    mutable std::mutex mutex_;
};

} // namespace aura
