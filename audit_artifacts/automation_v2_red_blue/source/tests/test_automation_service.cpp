#include "test_util.h"
#include "config/automation_service.h"
#include "config/rule_engine.h"
#include "third_party/json.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
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

const std::string kSampleConfig = R"json({
  "default_profile": "desktop",
  "fps": 25,
  "hardware_backend": "auto",
  "profiles": {
    "desktop": {
      "type": "static",
      "color": [0, 80, 200]
    },
    "cs2_gamer": {
      "type": "breathing",
      "color1": [255, 100, 0],
      "color2": [50, 10, 0],
      "period_ms": 2500
    },
    "coding": {
      "type": "static",
      "color": [0, 255, 128]
    }
  },
  "rules": [
    {
      "process": "cs2.exe",
      "profile": "cs2_gamer",
      "custom_extra_tag": "keep_intact"
    },
    {
      "process": "devenv.exe",
      "profile": "coding"
    }
  ]
})json";

} // namespace

int main() {
    int failures = 0;

    std::cout << "[Test 1] 进程名规范化 (CanonicalizeProcessName) 校验\n";
    {
        CHECK(aura::CanonicalizeProcessName("cs2") == "cs2.exe", "'cs2' 补全为 'cs2.exe'");
        CHECK(aura::CanonicalizeProcessName("cs2.exe") == "cs2.exe", "'cs2.exe' 保持不变");
        CHECK(aura::CanonicalizeProcessName("  CS2.EXE  ") == "cs2.exe", "去除首尾空格并转小写");
        CHECK(aura::CanonicalizeProcessName("Code") == "code.exe", "'Code' -> 'code.exe'");
        CHECK(aura::CanonicalizeProcessName("D:\\Games\\Steam\\CS2.EXE") == "cs2.exe", "剥离 Windows 路径提取文件名并转小写");
        CHECK(aura::CanonicalizeProcessName("/usr/bin/code") == "code.exe", "剥离 Unix 路径提取文件名并补齐 .exe");
        CHECK(aura::CanonicalizeProcessName("") == "", "空输入返回空字符串");
        CHECK(aura::CanonicalizeProcessName("   ") == "", "全空格输入返回空字符串");
        CHECK(aura::CanonicalizeProcessName(".exe") == "", "仅为 '.exe' 返回空字符串");
        CHECK(aura::CanonicalizeProcessName("  .EXE  ") == "", "仅为 '  .EXE  ' 返回空字符串");
        CHECK(aura::CanonicalizeProcessName("foo.dll") == "", "foo.dll 拒绝 (非 .exe 后缀)");
        CHECK(aura::CanonicalizeProcessName("script.bat") == "", "script.bat 拒绝 (非 .exe 后缀)");
        CHECK(aura::CanonicalizeProcessName("app.cmd") == "", "app.cmd 拒绝 (非 .exe 后缀)");
    }

    std::string test_cfg_path = CreateTempConfigFile("test_automation", kSampleConfig);
    std::string current_revision;

    std::cout << "[Test 2] GetRules 规则列表读取与顺序保持\n";
    {
        aura::AutomationControlService svc(test_cfg_path);
        std::vector<aura::AutomationControlService::AutomationRule> rules;
        auto op = svc.GetRules(rules, current_revision);
        CHECK(op.http_status == 200, "GetRules 返回 200");
        CHECK(!current_revision.empty(), "返回有效文件 Revision");
        CHECK(rules.size() == 2, "规则数量为 2");
        CHECK(rules[0].index == 0, "规则 0 raw index 为 0");
        CHECK(rules[0].process == "cs2.exe", "规则 0 进程为 cs2.exe");
        CHECK(rules[0].profile == "cs2_gamer", "规则 0 方案为 cs2_gamer");
        CHECK(rules[1].index == 1, "规则 1 raw index 为 1");
        CHECK(rules[1].process == "devenv.exe", "规则 1 进程为 devenv.exe");
        CHECK(rules[1].profile == "coding", "规则 1 方案为 coding");
    }

    std::cout << "[Test 3] AddRule 规则追加与前置校验\n";
    {
        aura::AutomationControlService svc(test_cfg_path);

        // 1. 未知 Profile 校验 (400 profile_not_found)
        aura::AutomationControlService::AutomationRule bad_prof_rule{"notepad.exe", "non_existent_profile"};
        aura::AutomationControlService::AutomationRule dummy_out;
        int dummy_idx = -1;
        std::string dummy_rev;
        auto op_bad_prof = svc.AddRule(current_revision, bad_prof_rule, dummy_out, dummy_idx, dummy_rev);
        CHECK(op_bad_prof.http_status == 400, "未知 profile 拒绝 (400)");
        CHECK(op_bad_prof.error_code == "profile_not_found", "错误码为 profile_not_found");

        // 2. 非法进程名 (400 Validation Error)
        aura::AutomationControlService::AutomationRule bad_proc_rule{"   ", "coding"};
        auto op_bad_proc = svc.AddRule(current_revision, bad_proc_rule, dummy_out, dummy_idx, dummy_rev);
        CHECK(op_bad_proc.http_status == 400, "空进程名拒绝 (400)");

        // 3. 进程名冲突 (409 duplicate_process)
        aura::AutomationControlService::AutomationRule dup_rule{"CS2.EXE", "coding"};
        auto op_dup = svc.AddRule(current_revision, dup_rule, dummy_out, dummy_idx, dummy_rev);
        CHECK(op_dup.http_status == 409, "重复进程规则拒绝 (409)");
        CHECK(op_dup.error_code == "duplicate_process", "错误码为 duplicate_process");

        // 4. 正常追加 (201 Created)
        aura::AutomationControlService::AutomationRule ok_rule{"code", "coding"};
        aura::AutomationControlService::AutomationRule added_rule;
        int added_idx = -1;
        std::string new_rev;
        auto op_ok = svc.AddRule(current_revision, ok_rule, added_rule, added_idx, new_rev);
        CHECK(op_ok.http_status == 201, "正常添加规则返回 201");
        CHECK(added_rule.process == "code.exe", "进程自动规范化为 'code.exe'");
        CHECK(added_rule.profile == "coding", "方案为 'coding'");
        CHECK(added_idx == 2, "新增规则索引为 2 (尾部追加)");
        CHECK(!new_rev.empty() && new_rev != current_revision, "生成新 Revision");

        current_revision = new_rev;

        // 验证重新读取
        std::vector<aura::AutomationControlService::AutomationRule> check_rules;
        std::string check_rev;
        svc.GetRules(check_rules, check_rev);
        CHECK(check_rules.size() == 3, "规则总数增至 3");
        CHECK(check_rules[2].process == "code.exe", "规则 2 进程已持久化为 code.exe");
    }

    std::cout << "[Test 4] UpdateRule 细粒度修改与字段保留\n";
    {
        aura::AutomationControlService svc(test_cfg_path);

        // 1. 越界索引 (404 rule_not_found)
        aura::AutomationControlService::AutomationRule dummy_out;
        std::string dummy_rev;
        auto op_oob = svc.UpdateRule(99, current_revision, std::nullopt, "coding", dummy_out, dummy_rev);
        CHECK(op_oob.http_status == 404, "越界索引更新返回 404");
        CHECK(op_oob.error_code == "rule_not_found", "错误码为 rule_not_found");

        // 2. 修改时重复进程冲突 (与规则 2 'code.exe' 冲突)
        auto op_dup = svc.UpdateRule(0, current_revision, "code.exe", std::nullopt, dummy_out, dummy_rev);
        CHECK(op_dup.http_status == 409, "修改进程为已有进程时返回 409 duplicate_process");
        CHECK(op_dup.error_code == "duplicate_process", "错误码为 duplicate_process");

        // 3. 修改同一规则进程为自身变体 (cs2.exe -> CS2.exe) 不应判定为冲突
        auto op_self = svc.UpdateRule(0, current_revision, "CS2.exe", std::nullopt, dummy_out, dummy_rev);
        CHECK(op_self.http_status == 200, "更新自身进程大小写不报错");

        current_revision = dummy_rev;

        // 4. 稀疏修改方案 (修改规则 0 的 profile: cs2_gamer -> desktop) 并保留原条目的 custom_extra_tag
        aura::AutomationControlService::AutomationRule updated_rule;
        std::string updated_rev;
        auto op_update = svc.UpdateRule(0, current_revision, std::nullopt, "desktop", updated_rule, updated_rev);
        CHECK(op_update.http_status == 200, "稀疏修改 profile 返回 200");
        CHECK(updated_rule.process == "cs2.exe", "进程保持 cs2.exe");
        CHECK(updated_rule.profile == "desktop", "方案变更为 desktop");

        current_revision = updated_rev;

        // 检查原 JSON 中的 custom_extra_tag 是否完好保留
        std::string raw_content;
        std::string raw_rev;
        aura::ReadRawConfigFile(test_cfg_path, raw_content, raw_rev);
        auto root = nlohmann::ordered_json::parse(raw_content);
        CHECK(root["rules"][0].value("custom_extra_tag", "") == "keep_intact", "非 CRUD 既有扩展字段 custom_extra_tag 完好保留");
    }

    std::cout << "[Test 5] DeleteRule 精确索引删除与列表收缩\n";
    {
        aura::AutomationControlService svc(test_cfg_path);

        // 1. 越界删除 (404 rule_not_found)
        std::string dummy_rev;
        auto op_oob = svc.DeleteRule(10, current_revision, dummy_rev);
        CHECK(op_oob.http_status == 404, "越界删除返回 404");

        // 2. 删除索引 1 ("devenv.exe" -> "coding")
        std::string del_rev;
        auto op_del = svc.DeleteRule(1, current_revision, del_rev);
        CHECK(op_del.http_status == 200, "删除规则 1 返回 200");
        CHECK(!del_rev.empty(), "生成新 Revision");

        current_revision = del_rev;

        // 3. 验证删除后列表收缩与后续元素移位
        std::vector<aura::AutomationControlService::AutomationRule> after_rules;
        std::string after_rev;
        svc.GetRules(after_rules, after_rev);
        CHECK(after_rules.size() == 2, "规则总数从 3 缩减至 2");
        CHECK(after_rules[0].process == "cs2.exe", "规则 0 仍为 cs2.exe");
        CHECK(after_rules[1].process == "code.exe", "原规则 2 前移成为规则 1");
    }

    std::cout << "[Test 6] 乐观并发控制 (409 Conflict revision_conflict) 校验\n";
    {
        aura::AutomationControlService svc(test_cfg_path);
        std::string stale_rev = "stale_dummy_revision_99999";

        // AddRule 携带过期 revision
        aura::AutomationControlService::AutomationRule new_r{"dummy", "desktop"};
        aura::AutomationControlService::AutomationRule dummy_out;
        int dummy_idx = -1;
        std::string dummy_rev;
        auto op_add = svc.AddRule(stale_rev, new_r, dummy_out, dummy_idx, dummy_rev);
        CHECK(op_add.http_status == 409, "过期 Revision AddRule 返回 409 Conflict");
        CHECK(op_add.error_code == "revision_conflict", "错误码为 revision_conflict");
        CHECK(op_add.current_revision == current_revision, "携带最新 current_revision");

        // UpdateRule 携带过期 revision
        auto op_up = svc.UpdateRule(0, stale_rev, "new_proc.exe", std::nullopt, dummy_out, dummy_rev);
        CHECK(op_up.http_status == 409, "过期 Revision UpdateRule 返回 409 Conflict");
        CHECK(op_up.error_code == "revision_conflict", "错误码为 revision_conflict");

        // DeleteRule 携带过期 revision
        auto op_del = svc.DeleteRule(0, stale_rev, dummy_rev);
        CHECK(op_del.http_status == 409, "过期 Revision DeleteRule 返回 409 Conflict");
        CHECK(op_del.error_code == "revision_conflict", "错误码为 revision_conflict");
    }

    std::cout << "[Test 7] 原子替换失败测试接缝 (Atomic File Replacer Seam)\n";
    {
        aura::AutomationControlService svc(test_cfg_path);
        // 注入模拟替换失败
        svc.SetFileReplacerForTesting([](const std::wstring& /*tmp*/, const std::wstring& /*target*/) {
            return false;
        });

        aura::AutomationControlService::AutomationRule test_rule{"seam_test", "desktop"};
        aura::AutomationControlService::AutomationRule dummy_out;
        int dummy_idx = -1;
        std::string dummy_rev;
        auto op = svc.AddRule(current_revision, test_rule, dummy_out, dummy_idx, dummy_rev);
        CHECK(op.http_status == 500, "替换失败返回 500");

        // 验证原文件完整性与临时文件清理
        std::vector<aura::AutomationControlService::AutomationRule> check_rules;
        std::string check_rev;
        svc.GetRules(check_rules, check_rev);
        CHECK(check_rules.size() == 2, "写入失败时不破坏原文件 Last Known Good");
        std::filesystem::path tmp_check = std::filesystem::path(test_cfg_path).wstring() + L".tmp";
        CHECK(!std::filesystem::exists(tmp_check), "失败后清理 .tmp 临时文件");
    }

    std::cout << "[Test 8] HTTP 路由端到端通信验证\n";
    {
        aura::AutomationControlService svc(test_cfg_path);
        httplib::Server svr;
        svc.RegisterRoutes(svr);

        svr.set_socket_options([](socket_t sock) {
#ifdef _WIN32
            int opt = 1;
            ::setsockopt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&opt), sizeof(opt));
#endif
        });

        int test_port = 19988;
        bool bound = svr.bind_to_port("127.0.0.1", test_port);
        CHECK(bound, "绑定本地测试端口 19988 成功");

        std::thread svr_thread([&]() {
            svr.listen_after_bind();
        });
        svr.wait_until_ready();

        httplib::Client cli("127.0.0.1", test_port);

        // 1. GET /api/automation/rules
        auto res_get = cli.Get("/api/automation/rules");
        CHECK(res_get && res_get->status == 200, "GET /api/automation/rules 返回 200");
        auto j_get = nlohmann::json::parse(res_get->body);
        CHECK(j_get["status"] == "ok", "status == ok");
        CHECK(j_get["api_version"] == 1, "api_version == 1");
        CHECK(j_get["rules"].size() == 2, "rules.size == 2");
        std::string live_rev = j_get["revision"].get<std::string>();

        // 2. POST /api/automation/rules
        nlohmann::json post_body = {
            {"expected_revision", live_rev},
            {"rule", {
                {"process", "discord"},
                {"profile", "desktop"}
            }}
        };
        // 2a. Content-Type 校验 (非 application/json 返回 415)
        auto res_bad_ct = cli.Post("/api/automation/rules", post_body.dump(), "text/plain");
        CHECK(res_bad_ct && res_bad_ct->status == 415, "非 application/json 请求头返回 415 Unsupported Media Type");

        // 2b. 非法跨站 Origin 校验 (防 localhost 前缀绕过)
        httplib::Headers evil_origin_hdr = {{"Origin", "http://localhost.attacker.com"}};
        auto res_evil_orig = cli.Post("/api/automation/rules", evil_origin_hdr, post_body.dump(), "application/json");
        CHECK(res_evil_orig && res_evil_orig->status == 403, "非法 Origin (http://localhost.attacker.com) 返回 403 Forbidden");

        // 2c. 原生客户端 POST (无 Origin/Referer) 返回 201
        auto res_post = cli.Post("/api/automation/rules", post_body.dump(), "application/json");
        CHECK(res_post && res_post->status == 201, "POST /api/automation/rules 返回 201");
        auto j_post = nlohmann::json::parse(res_post->body);
        CHECK(j_post["rule"]["process"] == "discord.exe", "规范化为 discord.exe");
        CHECK(j_post["rule"]["index"] == 2, "rule 对象返回 authoritative index 2");
        CHECK(j_post["index"] == 2, "追加索引为 2");
        live_rev = j_post["revision"].get<std::string>();

        // 3. PATCH /api/automation/rules/2
        nlohmann::json patch_body = {
            {"expected_revision", live_rev},
            {"rule", {
                {"profile", "coding"}
            }}
        };
        auto res_patch = cli.Patch("/api/automation/rules/2", patch_body.dump(), "application/json");
        CHECK(res_patch && res_patch->status == 200, "PATCH /api/automation/rules/2 返回 200");
        auto j_patch = nlohmann::json::parse(res_patch->body);
        CHECK(j_patch["rule"]["profile"] == "coding", "profile 更新为 coding");
        CHECK(j_patch["rule"]["index"] == 2, "rule 对象包含 authoritative index 2");
        live_rev = j_patch["revision"].get<std::string>();

        // 4. DELETE /api/automation/rules/2
        nlohmann::json del_body = {
            {"expected_revision", live_rev}
        };
        auto res_del = cli.Delete("/api/automation/rules/2", del_body.dump(), "application/json");
        CHECK(res_del && res_del->status == 200, "DELETE /api/automation/rules/2 返回 200");
        auto j_del = nlohmann::json::parse(res_del->body);
        CHECK(j_del["deleted_index"] == 2, "deleted_index == 2");

        svr.stop();
        if (svr_thread.joinable()) svr_thread.join();
    }

    // -------------------------------------------------------------------------
    // Test 9: 统一跨进程互斥量名称与锁互斥验证
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- Test 9: Unified Cross-Process Mutex Identity & Mutex Lock ---\n";
        CHECK(std::wstring(aura::kConfigWriteMutexName) == L"Local\\AceHFXAuraConfigWriteMutex",
              "kConfigWriteMutexName 严格等于 Local\\AceHFXAuraConfigWriteMutex");

        aura::NamedConfigLock lock1;
        CHECK(lock1.IsAcquired(), "lock1 successfully acquired with default kConfigWriteMutexName");

        bool lock2_acquired = false;
        std::thread t([&]() {
            aura::NamedConfigLock lock2(aura::kConfigWriteMutexName, 100);
            lock2_acquired = lock2.IsAcquired();
        });
        t.join();
        CHECK(!lock2_acquired, "lock2 timed out because lock1 holds kConfigWriteMutexName");
    }

    // -------------------------------------------------------------------------
    // Test 10: 权威原始索引保全 (跳过畸形对象后原始数组索引依然准确映射)
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- Test 10: Authoritative Raw Rule Indices with Malformed Entries ---\n";
        const std::string kConfigWithMalformed = R"json({
          "default_profile": "desktop",
          "profiles": {
            "desktop": {"type": "static", "color": [0,0,0]},
            "cs2_gamer": {"type": "breathing"},
            "coding": {"type": "static"}
          },
          "rules": [
            {"process": "cs2.exe", "profile": "cs2_gamer"},
            {"legacy_mode": true, "note": "malformed/legacy without process"},
            {"process": "devenv.exe", "profile": "coding"}
          ]
        })json";

        std::string malformed_cfg_path = CreateTempConfigFile("test_malformed_rules", kConfigWithMalformed);
        aura::AutomationControlService svc(malformed_cfg_path);

        std::vector<aura::AutomationControlService::AutomationRule> rules;
        std::string rev;
        auto op = svc.GetRules(rules, rev);
        CHECK(op.http_status == 200, "GetRules 返回 200");
        CHECK(rules.size() == 2, "仅返回 2 条有效规则 (跳过第 1 项畸形条目)");
        CHECK(rules[0].index == 0, "首条规则的 raw index 为 0");
        CHECK(rules[0].process == "cs2.exe", "首条规则为 cs2.exe");
        CHECK(rules[1].index == 2, "第二条可见规则的 raw index 为 2 (保留原始 JSON 数组索引)");
        CHECK(rules[1].process == "devenv.exe", "第二条可见规则为 devenv.exe");

        // 编辑第二条可见规则 (使用权威 index 2)
        aura::AutomationControlService::AutomationRule updated_rule;
        std::string new_rev;
        auto op_update = svc.UpdateRule(rules[1].index, rev, std::nullopt, std::string("desktop"), updated_rule, new_rev);
        CHECK(op_update.http_status == 200, "UpdateRule 针对原始索引 2 更新成功");
        CHECK(updated_rule.index == 2, "更新后的规则 index 依然为 2");
        CHECK(updated_rule.profile == "desktop", "方案更新为 desktop");

        // 重新读取磁盘文件，验证索引 1 的畸形对象未被误覆盖，索引 2 已更新
        std::string check_content, check_rev;
        CHECK(aura::ReadRawConfigFile(malformed_cfg_path, check_content, check_rev), "读取修改后配置文件成功");
        auto root = nlohmann::ordered_json::parse(check_content);
        CHECK(root["rules"].size() == 3, "rules 数组长度仍为 3");
        CHECK(root["rules"][1].contains("legacy_mode"), "索引 1 的 legacy 对象完好无损");
        CHECK(root["rules"][2]["profile"] == "desktop", "索引 2 的 profile 已正确更新为 desktop");

        // 删除第二条可见规则 (使用权威 index 2)
        auto op_del = svc.DeleteRule(rules[1].index, new_rev, check_rev);
        CHECK(op_del.http_status == 200, "DeleteRule 针对原始索引 2 删除成功");

        std::string del_content, del_rev;
        aura::ReadRawConfigFile(malformed_cfg_path, del_content, del_rev);
        auto del_root = nlohmann::ordered_json::parse(del_content);
        CHECK(del_root["rules"].size() == 2, "rules 数组删除后缩减至 2 项");
        CHECK(del_root["rules"][1].contains("legacy_mode"), "索引 1 的 legacy 对象依然保留");

        std::error_code ec;
        std::filesystem::remove(malformed_cfg_path, ec);
    }

    // 清理临时配置文件
    std::error_code ec;
    std::filesystem::remove(test_cfg_path, ec);

    std::cout << "\n=========================================================\n";
    if (failures == 0) {
        std::cout << "  ALL AUTOMATION SERVICE TESTS PASSED!\n";
    } else {
        std::cerr << "  FAILED: " << failures << " assertions failed!\n";
    }
    std::cout << "=========================================================\n";

    return failures;
}
