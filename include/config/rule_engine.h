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

    // Overload for backwards compatibility
    std::shared_ptr<const Profile> MatchProfile(const std::string& process_name) {
        return MatchProfile(process_name, nullptr);
    }

    // Checks if the matched rule requests suppressing the web UI service
    bool ShouldSuppressWebUi(const std::string& process_name);

    const std::string& GetDefaultProfileName() const { return default_profile_name_; }
    const std::string& GetConfigPath() const { return config_path_; }
    const std::vector<GsiBinding>& GetGsiBindings() const { return gsi_bindings_; }

private:
    static std::string ToLower(const std::string& s);
    FILETIME GetConfigFileTime() const;

    std::string config_path_;
    FILETIME last_write_time_{0, 0};

    std::string default_profile_name_;
    std::vector<RuleEntry> rules_;
    std::vector<GsiBinding> gsi_bindings_;
    std::unordered_map<std::string, std::shared_ptr<Profile>> profiles_;

    mutable std::mutex mutex_;
};

} // namespace aura
