#include "config/automation_contract.h"
#include "config/rule_engine.h"
#include "config/lighting_service.h"
#include "engine/builtin_effects.h"
#include "engine/plugin_manager.h"
#include "engine/automation_effect_runtime.h"
#include "gsi/gsi_adapter.h"
#include "third_party/json.hpp"
#include "utils/logger.h"
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <set>

namespace aura {

std::string ConditionNode::CompareOpToString(CompareOp op) {
    switch (op) {
        case CompareOp::Eq: return "==";
        case CompareOp::Ne: return "!=";
        case CompareOp::Lt: return "<";
        case CompareOp::Le: return "<=";
        case CompareOp::Gt: return ">";
        case CompareOp::Ge: return ">=";
        case CompareOp::Contains: return "contains";
        default: return "==";
    }
}

CompareOp ConditionNode::StringToCompareOp(const std::string& op_str) {
    if (op_str == "==" || op_str == "=" || op_str == "eq" || op_str == "equals") return CompareOp::Eq;
    if (op_str == "!=" || op_str == "ne" || op_str == "neq" || op_str == "not_equals") return CompareOp::Ne;
    if (op_str == "<" || op_str == "lt") return CompareOp::Lt;
    if (op_str == "<=" || op_str == "le" || op_str == "lte") return CompareOp::Le;
    if (op_str == ">" || op_str == "gt") return CompareOp::Gt;
    if (op_str == ">=" || op_str == "ge" || op_str == "gte") return CompareOp::Ge;
    if (op_str == "contains") return CompareOp::Contains;
    return CompareOp::Eq;
}

std::string ConditionNode::LogicOpToString(LogicOp op) {
    switch (op) {
        case LogicOp::And: return "and";
        case LogicOp::Or: return "or";
        case LogicOp::Not: return "not";
        default: return "none";
    }
}

LogicOp ConditionNode::StringToLogicOp(const std::string& op_str) {
    std::string s = RuleEngine::ToLower(op_str);
    if (s == "and") return LogicOp::And;
    if (s == "or") return LogicOp::Or;
    if (s == "not") return LogicOp::Not;
    return LogicOp::None;
}

nlohmann::json ConditionNode::ToJson() const {
    if (!event.empty()) return {{"event", event}};
    nlohmann::json j;
    if (logic_op == LogicOp::And) {
        j["type"] = "and";
        j["op"] = "and";
        j["conditions"] = nlohmann::json::array();
        for (const auto& child : children) {
            j["conditions"].push_back(child.ToJson());
        }
        return j;
    }
    if (logic_op == LogicOp::Or) {
        j["type"] = "or";
        j["op"] = "or";
        j["conditions"] = nlohmann::json::array();
        for (const auto& child : children) {
            j["conditions"].push_back(child.ToJson());
        }
        return j;
    }
    if (logic_op == LogicOp::Not) {
        j["type"] = "not";
        j["op"] = "not";
        j["conditions"] = nlohmann::json::array();
        if (!children.empty()) {
            j["conditions"].push_back(children[0].ToJson());
        }
        return j;
    }

    j["field"] = field;
    j["op"] = CompareOpToString(comp_op);
    j["value"] = target_value;
    return j;
}

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
            double sv = sval.get<double>();
            if (!std::isfinite(sv)) {
                LOG_WARN("方案 '" << pname << "' 的 speed_index 非有限数值 (" << sval.dump()
                         << ")，已使用默认周期 " << def_period);
                return def_period;
            }
            int s = static_cast<int>(std::round(sv));
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
    if (!std::isfinite(v)) {
        LOG_WARN("方案 '" << pname << "' 的 brightness 字段非有限数值 (" << bval.dump()
                 << ")，已使用默认值 255");
        return 255;
    }
    if (v < 0.0) {
        LOG_WARN("方案 '" << pname << "' 的 brightness 小于 0 (" << v << ")，已被钳制为 0");
        return 0;
    }
    // 浮点比例 0.0–1.0 (容忍浮点计算与序列化微小抖动至 1.0001)
    if (v <= 1.0001) {
        double clamped_ratio = std::clamp(v, 0.0, 1.0);
        return static_cast<uint8_t>(std::clamp(static_cast<int>(std::round(clamped_ratio * 255.0)), 0, 255));
    }
    if (v > 255.0) {
        LOG_WARN("方案 '" << pname << "' 的 brightness 超过 255 (" << v << ")，已被钳制为 255");
        return 255;
    }
    return static_cast<uint8_t>(std::clamp(static_cast<int>(std::round(v)), 0, 255));
}

} // namespace

