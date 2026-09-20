#include "config/lighting_service.h"
#include "utils/logger.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

namespace aura {


// ========================================================
// 64-bit FNV-1a 哈希与版本计算
// ========================================================
uint64_t ComputeFnv1a64(const void* data, size_t len) {
    const auto* ptr = static_cast<const uint8_t*>(data);
    uint64_t hash = 14695981039346656037ULL; // FNV_offset_basis
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint64_t>(ptr[i]);
        hash *= 1099511628211ULL; // FNV_prime
    }
    return hash;
}

std::string FormatFnv1aHex(uint64_t hash) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(hash));
    return std::string(buf);
}

std::string ComputeFileRevision(const std::string& content) {
    uint64_t h = ComputeFnv1a64(content.data(), content.size());
    return FormatFnv1aHex(h);
}

// ========================================================
// 共享有效参数解析 (与 RuleEngine 统一事实来源)
// ========================================================
ColorRGB ParseColorFromArray(const nlohmann::json& arr, const ColorRGB& def) {
    if (!arr.is_array() || arr.size() < 3) return def;
    if (!arr[0].is_number() || !arr[1].is_number() || !arr[2].is_number()) return def;
    double r = arr[0].get<double>();
    double g = arr[1].get<double>();
    double b = arr[2].get<double>();
    if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b)) return def;
    return ColorRGB(
        static_cast<uint8_t>(std::clamp(static_cast<int>(std::round(r)), 0, 255)),
        static_cast<uint8_t>(std::clamp(static_cast<int>(std::round(g)), 0, 255)),
        static_cast<uint8_t>(std::clamp(static_cast<int>(std::round(b)), 0, 255))
    );
}

ColorRGB ParseColor(const nlohmann::json& pval, const std::string& primary_key, const std::string& fallback_key, const ColorRGB& def_color) {
    if (pval.contains(primary_key)) {
        return ParseColorFromArray(pval[primary_key], def_color);
    }
    if (!fallback_key.empty() && pval.contains(fallback_key)) {
        return ParseColorFromArray(pval[fallback_key], def_color);
    }
    return def_color;
}

ColorRGB ParseBgColor(const nlohmann::json& pval, const ColorRGB& def_bg) {
    if (pval.contains("bg")) {
        return ParseColorFromArray(pval["bg"], def_bg);
    }
    if (pval.contains("background")) {
        return ParseColorFromArray(pval["background"], def_bg);
    }
    return def_bg;
}

double ParseAndClampThickness(const std::string& pname, const nlohmann::json& pval, double def_val) {
    if (!pval.contains("thickness")) return def_val;
    const auto& tv = pval["thickness"];
    if (!tv.is_number()) {
        if (!pname.empty()) {
            LOG_WARN("方案 '" << pname << "' 的 thickness 字段非数值 (" << tv.dump() << ")，已使用默认值 " << def_val);
        }
        return def_val;
    }
    double v = tv.get<double>();
    if (!std::isfinite(v)) {
        if (!pname.empty()) {
            LOG_WARN("方案 '" << pname << "' 的 thickness 非有限数值 (" << tv.dump() << ")，已使用默认值 " << def_val);
        }
        return def_val;
    }
    if (v < 0.1) {
        if (!pname.empty()) {
            LOG_WARN("方案 '" << pname << "' 的 thickness 小于 0.1 (" << v << ")，已被钳制为 0.1");
        }
        return 0.1;
    }
    if (v > 5.0) {
        if (!pname.empty()) {
            LOG_WARN("方案 '" << pname << "' 的 thickness 超过 5.0 (" << v << ")，已被钳制为 5.0");
        }
        return 5.0;
    }
    return v;
}

double ParseAndClampThickness(const nlohmann::json& pval, double def_val) {
    return ParseAndClampThickness("", pval, def_val);
}

nlohmann::json ResolveEffectiveParameters(const std::string& effect_type, const nlohmann::json& pval, const std::string& pname) {
    nlohmann::json params = nlohmann::json::object();
    if (effect_type == "static") {
        ColorRGB col = ParseColor(pval, "color", "color1", ColorRGB(0, 80, 200));
        params["color"] = nlohmann::json::array({col.r, col.g, col.b});
        params["analog"] = pval.value("analog", false);
    } else if (effect_type == "breathing") {
        ColorRGB c1 = ParseColor(pval, "color1", "color", ColorRGB(0, 100, 255));
        ColorRGB c2 = ParseColor(pval, "color2", "", ColorRGB(0, 10, 50));
        params["color1"] = nlohmann::json::array({c1.r, c1.g, c1.b});
        params["color2"] = nlohmann::json::array({c2.r, c2.g, c2.b});
    } else if (effect_type == "color_cycle") {
        // 无额外参数
    } else if (effect_type == "wave") {
        std::string dir = pval.value("direction", "diag_dl");
        static const std::unordered_set<std::string> kDirs = {
            "diag_dl", "diag_dr", "diag_ul", "diag_ur", "left", "right", "up", "down", "spread"
        };
        if (kDirs.find(dir) == kDirs.end()) {
            dir = "diag_dl";
        }
        params["direction"] = dir;
        params["thickness"] = ParseAndClampThickness(pname, pval, 1.0);
    } else if (effect_type == "reactive") {
        ColorRGB bg = ParseBgColor(pval, ColorRGB(0, 5, 15));
        ColorRGB c1 = ParseColor(pval, "color", "color1", ColorRGB(255, 25, 41));
        params["bg"] = nlohmann::json::array({bg.r, bg.g, bg.b});
        params["color"] = nlohmann::json::array({c1.r, c1.g, c1.b});
    } else if (effect_type == "ripple") {
        ColorRGB bg = ParseBgColor(pval, ColorRGB(0, 0, 0));
        ColorRGB c1 = ParseColor(pval, "color", "color1", ColorRGB(0, 240, 255));
        params["bg"] = nlohmann::json::array({bg.r, bg.g, bg.b});
        params["color"] = nlohmann::json::array({c1.r, c1.g, c1.b});
        params["thickness"] = ParseAndClampThickness(pname, pval, 1.0);
    } else if (effect_type == "starry_night") {
        ColorRGB col = ParseColor(pval, "color", "color1", ColorRGB(0, 240, 255));
        params["color"] = nlohmann::json::array({col.r, col.g, col.b});
        params["random_colors"] = pval.value("random_colors", false);
    } else if (effect_type == "quicksand") {
        ColorRGB c1 = ParseColor(pval, "color1", "color", ColorRGB(255, 25, 41));
        ColorRGB c2 = ParseColor(pval, "color2", "", ColorRGB(20, 138, 196));
        params["color1"] = nlohmann::json::array({c1.r, c1.g, c1.b});
        params["color2"] = nlohmann::json::array({c2.r, c2.g, c2.b});
        std::string dir = pval.value("direction", "diag_dl");
        static const std::unordered_set<std::string> kDirs = {
            "diag_dl", "diag_dr", "diag_ul", "diag_ur", "left", "right", "up", "down", "spread"
        };
        if (kDirs.find(dir) == kDirs.end()) {
            dir = "diag_dl";
        }
        params["direction"] = dir;
        params["thickness"] = ParseAndClampThickness(pname, pval, 1.0);
    } else if (effect_type == "current") {
        ColorRGB col = ParseColor(pval, "color", "color1", ColorRGB(0, 240, 255));
        params["color"] = nlohmann::json::array({col.r, col.g, col.b});
        params["thickness"] = ParseAndClampThickness(pname, pval, 1.0);
    } else if (effect_type == "raindrop") {
        ColorRGB col = ParseColor(pval, "color", "color1", ColorRGB(0, 240, 255));
        params["color"] = nlohmann::json::array({col.r, col.g, col.b});
    }
    return params;
}

