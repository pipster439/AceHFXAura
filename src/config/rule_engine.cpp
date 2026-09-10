#include "config/rule_engine.h"
#include "engine/builtin_effects.h"
#include "gsi/gsi_adapter.h"
#include "third_party/json.hpp"
#include "utils/logger.h"
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>

namespace aura {

namespace {

uint64_t ParseAndClampPeriod(const std::string& pname, const nlohmann::json& pval, uint64_t def_period) {
    if (pval.contains("period_ms")) {
        const auto& period_val = pval["period_ms"];
        if (period_val.is_number_unsigned()) {
            uint64_t u = period_val.get<uint64_t>();
            if (u < 33) {
                uint64_t clamped = ClampPeriod(u, def_period, 33);
                LOG_WARN("方案 '" << pname << "' 的 period_ms 非法 (" << period_val.dump()
                         << ")，已被钳制为 " << clamped);
                return clamped;
            }
            return u;
        }

        // 非无符号整数：有符号负整数或非整型（如浮点数、字符串、布尔、对象等）
        uint64_t clamped = ClampPeriod(0, def_period, 33);
        LOG_WARN("方案 '" << pname << "' 的 period_ms 非法 (" << period_val.dump()
                 << ")，已被钳制为 " << clamped);
        return clamped;
    }

    if (pval.contains("speed_index")) {
        const auto& sval = pval["speed_index"];
        if (sval.is_number()) {
            int s = static_cast<int>(std::round(sval.get<double>()));
            if (s == 0) return 5500;
            if (s == 1) return 3200;
            if (s == 2) return 1600;
            LOG_WARN("方案 '" << pname << "' 的 speed_index 非法 (" << sval.dump()
                     << ")，已使用默认周期 " << def_period);
        } else {
            LOG_WARN("方案 '" << pname << "' 的 speed_index 类型非法 (" << sval.dump()
                     << ")，已使用默认周期 " << def_period);
        }
    }

    return def_period;
}

uint8_t ParseBrightness(const std::string& pname, const nlohmann::json& pval) {
    if (!pval.contains("brightness")) {
        return 255;
    }
    const auto& bval = pval["brightness"];
    if (!bval.is_number()) {
        LOG_WARN("方案 '" << pname << "' 的 brightness 字段类型非法 (" << bval.dump()
                 << ")，已使用默认值 255");
        return 255;
    }
    double v = bval.get<double>();
    if (v < 0.0) {
        LOG_WARN("方案 '" << pname << "' 的 brightness 小于 0 (" << v << ")，已被钳制为 0");
        return 0;
    }
    if (v <= 1.0) {
        return static_cast<uint8_t>(std::clamp(static_cast<int>(std::round(v * 255.0)), 0, 255));
    }
    if (v > 255.0) {
        LOG_WARN("方案 '" << pname << "' 的 brightness 超过 255 (" << v << ")，已被钳制为 255");
        return 255;
    }
    return static_cast<uint8_t>(std::clamp(static_cast<int>(std::round(v)), 0, 255));
}

} // namespace

std::string RuleEngine::ToLower(const std::string& s) {
    std::string res = s;
    std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return res;
}

FILETIME RuleEngine::GetConfigFileTime(const std::string& path) {
    FILETIME ft{0, 0};
    if (path.empty()) return ft;

    std::filesystem::path fs_path(path);
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(fs_path.c_str(), GetFileExInfoStandard, &fad)) {
        ft = fad.ftLastWriteTime;
    }
    return ft;
}

FILETIME RuleEngine::GetConfigFileTime() const {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        path = config_path_;
    }
    return GetConfigFileTime(path);
}

RuleEngine::RuleEngine() : default_profile_name_("desktop") {}

