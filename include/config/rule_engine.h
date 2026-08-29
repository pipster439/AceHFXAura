#pragma once

#include "engine/effect.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <windows.h>

namespace aura {

struct RuleEntry {
    std::string process_name;
    std::string profile_name;
    bool suppress_web_ui{false};
};

class RuleEngine {
public:
    RuleEngine();

    bool LoadConfig(const std::string& config_path);

    // Checks file modification time and reloads if changed
    bool CheckAndReload();

    // Matches process name against configured rules, falling back to default
    const Profile* MatchProfile(const std::string& process_name);

    // Checks if the matched rule requests suppressing the web UI service
    bool ShouldSuppressWebUi(const std::string& process_name);

    const std::string& GetDefaultProfileName() const { return default_profile_name_; }
    const std::string& GetConfigPath() const { return config_path_; }

private:
    static std::string ToLower(const std::string& s);
    FILETIME GetConfigFileTime() const;

    std::string config_path_;
    FILETIME last_write_time_{0, 0};

    std::string default_profile_name_;
    std::vector<RuleEntry> rules_;
    std::unordered_map<std::string, std::shared_ptr<Profile>> profiles_;

    std::mutex mutex_;
};

} // namespace aura