static const std::vector<EffectParamOption> kDirectionOptions = {
    {"diag_dl", "左下对角 (Diagonal Down-Left)"},
    {"diag_dr", "右下对角 (Diagonal Down-Right)"},
    {"diag_ul", "左上对角 (Diagonal Up-Left)"},
    {"diag_ur", "右上对角 (Diagonal Up-Right)"},
    {"left", "向左 (Left)"},
    {"right", "向右 (Right)"},
    {"up", "向上 (Up)"},
    {"down", "向下 (Down)"},
    {"spread", "居中扩散 (Spread)"}
};

// ========================================================
// 内建预设 (Builtin Lighting Preset) Catalog 规范与单一事实来源
// ========================================================
const std::vector<BuiltinLightingPreset>& GetBuiltinLightingPresets() {
    static const std::vector<BuiltinLightingPreset> kPresets = {
        {
            "static",
            "Static",
            "static",
            false,
            0,
            {
                {"type", "static"},
                {"color", {0, 80, 200}},
                {"analog", true}
            },
            {
                {"color", "静态颜色", EffectParamType::Color, nlohmann::json::array({0, 80, 200})},
                {"analog", "模拟按压增强", EffectParamType::Boolean, true}
            }
        },
        {
            "breathing",
            "Breathing",
            "breathing",
            true,
            3000,
            {
                {"type", "breathing"},
                {"color1", {0, 100, 255}},
                {"color2", {0, 10, 50}},
                {"period_ms", 3000}
            },
            {
                {"color1", "主色彩 1", EffectParamType::Color, nlohmann::json::array({0, 100, 255})},
                {"color2", "主色彩 2", EffectParamType::Color, nlohmann::json::array({0, 10, 50})}
            }
        },
        {
            "color_cycle",
            "Color Cycle",
            "color_cycle",
            true,
            3500,
            {
                {"type", "color_cycle"},
                {"period_ms", 3500}
            },
            {}
        },
        {
            "wave",
            "Wave",
            "wave",
            true,
            3500,
            {
                {"type", "wave"},
                {"direction", "diag_dl"},
                {"thickness", 1.0},
                {"period_ms", 3500}
            },
            {
                {"direction", "波浪方向", EffectParamType::Enum, "diag_dl", 0, 0, 0, kDirectionOptions},
                {"thickness", "波浪粗细", EffectParamType::Number, 1.0, 0.1, 5.0, 0.1, {}}
            }
        },
        {
            "reactive",
            "Reactive",
            "reactive",
            true,
            2500,
            {
                {"type", "reactive"},
                {"bg", {0, 5, 15}},
                {"color", {255, 25, 41}},
                {"period_ms", 2500}
            },
            {
                {"bg", "背景底色", EffectParamType::Color, nlohmann::json::array({0, 5, 15})},
                {"color", "触发色彩", EffectParamType::Color, nlohmann::json::array({255, 25, 41})}
            }
        },
        {
            "ripple",
            "Ripple",
            "ripple",
            true,
            2500,
            {
                {"type", "ripple"},
                {"bg", {0, 0, 0}},
                {"color", {0, 240, 255}},
                {"thickness", 1.0},
                {"period_ms", 2500}
            },
            {
                {"bg", "背景底色", EffectParamType::Color, nlohmann::json::array({0, 0, 0})},
                {"color", "水波色彩", EffectParamType::Color, nlohmann::json::array({0, 240, 255})},
                {"thickness", "水波粗细", EffectParamType::Number, 1.0, 0.1, 5.0, 0.1, {}}
            }
        },
        {
            "starry_night",
            "Starry Night",
            "starry_night",
            true,
            2500,
            {
                {"type", "starry_night"},
                {"color", {0, 240, 255}},
                {"random_colors", true},
                {"period_ms", 2500}
            },
            {
                {"color", "繁星基色", EffectParamType::Color, nlohmann::json::array({0, 240, 255})},
                {"random_colors", "彩色繁星", EffectParamType::Boolean, true}
            }
        },
        {
            "quicksand",
            "Quicksand",
            "quicksand",
            true,
            3500,
            {
                {"type", "quicksand"},
                {"color1", {255, 25, 41}},
                {"color2", {20, 138, 196}},
                {"direction", "diag_dl"},
                {"thickness", 1.0},
                {"period_ms", 3500}
            },
            {
                {"color1", "流沙色彩 1", EffectParamType::Color, nlohmann::json::array({255, 25, 41})},
                {"color2", "流沙色彩 2", EffectParamType::Color, nlohmann::json::array({20, 138, 196})},
                {"direction", "涌动方向", EffectParamType::Enum, "diag_dl", 0, 0, 0, kDirectionOptions},
                {"thickness", "沙浪粗细", EffectParamType::Number, 1.0, 0.1, 5.0, 0.1, {}}
            }
        },
        {
            "current",
            "Current",
            "current",
            true,
            2000,
            {
                {"type", "current"},
                {"color", {0, 240, 255}},
                {"thickness", 1.0},
                {"period_ms", 2000}
            },
            {
                {"color", "电光底色", EffectParamType::Color, nlohmann::json::array({0, 240, 255})},
                {"thickness", "电光粗细", EffectParamType::Number, 1.0, 0.1, 5.0, 0.1, {}}
            }
        },
        {
            "raindrop",
            "Raindrop",
            "raindrop",
            true,
            2500,
            {
                {"type", "raindrop"},
                {"color", {0, 240, 255}},
                {"period_ms", 2500}
            },
            {
                {"color", "雨滴色彩", EffectParamType::Color, nlohmann::json::array({0, 240, 255})}
            }
        }
    };
    return kPresets;
}