std::shared_ptr<Effect> CreateEffectFromProfile(const std::string& pname, const nlohmann::json& pval) {
    std::string type = pval.value("type", "static");
    if (type == "static") {
        ColorRGB col = ParseColor(pval, "color", "color1", ColorRGB(0, 80, 200));
        bool analog = pval.value("analog", false);
        return std::make_shared<StaticEffect>(col, analog);
    } else if (type == "breathing") {
        ColorRGB c1 = ParseColor(pval, "color1", "color", ColorRGB(0, 100, 255));
        ColorRGB c2 = ParseColor(pval, "color2", "", ColorRGB(0, 10, 50));
        uint64_t period = ParseAndClampPeriod(pname, pval, 3000);
        return std::make_shared<BreathingEffect>(c1, c2, period);
    } else if (type == "color_cycle") {
        uint64_t period = ParseAndClampPeriod(pname, pval, 3500);
        return std::make_shared<ColorCycleEffect>(period);
    } else if (type == "wave") {
        uint64_t period = ParseAndClampPeriod(pname, pval, 3500);
        std::string dir = pval.value("direction", "diag_dl");
        double thickness = ParseAndClampThickness(pname, pval, 1.0);
        return std::make_shared<WaveEffect>(period, dir, thickness);
    } else if (type == "custom_keymap") {
        ColorRGB bg = ParseBgColor(pval, ColorRGB(0, 0, 0));
        return std::make_shared<CustomKeymapEffect>(bg);
    } else if (type == "reactive") {
        ColorRGB bg = ParseBgColor(pval, ColorRGB(0, 5, 15));
        ColorRGB c1 = ParseColor(pval, "color", "color1", ColorRGB(255, 25, 41));
        uint64_t period = ParseAndClampPeriod(pname, pval, 2500);
        return std::make_shared<ReactiveEffect>(bg, c1, period);
    } else if (type == "ripple") {
        ColorRGB bg = ParseBgColor(pval, ColorRGB(0, 5, 15));
        ColorRGB c1 = ParseColor(pval, "color", "color1", ColorRGB(0, 240, 255));
        uint64_t period = ParseAndClampPeriod(pname, pval, 2500);
        double thickness = ParseAndClampThickness(pname, pval, 1.0);
        return std::make_shared<RippleEffect>(bg, c1, period, thickness);
    } else if (type == "starry_night") {
        ColorRGB col = ParseColor(pval, "color", "color1", ColorRGB(0, 240, 255));
        bool random_colors = pval.value("random_colors", false);
        uint64_t period = ParseAndClampPeriod(pname, pval, 2500);
        return std::make_shared<StarryNightEffect>(col, random_colors, period);
    } else if (type == "quicksand") {
        ColorRGB c1 = ParseColor(pval, "color1", "color", ColorRGB(255, 25, 41));
        ColorRGB c2 = ParseColor(pval, "color2", "", ColorRGB(20, 138, 196));
        uint64_t period = ParseAndClampPeriod(pname, pval, 3500);
        std::string dir = pval.value("direction", "diag_dl");
        double thickness = ParseAndClampThickness(pname, pval, 1.0);
        return std::make_shared<QuicksandEffect>(c1, c2, period, dir, thickness);
    } else if (type == "current") {
        ColorRGB col = ParseColor(pval, "color", "color1", ColorRGB(0, 240, 255));
        uint64_t period = ParseAndClampPeriod(pname, pval, 2000);
        double thickness = ParseAndClampThickness(pname, pval, 1.0);
        return std::make_shared<CurrentEffect>(col, period, thickness);
    } else if (type == "raindrop") {
        ColorRGB col = ParseColor(pval, "color", "color1", ColorRGB(0, 240, 255));
        uint64_t period = ParseAndClampPeriod(pname, pval, 2500);
        return std::make_shared<RaindropEffect>(col, period);
    } else if (type == "plugin") {
        std::string plugin_name = pval.value("plugin_name", pval.value("plugin", pval.value("effect", pval.value("effect_name", pval.value("plugin_path", "")))));
        if (plugin_name.empty()) {
            LOG_ERROR("方案 '" << pname << "' (type: plugin) 缺少 plugin_name 字段");
            return nullptr;
        }
        auto eff = PluginManager::Instance().CreateEffect(plugin_name);
        if (!eff) {
            eff = PluginManager::Instance().LoadPlugin(plugin_name);
        }
        if (!eff) {
            LOG_WARN("方案 '" << pname << "' 引用的插件 '" << plugin_name << "' 暂未就绪，使用静默占位效果");
            return std::make_shared<StaticEffect>(ColorRGB(0, 0, 0));
        }
        return eff;
    } else {
        LOG_ERROR("方案 '" << pname << "' 配置了未知的效果类型: '" << type
                  << "' (支持的有效类型: static, breathing, color_cycle, wave, custom_keymap, reactive, ripple, starry_night, quicksand, current, raindrop, plugin)");
        return nullptr;
    }
}