bool RuleEngine::LoadConfig(const std::string& config_path) {
    std::filesystem::path fs_path(config_path);
    std::ifstream file(fs_path);

    FILETIME file_ft = GetConfigFileTime(config_path);

    auto record_failure_ft = [&]() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (config_path_.empty() || config_path_ == config_path) {
            config_path_ = config_path;
            last_write_time_ = file_ft;
        }
    };

    if (!file.is_open()) {
        LOG_ERROR("无法打开配置文件: " << config_path);
        record_failure_ft();
        return false;
    }

    try {
        nlohmann::json j;
        file >> j;

        if (!j.is_object()) {
            LOG_ERROR("配置文件格式错误：根节点必须为 JSON 对象: " << config_path);
            record_failure_ft();
            return false;
        }

        std::string def_name = "desktop";
        bool valid = true;

        if (j.contains("default_profile")) {
            if (j["default_profile"].is_string()) {
                def_name = j["default_profile"].get<std::string>();
            } else {
                LOG_ERROR("默认方案字段 'default_profile' 必须为字符串: " << config_path);
                valid = false;
            }
        }

        std::vector<RuleEntry> new_rules;
        if (j.contains("rules")) {
            if (!j["rules"].is_array()) {
                LOG_ERROR("配置文件中 'rules' 字段必须为数组: " << config_path);
                valid = false;
            } else {
                for (auto& item : j["rules"]) {
                    if (!item.is_object()) {
                        LOG_ERROR("进程规则项必须为 JSON 对象: " << item.dump());
                        valid = false;
                        continue;
                    }
                    RuleEntry re;
                    re.process_name = ToLower(item.value("process", ""));
                    re.profile_name = item.value("profile", "");
                    re.suppress_web_ui = item.value("suppress_web_ui", false);
                    if (re.process_name.empty()) {
                        LOG_ERROR("进程规则缺少有效的 process 字段: " << item.dump());
                        valid = false;
                        continue;
                    }
                    if (re.profile_name.empty()) {
                        LOG_ERROR("进程规则 (进程: '" << re.process_name << "') 缺少有效的 profile 字段");
                        valid = false;
                        continue;
                    }
                    new_rules.push_back(re);
                }
            }
        }

        std::vector<GsiBinding> new_gsi_bindings;
        if (j.contains("gsi_bindings")) {
            if (!j["gsi_bindings"].is_array()) {
                LOG_ERROR("配置文件中 'gsi_bindings' 字段必须为数组: " << config_path);
                valid = false;
            } else {
                for (auto& item : j["gsi_bindings"]) {
                    if (!item.is_object()) {
                        LOG_ERROR("GSI 绑定规则项必须为 JSON 对象: " << item.dump());
                        valid = false;
                        continue;
                    }
                    GsiBinding b;
                    b.field = item.value("field", "");
                    b.op = item.value("operator", "==");
                    if (item.contains("value")) {
                        b.target_value = item["value"];
                    }
                    b.profile_name = item.value("profile", "");
                    if (b.field.empty()) {
                        LOG_ERROR("GSI 绑定规则缺少有效的 field 字段: " << item.dump());
                        valid = false;
                        continue;
                    }
                    if (b.profile_name.empty()) {
                        LOG_ERROR("GSI 绑定规则 (字段: '" << b.field << "') 缺少有效的 profile 字段");
                        valid = false;
                        continue;
                    }
                    new_gsi_bindings.push_back(b);
                }
            }
        }

        std::unordered_map<std::string, std::shared_ptr<Profile>> new_profiles;
        if (!j.contains("profiles") || !j["profiles"].is_object() || j["profiles"].empty()) {
            LOG_ERROR("配置文件缺少有效的 'profiles' 节点或 profiles 为空: " << config_path);
            valid = false;
        } else {
            for (auto& [pname, pval] : j["profiles"].items()) {
                if (!pval.is_object()) {
                    LOG_ERROR("方案 '" << pname << "' 必须为 JSON 对象");
                    valid = false;
                    continue;
                }
                auto prof = std::make_shared<Profile>();
                prof->name = pname;
                prof->brightness = ParseBrightness(pname, pval);

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
                    uint64_t period = ParseAndClampPeriod(pname, pval, 3000);
                    if (pval.contains("color1") && pval["color1"].is_array() && pval["color1"].size() >= 3) {
                        c1 = ColorRGB(pval["color1"][0], pval["color1"][1], pval["color1"][2]);
                    }
                    if (pval.contains("color2") && pval["color2"].is_array() && pval["color2"].size() >= 3) {
                        c2 = ColorRGB(pval["color2"][0], pval["color2"][1], pval["color2"][2]);
                    }
                    prof->base_effect = std::make_shared<BreathingEffect>(c1, c2, period);
                } else if (type == "color_cycle") {
                    uint64_t period = ParseAndClampPeriod(pname, pval, 3500);
                    prof->base_effect = std::make_shared<ColorCycleEffect>(period);
                } else if (type == "wave") {
                    uint64_t period = ParseAndClampPeriod(pname, pval, 3500);
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
                    uint64_t period = ParseAndClampPeriod(pname, pval, 2500);
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
                    uint64_t period = ParseAndClampPeriod(pname, pval, 2500);
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
                    uint64_t period = ParseAndClampPeriod(pname, pval, 2500);
                    prof->base_effect = std::make_shared<StarryNightEffect>(col, random_colors, period);
                } else if (type == "quicksand") {
                    ColorRGB c1(255, 25, 41);
                    ColorRGB c2(20, 138, 196);
                    uint64_t period = ParseAndClampPeriod(pname, pval, 3500);
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
                    uint64_t period = ParseAndClampPeriod(pname, pval, 2000);
                    prof->base_effect = std::make_shared<CurrentEffect>(col, period);
                } else if (type == "raindrop") {
                    ColorRGB col(0, 240, 255);
                    if (pval.contains("color") && pval["color"].is_array() && pval["color"].size() >= 3) {
                        col = ColorRGB(pval["color"][0], pval["color"][1], pval["color"][2]);
                    } else if (pval.contains("color1") && pval["color1"].is_array() && pval["color1"].size() >= 3) {
                        col = ColorRGB(pval["color1"][0], pval["color1"][1], pval["color1"][2]);
                    }
                    uint64_t period = ParseAndClampPeriod(pname, pval, 2500);
                    prof->base_effect = std::make_shared<RaindropEffect>(col, period);
                } else {
                    LOG_ERROR("方案 '" << pname << "' 配置了未知的效果类型: '" << type 
                              << "' (支持的有效类型: static, breathing, color_cycle, wave, custom_keymap, reactive, ripple, starry_night, quicksand, current, raindrop)");
                    valid = false;
                }

                // Parse key overrides
                if (pval.contains("keys") && pval["keys"].is_object()) {
                    for (auto& [kspec, kval] : pval["keys"].items()) {
                        if (kval.is_array() && kval.size() >= 3 &&
                            kval[0].is_number() && kval[1].is_number() && kval[2].is_number()) {
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

        // 2. 默认方案完整性校验
        if (new_profiles.find(def_name) == new_profiles.end()) {
            LOG_ERROR("默认方案 default_profile '" << def_name 
                      << "' 未在 profiles 中定义，请检查拼写或在 profiles 中添加该方案定义");
            valid = false;
        }

        // 3. 规则 (rules) 方案引用完整性校验
        for (const auto& rule : new_rules) {
            if (new_profiles.find(rule.profile_name) == new_profiles.end()) {
                LOG_ERROR("进程规则 (进程: '" << rule.process_name 
                          << "') 引用了未定义的方案: '" << rule.profile_name 
                          << "'，请在 profiles 中定义方案 '" << rule.profile_name 
                          << "' 或修正该规则中的 profile 字段");
                valid = false;
            }
        }

        // 4. GSI 绑定 (gsi_bindings) 方案引用完整性校验
        for (const auto& binding : new_gsi_bindings) {
            if (new_profiles.find(binding.profile_name) == new_profiles.end()) {
                LOG_ERROR("GSI 绑定规则 (条件: " << binding.field << " " << binding.op 
                          << ") 引用了未定义的方案: '" << binding.profile_name 
                          << "'，请在 profiles 中定义方案 '" << binding.profile_name 
                          << "' 或修正该绑定中的 profile 字段");
                valid = false;
            }
        }

        if (!valid) {
            record_failure_ft();
            return false;
        }

        const size_t rules_count = new_rules.size();
        const size_t bindings_count = new_gsi_bindings.size();
        const size_t profiles_count = new_profiles.size();

        // Apply under lock
        {
            std::lock_guard<std::mutex> lock(mutex_);
            config_path_ = config_path;
            default_profile_name_ = def_name;
            rules_ = std::move(new_rules);
            gsi_bindings_ = std::move(new_gsi_bindings);
            profiles_ = std::move(new_profiles);
            last_write_time_ = file_ft;
        }

        LOG_INFO("成功加载配置文件: " << config_path << " (默认方案: " << def_name 
                 << ", 规则数: " << rules_count 
                 << ", GSI绑定数: " << bindings_count 
                 << ", Profile数: " << profiles_count << ")");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("解析配置文件异常: " << e.what());
        record_failure_ft();
        return false;
    }
}

bool RuleEngine::CheckAndReload() {
    std::string path;
    FILETIME last_ft{0, 0};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        path = config_path_;
        last_ft = last_write_time_;
    }
    if (path.empty()) return false;

    FILETIME current_ft = GetConfigFileTime(path);
    if (current_ft.dwLowDateTime == 0 && current_ft.dwHighDateTime == 0) {
        return false;
    }

    if (CompareFileTime(&current_ft, &last_ft) != 0) {
        LOG_INFO("检测到配置文件已修改，正在执行热重载: " << path);
        if (LoadConfig(path)) {
            return true;
        } else {
            std::lock_guard<std::mutex> lock(mutex_);
            last_write_time_ = current_ft;
            LOG_WARN("配置文件热重载失败，保留既有有效配置；文件再次修改前将暂停重试");
            return false;
        }
    }

    return false;
}

std::shared_ptr<const Profile> RuleEngine::MatchProfile(const std::string& process_name, const GsiState* gsi_state) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string lower_proc = ToLower(process_name);
    bool is_cs2 = (lower_proc == "cs2.exe" || lower_proc == "cs2" || lower_proc == "csgo.exe" || lower_proc == "csgo");

    // 1. 如果当前处于 CS2 游戏中，优先按优先级顺序评估 GSI 绑定 (靠前优先)
    if (is_cs2 && gsi_state != nullptr && gsi_state->IsActive()) {
        for (const auto& binding : gsi_bindings_) {
            if (gsi_state->Evaluate(binding.field, binding.op, binding.target_value)) {
                auto it = profiles_.find(binding.profile_name);
                if (it != profiles_.end()) {
                    return it->second;
                }
            }
        }
    }

    // 2. 匹配具体的前台进程规则
    if (!lower_proc.empty()) {
        for (const auto& rule : rules_) {
            if (rule.process_name == lower_proc) {
                auto it = profiles_.find(rule.profile_name);
                if (it != profiles_.end()) {
                    return it->second;
                }
            }
            // Also try without .exe if applicable
            if (rule.process_name.size() > 4 && rule.process_name.substr(rule.process_name.size() - 4) == ".exe") {
                std::string base = rule.process_name.substr(0, rule.process_name.size() - 4);
                if (base == lower_proc) {
                    auto it = profiles_.find(rule.profile_name);
                    if (it != profiles_.end()) {
                        return it->second;
                    }
                }
            }
        }
    }

    // 3. Fallback to default profile (前台不是 cs2.exe 时，GSI 绑定绝不生效)
    auto def_it = profiles_.find(default_profile_name_);
    if (def_it != profiles_.end()) {
        return def_it->second;
    }

    // If default profile name not found, return first available profile or nullptr
    if (!profiles_.empty()) {
        return profiles_.begin()->second;
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

bool RuleEngine::HasProfile(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return profiles_.find(name) != profiles_.end();
}

std::shared_ptr<const Profile> RuleEngine::GetProfile(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = profiles_.find(name);
    if (it != profiles_.end()) {
        return it->second;
    }
    return nullptr;
}

} // namespace aura
