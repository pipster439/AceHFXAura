#include "config/rule_engine.h"
#include "config/lighting_service.h"
#include "engine/builtin_effects.h"
#include "engine/plugin_manager.h"
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

ConditionNode ConditionNode::FromJson(const nlohmann::json& j) {
    ConditionNode node;
    if (!j.is_object()) return node;

    std::string logic_str;
    if (j.contains("type") && j["type"].is_string()) {
        logic_str = j["type"].get<std::string>();
    } else if (j.contains("op") && j["op"].is_string() && (j.contains("conditions") || j.contains("condition"))) {
        logic_str = j["op"].get<std::string>();
    }
    if (!logic_str.empty()) {
        node.logic_op = StringToLogicOp(logic_str);
        if (node.logic_op != LogicOp::None) {
            if (j.contains("conditions") && j["conditions"].is_array()) {
                for (const auto& item : j["conditions"]) {
                    node.children.push_back(FromJson(item));
                }
            } else if (j.contains("condition")) {
                node.children.push_back(FromJson(j["condition"]));
            }
            return node;
        }
    }

    if (j.contains("and") && j["and"].is_array()) {
        node.logic_op = LogicOp::And;
        for (const auto& item : j["and"]) {
            node.children.push_back(FromJson(item));
        }
        return node;
    }
    if (j.contains("or") && j["or"].is_array()) {
        node.logic_op = LogicOp::Or;
        for (const auto& item : j["or"]) {
            node.children.push_back(FromJson(item));
        }
        return node;
    }
    if (j.contains("not")) {
        node.logic_op = LogicOp::Not;
        node.children.push_back(FromJson(j["not"]));
        return node;
    }

    node.logic_op = LogicOp::None;
    node.field = j.value("field", "");
    std::string op_str = j.value("op", j.value("operator", "=="));
    node.comp_op = StringToCompareOp(op_str);
    if (j.contains("value")) {
        node.target_value = j["value"];
    } else if (j.contains("target_value")) {
        node.target_value = j["target_value"];
    }
    return node;
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

bool ConditionNode::Evaluate(const GsiState* gsi, const std::string& foreground_proc) const {
    if (logic_op == LogicOp::And) {
        if (children.empty()) return true;
        for (const auto& child : children) {
            if (!child.Evaluate(gsi, foreground_proc)) return false;
        }
        return true;
    }
    if (logic_op == LogicOp::Or) {
        if (children.empty()) return false;
        for (const auto& child : children) {
            if (child.Evaluate(gsi, foreground_proc)) return true;
        }
        return false;
    }
    if (logic_op == LogicOp::Not) {
        if (children.empty()) return false;
        return !children[0].Evaluate(gsi, foreground_proc);
    }

    // Leaf node
    if (field.empty()) return true;

    std::string lower_field = RuleEngine::ToLower(field);
    if (lower_field == "process.name" || lower_field == "process" || lower_field == "process_name") {
        std::string proc = RuleEngine::ToLower(foreground_proc);
        std::string target = target_value.is_string() ? RuleEngine::ToLower(target_value.get<std::string>()) : "";
        bool matches = (proc == target);
        if (!matches && target.size() > 4 && target.substr(target.size() - 4) == ".exe") {
            matches = (proc == target.substr(0, target.size() - 4));
        }
        if (!matches && proc.size() > 4 && proc.substr(proc.size() - 4) == ".exe") {
            matches = (proc.substr(0, proc.size() - 4) == target);
        }

        if (comp_op == CompareOp::Eq) return matches;
        if (comp_op == CompareOp::Ne) return !matches;
        return false;
    }

    if (!gsi) return false;
    std::string op_str = CompareOpToString(comp_op);
    return gsi->Evaluate(field, op_str, target_value);
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

        OrchestrationConfig new_orchestration;
        std::vector<RulePlanEntry> new_plan;
        std::set<std::string> v2_ids;
        uint64_t new_freshness = 3000;
        if (j.contains("orchestration") && j["orchestration"].is_object()) {
            const auto& orch = j["orchestration"];
            if (orch.contains("automation_freshness_ms")) {
                const auto& freshness = orch["automation_freshness_ms"];
                if (!freshness.is_number_integer() || freshness.get<int64_t>() <= 0)
                    throw std::runtime_error("automation_freshness_ms must be a positive integer");
                new_freshness = freshness.get<uint64_t>();
            }
            if (orch.contains("fallback_profile") && orch["fallback_profile"].is_string()) {
                new_orchestration.fallback_profile = orch["fallback_profile"].get<std::string>();
            }
            if (orch.contains("rules") && orch["rules"].is_array()) {
                for (const auto& item : orch["rules"]) {
                    if (!item.is_object()) {
                        LOG_ERROR("编排规则项必须为 JSON 对象: " << item.dump());
                        valid = false;
                        continue;
                    }
                    if (item.contains("model") && item["model"].is_string() && item["model"] == "automation_v2") {
                        RulePlanEntry entry;
                        entry.provenance = RuleProvenance::AutomationV2;
                        entry.compatibility = CompatibilityPolicy::V2Snapshot;
                        entry.config_order = static_cast<size_t>(&item - &orch["rules"][0]);
                        entry.automation = ParseAutomationRule(item);
                        if (!v2_ids.insert(entry.automation.id).second) throw std::runtime_error("duplicate v2 id");
                        new_plan.push_back(std::move(entry));
                        continue;
                    }
                    if (!item.value("enabled", true)) continue;
                    OrchestrationRule r;
                    r.id = item.value("id", "");
                    r.name = item.value("name", "");
                    r.process = ToLower(item.value("process", ""));
                    r.dnd = item.value("dnd", item.value("suppress_web_ui", false));
                    r.target_profile = item.value("target_profile", item.value("profile", ""));
                    if (item.contains("condition")) {
                        r.condition = ConditionNode::FromJson(item["condition"]);
                    }
                    if (r.target_profile.empty()) {
                        LOG_ERROR("编排规则缺少有效的 target_profile / profile 字段: " << item.dump());
                        valid = false;
                        continue;
                    }
                    new_orchestration.rules.push_back(r);
                    RulePlanEntry entry;
                    entry.provenance = RuleProvenance::Orchestration;
                    entry.compatibility = CompatibilityPolicy::LegacyBoolean;
                    entry.config_order = static_cast<size_t>(&item - &orch["rules"][0]);
                    entry.orchestration = r;
                    new_plan.push_back(std::move(entry));
                }
            }
            if (orch.contains("event_overlays") && orch["event_overlays"].is_array()) {
                for (const auto& item : orch["event_overlays"]) {
                    if (!item.is_object()) {
                        LOG_ERROR("事件覆盖规则项必须为 JSON 对象: " << item.dump());
                        valid = false;
                        continue;
                    }
                    EventOverlayRule ev;
                    ev.id = item.value("id", "overlay_" + std::to_string(new_orchestration.event_overlays.size()));
                    ev.condition = ConditionNode::FromJson(item.value("condition", nlohmann::json::object()));
                    ev.trigger = item.value("trigger", "event");
                    ev.priority = item.value("priority", 10);
                    ev.event = item.value("event", "");
                    ev.name = item.value("name", "");
                    ev.effect = item.value("effect", item.value("profile", ""));
                    ev.duration_ms = item.value("duration_ms", 1200ULL);
                    ev.fade_out_ms = item.value("fade_ms", item.value("fade_out_ms", 400ULL));
                    ev.attack_ms = item.value("attack_ms", 0ULL);
                    ev.blend_mode = item.value("blend_mode", "blend");
                    if (ev.event.empty() && ev.trigger != "state") {
                        LOG_ERROR("事件覆盖规则缺少有效的 event 字段: " << item.dump());
                        valid = false;
                        continue;
                    }
                    if (ev.effect.empty()) {
                        LOG_ERROR("事件覆盖规则缺少有效的 effect / profile 字段: " << item.dump());
                        valid = false;
                        continue;
                    }
                    new_orchestration.event_overlays.push_back(ev);
                }
            }
        }

        // One executable plan; legacy vectors below remain compatibility/introspection data only.
        for (size_t i = 0; i < new_gsi_bindings.size(); ++i) {
            RulePlanEntry e; e.provenance = RuleProvenance::GsiBinding;
            e.compatibility = CompatibilityPolicy::LegacyCs2Binding; e.config_order = i;
            e.binding = new_gsi_bindings[i]; new_plan.push_back(std::move(e));
        }
        for (size_t i = 0; i < new_rules.size(); ++i) {
            RulePlanEntry e; e.provenance = RuleProvenance::Application;
            e.compatibility = CompatibilityPolicy::LegacyApplication; e.config_order = i;
            e.application = new_rules[i]; new_plan.push_back(std::move(e));
        }
        for (size_t i = 0; i < new_orchestration.event_overlays.size(); ++i) {
            RulePlanEntry e; e.provenance = RuleProvenance::EventOverlay;
            e.compatibility = CompatibilityPolicy::LegacyOverlayExecutor; e.config_order = i;
            e.overlay = new_orchestration.event_overlays[i]; new_plan.push_back(std::move(e));
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
            if (entry.provenance != RuleProvenance::AutomationV2) continue;
            const auto& rule = entry.automation;
            const auto& action = rule.action;
            std::string profile_name;
            if (action.at("type") == "activate_profile") {
                profile_name = action.at("profile").get<std::string>();
            } else if (action.at("effect").at("kind") == "profile_effect") {
                profile_name = action.at("effect").at("name").get<std::string>();
            } else {
                continue; // plugin reference: generation resolution is deferred
            }
            if (new_profiles.find(profile_name) == new_profiles.end()) {
                LOG_ERROR("[Automation] Rule '" << rule.id << "' references missing candidate profile '" << profile_name << "'");
                valid = false;
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

        // 5. 编排规则 (orchestration.rules) 方案引用完整性校验
        for (const auto& rule : new_orchestration.rules) {
            if (new_profiles.find(rule.target_profile) == new_profiles.end()) {
                LOG_ERROR("编排规则 (ID: '" << rule.id << "') 引用了未定义的方案: '" << rule.target_profile 
                          << "'，请在 profiles 中定义方案 '" << rule.target_profile << "'");
                valid = false;
            }
        }

        // 6. 编排兜底方案 (orchestration.fallback_profile) 校验
        if (!new_orchestration.fallback_profile.empty()) {
            if (new_profiles.find(new_orchestration.fallback_profile) == new_profiles.end()) {
                LOG_ERROR("编排兜底方案 fallback_profile '" << new_orchestration.fallback_profile 
                          << "' 未在 profiles 中定义");
                valid = false;
            }
        }

        if (!valid) {
            record_failure_ft();
            return false;
        }

        const size_t rules_count = new_rules.size();
        const size_t bindings_count = new_gsi_bindings.size();
        const size_t orch_rules_count = new_orchestration.rules.size();
        const size_t profiles_count = new_profiles.size();

        // Apply under lock
        {
            std::lock_guard<std::mutex> lock(mutex_);
            config_path_ = config_path;
            default_profile_name_ = def_name;
            target_fps_ = new_fps;
            hardware_backend_ = new_backend;
            rules_ = std::move(new_rules);
            gsi_bindings_ = std::move(new_gsi_bindings);
            orchestration_ = std::move(new_orchestration);
            profiles_ = std::move(new_profiles);
            // Preserve unchanged rule memory; edited/enabled records seed on next decision.
            if (automation_freshness_ms_ != new_freshness) edge_memory_.clear();
            for (auto it = edge_memory_.begin(); it != edge_memory_.end();) {
                if (!v2_ids.count(it->first)) it = edge_memory_.erase(it); else ++it;
            }
            plan_ = std::move(new_plan);
            automation_freshness_ms_ = new_freshness;
            last_write_time_ = file_ft;
        }

        LOG_INFO("成功加载配置文件: " << config_path << " (默认方案: " << def_name 
                 << ", 规则数: " << rules_count 
                 << ", GSI绑定数: " << bindings_count 
                 << ", 编排规则数: " << orch_rules_count
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
    return EvaluateProfilesLocked(process_name, gsi_state, input).profile;
}

bool RuleEngine::ShouldSuppressWebUi(const std::string& process_name, const GsiState* gsi) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto input = AutomationInputSnapshot::Capture(process_name,
        gsi ? gsi->GetAutomationTelemetry() : nullptr, AutomationMonotonicMs(), automation_freshness_ms_);
    return EvaluateProfilesLocked(process_name, gsi, input).suppress_web_ui;
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