std::string ProfilePluginName(const Profile& profile) {
    if (profile.effect_recipe.empty()) return profile.plugin_name;
    const auto recipe = nlohmann::json::parse(profile.effect_recipe);
    if (recipe.value("type", "static") != "plugin") return "";
    return recipe.value("plugin_name", recipe.value("plugin", recipe.value("effect",
        recipe.value("effect_name", recipe.value("plugin_path", "")))));
}
std::string ProfileEffectIdentity(const Profile& profile) {
    if (profile.effect_recipe.empty()) return profile.name;
    auto recipe = nlohmann::json::parse(profile.effect_recipe);
    for (const auto* key : {"brightness", "fps", "keys", "title", "name", "description"}) recipe.erase(key);
    return recipe.dump();
}
TriggeredEffectInstance CreateProfileEffectInstance(const Profile& profile) {
    if (profile.effect_recipe.empty()) return {};
    const auto recipe = nlohmann::json::parse(profile.effect_recipe);
    if (recipe.value("type", "static") == "plugin") {
        const auto id = ProfilePluginName(profile);
        return id.empty() ? TriggeredEffectInstance{} : PluginManager::Instance().CreateEffectInstance(id, true);
    }
    return TriggeredEffectInstance::FromHostEffect(CreateEffectFromProfile(profile.name, recipe));
}
TriggeredEffectInstance ResolveAutomationEffect(const nlohmann::json& reference, const RuleEngine& rules) {
    const auto kind = reference.at("kind").get<std::string>();
    const auto name = reference.at("name").get<std::string>();
    if (kind == "plugin") return PluginManager::Instance().CreateEffectInstance(name, true);
    if (kind != "profile_effect") return {};
    const auto profile = rules.GetProfile(name);
    return profile ? CreateProfileEffectInstance(*profile) : TriggeredEffectInstance{};
}
TriggeredEffectInstance PreparedEffectSource::Create() const {
    if (generation_) return PluginManager::CreateFromGeneration(generation_);
    if (recipe_.empty()) return {};
    return TriggeredEffectInstance::FromHostEffect(CreateEffectFromProfile("queued",nlohmann::json::parse(recipe_)));
}
PreparedEffectSource PrepareAutomationEffect(const nlohmann::json& reference, const RuleEngine& rules) {
    const auto name=reference.at("name").get<std::string>();
    if (reference.at("kind")=="plugin") return PreparedEffectSource::Plugin(PluginManager::Instance().PrepareEffectGeneration(name));
    const auto profile=rules.GetProfile(name);
    if (!profile || profile->effect_recipe.empty()) return {};
    const auto plugin=ProfilePluginName(*profile);
    if (nlohmann::json::parse(profile->effect_recipe).value("type","static")=="plugin")
        return plugin.empty()?PreparedEffectSource{}:PreparedEffectSource::Plugin(PluginManager::Instance().PrepareEffectGeneration(plugin));
    return PreparedEffectSource::Recipe(ProfileEffectIdentity(*profile));
}