const BuiltinLightingPreset* FindBuiltinLightingPreset(const std::string& id_or_type) {
    const auto& presets = GetBuiltinLightingPresets();
    for (const auto& p : presets) {
        if (p.id == id_or_type || p.effect_type == id_or_type) {
            return &p;
        }
    }
    return nullptr;
}

// ========================================================
// 动画周期共享元数据
// ========================================================
bool IsPeriodSupported(const std::string& effect_type) {
    const auto* p = FindBuiltinLightingPreset(effect_type);
    if (p != nullptr) {
        return p->supports_period;
    }
    return false;
}

uint64_t GetEffectDefaultPeriod(const std::string& effect_type) {
    const auto* p = FindBuiltinLightingPreset(effect_type);
    if (p != nullptr && p->supports_period) {
        return p->default_period_ms;
    }
    return 2500;
}

uint64_t ResolveEffectivePeriodMs(const std::string& effect_type, const nlohmann::json& pval, const std::string& /*pname*/) {
    if (!IsPeriodSupported(effect_type)) {
        return 0;
    }
    uint64_t def_period = GetEffectDefaultPeriod(effect_type);

    if (pval.contains("period_ms")) {
        const auto& period_val = pval["period_ms"];
        if (period_val.is_number_unsigned()) {
            uint64_t u = period_val.get<uint64_t>();
            if (u < 33) {
                return def_period < 33 ? 33 : def_period;
            }
            return u;
        } else if (period_val.is_number_integer()) {
            int64_t s = period_val.get<int64_t>();
            if (s >= 33) {
                return static_cast<uint64_t>(s);
            }
            return def_period < 33 ? 33 : def_period;
        }
        return def_period < 33 ? 33 : def_period;
    }

    if (pval.contains("speed_index")) {
        const auto& sval = pval["speed_index"];
        if (sval.is_number()) {
            double sv = sval.get<double>();
            if (std::isfinite(sv)) {
                int s = static_cast<int>(std::round(sv));
                if (s == 0) return 5500;
                if (s == 1) return 3200;
                if (s == 2) return 1600;
            }
        }
    }

    return def_period;
}

// ========================================================
// 亮度规范化
// ========================================================
double NormalizeBrightnessToRatio(const nlohmann::json& pval) {
    if (!pval.contains("brightness")) {
        return 1.0;
    }
    const auto& bval = pval["brightness"];
    if (!bval.is_number()) {
        return 1.0;
    }
    double v = bval.get<double>();
    if (!std::isfinite(v)) {
        return 1.0;
    }
    if (v <= 0.0) {
        return 0.0;
    }
    if (v <= 1.0001) {
        return std::clamp(v, 0.0, 1.0);
    }
    return std::clamp(v / 255.0, 0.0, 1.0);
}

// ========================================================
// LightingControlService 实现
// ========================================================
LightingControlService::LightingControlService(std::filesystem::path config_path)
    : config_path_(std::move(config_path)) {
    // 默认使用 Windows 原生原子替换 MoveFileExW
    file_replacer_ = [](const std::wstring& tmp, const std::wstring& target) {
        return MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    };
}

