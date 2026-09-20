#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <functional>
#include <cstdint>
#include <mutex>
#include <unordered_set>

#include "aura/aura_types.h"
#include "config/config_writer_util.h"
#include "third_party/json.hpp"
#include "third_party/httplib.h"

namespace aura {

// ========================================================
// 动画周期 (period_ms) 共享元数据与解析
// ========================================================
bool IsPeriodSupported(const std::string& effect_type);
uint64_t GetEffectDefaultPeriod(const std::string& effect_type);
uint64_t ResolveEffectivePeriodMs(const std::string& effect_type, const nlohmann::json& pval, const std::string& pname = "");

// ========================================================
// 共享有效参数解析 (与 RuleEngine 统一事实来源)
// ========================================================
ColorRGB ParseColorFromArray(const nlohmann::json& arr, const ColorRGB& def);
ColorRGB ParseColor(const nlohmann::json& pval, const std::string& primary_key, const std::string& fallback_key, const ColorRGB& def_color);
ColorRGB ParseBgColor(const nlohmann::json& pval, const ColorRGB& def_bg = ColorRGB(0, 0, 0));
double ParseAndClampThickness(const std::string& pname, const nlohmann::json& pval, double def_val = 1.0);
double ParseAndClampThickness(const nlohmann::json& pval, double def_val = 1.0);
nlohmann::json ResolveEffectiveParameters(const std::string& effect_type, const nlohmann::json& pval, const std::string& pname = "");

// ========================================================
// 内建预设 (Builtin Lighting Preset) 参数 Schema 规范
// ========================================================
enum class EffectParamType {
    Color,      // RGB 数组 [r, g, b]，各通道 [0, 255]
    Boolean,    // true / false
    Enum,       // 字符串枚举，必须在 options 中
    Number      // 浮点数/数值，包含 min, max, step
};

struct EffectParamOption {
    std::string value;
    std::string label;
};

struct EffectParamSchema {
    std::string key;
    std::string display_name;
    EffectParamType type{EffectParamType::Color};
    nlohmann::json default_value;
    double min_val{0.0};
    double max_val{0.0};
    double step{0.0};
    std::vector<EffectParamOption> options;
};

struct BuiltinLightingPreset {
    std::string id;
    std::string display_name;
    std::string effect_type;
    bool supports_period{false};
    uint64_t default_period_ms{0};
    nlohmann::json canonical_params;
    std::vector<EffectParamSchema> param_schemas;
};

const std::vector<BuiltinLightingPreset>& GetBuiltinLightingPresets();
const BuiltinLightingPreset* FindBuiltinLightingPreset(const std::string& id_or_type);

// ========================================================
// 亮度 (brightness) 规范化
// ========================================================
double NormalizeBrightnessToRatio(const nlohmann::json& pval);

// ========================================================
// LightingControlService
// 运行于 127.0.0.1:19897，负责 Lighting API v1
// ========================================================
class LightingControlService {
public:
    using FileReplacerFunc = std::function<bool(const std::wstring& tmp_path, const std::wstring& target_path)>;

    explicit LightingControlService(std::filesystem::path config_path);

    // 注入可控的文件替换测试接缝 (默认调用 MoveFileExW)
    void SetFileReplacerForTesting(FileReplacerFunc replacer) {
        file_replacer_ = std::move(replacer);
    }

    const std::filesystem::path& GetConfigPath() const { return config_path_; }

    // 注册 HTTP 路由至 GsiAdapter 服务器 (必须在 Start 前调用)
    void RegisterRoutes(httplib::Server& svr);

    // 核心业务方法 (供 HTTP 路由与直接单元测试调用)
    struct ProfileSummary {
        std::string name;
        std::string type;
    };

    struct ProfileDetail {
        std::string name;
        std::string type;
        double brightness{1.0};
        int fps{25};
        bool fps_inherited{true};
        bool supports_period{false};
        bool has_period{false};
        uint64_t period_ms{0};
        std::string revision;
    };

    struct PatchInput {
        std::string expected_revision;
        bool has_brightness{false};
        double brightness{1.0};
        bool has_period_ms{false};
        uint64_t period_ms{0};
        bool has_fps{false};
        int fps{25};
    };

    // Phase 3.5 & 3.6: Base Lighting 与 Builtin Preset 语义模型
    struct BaseLightingDetail {
        std::string profile_name;
        std::string effect;
        std::string preset_id;
        bool is_builtin_preset{false};
        double brightness{1.0};
        bool supports_period{false};
        bool has_period{false};
        uint64_t period_ms{0};
        nlohmann::json parameters;
        std::string revision;
    };

    struct BaseLightingPatchInput {
        std::string expected_revision;
        bool has_preset{false};
        std::string preset;
        bool has_brightness{false};
        double brightness{1.0};
        bool has_period_ms{false};
        uint64_t period_ms{0};
        bool has_parameters{false};
        nlohmann::json parameters;
    };

    struct OpResult {
        int http_status{200};
        std::string error_code;
        std::string message;
        std::string current_revision;
    };

    OpResult GetProfileList(std::vector<ProfileSummary>& out_profiles, std::string& out_revision);
    OpResult GetProfileDetail(const std::string& name, ProfileDetail& out_detail);
    OpResult UpdateProfile(const std::string& name, const PatchInput& patch, std::string& out_new_revision);

    // Phase 3.5 新增方法
    OpResult GetPresetCatalog(std::vector<BuiltinLightingPreset>& out_presets);
    OpResult GetBaseLighting(BaseLightingDetail& out_detail);
    OpResult UpdateBaseLighting(const BaseLightingPatchInput& patch, std::string& out_new_revision);

private:
    std::filesystem::path config_path_;
    FileReplacerFunc file_replacer_;

    bool ReadRawConfigFile(std::string& out_content, std::string& out_revision) const;
    static bool ValidatePatchRequestSecurity(const httplib::Request& req, httplib::Response& res);
};

} // namespace aura