std::string RuleEngine::ToLower(const std::string& s) {
    std::string res = s;
    std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return res;
}

void RuleEngine::ValidateAuthoringReferences(const AutomationRule& rule, const nlohmann::json& profiles) {
    ValidateAutomationReferences(rule,[&](const std::string& name){return profiles.is_object() && profiles.contains(name) && profiles[name].is_object();});
    const auto& action=rule.action;
    if(action.at("type")!="trigger_effect") return;
    const auto& reference=action.at("effect");
    std::string plugin;
    if(reference.at("kind")=="plugin") plugin=reference.at("name").get<std::string>();
    else {
        Profile profile; profile.effect_recipe=profiles.at(reference.at("name").get<std::string>()).dump();
        plugin=ProfilePluginName(profile);
        if(nlohmann::json::parse(profile.effect_recipe).value("type",std::string{})=="plugin" && plugin.empty())
            throw std::runtime_error("Referenced plugin is unpublished or unavailable");
    }
    // Same immutable generation registry used by the runtime resolver, no factory
    // or plugin lifecycle calls on HTTP threads.
    if(!plugin.empty() && !PluginManager::Instance().GetGeneration(plugin))
        throw std::runtime_error("Referenced plugin '"+plugin+"' is unpublished or unavailable");
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

        int new_fps = 25;
        if (j.contains("fps") && j["fps"].is_number()) {
            new_fps = std::clamp(j["fps"].get<int>(), 10, 100);
        }

        HardwareBackend new_backend = HardwareBackend::Auto;
        if (j.contains("hardware_backend")) {
            if (!j["hardware_backend"].is_string()) {
                LOG_ERROR("配置文件中 'hardware_backend' 字段必须为字符串: " << config_path);
                valid = false;
            } else {
                std::string backend_str = j["hardware_backend"].get<std::string>();
                if (!TryParseHardwareBackend(backend_str, new_backend)) {
                    LOG_ERROR("配置文件中 'hardware_backend' 包含未知或不支持的值: '" << backend_str
                              << "' (支持的有效值: auto, native_hid, native, hid, legacy_hal, legacy, hal): " << config_path);
                    valid = false;
                }
            }
        }

        if (j.contains("default_profile")) {
            if (j["default_profile"].is_string()) {
                def_name = j["default_profile"].get<std::string>();
            } else {
                LOG_ERROR("默认方案字段 'default_profile' 必须为字符串: " << config_path);
                valid = false;
            }
        }

        ValidateAutomationContainers(j);
        std::vector<RulePlanEntry> new_plan;
        std::set<std::string> v2_ids;
        uint64_t new_freshness = 3000;
        std::string new_fallback;
        if (j.contains("orchestration")) {
            const auto& orch = j["orchestration"];
            if (orch.contains("automation_freshness_ms")) {
                const auto& freshness = orch["automation_freshness_ms"];
                if (!freshness.is_number_integer() || freshness.get<int64_t>() <= 0)
                    throw std::runtime_error("automation_freshness_ms must be a positive integer");
                new_freshness = freshness.get<uint64_t>();
            }
            new_fallback = orch.value("fallback_profile", std::string{});
            if (orch.contains("rules")) for (const auto& item : orch["rules"]) {
                RulePlanEntry entry;
                entry.config_order = new_plan.size();
                entry.automation = ParseAutomationRule(item);
                if (!v2_ids.insert(entry.automation.id).second) throw std::runtime_error("duplicate v2 id");
                new_plan.push_back(std::move(entry));
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
                prof->effect_recipe = pval.dump();
                prof->plugin_name = pval.value("plugin_name", "");
                prof->brightness = ParseBrightness(pname, pval);
                int prof_fps = new_fps;
                if (pval.contains("fps") && pval["fps"].is_number()) {
                    prof_fps = std::clamp(pval["fps"].get<int>(), 10, 100);
                }
                prof->fps = prof_fps;

                auto effect = CreateEffectFromProfile(pname, pval);
                if (!effect) {
                    valid = false;
                }
                prof->base_effect = effect;

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

        // Resolve config-local v2 references against the complete candidate, never
        // the previous live profiles. Validate disabled rules too. Plugin generation
        // resolution belongs to Stage 3; this pass must not load/instantiate plugins.
        for (const auto& entry : new_plan) {
            const auto& rule = entry.automation;
            try {
                ValidateAutomationReferences(rule, [&](const std::string& name) { return new_profiles.count(name) != 0; });
            } catch (const std::exception& error) {
                LOG_ERROR("[Automation] Rule '" << rule.id << "': " << error.what());
                valid = false;
            }
        }

        // 2. 默认方案完整性校验
        if (new_profiles.find(def_name) == new_profiles.end()) {
            LOG_ERROR("默认方案 default_profile '" << def_name
                      << "' 未在 profiles 中定义，请检查拼写或在 profiles 中添加该方案定义");
            valid = false;
        }

        // 6. 编排兜底方案 (orchestration.fallback_profile) 校验
        if (!new_fallback.empty()) {
            if (new_profiles.find(new_fallback) == new_profiles.end()) {
                LOG_ERROR("编排兜底方案 fallback_profile '" << new_fallback
                          << "' 未在 profiles 中定义");
                valid = false;
            }
        }

        if (!valid) {
            record_failure_ft();
            return false;
        }

        const size_t rules_count = new_plan.size();
        const size_t profiles_count = new_profiles.size();

        // Apply under lock
        {
            std::lock_guard<std::mutex> lock(mutex_);
            config_path_ = config_path;
            default_profile_name_ = def_name;
            target_fps_ = new_fps;
            hardware_backend_ = new_backend;
            fallback_profile_ = std::move(new_fallback);
            profiles_ = std::move(new_profiles);
            // Preserve unchanged rule memory; edited/enabled records seed on next decision.
            if (automation_freshness_ms_ != new_freshness) edge_memory_.clear();
            for (auto it = edge_memory_.begin(); it != edge_memory_.end();) {
                const auto found = std::find_if(new_plan.begin(), new_plan.end(), [&](const RulePlanEntry& entry) {
                    return entry.automation.id == it->first &&
                        entry.automation.enabled && entry.automation.fingerprint == it->second.fingerprint;
                });
                if (found == new_plan.end()) it = edge_memory_.erase(it); else ++it;
            }
            plan_ = std::move(new_plan);
            ++config_generation_;
            automation_freshness_ms_ = new_freshness;
            last_write_time_ = file_ft;
        }

        LOG_INFO("成功加载配置文件: " << config_path << " (默认方案: " << def_name
                 << ", 规则数: " << rules_count
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
    const auto input = AutomationInputSnapshot::Capture(process_name,
        gsi_state ? gsi_state->GetAutomationTelemetry() : nullptr, AutomationMonotonicMs(), automation_freshness_ms_);
    return EvaluateProfilesLocked(input).profile;
}

bool RuleEngine::ShouldSuppressWebUi(const std::string& process_name, const GsiState* gsi) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto input = AutomationInputSnapshot::Capture(process_name,
        gsi ? gsi->GetAutomationTelemetry() : nullptr, AutomationMonotonicMs(), automation_freshness_ms_);
    return EvaluateProfilesLocked(input).suppress_web_ui;
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