bool LightingControlService::ReadRawConfigFile(std::string& out_content, std::string& out_revision) const {
    std::ifstream file(config_path_, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    out_content.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    out_revision = ComputeFileRevision(out_content);
    return true;
}

LightingControlService::OpResult LightingControlService::GetProfileList(
    std::vector<ProfileSummary>& out_profiles, std::string& out_revision) {
    std::string content;
    if (!ReadRawConfigFile(content, out_revision)) {
        return {500, "Internal Error", "Failed to read configuration file", ""};
    }

    try {
        auto root = nlohmann::ordered_json::parse(content);
        if (!root.is_object() || !root.contains("profiles") || !root["profiles"].is_object()) {
            return {500, "Internal Error", "Configuration root missing 'profiles' object", ""};
        }

        for (auto& [pname, pval] : root["profiles"].items()) {
            if (pval.is_object()) {
                ProfileSummary ps;
                ps.name = pname;
                ps.type = pval.value("type", "static");
                out_profiles.push_back(std::move(ps));
            }
        }
        return {200, "", "", out_revision};
    } catch (const std::exception& e) {
        return {500, "Internal Error", std::string("JSON parsing error: ") + e.what(), ""};
    }
}

LightingControlService::OpResult LightingControlService::GetProfileDetail(
    const std::string& name, ProfileDetail& out_detail) {
    std::string content;
    std::string rev;
    if (!ReadRawConfigFile(content, rev)) {
        return {500, "Internal Error", "Failed to read configuration file", ""};
    }

    try {
        auto root = nlohmann::ordered_json::parse(content);
        if (!root.is_object() || !root.contains("profiles") || !root["profiles"].is_object()) {
            return {500, "Internal Error", "Configuration root missing 'profiles' object", ""};
        }

        if (!root["profiles"].contains(name) || !root["profiles"][name].is_object()) {
            return {404, "Not Found", "Profile not found: " + name, rev};
        }

        const auto& pval = root["profiles"][name];
        ProfileDetail d;
        d.name = name;
        d.type = pval.value("type", "static");
        d.brightness = NormalizeBrightnessToRatio(pval);

        int root_fps = 25;
        if (root.contains("fps") && root["fps"].is_number()) {
            root_fps = std::clamp(root["fps"].get<int>(), 10, 100);
        }

        if (pval.contains("fps") && pval["fps"].is_number()) {
            d.fps = std::clamp(pval["fps"].get<int>(), 10, 100);
            d.fps_inherited = false;
        } else {
            d.fps = root_fps;
            d.fps_inherited = true;
        }

        d.supports_period = IsPeriodSupported(d.type);
        if (d.supports_period) {
            d.has_period = true;
            d.period_ms = ResolveEffectivePeriodMs(d.type, pval, name);
        } else {
            d.has_period = false;
            d.period_ms = 0;
        }

        d.revision = rev;
        out_detail = d;
        return {200, "", "", rev};
    } catch (const std::exception& e) {
        return {500, "Internal Error", std::string("JSON parsing error: ") + e.what(), ""};
    }
}

LightingControlService::OpResult LightingControlService::UpdateProfile(
    const std::string& name, const PatchInput& patch, std::string& out_new_revision) {
    if (patch.expected_revision.empty()) {
        return {400, "Validation Error", "Missing required field 'expected_revision'", ""};
    }

    // 1. 获取跨进程 Windows Named Mutex 互斥锁
    NamedConfigLock lock(L"Local\\AceHFXAuraConfigWriteMutex", 5000);
    if (!lock.IsAcquired()) {
        return {500, "Internal Error", "Failed to acquire cross-process configuration lock", ""};
    }

    // 2. 锁内重新读取最新文件内容与版本
    std::string content;
    std::string current_rev;
    if (!ReadRawConfigFile(content, current_rev)) {
        return {500, "Internal Error", "Failed to read configuration file", ""};
    }

    // 3. 版本指纹比对 (FNV-1a 64-bit 严格一致性，防止竞态冲突与静默覆盖)
    if (current_rev != patch.expected_revision) {
        return {409, "Conflict", "Configuration has been modified externally", current_rev};
    }

    try {
        auto root = nlohmann::ordered_json::parse(content);
        if (!root.is_object() || !root.contains("profiles") || !root["profiles"].is_object()) {
            return {500, "Internal Error", "Configuration root missing 'profiles' object", current_rev};
        }

        if (!root["profiles"].contains(name) || !root["profiles"][name].is_object()) {
            return {404, "Not Found", "Profile not found: " + name, current_rev};
        }

        auto& prof = root["profiles"][name];
        std::string effect_type = prof.value("type", "static");
        bool supports_period = IsPeriodSupported(effect_type);

        // 4. 执行真正 Sparse Patch：仅修改显式提供的字段，保留其余所有既有字段
        if (patch.has_brightness) {
            if (!std::isfinite(patch.brightness) || patch.brightness < 0.0 || patch.brightness > 1.0) {
                return {400, "Validation Error", "Field 'brightness' must be a valid number in [0.0, 1.0]", current_rev};
            }
            prof["brightness"] = patch.brightness;
        }

        if (patch.has_fps) {
            if (patch.fps < 10 || patch.fps > 100) {
                return {400, "Validation Error", "Field 'fps' must be an integer between 10 and 100", current_rev};
            }
            prof["fps"] = patch.fps;
        }

        if (patch.has_period_ms) {
            if (!supports_period) {
                return {400, "Validation Error", "Effect type '" + effect_type + "' does not support 'period_ms'", current_rev};
            }
            if (patch.period_ms < 33) {
                return {400, "Validation Error", "Field 'period_ms' must be at least 33ms", current_rev};
            }
            prof["period_ms"] = patch.period_ms;
        }

        // 5. 格式化输出 (保持 2 格缩进)
        std::string formatted = root.dump(2);

        // 6. 写入临时文件
        std::filesystem::path tmp_path = config_path_.wstring() + L".tmp";
        {
            std::ofstream f(tmp_path, std::ios::binary | std::ios::trunc);
            if (!f.is_open()) {
                return {500, "Internal Error", "Failed to create temporary configuration file", current_rev};
            }
            f.write(formatted.data(), formatted.size());
            f.flush();
            if (!f.good()) {
                f.close();
                std::error_code ec;
                std::filesystem::remove(tmp_path, ec);
                return {500, "Internal Error", "Failed to write temporary configuration data", current_rev};
            }
        }

        // 7. 原子替换覆盖 (通过 file_replacer_ 支持测试接缝)
        if (!file_replacer_(tmp_path.wstring(), config_path_.wstring())) {
            std::error_code ec;
            std::filesystem::remove(tmp_path, ec);
            return {500, "Internal Error", "Failed to atomically replace configuration file", current_rev};
        }

        out_new_revision = ComputeFileRevision(formatted);
        return {200, "", "Profile updated", out_new_revision};
    } catch (const std::exception& e) {
        return {500, "Internal Error", std::string("Failed to mutate configuration: ") + e.what(), current_rev};
    }
}

LightingControlService::OpResult LightingControlService::GetPresetCatalog(
    std::vector<BuiltinLightingPreset>& out_presets) {
    out_presets = GetBuiltinLightingPresets();
    return {200, "", "", ""};
}

LightingControlService::OpResult LightingControlService::GetBaseLighting(BaseLightingDetail& out_detail) {
    std::string content;
    std::string rev;
    if (!ReadRawConfigFile(content, rev)) {
        return {500, "Internal Error", "Failed to read configuration file", ""};
    }

    try {
        auto root = nlohmann::ordered_json::parse(content);
        if (!root.is_object()) {
            return {500, "Config Error", "Configuration root must be a JSON object", rev};
        }

        if (!root.contains("default_profile") || !root["default_profile"].is_string()) {
            return {500, "Config Error", "Configuration root missing valid string 'default_profile'", rev};
        }

        std::string def_name = root["default_profile"].get<std::string>();
        if (!root.contains("profiles") || !root["profiles"].is_object()) {
            return {500, "Config Error", "Configuration root missing 'profiles' object", rev};
        }

        if (!root["profiles"].contains(def_name)) {
            return {404, "Profile Not Found", "Default profile '" + def_name + "' does not exist in 'profiles'", rev};
        }

        if (!root["profiles"][def_name].is_object()) {
            return {500, "Config Error", "Default profile '" + def_name + "' is not a valid JSON object", rev};
        }

        const auto& pval = root["profiles"][def_name];
        BaseLightingDetail d;
        d.profile_name = def_name;
        d.effect = pval.value("type", "static");
        d.brightness = NormalizeBrightnessToRatio(pval);
        d.revision = rev;

        const auto* preset = FindBuiltinLightingPreset(d.effect);
        if (preset != nullptr) {
            d.is_builtin_preset = true;
            d.preset_id = preset->id;
            d.supports_period = preset->supports_period;
            if (d.supports_period) {
                d.has_period = true;
                d.period_ms = ResolveEffectivePeriodMs(d.effect, pval, def_name);
            } else {
                d.has_period = false;
                d.period_ms = 0;
            }
            d.parameters = ResolveEffectiveParameters(d.effect, pval, def_name);
        } else {
            // 支持高级效果 (custom_keymap, plugin, etc.)
            d.is_builtin_preset = false;
            d.preset_id = "";
            d.supports_period = false;
            d.has_period = false;
            d.period_ms = 0;
            d.parameters = nlohmann::json::object();
        }

        out_detail = std::move(d);
        return {200, "", "", rev};
    } catch (const std::exception& e) {
        return {500, "Internal Error", std::string("JSON parsing error: ") + e.what(), rev};
    }
}

LightingControlService::OpResult LightingControlService::UpdateBaseLighting(
    const BaseLightingPatchInput& patch, std::string& out_new_revision) {
    if (patch.expected_revision.empty()) {
        return {400, "Validation Error", "Missing required field 'expected_revision'", ""};
    }

    // 1. 获取跨进程 Windows Named Mutex 互斥锁
    NamedConfigLock lock(L"Local\\AceHFXAuraConfigWriteMutex", 5000);
    if (!lock.IsAcquired()) {
        return {500, "Internal Error", "Failed to acquire cross-process configuration lock", ""};
    }

    // 2. 锁内重新读取最新文件内容与版本
    std::string content;
    std::string current_rev;
    if (!ReadRawConfigFile(content, current_rev)) {
        return {500, "Internal Error", "Failed to read configuration file", ""};
    }

    // 3. 版本指纹比对 (FNV-1a 64-bit 严格一致性，409 Conflict)
    if (current_rev != patch.expected_revision) {
        return {409, "Conflict", "Configuration has been modified externally", current_rev};
    }

    try {
        auto root = nlohmann::ordered_json::parse(content);
        if (!root.is_object()) {
            return {500, "Config Error", "Configuration root must be a JSON object", current_rev};
        }

        if (!root.contains("default_profile") || !root["default_profile"].is_string()) {
            return {500, "Config Error", "Configuration root missing valid string 'default_profile'", current_rev};
        }

        std::string def_name = root["default_profile"].get<std::string>();
        if (!root.contains("profiles") || !root["profiles"].is_object()) {
            return {500, "Config Error", "Configuration root missing 'profiles' object", current_rev};
        }

        if (!root["profiles"].contains(def_name)) {
            return {404, "Profile Not Found", "Default profile '" + def_name + "' does not exist in 'profiles'", current_rev};
        }

        if (!root["profiles"][def_name].is_object()) {
            return {500, "Config Error", "Default profile '" + def_name + "' is not a valid JSON object", current_rev};
        }

        auto& prof = root["profiles"][def_name];

        // 4. 确定目标 preset 并执行前置校验
        const BuiltinLightingPreset* target_preset = nullptr;
        if (patch.has_preset) {
            target_preset = FindBuiltinLightingPreset(patch.preset);
            if (!target_preset) {
                return {400, "Validation Error", "Preset '" + patch.preset + "' is not a supported builtin preset", current_rev};
            }
        } else {
            std::string cur_type = prof.value("type", "static");
            target_preset = FindBuiltinLightingPreset(cur_type);
            if (!target_preset && (patch.has_period_ms || patch.has_parameters)) {
                return {400, "Validation Error", "Target profile has non-builtin effect type '" + cur_type + "'", current_rev};
            }
        }

        // 校验 brightness
        if (patch.has_brightness) {
            if (!std::isfinite(patch.brightness) || patch.brightness < 0.0 || patch.brightness > 1.0) {
                return {400, "Validation Error", "Field 'brightness' must be a valid number in [0.0, 1.0]", current_rev};
            }
        }

        // 校验 period_ms
        if (patch.has_period_ms) {
            bool supports_period = target_preset ? target_preset->supports_period : false;
            if (!supports_period) {
                return {400, "Validation Error", "Target effect does not support 'period_ms'", current_rev};
            }
            if (patch.period_ms < 33) {
                return {400, "Validation Error", "Field 'period_ms' must be at least 33ms", current_rev};
            }
        }

        // 校验 parameters (严格依据 target preset schema 校验，约束 2/3)
        if (patch.has_parameters) {
            if (!target_preset) {
                return {400, "Validation Error", "Cannot apply parameters to non-builtin preset", current_rev};
            }
            if (!patch.parameters.is_object()) {
                return {400, "Validation Error", "Field 'parameters' must be a JSON object", current_rev};
            }

            for (auto it = patch.parameters.begin(); it != patch.parameters.end(); ++it) {
                const std::string& key = it.key();
                const auto& val = it.value();

                const EffectParamSchema* schema = nullptr;
                for (const auto& s : target_preset->param_schemas) {
                    if (s.key == key) {
                        schema = &s;
                        break;
                    }
                }

                if (!schema) {
                    return {400, "Validation Error", "Parameter '" + key + "' is not supported for preset '" + target_preset->id + "'", current_rev};
                }

                switch (schema->type) {
                    case EffectParamType::Color: {
                        if (!val.is_array() || val.size() != 3) {
                            return {400, "Validation Error", "Parameter '" + key + "' must be an RGB array of 3 integers in [0, 255]", current_rev};
                        }
                        for (size_t c = 0; c < 3; ++c) {
                            if (!val[c].is_number_integer() || val[c].get<int>() < 0 || val[c].get<int>() > 255) {
                                return {400, "Validation Error", "Parameter '" + key + "' RGB channel " + std::to_string(c) + " must be an integer in [0, 255]", current_rev};
                            }
                        }
                        break;
                    }
                    case EffectParamType::Boolean: {
                        if (!val.is_boolean()) {
                            return {400, "Validation Error", "Parameter '" + key + "' must be a boolean", current_rev};
                        }
                        break;
                    }
                    case EffectParamType::Enum: {
                        if (!val.is_string()) {
                            return {400, "Validation Error", "Parameter '" + key + "' must be a string", current_rev};
                        }
                        std::string opt_val = val.get<std::string>();
                        bool found = false;
                        for (const auto& opt : schema->options) {
                            if (opt.value == opt_val) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            return {400, "Validation Error", "Parameter '" + key + "' value '" + opt_val + "' is not a valid option for preset '" + target_preset->id + "'", current_rev};
                        }
                        break;
                    }
                    case EffectParamType::Number: {
                        if (!val.is_number()) {
                            return {400, "Validation Error", "Parameter '" + key + "' must be a number", current_rev};
                        }
                        double num_val = val.get<double>();
                        if (!std::isfinite(num_val) || num_val < schema->min_val || num_val > schema->max_val) {
                            return {400, "Validation Error", "Parameter '" + key + "' must be a finite number in [" + std::to_string(schema->min_val) + ", " + std::to_string(schema->max_val) + "]", current_rev};
                        }
                        break;
                    }
                }
            }
        }

        // 5. 执行更新操作 (严格遵循约束 3 的应用顺序)
        // Step A: Apply target preset canonical defaults
        if (patch.has_preset && target_preset) {
            for (auto it = target_preset->canonical_params.begin(); it != target_preset->canonical_params.end(); ++it) {
                prof[it.key()] = it.value();
            }
        }

        // Step B: Overlay explicit brightness
        if (patch.has_brightness) {
            prof["brightness"] = patch.brightness;
        }

        // Step C: Overlay explicit period_ms
        if (patch.has_period_ms) {
            prof["period_ms"] = patch.period_ms;
        }

        // Step D: Overlay explicit parameters
        if (patch.has_parameters) {
            for (auto it = patch.parameters.begin(); it != patch.parameters.end(); ++it) {
                prof[it.key()] = it.value();
            }
        }

        // 7. 格式化输出 (保持 2 格缩进)
        std::string formatted = root.dump(2);

        // 8. 写入临时文件并原子替换
        std::filesystem::path tmp_path = config_path_.wstring() + L".tmp";
        {
            std::ofstream f(tmp_path, std::ios::binary | std::ios::trunc);
            if (!f.is_open()) {
                return {500, "Internal Error", "Failed to create temporary configuration file", current_rev};
            }
            f.write(formatted.data(), formatted.size());
            f.flush();
            if (!f.good()) {
                f.close();
                std::error_code ec;
                std::filesystem::remove(tmp_path, ec);
                return {500, "Internal Error", "Failed to write temporary configuration data", current_rev};
            }
        }

        if (!file_replacer_(tmp_path.wstring(), config_path_.wstring())) {
            std::error_code ec;
            std::filesystem::remove(tmp_path, ec);
            return {500, "Internal Error", "Failed to atomically replace configuration file", current_rev};
        }

        out_new_revision = ComputeFileRevision(formatted);
        return {200, "", "Base lighting updated", out_new_revision};
    } catch (const std::exception& e) {
        return {500, "Internal Error", std::string("Failed to mutate configuration: ") + e.what(), current_rev};
    }
}

// ========================================================
// HTTP 安全防御检查 (127.0.0.1:19897 写接口防御)
// ========================================================
bool LightingControlService::ValidatePatchRequestSecurity(const httplib::Request& req, httplib::Response& res) {
    // 1. Host 校验：必须为环回地址
    std::string host = req.get_header_value("Host");
    bool host_ok = (host == "127.0.0.1" || host == "localhost" || host == "[::1]" ||
                    host.rfind("127.0.0.1:", 0) == 0 ||
                    host.rfind("localhost:", 0) == 0 ||
                    host.rfind("[::1]:", 0) == 0);
    if (!host_ok) {
        res.status = 403;
        res.set_content(R"json({"status":"error","error":"Forbidden","message":"Host must be loopback"})json", "application/json; charset=utf-8");
        return false;
    }

    // 2. Content-Type 校验：必须为 application/json
    std::string ct = req.get_header_value("Content-Type");
    if (ct.find("application/json") == std::string::npos) {
        res.status = 415;
        res.set_content(R"json({"status":"error","error":"Unsupported Media Type","message":"Content-Type must be application/json"})json", "application/json; charset=utf-8");
        return false;
    }

    // 3. Origin / Referer 校验：防跨站伪造
    // WinUI 原生 HttpClient 默认无 Origin，允许通过；若存在必须为合法环回地址
    std::string origin = req.get_header_value("Origin");
    if (!origin.empty()) {
        bool origin_ok = (origin.rfind("http://127.0.0.1", 0) == 0 || origin.rfind("http://localhost", 0) == 0);
        if (!origin_ok) {
            res.status = 403;
            res.set_content(R"json({"status":"error","error":"Forbidden","message":"Untrusted Origin rejected"})json", "application/json; charset=utf-8");
            return false;
        }
    }

    std::string referer = req.get_header_value("Referer");
    if (!referer.empty()) {
        bool referer_ok = (referer.rfind("http://127.0.0.1", 0) == 0 || referer.rfind("http://localhost", 0) == 0);
        if (!referer_ok) {
            res.status = 403;
            res.set_content(R"json({"status":"error","error":"Forbidden","message":"Untrusted Referer rejected"})json", "application/json; charset=utf-8");
            return false;
        }
    }

    return true;
}

void LightingControlService::RegisterRoutes(httplib::Server& svr) {
    // 1. GET /api/lighting/profiles - 获取轻量 Profile 列表
    svr.Get("/api/lighting/profiles", [this](const httplib::Request&, httplib::Response& res) {
        std::vector<ProfileSummary> profiles;
        std::string revision;
        auto op = GetProfileList(profiles, revision);
        if (op.http_status != 200) {
            res.status = op.http_status;
            nlohmann::json err = {
                {"status", "error"},
                {"error", op.error_code},
                {"message", op.message}
            };
            res.set_content(err.dump(), "application/json; charset=utf-8");
            return;
        }

        nlohmann::json j;
        j["status"] = "ok";
        j["api_version"] = 1;
        j["revision"] = revision;
        j["profiles"] = nlohmann::json::array();
        for (const auto& p : profiles) {
            j["profiles"].push_back({
                {"name", p.name},
                {"type", p.type}
            });
        }
        res.status = 200;
        res.set_content(j.dump(), "application/json; charset=utf-8");
    });

    // 2. GET /api/lighting/profiles/:name - 获取指定方案详情
    svr.Get(R"(/api/lighting/profiles/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];
        ProfileDetail detail;
        auto op = GetProfileDetail(name, detail);
        if (op.http_status != 200) {
            res.status = op.http_status;
            nlohmann::json err = {
                {"status", "error"},
                {"error", op.error_code},
                {"message", op.message}
            };
            if (!op.current_revision.empty()) {
                err["current_revision"] = op.current_revision;
            }
            res.set_content(err.dump(), "application/json; charset=utf-8");
            return;
        }

        nlohmann::json j;
        j["status"] = "ok";
        j["api_version"] = 1;
        j["revision"] = detail.revision;
        j["profile"] = {
            {"name", detail.name},
            {"type", detail.type},
            {"brightness", detail.brightness},
            {"fps", detail.fps},
            {"fps_inherited", detail.fps_inherited},
            {"supports_period", detail.supports_period}
        };
        if (detail.supports_period) {
            j["profile"]["period_ms"] = detail.period_ms;
        } else {
            j["profile"]["period_ms"] = nullptr;
        }

        res.status = 200;
        res.set_content(j.dump(), "application/json; charset=utf-8");
    });

    // 3. PATCH /api/lighting/profiles/:name - 严格白名单与原子修改
    svr.Patch(R"(/api/lighting/profiles/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ValidatePatchRequestSecurity(req, res)) {
            return;
        }

        std::string name = req.matches[1];
        PatchInput patch;

        try {
            auto j = nlohmann::json::parse(req.body);
            if (!j.is_object()) {
                res.status = 400;
                res.set_content(R"json({"status":"error","error":"Validation Error","message":"Request body must be a JSON object"})json", "application/json; charset=utf-8");
                return;
            }

            // 严格白名单字段校验：禁止修改除 expected_revision, brightness, period_ms, fps 以外的任何字段
            static const std::unordered_set<std::string> kAllowedKeys = {
                "expected_revision", "brightness", "period_ms", "fps"
            };
            for (auto it = j.begin(); it != j.end(); ++it) {
                if (kAllowedKeys.find(it.key()) == kAllowedKeys.end()) {
                    res.status = 400;
                    nlohmann::json err = {
                        {"status", "error"},
                        {"error", "Validation Error"},
                        {"message", "Modifying field '" + it.key() + "' is not permitted via Lighting API v1"}
                    };
                    res.set_content(err.dump(), "application/json; charset=utf-8");
                    return;
                }
            }

            if (!j.contains("expected_revision") || !j["expected_revision"].is_string()) {
                res.status = 400;
                res.set_content(R"json({"status":"error","error":"Validation Error","message":"Missing required string field 'expected_revision'"})json", "application/json; charset=utf-8");
                return;
            }
            patch.expected_revision = j["expected_revision"].get<std::string>();

            if (j.contains("brightness")) {
                if (!j["brightness"].is_number()) {
                    res.status = 400;
                    res.set_content(R"json({"status":"error","error":"Validation Error","message":"'brightness' must be a numeric ratio in [0.0, 1.0]"})json", "application/json; charset=utf-8");
                    return;
                }
                patch.has_brightness = true;
                patch.brightness = j["brightness"].get<double>();
            }

            if (j.contains("fps")) {
                if (!j["fps"].is_number_integer()) {
                    res.status = 400;
                    res.set_content(R"json({"status":"error","error":"Validation Error","message":"'fps' must be an integer between 10 and 100"})json", "application/json; charset=utf-8");
                    return;
                }
                patch.has_fps = true;
                patch.fps = j["fps"].get<int>();
            }

            if (j.contains("period_ms")) {
                if (!j["period_ms"].is_number_unsigned()) {
                    res.status = 400;
                    res.set_content(R"json({"status":"error","error":"Validation Error","message":"'period_ms' must be an unsigned integer >= 33"})json", "application/json; charset=utf-8");
                    return;
                }
                patch.has_period_ms = true;
                patch.period_ms = j["period_ms"].get<uint64_t>();
            }
        } catch (const std::exception& e) {
            res.status = 400;
            nlohmann::json err = {
                {"status", "error"},
                {"error", "Validation Error"},
                {"message", std::string("Malformed JSON payload: ") + e.what()}
            };
            res.set_content(err.dump(), "application/json; charset=utf-8");
            return;
        }

        std::string new_rev;
        auto op = UpdateProfile(name, patch, new_rev);
        res.status = op.http_status;
        if (op.http_status == 200) {
            nlohmann::json ok_j = {
                {"status", "ok"},
                {"api_version", 1},
                {"message", op.message},
                {"revision", new_rev}
            };
            res.set_content(ok_j.dump(), "application/json; charset=utf-8");
        } else {
            nlohmann::json err_j = {
                {"status", "error"},
                {"error", op.error_code},
                {"message", op.message}
            };
            if (!op.current_revision.empty()) {
                err_j["current_revision"] = op.current_revision;
            }
            res.set_content(err_j.dump(), "application/json; charset=utf-8");
        }
    });

    // 4. GET /api/lighting/presets & GET /api/lighting/effects - 获取内建预设 Catalog
    auto handle_presets = [this](const httplib::Request&, httplib::Response& res) {
        std::vector<BuiltinLightingPreset> presets;
        GetPresetCatalog(presets);

        nlohmann::json arr = nlohmann::json::array();
        for (const auto& p : presets) {
            nlohmann::json item = {
                {"id", p.id},
                {"display_name", p.display_name},
                {"effect", p.effect_type},
                {"supports_period", p.supports_period}
            };
            if (p.supports_period) {
                item["default_period_ms"] = p.default_period_ms;
            } else {
                item["default_period_ms"] = nullptr;
            }

            nlohmann::json schema_arr = nlohmann::json::array();
            for (const auto& s : p.param_schemas) {
                nlohmann::json s_item = {
                    {"key", s.key},
                    {"display_name", s.display_name},
                    {"default_value", s.default_value}
                };
                switch (s.type) {
                    case EffectParamType::Color:
                        s_item["type"] = "color";
                        break;
                    case EffectParamType::Boolean:
                        s_item["type"] = "boolean";
                        break;
                    case EffectParamType::Enum: {
                        s_item["type"] = "enum";
                        nlohmann::json opts = nlohmann::json::array();
                        for (const auto& opt : s.options) {
                            opts.push_back({{"value", opt.value}, {"label", opt.label}});
                        }
                        s_item["options"] = std::move(opts);
                        break;
                    }
                    case EffectParamType::Number:
                        s_item["type"] = "number";
                        s_item["min"] = s.min_val;
                        s_item["max"] = s.max_val;
                        s_item["step"] = s.step;
                        break;
                }
                schema_arr.push_back(std::move(s_item));
            }
            item["parameter_schema"] = std::move(schema_arr);
            arr.push_back(std::move(item));
        }

        nlohmann::json j;
        j["status"] = "ok";
        j["api_version"] = 1;
        j["presets"] = arr;
        j["effects"] = arr;
        res.status = 200;
        res.set_content(j.dump(), "application/json; charset=utf-8");
    };

    svr.Get("/api/lighting/presets", handle_presets);
    svr.Get("/api/lighting/effects", handle_presets);

    // 5. GET /api/lighting/base - 获取基础/默认灯效配置
    svr.Get("/api/lighting/base", [this](const httplib::Request&, httplib::Response& res) {
        BaseLightingDetail detail;
        auto op = GetBaseLighting(detail);
        if (op.http_status != 200) {
            res.status = op.http_status;
            nlohmann::json err = {
                {"status", "error"},
                {"error", op.error_code},
                {"message", op.message}
            };
            if (!op.current_revision.empty()) {
                err["current_revision"] = op.current_revision;
            }
            res.set_content(err.dump(), "application/json; charset=utf-8");
            return;
        }

        nlohmann::json j;
        j["status"] = "ok";
        j["api_version"] = 1;
        j["revision"] = detail.revision;
        j["lighting"] = {
            {"profile_name", detail.profile_name},
            {"effect", detail.effect},
            {"preset_id", detail.preset_id},
            {"is_builtin_preset", detail.is_builtin_preset},
            {"brightness", detail.brightness},
            {"supports_period", detail.supports_period},
            {"parameters", detail.parameters}
        };
        if (detail.supports_period) {
            j["lighting"]["period_ms"] = detail.period_ms;
        } else {
            j["lighting"]["period_ms"] = nullptr;
        }

        res.status = 200;
        res.set_content(j.dump(), "application/json; charset=utf-8");
    });

    // 6. PATCH /api/lighting/base - 修改基础/默认灯效配置
    svr.Patch("/api/lighting/base", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ValidatePatchRequestSecurity(req, res)) {
            return;
        }

        BaseLightingPatchInput patch;
        try {
            auto j = nlohmann::json::parse(req.body);
            if (!j.is_object()) {
                res.status = 400;
                res.set_content(R"json({"status":"error","error":"Validation Error","message":"Request body must be a JSON object"})json", "application/json; charset=utf-8");
                return;
            }

            // 严格白名单校验 (约束 2: period_ms 为顶层字段，parameters 承载 effect-specific 参数)
            static const std::unordered_set<std::string> kAllowedKeys = {
                "expected_revision", "preset", "effect", "brightness", "period_ms", "parameters"
            };
            for (auto it = j.begin(); it != j.end(); ++it) {
                if (kAllowedKeys.find(it.key()) == kAllowedKeys.end()) {
                    res.status = 400;
                    nlohmann::json err = {
                        {"status", "error"},
                        {"error", "Validation Error"},
                        {"message", "Modifying field '" + it.key() + "' is not permitted via Base Lighting API"}
                    };
                    res.set_content(err.dump(), "application/json; charset=utf-8");
                    return;
                }
            }

            if (!j.contains("expected_revision") || !j["expected_revision"].is_string()) {
                res.status = 400;
                res.set_content(R"json({"status":"error","error":"Validation Error","message":"Missing required string field 'expected_revision'"})json", "application/json; charset=utf-8");
                return;
            }
            patch.expected_revision = j["expected_revision"].get<std::string>();

            if (j.contains("preset")) {
                if (!j["preset"].is_string()) {
                    res.status = 400;
                    res.set_content(R"json({"status":"error","error":"Validation Error","message":"'preset' must be a string"})json", "application/json; charset=utf-8");
                    return;
                }
                patch.has_preset = true;
                patch.preset = j["preset"].get<std::string>();
            } else if (j.contains("effect")) {
                if (!j["effect"].is_string()) {
                    res.status = 400;
                    res.set_content(R"json({"status":"error","error":"Validation Error","message":"'effect' must be a string"})json", "application/json; charset=utf-8");
                    return;
                }
                patch.has_preset = true;
                patch.preset = j["effect"].get<std::string>();
            }

            if (j.contains("brightness")) {
                if (!j["brightness"].is_number()) {
                    res.status = 400;
                    res.set_content(R"json({"status":"error","error":"Validation Error","message":"'brightness' must be a numeric ratio in [0.0, 1.0]"})json", "application/json; charset=utf-8");
                    return;
                }
                patch.has_brightness = true;
                patch.brightness = j["brightness"].get<double>();
            }

            if (j.contains("period_ms")) {
                if (!j["period_ms"].is_number_unsigned()) {
                    res.status = 400;
                    res.set_content(R"json({"status":"error","error":"Validation Error","message":"'period_ms' must be an unsigned integer >= 33"})json", "application/json; charset=utf-8");
                    return;
                }
                patch.has_period_ms = true;
                patch.period_ms = j["period_ms"].get<uint64_t>();
            }

            if (j.contains("parameters")) {
                if (!j["parameters"].is_object()) {
                    res.status = 400;
                    res.set_content(R"json({"status":"error","error":"Validation Error","message":"'parameters' must be a JSON object"})json", "application/json; charset=utf-8");
                    return;
                }
                patch.has_parameters = true;
                patch.parameters = j["parameters"];
            }
        } catch (const std::exception& e) {
            res.status = 400;
            nlohmann::json err = {
                {"status", "error"},
                {"error", "Validation Error"},
                {"message", std::string("Malformed JSON payload: ") + e.what()}
            };
            res.set_content(err.dump(), "application/json; charset=utf-8");
            return;
        }

        std::string new_rev;
        auto op = UpdateBaseLighting(patch, new_rev);
        res.status = op.http_status;
        if (op.http_status == 200) {
            nlohmann::json ok_j = {
                {"status", "ok"},
                {"api_version", 1},
                {"message", op.message},
                {"revision", new_rev}
            };
            res.set_content(ok_j.dump(), "application/json; charset=utf-8");
        } else {
            nlohmann::json err_j = {
                {"status", "error"},
                {"error", op.error_code},
                {"message", op.message}
            };
            if (!op.current_revision.empty()) {
                err_j["current_revision"] = op.current_revision;
            }
            res.set_content(err_j.dump(), "application/json; charset=utf-8");
        }
    });
}

} // namespace aura
