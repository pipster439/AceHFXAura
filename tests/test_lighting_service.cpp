#include "test_util.h"
#include "config/lighting_service.h"
#include "config/rule_engine.h"
#include "config/config_contract.h"
#include "config/config_writer_util.h"
#include "third_party/json.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <chrono>

namespace {

std::string CreateTempConfigFile(const std::string& prefix, const std::string& content) {
    auto tmp_dir = std::filesystem::temp_directory_path();
    auto p = tmp_dir / (prefix + "_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(rand()) + ".json");
    std::ofstream ofs(p, std::ios::binary);
    ofs.write(content.data(), content.size());
    ofs.close();
    return p.string();
}

} // namespace

int main() {
    int failures = 0;

    std::cout << "[Test 1] 亮度规范化 (NormalizeBrightnessToRatio) 校验\n";
    {
        nlohmann::json j1 = {{"brightness", 0.5}};
        double b1 = aura::NormalizeBrightnessToRatio(j1);
        CHECK(std::abs(b1 - 0.5) < 1e-4, "config 0.5 -> API 0.5");

        nlohmann::json j2 = {{"brightness", 128}};
        double b2 = aura::NormalizeBrightnessToRatio(j2);
        CHECK(std::abs(b2 - (128.0 / 255.0)) < 1e-4, "config 128 -> API ~0.502");

        nlohmann::json j3 = {{"brightness", 0.0}};
        CHECK(aura::NormalizeBrightnessToRatio(j3) == 0.0, "config 0.0 -> API 0.0");

        nlohmann::json j4 = {{"brightness", 255}};
        CHECK(aura::NormalizeBrightnessToRatio(j4) == 1.0, "config 255 -> API 1.0");

        nlohmann::json j5 = {{"brightness", -5.0}};
        CHECK(aura::NormalizeBrightnessToRatio(j5) == 0.0, "config -5.0 -> API 0.0");

        nlohmann::json j6 = nlohmann::json::object();
        CHECK(aura::NormalizeBrightnessToRatio(j6) == 1.0, "missing brightness -> 默认 1.0");
    }

    std::cout << "[Test 2] 动画周期支持与解析 (ResolveEffectivePeriodMs) 校验\n";
    {
        CHECK(aura::IsPeriodSupported("breathing") == true, "breathing 支持 period");
        CHECK(aura::IsPeriodSupported("color_cycle") == true, "color_cycle 支持 period");
        CHECK(aura::IsPeriodSupported("wave") == true, "wave 支持 period");
        CHECK(aura::IsPeriodSupported("static") == false, "static 不支持 period");
        CHECK(aura::IsPeriodSupported("custom_keymap") == false, "custom_keymap 不支持 period");
        CHECK(aura::IsPeriodSupported("plugin") == false, "plugin 不支持 period");

        // 默认周期与 explicit period
        nlohmann::json j_def = nlohmann::json::object();
        CHECK(aura::ResolveEffectivePeriodMs("breathing", j_def) == 3000, "breathing 默认周期 3000ms");
        CHECK(aura::ResolveEffectivePeriodMs("wave", j_def) == 3500, "wave 默认周期 3500ms");
        CHECK(aura::ResolveEffectivePeriodMs("current", j_def) == 2000, "current 默认周期 2000ms");

        nlohmann::json j_exp = {{"period_ms", 2800}};
        CHECK(aura::ResolveEffectivePeriodMs("breathing", j_exp) == 2800, "breathing 显式指定 2800ms");

        nlohmann::json j_speed = {{"speed_index", 2}};
        CHECK(aura::ResolveEffectivePeriodMs("breathing", j_speed) == 1600, "breathing legacy speed_index 2 -> 1600ms");

        nlohmann::json j_large = {{"period_ms", 90000}};
        CHECK(aura::ResolveEffectivePeriodMs("breathing", j_large) == 90000, "无 60000ms 人为限制 (90000ms 保留)");
    }

    std::cout << "[Test 3] Profile 列表读取与键序保持\n";
    std::string sample_config = R"json({
  "default_profile": "desktop",
  "fps": 25,
  "hardware_backend": "auto",
  "custom_unknown_root_field": "preserved_value",
  "profiles": {
    "desktop": {
      "type": "breathing",
      "color1": [
        0,
        80,
        200
      ],
      "color2": [
        0,
        10,
        40
      ],
      "period_ms": 3500,
      "brightness": 1.0,
      "unknown_profile_tag": "keep_me"
    },
    "coding": {
      "type": "static",
      "color": [
        10,
        30,
        50
      ],
      "brightness": 128
    },
    "wave_glow": {
      "type": "wave",
      "period_ms": 4000,
      "fps": 50
    }
  },
  "orchestration": {
    "rules": [
      {
        "id": "retained",
        "model": "automation_v2",
        "when": {
          "mode": "state",
          "condition": {
            "field": "process",
            "value": "cs2.exe"
          }
        },
        "action": {
          "type": "activate_profile",
          "profile": "desktop"
        }
      }
    ]
  }
})json";

    std::string test_cfg_path = CreateTempConfigFile("test_lighting_cfg", sample_config);

    {
        aura::LightingControlService svc(test_cfg_path);
        std::vector<aura::LightingControlService::ProfileSummary> list;
        std::string rev;
        auto op = svc.GetProfileList(list, rev);
        CHECK(op.http_status == 200, "GetProfileList 返回 200");
        CHECK(!rev.empty(), "版本指纹 revision 非空");
        CHECK(list.size() == 3, "成功读取 3 个 Profile");
        CHECK(list[0].name == "desktop" && list[0].type == "breathing", "Profile 0 为 desktop");
        CHECK(list[1].name == "coding" && list[1].type == "static", "Profile 1 为 coding");
        CHECK(list[2].name == "wave_glow" && list[2].type == "wave", "Profile 2 为 wave_glow");
    }

    std::cout << "[Test 4] Profile 详情获取与属性继承\n";
    {
        aura::LightingControlService svc(test_cfg_path);
        aura::LightingControlService::ProfileDetail d1;
        auto op1 = svc.GetProfileDetail("desktop", d1);
        CHECK(op1.http_status == 200, "GetProfileDetail(desktop) 成功");
        CHECK(d1.name == "desktop", "name == desktop");
        CHECK(d1.type == "breathing", "type == breathing");
        CHECK(d1.brightness == 1.0, "brightness == 1.0");
        CHECK(d1.fps == 25, "继承全局 fps == 25");
        CHECK(d1.fps_inherited == true, "fps_inherited == true");
        CHECK(d1.supports_period == true, "supports_period == true");
        CHECK(d1.period_ms == 3500, "period_ms == 3500");

        aura::LightingControlService::ProfileDetail d2;
        auto op2 = svc.GetProfileDetail("coding", d2);
        CHECK(op2.http_status == 200, "GetProfileDetail(coding) 成功");
        CHECK(d2.supports_period == false, "static supports_period == false");
        CHECK(std::abs(d2.brightness - (128.0 / 255.0)) < 1e-4, "coding brightness ~0.502");

        aura::LightingControlService::ProfileDetail d3;
        auto op3 = svc.GetProfileDetail("wave_glow", d3);
        CHECK(op3.http_status == 200, "GetProfileDetail(wave_glow) 成功");
        CHECK(d3.fps == 50, "显式 fps == 50");
        CHECK(d3.fps_inherited == false, "fps_inherited == false");

        aura::LightingControlService::ProfileDetail d4;
        auto op4 = svc.GetProfileDetail("non_existent", d4);
        CHECK(op4.http_status == 404, "不存在的方案返回 404");
    }

    std::cout << "[Test 5] 真正 Sparse Patch 字段隔离与未知键保留\n";
    std::string current_revision;
    {
        aura::LightingControlService svc(test_cfg_path);
        aura::LightingControlService::ProfileDetail before;
        svc.GetProfileDetail("desktop", before);
        current_revision = before.revision;

        // 仅修改 brightness 为 0.65，不提供 fps 与 period_ms
        aura::LightingControlService::PatchInput patch;
        patch.expected_revision = current_revision;
        patch.has_brightness = true;
        patch.brightness = 0.65;

        std::string new_rev;
        auto op = svc.UpdateProfile("desktop", patch, new_rev);
        CHECK(op.http_status == 200, "UpdateProfile 成功返回 200");
        CHECK(new_rev != current_revision, "版本指纹发生更新");
        current_revision = new_rev;

        // 验证持久化内容：未修改的字段与未知键严格保留
        std::ifstream f(test_cfg_path);
        nlohmann::json reloaded;
        f >> reloaded;
        f.close();

        CHECK(reloaded["profiles"]["desktop"]["brightness"] == 0.65, "desktop brightness 更新为 0.65");
        CHECK(!reloaded["profiles"]["desktop"].contains("fps"), "desktop 未提供 fps，未污染或硬写 profile.fps (保持继承)");
        CHECK(reloaded["profiles"]["desktop"]["period_ms"] == 3500, "desktop 未修改的 period_ms 保留");
        CHECK(reloaded["profiles"]["desktop"]["unknown_profile_tag"] == "keep_me", "desktop 未知字段保留");
        CHECK(reloaded["custom_unknown_root_field"] == "preserved_value", "根节点未知字段保留");
        CHECK(reloaded["orchestration"]["rules"][0]["id"] == "retained", "V2 rules preserved");
    }

    std::cout << "[Test 6] 校验防范：非法参数拒绝\n";
    {
        aura::LightingControlService svc(test_cfg_path);

        // 1. 亮度越界拒绝
        aura::LightingControlService::PatchInput p_bright;
        p_bright.expected_revision = current_revision;
        p_bright.has_brightness = true;
        p_bright.brightness = 1.2;
        std::string dummy;
        CHECK(svc.UpdateProfile("desktop", p_bright, dummy).http_status == 400, "brightness 1.2 拒绝 (400)");

        p_bright.brightness = -0.1;
        CHECK(svc.UpdateProfile("desktop", p_bright, dummy).http_status == 400, "brightness -0.1 拒绝 (400)");

        // 2. FPS 边界拒绝 (10–100)
        aura::LightingControlService::PatchInput p_fps;
        p_fps.expected_revision = current_revision;
        p_fps.has_fps = true;
        p_fps.fps = 9;
        CHECK(svc.UpdateProfile("desktop", p_fps, dummy).http_status == 400, "fps 9 拒绝 (400)");
        p_fps.fps = 101;
        CHECK(svc.UpdateProfile("desktop", p_fps, dummy).http_status == 400, "fps 101 拒绝 (400)");

        // 3. static 效果拒绝 period_ms
        aura::LightingControlService::PatchInput p_static_period;
        p_static_period.expected_revision = current_revision;
        p_static_period.has_period_ms = true;
        p_static_period.period_ms = 2000;
        CHECK(svc.UpdateProfile("coding", p_static_period, dummy).http_status == 400, "static 修改 period_ms 拒绝 (400)");

        // 4. period_ms 极限下界 (< 33ms 拒绝)
        aura::LightingControlService::PatchInput p_sub33;
        p_sub33.expected_revision = current_revision;
        p_sub33.has_period_ms = true;
        p_sub33.period_ms = 10;
        CHECK(svc.UpdateProfile("desktop", p_sub33, dummy).http_status == 400, "period_ms 10ms 拒绝 (400)");
    }

    std::cout << "[Test 7] 版本冲突检测 (409 Conflict)\n";
    {
        aura::LightingControlService svc(test_cfg_path);
        aura::LightingControlService::PatchInput stale_patch;
        stale_patch.expected_revision = "stale_fake_revision_1234";
        stale_patch.has_brightness = true;
        stale_patch.brightness = 0.5;

        std::string dummy;
        auto op = svc.UpdateProfile("desktop", stale_patch, dummy);
        CHECK(op.http_status == 409, "过期版本拒绝覆盖，返回 409 Conflict");
        CHECK(op.current_revision == current_revision, "409 携带当前最新 revision");
    }

    std::cout << "[Test 8] 可控原子写入测试接缝 (Atomic File Replacer Seam)\n";
    {
        aura::LightingControlService svc(test_cfg_path);
        // 注入模拟写入替换失败
        svc.SetFileReplacerForTesting([](const std::wstring& /*tmp*/, const std::wstring& /*target*/) {
            return false;
        });

        aura::LightingControlService::PatchInput p;
        p.expected_revision = current_revision;
        p.has_brightness = true;
        p.brightness = 0.12;

        std::string dummy;
        auto op = svc.UpdateProfile("desktop", p, dummy);
        CHECK(op.http_status == 500, "替换失败返回 500");

        // 校验原文件未受破坏
        aura::LightingControlService::ProfileDetail check_d;
        svc.GetProfileDetail("desktop", check_d);
        CHECK(check_d.brightness == 0.65, "写入失败时不破坏原文件 Last Known Good");

        // 临时文件被妥善清除
        std::filesystem::path tmp_check = std::filesystem::path(test_cfg_path).wstring() + L".tmp";
        CHECK(!std::filesystem::exists(tmp_check), "失败后清理 .tmp 临时文件");
    }

    std::cout << "[Test 9] RuleEngine::CheckAndReload 集成热重载检测\n";
    {
        // CheckAndReload watches mtime. Backdate only this temporary input so
        // the real atomic writer cannot share its timestamp on a coarse clock.
        // Do not touch the file after UpdateProfile: that write must trigger reload.
        const auto initial_time = std::filesystem::last_write_time(test_cfg_path)
            - std::chrono::seconds(2);
        std::filesystem::last_write_time(test_cfg_path, initial_time);
        aura::RuleEngine engine;
        bool loaded = engine.LoadConfig(test_cfg_path);
        CHECK(loaded, "RuleEngine 初始化加载配置文件成功");

        // 重新使用默认 replacer 更新配置
        aura::LightingControlService svc(test_cfg_path);
        aura::LightingControlService::PatchInput p;
        p.expected_revision = current_revision;
        p.has_brightness = true;
        p.brightness = 0.8;
        p.has_period_ms = true;
        p.period_ms = 4500;

        std::string new_rev;
        auto op = svc.UpdateProfile("desktop", p, new_rev);
        CHECK(op.http_status == 200, "UpdateProfile 更新成功");

        CHECK(std::filesystem::last_write_time(test_cfg_path) != initial_time,
              "atomic profile write changes the watched modification time");

        // RuleEngine 检测热重载
        bool reloaded = engine.CheckAndReload();
        CHECK(reloaded, "RuleEngine::CheckAndReload 成功检测到文件修改并重载");

        auto prof = engine.GetProfile("desktop");
        CHECK(prof != nullptr, "desktop 方案有效");
        // RuleEngine::ParseBrightness 0.8 -> round(0.8 * 255) = 204
        CHECK(prof->brightness == static_cast<uint8_t>(std::round(0.8 * 255.0)), "RuleEngine 内存中 brightness 成功热重载为 0.8 (204)");
    }

    std::cout << "[Test 10] Builtin Presets Catalog 获取与排除校验\n";
    {
        aura::LightingControlService svc(test_cfg_path);
        std::vector<aura::BuiltinLightingPreset> presets;
        auto op = svc.GetPresetCatalog(presets);
        CHECK(op.http_status == 200, "GetPresetCatalog 返回 200");
        CHECK(presets.size() == 10, "预设数量严格为 10 种内建预设");

        bool has_static = false;
        bool has_breathing = false;
        bool has_custom_keymap = false;
        bool has_plugin = false;

        for (const auto& p : presets) {
            if (p.id == "static") {
                has_static = true;
                CHECK(!p.supports_period, "static 不支持周期");
                CHECK(p.canonical_params.contains("color"), "static 预设包含 canonical color");
            }
            if (p.id == "breathing") {
                has_breathing = true;
                CHECK(p.supports_period, "breathing 支持周期");
                CHECK(p.default_period_ms == 3000, "breathing 默认周期 3000");
                CHECK(p.canonical_params.contains("color1"), "breathing 预设包含 color1");
                CHECK(p.canonical_params.contains("color2"), "breathing 预设包含 color2");
            }
            if (p.id == "custom_keymap" || p.effect_type == "custom_keymap") has_custom_keymap = true;
            if (p.id == "plugin" || p.effect_type == "plugin") has_plugin = true;
        }
        CHECK(has_static, "Catalog 包含 static");
        CHECK(has_breathing, "Catalog 包含 breathing");
        CHECK(!has_custom_keymap, "Catalog 排除 custom_keymap");
        CHECK(!has_plugin, "Catalog 排除 plugin");
    }

    std::cout << "[Test 11] Base Lighting 读取 (正常预设与高级效果兼容)\n";
    {
        aura::LightingControlService svc(test_cfg_path);
        aura::LightingControlService::BaseLightingDetail base_detail;
        auto op = svc.GetBaseLighting(base_detail);
        CHECK(op.http_status == 200, "GetBaseLighting 返回 200");
        CHECK(base_detail.profile_name == "desktop", "默认方案名称为 desktop");
        CHECK(base_detail.effect == "breathing", "默认方案效果为 breathing");
        CHECK(base_detail.is_builtin_preset == true, "breathing 为内置预设");
        CHECK(base_detail.preset_id == "breathing", "preset_id == breathing");
        CHECK(base_detail.supports_period == true, "supports_period == true");
        CHECK(base_detail.period_ms == 4500, "period_ms == 4500 (来自前序测试修改值)");
        CHECK(base_detail.brightness == 0.8, "brightness == 0.8");

        // 测试高级效果 default_profile 不报错 (Constraint 3)
        std::string adv_cfg = R"json({
          "default_profile": "my_studio",
          "profiles": {
            "my_studio": {
              "type": "custom_keymap",
              "bg": [0, 0, 0],
              "brightness": 0.7,
              "keys": {"W": [255, 0, 0]}
            }
          }
        })json";
        std::string adv_path = CreateTempConfigFile("test_adv_cfg", adv_cfg);
        aura::LightingControlService adv_svc(adv_path);
        aura::LightingControlService::BaseLightingDetail adv_detail;
        auto adv_op = adv_svc.GetBaseLighting(adv_detail);
        CHECK(adv_op.http_status == 200, "高级方案 GetBaseLighting 不得报错 (返回 200)");
        CHECK(adv_detail.profile_name == "my_studio", "profile_name == my_studio");
        CHECK(adv_detail.effect == "custom_keymap", "effect == custom_keymap");
        CHECK(adv_detail.is_builtin_preset == false, "is_builtin_preset == false");
        CHECK(adv_detail.preset_id.empty(), "preset_id 为空");
        CHECK(!adv_detail.supports_period, "supports_period == false");
        CHECK(adv_detail.brightness == 0.7, "brightness == 0.7");
        std::error_code ec_adv;
        std::filesystem::remove(adv_path, ec_adv);
    }

    std::cout << "[Test 12] Base Lighting 错误防御 (缺少 default_profile 或指向不存在方案)\n";
    {
        // 缺少 default_profile
        std::string bad_cfg1 = R"json({"profiles": {"desktop": {"type": "static"}}})json";
        std::string bad_path1 = CreateTempConfigFile("test_bad1", bad_cfg1);
        aura::LightingControlService bad_svc1(bad_path1);
        aura::LightingControlService::BaseLightingDetail dummy_d;
        CHECK(bad_svc1.GetBaseLighting(dummy_d).http_status == 500, "缺少 default_profile 返回 500");
        std::error_code ec1;
        std::filesystem::remove(bad_path1, ec1);

        // default_profile 指向不存在的方案
        std::string bad_cfg2 = R"json({"default_profile": "ghost", "profiles": {"desktop": {"type": "static"}}})json";
        std::string bad_path2 = CreateTempConfigFile("test_bad2", bad_cfg2);
        aura::LightingControlService bad_svc2(bad_path2);
        CHECK(bad_svc2.GetBaseLighting(dummy_d).http_status == 404, "不存在的默认方案返回 404");
        std::error_code ec2;
        std::filesystem::remove(bad_path2, ec2);
    }

    std::cout << "[Test 13] UpdateBaseLighting Targeted Patch (写入 canonical params 并保留无关字段)\n";
    {
        aura::LightingControlService svc(test_cfg_path);
        aura::LightingControlService::BaseLightingDetail before;
        svc.GetBaseLighting(before);

        // 切换 preset 为 wave，调整 brightness 为 0.95
        aura::LightingControlService::BaseLightingPatchInput patch;
        patch.expected_revision = before.revision;
        patch.has_preset = true;
        patch.preset = "wave";
        patch.has_brightness = true;
        patch.brightness = 0.95;

        std::string new_rev;
        auto op = svc.UpdateBaseLighting(patch, new_rev);
        CHECK(op.http_status == 200, "UpdateBaseLighting 切换至 wave 成功");

        // 校验文件内容
        std::ifstream f(test_cfg_path);
        nlohmann::json reloaded;
        f >> reloaded;
        f.close();

        const auto& prof = reloaded["profiles"]["desktop"];
        CHECK(prof["type"] == "wave", "type 变更为 wave");
        CHECK(prof["direction"] == "diag_dl", "写入 wave 的 canonical direction");
        CHECK(prof["thickness"] == 1.0, "写入 wave 的 canonical thickness");
        CHECK(prof["period_ms"] == 3500, "写入 wave 的 canonical period_ms (3500)");
        CHECK(prof["brightness"] == 0.95, "brightness 变更为 0.95");

        // 关键验证：原有的 color1/color2 未知标签严格保留，绝不清空
        CHECK(prof.contains("color1"), "保留原有 color1 (历史调色保留)");
        CHECK(prof.contains("color2"), "保留原有 color2");
        CHECK(prof["unknown_profile_tag"] == "keep_me", "保留原有自定义标签");
        CHECK(reloaded["custom_unknown_root_field"] == "preserved_value", "保留根节点未知属性");
    }

    std::cout << "[Test 14] UpdateBaseLighting 校验防范与 409 冲突\n";
    {
        aura::LightingControlService svc(test_cfg_path);
        aura::LightingControlService::BaseLightingDetail cur;
        svc.GetBaseLighting(cur);

        // 1. 非法 preset (custom_keymap 或 plugin 不得作为普通 preset 写入)
        aura::LightingControlService::BaseLightingPatchInput p_bad_preset;
        p_bad_preset.expected_revision = cur.revision;
        p_bad_preset.has_preset = true;
        p_bad_preset.preset = "custom_keymap";
        std::string dummy;
        CHECK(svc.UpdateBaseLighting(p_bad_preset, dummy).http_status == 400, "custom_keymap 拒绝作为 builtin preset (400)");

        p_bad_preset.preset = "non_existent_effect";
        CHECK(svc.UpdateBaseLighting(p_bad_preset, dummy).http_status == 400, "未知 preset 拒绝 (400)");

        // 2. 静态效果 static 拒绝 period_ms
        aura::LightingControlService::BaseLightingPatchInput p_static;
        p_static.expected_revision = cur.revision;
        p_static.has_preset = true;
        p_static.preset = "static";
        p_static.has_period_ms = true;
        p_static.period_ms = 1000;
        CHECK(svc.UpdateBaseLighting(p_static, dummy).http_status == 400, "static 提供 period_ms 拒绝 (400)");

        // 3. 409 Conflict 冲突
        aura::LightingControlService::BaseLightingPatchInput p_stale;
        p_stale.expected_revision = "outdated_rev_9999";
        p_stale.has_brightness = true;
        p_stale.brightness = 0.5;
        auto op_stale = svc.UpdateBaseLighting(p_stale, dummy);
        CHECK(op_stale.http_status == 409, "过期版本返回 409 Conflict");
        CHECK(op_stale.current_revision == cur.revision, "409 返回当前最新 revision");
    }

    std::cout << "[Test 15] Builtin Presets Parameter Schema 完整性校验\n";
    {
        const auto& presets = aura::GetBuiltinLightingPresets();
        CHECK(presets.size() == 10, "预设总数为 10");

        const aura::BuiltinLightingPreset* wave = aura::FindBuiltinLightingPreset("wave");
        CHECK(wave != nullptr, "查找到 wave 预设");
        CHECK(wave->param_schemas.size() == 2, "wave 拥有 2 个参数 schema");
        CHECK(wave->param_schemas[0].key == "direction", "wave 参数 0 为 direction");
        CHECK(wave->param_schemas[0].type == aura::EffectParamType::Enum, "direction 类型为 Enum");
        CHECK(wave->param_schemas[0].options.size() == 9, "direction 拥有 9 个合法选项");
        CHECK(wave->param_schemas[1].key == "thickness", "wave 参数 1 为 thickness");
        CHECK(wave->param_schemas[1].type == aura::EffectParamType::Number, "thickness 类型为 Number");
        CHECK(wave->param_schemas[1].min_val == 0.1 && wave->param_schemas[1].max_val == 5.0, "thickness 范围 [0.1, 5.0]");

        const aura::BuiltinLightingPreset* cc = aura::FindBuiltinLightingPreset("color_cycle");
        CHECK(cc != nullptr, "查找到 color_cycle 预设");
        CHECK(cc->param_schemas.empty(), "color_cycle 无额外参数 schema (Constraint 2: period_ms 不在 schema 中)");

        const aura::BuiltinLightingPreset* st = aura::FindBuiltinLightingPreset("static");
        CHECK(st != nullptr, "查找到 static 预设");
        CHECK(st->param_schemas.size() == 2, "static 拥有 2 个参数 schema (color, analog)");
        CHECK(st->param_schemas[0].type == aura::EffectParamType::Color, "color 为 Color 类型");
        CHECK(st->param_schemas[1].type == aura::EffectParamType::Boolean, "analog 为 Boolean 类型");
    }

    std::cout << "[Test 16] 共享 Effective-Value 解析器与兼容性语义校验 (Constraint 1 & 6)\n";
    {
        // 1. static 缺少 analog -> RuleEngine 实际默认 false
        nlohmann::json j_static_no_analog = {{"color", {10, 20, 30}}};
        auto p1 = aura::ResolveEffectiveParameters("static", j_static_no_analog);
        CHECK(p1["analog"] == false, "static 缺少 analog 解析为 false (与 RuleEngine 一致)");

        // 2. starry_night 缺少 random_colors -> RuleEngine 实际默认 false
        nlohmann::json j_sn_no_rc = {{"color", {10, 20, 30}}};
        auto p2 = aura::ResolveEffectiveParameters("starry_night", j_sn_no_rc);
        CHECK(p2["random_colors"] == false, "starry_night 缺少 random_colors 解析为 false (与 RuleEngine 一致)");

        // 3. breathing 仅有 legacy color -> color1 回退并与 RuleEngine 一致
        nlohmann::json j_br_legacy_col = {{"color", {12, 34, 56}}, {"color2", {1, 2, 3}}};
        auto p3 = aura::ResolveEffectiveParameters("breathing", j_br_legacy_col);
        CHECK(p3["color1"] == nlohmann::json::array({12, 34, 56}), "breathing 仅有 color 时 color1 成功回退");

        // 4. bg 缺失但存在 background -> 返回实际 background
        nlohmann::json j_bg_fallback = {{"background", {99, 88, 77}}, {"color", {1, 2, 3}}};
        auto p4 = aura::ResolveEffectiveParameters("reactive", j_bg_fallback);
        CHECK(p4["bg"] == nlohmann::json::array({99, 88, 77}), "bg 缺失时 background 成功回退");

        // 5. 仅有 speed_index 无 period_ms -> 与 RuleEngine 解析一致
        nlohmann::json j_speed_only = {{"speed_index", 2}};
        CHECK(aura::ResolveEffectivePeriodMs("breathing", j_speed_only) == 1600, "仅 speed_index 时解析为 1600ms");

        // 6. thickness 钳制
        nlohmann::json j_thick_low = {{"thickness", 0.01}};
        CHECK(aura::ParseAndClampThickness(j_thick_low) == 0.1, "thickness 0.01 钳制为 0.1");
        nlohmann::json j_thick_high = {{"thickness", 99.0}};
        CHECK(aura::ParseAndClampThickness(j_thick_high) == 5.0, "thickness 99.0 钳制为 5.0");
    }

    std::cout << "[Test 17] UpdateBaseLighting 稀疏参数更新与覆盖优先级校验 (Constraint 3 & 6)\n";
    {
        aura::LightingControlService svc(test_cfg_path);
        aura::LightingControlService::BaseLightingDetail cur;
        svc.GetBaseLighting(cur);

        // 切换 preset 为 quicksand，同时提供自定义 thickness: 2.2 与自定义 color1: [12, 34, 56]
        // 验证更新顺序：先应用 quicksand canonical defaults (color2, direction)，
        // 然后用户提供的 explicit parameters (thickness=2.2, color1=[12,34,56]) 最终覆盖并生效！
        aura::LightingControlService::BaseLightingPatchInput patch;
        patch.expected_revision = cur.revision;
        patch.has_preset = true;
        patch.preset = "quicksand";
        patch.has_brightness = true;
        patch.brightness = 0.88;
        patch.has_period_ms = true;
        patch.period_ms = 4000;
        patch.has_parameters = true;
        patch.parameters = {
            {"thickness", 2.2},
            {"color1", {12, 34, 56}}
        };

        std::string new_rev;
        auto op = svc.UpdateBaseLighting(patch, new_rev);
        CHECK(op.http_status == 200, "切 preset 同时提供参数更新成功 (200)");

        // 校验文件内容
        std::ifstream f(test_cfg_path);
        nlohmann::json reloaded;
        f >> reloaded;
        f.close();

        const auto& prof = reloaded["profiles"]["desktop"];
        CHECK(prof["type"] == "quicksand", "type 更新为 quicksand");
        CHECK(prof["brightness"] == 0.88, "brightness 覆盖为 0.88");
        CHECK(prof["period_ms"] == 4000, "period_ms 覆盖为 4000");
        CHECK(prof["color1"] == nlohmann::json::array({12, 34, 56}), "用户显式 color1 覆盖 canonical default");
        CHECK(prof["thickness"] == 2.2, "用户显式 thickness 覆盖 canonical default");
        CHECK(prof["color2"] == nlohmann::json::array({20, 138, 196}), "未指定字段保留 canonical default color2");
        CHECK(prof["direction"] == "diag_dl", "未指定字段保留 canonical default direction");

        // 进一步测试：单独稀疏修改 parameters 中的 direction 为 "spread"，不传 preset
        aura::LightingControlService::BaseLightingPatchInput p_param_only;
        p_param_only.expected_revision = new_rev;
        p_param_only.has_parameters = true;
        p_param_only.parameters = {
            {"direction", "spread"}
        };
        std::string new_rev2;
        auto op2 = svc.UpdateBaseLighting(p_param_only, new_rev2);
        CHECK(op2.http_status == 200, "单独更新 parameters 成功 (200)");

        std::ifstream f2(test_cfg_path);
        nlohmann::json reloaded2;
        f2 >> reloaded2;
        f2.close();
        const auto& prof2 = reloaded2["profiles"]["desktop"];
        CHECK(prof2["direction"] == "spread", "direction 更新为 spread");
        CHECK(prof2["thickness"] == 2.2, "原有 thickness 2.2 完整保留");
        CHECK(prof2["color1"] == nlohmann::json::array({12, 34, 56}), "原有 color1 完整保留");
    }

    std::cout << "[Test 18] UpdateBaseLighting 参数严格防御性校验 (Constraint 2, 3)\n";
    {
        aura::LightingControlService svc(test_cfg_path);
        aura::LightingControlService::BaseLightingDetail cur;
        svc.GetBaseLighting(cur);

        // 1. 目标预设不支持的参数拒绝
        aura::LightingControlService::BaseLightingPatchInput p_bad_key;
        p_bad_key.expected_revision = cur.revision;
        p_bad_key.has_parameters = true;
        p_bad_key.parameters = {{"unsupported_key", 123}};
        std::string dummy;
        CHECK(svc.UpdateBaseLighting(p_bad_key, dummy).http_status == 400, "不支持的参数 key 拒绝 (400)");

        // 2. 约束 2: period_ms 不得在 parameters 中传递
        aura::LightingControlService::BaseLightingPatchInput p_period_in_params;
        p_period_in_params.expected_revision = cur.revision;
        p_period_in_params.has_parameters = true;
        p_period_in_params.parameters = {{"period_ms", 3000}};
        CHECK(svc.UpdateBaseLighting(p_period_in_params, dummy).http_status == 400, "parameters 包含 period_ms 拒绝 (400)");

        // 3. enum 越界 (非合法 9 种方向)
        aura::LightingControlService::BaseLightingPatchInput p_bad_enum;
        p_bad_enum.expected_revision = cur.revision;
        p_bad_enum.has_parameters = true;
        p_bad_enum.parameters = {{"direction", "forward"}};
        CHECK(svc.UpdateBaseLighting(p_bad_enum, dummy).http_status == 400, "非法 direction 枚举拒绝 (400)");

        // 4. thickness 数值越界 (< 0.1 或 > 5.0)
        aura::LightingControlService::BaseLightingPatchInput p_bad_thick;
        p_bad_thick.expected_revision = cur.revision;
        p_bad_thick.has_parameters = true;
        p_bad_thick.parameters = {{"thickness", 0.05}};
        CHECK(svc.UpdateBaseLighting(p_bad_thick, dummy).http_status == 400, "thickness < 0.1 拒绝 (400)");
        p_bad_thick.parameters = {{"thickness", 5.5}};
        CHECK(svc.UpdateBaseLighting(p_bad_thick, dummy).http_status == 400, "thickness > 5.0 拒绝 (400)");

        // 5. color 格式错误 (非 3 元素、非整型、超出 [0, 255])
        aura::LightingControlService::BaseLightingPatchInput p_bad_color;
        p_bad_color.expected_revision = cur.revision;
        p_bad_color.has_parameters = true;
        p_bad_color.parameters = {{"color1", {255, 0}}};
        CHECK(svc.UpdateBaseLighting(p_bad_color, dummy).http_status == 400, "color 长度不足 3 拒绝 (400)");
        p_bad_color.parameters = {{"color1", {256, 0, 0}}};
        CHECK(svc.UpdateBaseLighting(p_bad_color, dummy).http_status == 400, "color 通道 > 255 拒绝 (400)");

        // 6. boolean 格式错误 (切至 static，给 analog 传字符串 "true")
        aura::LightingControlService::BaseLightingPatchInput p_bad_bool;
        p_bad_bool.expected_revision = cur.revision;
        p_bad_bool.has_preset = true;
        p_bad_bool.preset = "static";
        p_bad_bool.has_parameters = true;
        p_bad_bool.parameters = {{"analog", "true"}};
        CHECK(svc.UpdateBaseLighting(p_bad_bool, dummy).http_status == 400, "boolean 传入字符串拒绝 (400)");
    }

    {
        auto global_path = CreateTempConfigFile("global_fps", R"({"default_profile":"base","profiles":{"base":{"type":"static"},"override":{"type":"static","fps":60}},"custom":{"keep":[1,null]}})");
        aura::LightingControlService global(global_path);
        int fps=0; std::string revision, next;
        CHECK(global.GetGlobalFps(fps, revision).http_status == 200 && fps == 25, "global default 25");
        auto original_revision = revision;
        CHECK(global.UpdateGlobalFps(10, revision, next).http_status == 200, "global lower boundary");
        CHECK(global.UpdateGlobalFps(100, original_revision, revision).http_status == 409, "global stale revision");
        CHECK(global.UpdateGlobalFps(100, next, revision).http_status == 200, "global upper boundary");
        CHECK(global.UpdateGlobalFps(101, revision, next).http_status == 400, "global rejects out of range");
        CHECK(global.UpdateGlobalFps(25, "", next).http_status == 400, "global requires revision");
        std::ifstream input(global_path); auto data=nlohmann::json::parse(input); input.close();
        CHECK(data["fps"] == 100 && data["profiles"]["override"]["fps"] == 60 && !data["profiles"]["base"].contains("fps") && data["custom"]["keep"][1].is_null(), "global preserves overrides and unrelated data");
        global.SetFileReplacerForTesting([](const std::wstring&, const std::wstring&) { return false; });
        CHECK(global.UpdateGlobalFps(25, revision, next).http_status == 500, "global atomic failure");
        CHECK(global.GetGlobalFps(fps, next).http_status == 200 && fps == 100 && next == revision, "global atomic failure preserves original");
        aura::RuleEngine reader; CHECK(reader.LoadConfig(global_path), "global config remains valid runtime input");
        CHECK(reader.GetFps() == 100, "runtime consumes persisted global FPS");
        std::filesystem::remove(global_path);
    }

    {
        const auto path = CreateTempConfigFile("config_contract", R"({"profiles":{"desktop":{"type":"static"}},"orchestration":{"rules":[]},"opaque":{"keep":true}})");
        auto read_config = [&]() { std::ifstream input(path); return nlohmann::json::parse(input); };
        const auto original = read_config();
        aura::RuleEngine reader;
        CHECK(reader.LoadConfig(path) && reader.GetFps() == 25, "missing optional root fields use documented defaults");
        for (const auto& invalid : {
                 R"([])",
                 R"({"fps":"25","profiles":{"desktop":{"type":"static"}}})",
                 R"({"fps":9,"profiles":{"desktop":{"type":"static"}}})",
                 R"({"fps":101,"profiles":{"desktop":{"type":"static"}}})",
                 R"({"profiles":{}})",
                 R"({"default_profile":"missing","profiles":{"desktop":{"type":"static"}}})",
                 R"({"profiles":{"desktop":{"type":"unknown"}}})",
                 R"({"profiles":{"desktop":{"type":"static","color":[256,0,0]}}})",
                 R"({"profiles":{"desktop":{"type":"breathing","period_ms":0}}})",
                 R"({"profiles":{"desktop":{"type":"wave","direction":"sideways"}}})",
                 R"({"profiles":{"desktop":{"type":"static","brightness":256}}})"}) {
            bool rejected = false;
            try { aura::AtomicWriteConfigFile(path, invalid); }
            catch (const std::exception&) { rejected = true; }
            CHECK(rejected, "invalid configuration rejected before atomic replacement");
            CHECK(read_config() == original, "invalid write preserves existing configuration");
        }
        for (int boundary : {10, 100}) {
            auto document = original;
            document["fps"] = boundary;
            CHECK(aura::AtomicWriteConfigFile(path, document.dump()), "valid FPS boundary is writable");
            CHECK(reader.LoadConfig(path) && reader.GetFps() == boundary, "runtime reads canonical FPS boundary");
        }
        std::filesystem::remove(path);
    }

    {
        const auto path = CreateTempConfigFile("same_timestamp_reload", R"({"fps":25,"profiles":{"desktop":{"type":"static"}}})");
        aura::RuleEngine reader;
        CHECK(reader.LoadConfig(path), "initial config loads for replacement identity test");
        const auto original_time = std::filesystem::last_write_time(path);
        for (int fps : {40, 60}) {
            const auto content = std::string(R"({"fps":)") + std::to_string(fps) + R"(,"profiles":{"desktop":{"type":"static"}}})";
            CHECK(aura::AtomicWriteConfigFile(path, content), "rapid config replacement succeeds");
            std::filesystem::last_write_time(path, original_time);
            CHECK(reader.CheckAndReload() && reader.GetFps() == fps,
                "atomic replacement reloads newest config even when timestamp is unchanged");
        }
        std::filesystem::remove(path);
    }

    // 清理测试临时文件
    std::error_code ec;
    std::filesystem::remove(test_cfg_path, ec);

    if (failures == 0) {
        std::cout << "\n[PASS] 所有 LightingControlService 单元测试均已成功通过！\n";
        return 0;
    } else {
        std::cerr << "\n[FAIL] 存在 " << failures << " 个测试断言失败！\n";
        return 1;
    }
}
