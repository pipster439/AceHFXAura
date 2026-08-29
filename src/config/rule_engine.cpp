#include "config/rule_engine.h"
#include "engine/builtin_effects.h"
#include "third_party/json.hpp"
#include "utils/logger.h"
#include <fstream>
#include <algorithm>
#include <cctype>

namespace aura {

std::string RuleEngine::ToLower(const std::string& s) {
    std::string res = s;
    std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return res;
}

FILETIME RuleEngine::GetConfigFileTime() const {
    FILETIME ft{0, 0};
    if (config_path_.empty()) return ft;

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExA(config_path_.c_str(), GetFileExInfoStandard, &fad)) {
        ft = fad.ftLastWriteTime;
    }
    return ft;
}

RuleEngine::RuleEngine() : default_profile_name_("desktop") {}

bool RuleEngine::LoadConfig(const std::string& config_path) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        LOG_ERROR("无法打开配置文件: " << config_path);
        return false;
    }

    try {
        nlohmann::json j;
        file >> j;

        std::string def_name = j.value("default_profile", "desktop");

        std::vector<RuleEntry> new_rules;
        if (j.contains("rules") && j["rules"].is_array()) {
            for (auto& item : j["rules"]) {
                RuleEntry re;
                re.process_name = ToLower(item.value("process", ""));
                re.profile_name = item.value("profile", "");
                re.suppress_web_ui = item.value("suppress_web_ui", false);
                if (!re.process_name.empty() && !re.profile_name.empty()) {
                    new_rules.push_back(re);
                }
            }
        }

        std::unordered_map<std::string, std::shared_ptr<Profile>> new_profiles;
        if (j.contains("profiles") && j["profiles"].is_object()) {
            for (auto& [pname, pval] : j["profiles"].items()) {
                auto prof = std::make_shared<Profile>();
                prof->name = pname;

                std::string type = pval.value("type", "static");
                if (type == "static") {
                    ColorRGB col(0, 80, 200);
                    if (pval.contains("color") && pval["color"].is_array() && pval["color"].size() >= 3) {
                        col = ColorRGB(pval["color"][0], pval["color"][1], pval["color"][2]);
                    }
                    bool analog = pval.value("analog", false);
                    prof->base_effect = std::make_shared<StaticEffect>(col, analog);
                } else if (type == "breathing") {
                    ColorRGB c1(0, 100, 255);
                    ColorRGB c2(0, 10, 50);
                    uint64_t period = pval.value("period_ms", 3000);
                    if (pval.contains("color1") && pval["color1"].is_array() && pval["color1"].size() >= 3) {
                        c1 = ColorRGB(pval["color1"][0], pval["color1"][1], pval["color1"][2]);
                    }
                    if (pval.contains("color2") && pval["color2"].is_array() && pval["color2"].size() >= 3) {
                        c2 = ColorRGB(pval["color2"][0], pval["color2"][1], pval["color2"][2]);
                    }
                    prof->base_effect = std::make_shared<BreathingEffect>(c1, c2, period);
                } else if (type == "color_cycle") {
                    uint64_t period = pval.value("period_ms", 3500);
                    prof->base_effect = std::make_shared<ColorCycleEffect>(period);
                } else if (type == "wave") {
                    uint64_t period = pval.value("period_ms", 3500);
                    std::string dir = pval.value("direction", "diag_dl");
                    prof->base_effect = std::make_shared<WaveEffect>(period, dir);
                } else if (type == "custom_keymap") {
                    ColorRGB bg(0, 0, 0);
                    if (pval.contains("bg") && pval["bg"].is_array() && pval["bg"].size() >= 3) {
                        bg = ColorRGB(pval["bg"][0], pval["bg"][1], pval["bg"][2]);
                    }
                    prof->base_effect = std::make_shared<CustomKeymapEffect>(bg);
                } else if (type == "reactive") {
                    ColorRGB bg(0, 5, 15);
                    ColorRGB c1(255, 25, 41);
                    uint64_t period = pval.value("period_ms", 2500);
                    if (pval.contains("bg") && pval["bg"].is_array() && pval["bg"].size() >= 3) {
                        bg = ColorRGB(pval["bg"][0], pval["bg"][1], pval["bg"][2]);
                    }
                    if (pval.contains("color") && pval["color"].is_array() && pval["color"].size() >= 3) {
                        c1 = ColorRGB(pval["color"][0], pval["color"][1], pval["color"][2]);
                    } else if (pval.contains("color1") && pval["color1"].is_array() && pval["color1"].size() >= 3) {
                        c1 = ColorRGB(pval["color1"][0], pval["color1"][1], pval["color1"][2]);
                    }
                    prof->base_effect = std::make_shared<ReactiveEffect>(bg, c1, period);
                } else if (type == "ripple") {
                    ColorRGB bg(0, 5, 15);
                    ColorRGB c1(0, 240, 255);
                    uint64_t period = pval.value("period_ms", 2500);
                    if (pval.contains("bg") && pval["bg"].is_array() && pval["bg"].size() >= 3) {
                        bg = ColorRGB(pval["bg"][0], pval["bg"][1], pval["bg"][2]);
                    }
                    if (pval.contains("color") && pval["color"].is_array() && pval["color"].size() >= 3) {
                        c1 = ColorRGB(pval["color"][0], pval["color"][1], pval["color"][2]);
                    } else if (pval.contains("color1") && pval["color1"].is_array() && pval["color1"].size() >= 3) {
                        c1 = ColorRGB(pval["color1"][0], pval["color1"][1], pval["color1"][2]);
                    }
                    prof->base_effect = std::make_shared<RippleEffect>(bg, c1, period);
                } else if (type == "starry_night") {
                    ColorRGB col(0, 240, 255);
                    if (pval.contains("color") && pval["color"].is_array() && pval["color"].size() >= 3) {
                        col = ColorRGB(pval["color"][0], pval["color"][1], pval["color"][2]);
                    } else if (pval.contains("color1") && pval["color1"].is_array() && pval["color1"].size() >= 3) {
                        col = ColorRGB(pval["color1"][0], pval["color1"][1], pval["color1"][2]);
                    }
                    bool random_colors = pval.value("random_colors", false);
                    uint64_t period = pval.value("period_ms", 2500);
                    prof->base_effect = std::make_shared<StarryNightEffect>(col, random_colors, period);
                } else if (type == "quicksand") {
                    ColorRGB c1(255, 25, 41);
                    ColorRGB c2(20, 138, 196);
                    uint64_t period = pval.value("period_ms", 3500);
                    std::string dir = pval.value("direction", "diag_dl");
                    if (pval.contains("color1") && pval["color1"].is_array() && pval["color1"].size() >= 3) {
                        c1 = ColorRGB(pval["color1"][0], pval["color1"][1], pval["color1"][2]);
                    }
                    if (pval.contains("color2") && pval["color2"].is_array() && pval["color2"].size() >= 3) {
                        c2 = ColorRGB(pval["color2"][0], pval["color2"][1], pval["color2"][2]);
                    }
                    prof->base_effect = std::make_shared<QuicksandEffect>(c1, c2, period, dir);
                } else if (type == "current") {
                    ColorRGB col(0, 240, 255);
                    if (pval.contains("color") && pval["color"].is_array() && pval["color"].size() >= 3) {
                        col = ColorRGB(pval["color"][0], pval["color"][1], pval["color"][2]);
                    } else if (pval.contains("color1") && pval["color1"].is_array() && pval["color1"].size() >= 3) {
                        col = ColorRGB(pval["color1"][0], pval["color1"][1], pval["color1"][2]);
                    }
                    uint64_t period = pval.value("period_ms", 2000);
                    prof->base_effect = std::make_shared<CurrentEffect>(col, period);
                } else if (type == "raindrop") {
                    ColorRGB col(0, 240, 255);
                    if (pval.contains("color") && pval["color"].is_array() && pval["color"].size() >= 3) {
                        col = ColorRGB(pval["color"][0], pval["color"][1], pval["color"][2]);
                    } else if (pval.contains("color1") && pval["color1"].is_array() && pval["color1"].size() >= 3) {
                        col = ColorRGB(pval["color1"][0], pval["color1"][1], pval["color1"][2]);
                    }
                    uint64_t period = pval.value("period_ms", 2500);
                    prof->base_effect = std::make_shared<RaindropEffect>(col, period);
                }


                // Parse key overrides
                if (pval.contains("keys") && pval["keys"].is_object()) {
                    for (auto& [kspec, kval] : pval["keys"].items()) {
                        if (kval.is_array() && kval.size() >= 3) {
                            KeyOverride ko;
                            ko.key_spec = kspec;
                            ko.color = ColorRGB(kval[0], kval[1], kval[2]);
                            prof->key_overrides.push_back(ko);
                        }
                    }
                }

                new_profiles[pname] = prof;
            }
        }

        // Apply under lock
        {
            std::lock_guard<std::mutex> lock(mutex_);
            config_path_ = config_path;
            default_profile_name_ = def_name;
            rules_ = std::move(new_rules);
            profiles_ = std::move(new_profiles);
            last_write_time_ = GetConfigFileTime();
        }

        LOG_INFO("成功加载配置文件: " << config_path << " (默认方案: " << def_name 
                 << ", 规则数: " << rules_.size() << ", Profile数: " << profiles_.size() << ")");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("解析配置文件异常: " << e.what());
        return false;
    }
}

