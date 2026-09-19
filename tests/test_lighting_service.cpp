#include "test_util.h"
#include "config/lighting_service.h"
#include "config/rule_engine.h"
#include "third_party/json.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>

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
  "rules": [
    {"process": "cs2.exe", "profile": "desktop"}
  ],
  "gsi_bindings": [
    {"field": "player_state.health", "operator": "<", "value": 20, "profile": "desktop"}
  ],
  "profiles": {
    "desktop": {
      "type": "breathing",
      "color1": [0, 80, 200],
      "period_ms": 3500,
      "brightness": 1.0,
      "unknown_profile_tag": "keep_me"
    },
    "coding": {
      "type": "static",
      "color": [10, 30, 50],
      "brightness": 128
    },
    "wave_glow": {
      "type": "wave",
      "period_ms": 4000,
      "fps": 50
    }
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
        CHECK(reloaded["rules"].size() == 1, "进程规则 rules 保留");
        CHECK(reloaded["gsi_bindings"].size() == 1, "GSI 绑定保留");
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

        // RuleEngine 检测热重载
        bool reloaded = engine.CheckAndReload();
        CHECK(reloaded, "RuleEngine::CheckAndReload 成功检测到文件修改并重载");

        auto prof = engine.GetProfile("desktop");
        CHECK(prof != nullptr, "desktop 方案有效");
        // RuleEngine::ParseBrightness 0.8 -> round(0.8 * 255) = 204
        CHECK(prof->brightness == static_cast<uint8_t>(std::round(0.8 * 255.0)), "RuleEngine 内存中 brightness 成功热重载为 0.8 (204)");
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