bool RuleEngine::CheckAndReload() {
    FILETIME current_ft = GetConfigFileTime();
    if (current_ft.dwLowDateTime == 0 && current_ft.dwHighDateTime == 0) {
        return false;
    }

    if (CompareFileTime(&current_ft, &last_write_time_) != 0) {
        LOG_INFO("检测到配置文件已修改，正在执行热重载...");
        return LoadConfig(config_path_);
    }

    return false;
}

const Profile* RuleEngine::MatchProfile(const std::string& process_name) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string lower_proc = ToLower(process_name);

    if (!lower_proc.empty()) {
        for (const auto& rule : rules_) {
            if (rule.process_name == lower_proc) {
                auto it = profiles_.find(rule.profile_name);
                if (it != profiles_.end()) {
                    return it->second.get();
                }
            }
            // Also try without .exe if applicable
            if (rule.process_name.size() > 4 && rule.process_name.substr(rule.process_name.size() - 4) == ".exe") {
                std::string base = rule.process_name.substr(0, rule.process_name.size() - 4);
                if (base == lower_proc) {
                    auto it = profiles_.find(rule.profile_name);
                    if (it != profiles_.end()) {
                        return it->second.get();
                    }
                }
            }
        }
    }

    // Fallback to default profile
    auto def_it = profiles_.find(default_profile_name_);
    if (def_it != profiles_.end()) {
        return def_it->second.get();
    }

    // If default profile name not found, return first available profile or nullptr
    if (!profiles_.empty()) {
        return profiles_.begin()->second.get();
    }

    return nullptr;
}

bool RuleEngine::ShouldSuppressWebUi(const std::string& process_name) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string lower_proc = ToLower(process_name);
    if (!lower_proc.empty()) {
        for (const auto& rule : rules_) {
            if (rule.process_name == lower_proc) {
                return rule.suppress_web_ui;
            }
            if (rule.process_name.size() > 4 && rule.process_name.substr(rule.process_name.size() - 4) == ".exe") {
                std::string base = rule.process_name.substr(0, rule.process_name.size() - 4);
                if (base == lower_proc) {
                    return rule.suppress_web_ui;
                }
            }
        }
    }

    // Default policy: unmapped (desktop / unknown) => false (do not suppress)
    return false;
}

} // namespace aura
