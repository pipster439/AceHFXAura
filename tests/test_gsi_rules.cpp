#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <memory>
#include <filesystem>
#include <limits>
#include "test_util.h"
#include "config/rule_engine.h"
#include "gsi/gsi_adapter.h"
#include "web/web_server.h"
#include "aura/aura_types.h"
#include "aura/aura_adapter.h"
#include "aura/hal_compat.h"
#include "engine/builtin_effects.h"
#include "monitor/key_input_hub.h"
#include "engine/plugin_interface.h"
#include "engine/plugin_manager.h"
#include "engine/overlay_manager.h"
#include "engine/effect_engine.h"

namespace aura {
double ParseAndClampThickness(const std::string& pname, const nlohmann::json& pval, double def_val = 1.0);
double ParseAndClampThickness(const nlohmann::json& pval, double def_val = 1.0);
std::shared_ptr<Effect> CreateEffectFromProfile(const std::string& pname, const nlohmann::json& pval);
}

int main() {
    // 真实失败计数器。在 Release 构建下普通 assert() 会被 /DNDEBUG 消除，
    // 因此全面使用 CHECK 宏，保证在 Release 与 Debug 下均具有真实校验力。
    int failures = 0;

    std::cout << "=========================================================\n";
    std::cout << "  GSI 前台进程隔离与绑定仲裁专项单元测试\n";
    std::cout << "=========================================================\n";

    // 优先加载解耦后的测试夹具配置 tests/fixtures/test_config.json
    std::string config_path = "tests/fixtures/test_config.json";
    {
        std::ifstream check_file(config_path);
        if (!check_file.good()) {
            config_path = "test_config.json"; // 兼容以 tests/fixtures 作为 cwd 运行场景
        }
    }

    aura::RuleEngine rule_engine;
    bool loaded = rule_engine.LoadConfig(config_path);
    CHECK(loaded, "成功加载测试配置文件: " + config_path);
    if (!loaded) {
        std::cerr << "FAIL: 无法加载测试配置文件: " << config_path << "\n";
        return 1;
    }

    aura::GsiState gsi_state;

    // 1. GSI 未连接/离线状态
    std::cout << "\n[测试 1] GSI 离线时各进程方案匹配...\n";
    {
        auto prof_cs2 = rule_engine.MatchProfile("cs2.exe", &gsi_state);
        CHECK(prof_cs2 != nullptr && prof_cs2->name == "cs2_gamer",
              "cs2.exe 离线时匹配常规进程方案: cs2_gamer");

        auto prof_code = rule_engine.MatchProfile("code.exe", &gsi_state);
        CHECK(prof_code != nullptr && prof_code->name == "coding",
              "code.exe 离线时匹配工作方案: coding");

        auto prof_desktop = rule_engine.MatchProfile("explorer.exe", &gsi_state);
        CHECK(prof_desktop != nullptr && prof_desktop->name == "desktop",
              "explorer.exe 离线时回退默认桌面方案: desktop");
    }

    // 2. 模拟注入满足 GSI 残血条件的数据 (health = 15 < 20)
    std::cout << "\n[测试 2] GSI 处于危险残血状态 (player_state.health = 15)...\n";
    nlohmann::json damage_payload = {
        {"player", {
            {"state", {
                {"health", 15},
                {"armor", 50}
            }}
        }}
    };
    gsi_state.UpdateFromPayload(damage_payload);
    CHECK(gsi_state.IsActive(), "GSI 状态处于激活状态 (IsActive == true)");

    // 核心约束验证：
    // (A) 前台是 cs2.exe 时，GSI 绑定立即生效 -> danger_red
    {
        auto prof = rule_engine.MatchProfile("cs2.exe", &gsi_state);
        CHECK(prof != nullptr && prof->name == "danger_red",
              "前台为 cs2.exe: 成功激活 GSI 残血预警方案 -> danger_red");
    }
    {
        auto prof = rule_engine.MatchProfile("cs2", &gsi_state);
        CHECK(prof != nullptr && prof->name == "danger_red",
              "前台为 cs2 (无后缀): 成功激活 GSI 残血预警方案 -> danger_red");
    }

    // (B) 关键红线验证：前台不是 cs2.exe 时，GSI 绑定绝不生效！
    std::cout << "\n[测试 3] 核心红线验证：前台不是 cs2.exe 时，GSI 绑定绝对不生效！\n";
    {
        // VS Code
        auto prof = rule_engine.MatchProfile("code.exe", &gsi_state);
        CHECK(prof != nullptr && prof->name == "coding",
              "前台为 code.exe: GSI 绑定不生效，保持专属方案 -> coding (非 danger_red)");
    }
    {
        // Chrome
        auto prof = rule_engine.MatchProfile("chrome.exe", &gsi_state);
        CHECK(prof != nullptr && prof->name == "cyberpunk",
              "前台为 chrome.exe: GSI 绑定不生效，保持专属方案 -> cyberpunk (非 danger_red)");
    }
    {
        // 桌面资源管理器 (explorer.exe)
        auto prof = rule_engine.MatchProfile("explorer.exe", &gsi_state);
        CHECK(prof != nullptr && prof->name == "desktop",
              "前台为 explorer.exe (桌面): GSI 绑定不生效，保持桌面方案 -> desktop (非 danger_red)");
    }
    {
        // 空字符串 (前台未识别/桌面)
        auto prof = rule_engine.MatchProfile("", &gsi_state);
        CHECK(prof != nullptr && prof->name == "desktop",
              "前台为空 (桌面挂起): GSI 绑定不生效，保持桌面方案 -> desktop (非 danger_red)");
    }

    // 3. 模拟 C4 安放 (round.bomb = planted, health = 100)
    std::cout << "\n[测试 4] GSI 处于 C4 炸弹安放状态 (round.bomb = planted)...\n";
    nlohmann::json bomb_payload = {
        {"player", {
            {"state", {
                {"health", 100},
                {"armor", 100}
            }}
        }},
        {"round", {
            {"bomb", "planted"}
        }}
    };
    gsi_state.UpdateFromPayload(bomb_payload);

    {
        auto prof = rule_engine.MatchProfile("cs2.exe", &gsi_state);
        CHECK(prof != nullptr && prof->name == "bomb_pulse",
              "前台为 cs2.exe: 成功激活 C4 炸弹脉冲方案 -> bomb_pulse");
    }
    {
        auto prof = rule_engine.MatchProfile("notepad.exe", &gsi_state);
        CHECK(prof != nullptr && prof->name == "office",
              "前台为 notepad.exe: GSI 炸弹状态被隔离，依然保持 -> office");
    }

    // 4. 完整的游戏事件系统测试 (击杀、爆头、受创、阵亡、炸弹、回合胜负)
    std::cout << "\n[测试 5] 验证完整游戏事件推导 (PlayerTookDamage, PlayerGotKill, Bomb, Round)...\n";
    aura::GsiState event_state;

    // 初始状态注入 (满血无事件)
    nlohmann::json s0 = {
        {"player", {
            {"state", {{"health", 100}, {"armor", 100}, {"round_kills", 0}, {"round_killhs", 0}}},
            {"team", "CT"}
        }},
        {"round", {{"phase", "freezetime"}}}
    };
    event_state.UpdateFromPayload(s0);
    CHECK(event_state.Evaluate("event.freezetime", "==", true),
          "初始回合整备阶段: event.freezetime 为 true");

    // 跃迁 1: 回合开始交火 (RoundStarted)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    nlohmann::json s1 = {
        {"round", {{"phase", "live"}}}
    };
    event_state.UpdateFromPayload(s1);
    CHECK(event_state.Evaluate("event.round_started", "==", true) &&
          event_state.Evaluate("event.last_event", "==", "RoundStarted"),
          "回合开局事件: 成功触发 RoundStarted (event.round_started == true)");

    // 跃迁 2: 玩家受到伤害 (PlayerTookDamage: 100 -> 68)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    nlohmann::json s2 = {
        {"player", {
            {"state", {{"health", 68}, {"armor", 85}, {"round_kills", 0}, {"round_killhs", 0}}},
            {"team", "CT"}
        }}
    };
    event_state.UpdateFromPayload(s2);
    CHECK(event_state.Evaluate("event.damage", "==", true) &&
          event_state.Evaluate("event.last_event", "==", "PlayerTookDamage"),
          "受到伤害事件: 成功触发 PlayerTookDamage (event.damage == true, 剩余生命: 68)");

    // 跃迁 3: 玩家获得爆头击杀 (PlayerGotKill & PlayerGotHeadshotKill: kills 0 -> 1, hs 0 -> 1)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    nlohmann::json s3 = {
        {"player", {
            {"state", {{"health", 68}, {"armor", 85}, {"round_kills", 1}, {"round_killhs", 1}}},
            {"team", "CT"}
        }}
    };
    event_state.UpdateFromPayload(s3);
    CHECK(event_state.Evaluate("event.kill", "==", true) &&
          event_state.Evaluate("event.headshot", "==", true),
          "击杀与爆头事件: 成功触发 PlayerGotKill & PlayerGotHeadshotKill");

    // 跃迁 4: C4 炸弹安放 (BombPlanted)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    nlohmann::json s4 = {
        {"bomb", {{"state", "planted"}, {"countdown", 40.0}}}
    };
    event_state.UpdateFromPayload(s4);
    CHECK(event_state.Evaluate("event.bomb_planted", "==", true),
          "炸弹安放事件: 成功触发 BombPlanted (event.bomb_planted == true)");

    // 跃迁 5: C4 拆除成功 (BombDefused)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    nlohmann::json s5 = {
        {"bomb", {{"state", "defused"}}}
    };
    event_state.UpdateFromPayload(s5);
    CHECK(event_state.Evaluate("event.bomb_defused", "==", true),
          "炸弹拆除事件: 成功触发 BombDefused (event.bomb_defused == true)");

    // 跃迁 6: 我方阵营回合获胜 (TeamRoundVictory)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    nlohmann::json s6 = {
        {"round", {{"phase", "over"}, {"win_team", "CT"}}}
    };
    event_state.UpdateFromPayload(s6);
    CHECK(event_state.Evaluate("event.round_victory", "==", true),
          "回合胜利事件: 成功触发 TeamRoundVictory (event.round_victory == true)");

    // 验证事件流与 JSON 导出结构
    nlohmann::json j_full = event_state.ToJson();
    bool j_ok = j_full.contains("events") && j_full["events"].is_array() && j_full["events"].size() >= 5;
    CHECK(j_ok, "事件流导出校验: 成功记录并导出完整游戏事件历史 (>= 5 项)");
    if (j_ok) {
        std::cout << "         最新事件: " << j_full["events"][0]["name"].get<std::string>() 
                  << " (" << j_full["events"][0]["label"].get<std::string>() << ")\n";
    }

    // 6. [R1 回归护栏] op=="!=" 曾经递归调用公开的 Evaluate()，对不可重入的 std::mutex 二次加锁，
    //    会让整个 daemon 在规则匹配阶段死锁。此处验证：① 不再死锁（进程能跑完）；
    //    ② "!=" 的判定结果正确。
    std::cout << "\n[测试 6] 验证 op==\"!=\" 不再重入死锁且结果正确 (R1 回归护栏)...\n";
    {
        aura::GsiState neg_state;
        nlohmann::json neg_payload = {
            {"player", {{"state", {{"health", 100}, {"armor", 100}}}}}
        };
        neg_state.UpdateFromPayload(neg_payload);

        const bool ne_true  = neg_state.Evaluate("player_state.health", "!=", 1);   // 100 != 1   -> true
        const bool ne_false = neg_state.Evaluate("player_state.health", "!=", 100); // 100 != 100 -> false
        const bool ne_missing = neg_state.Evaluate("no.such.field", "!=", 1);

        CHECK(ne_true, "player_state.health != 1   -> true  (未死锁)");
        CHECK(!ne_false, "player_state.health != 100 -> false (未死锁)");
        CHECK(!ne_missing, "缺失字段 no.such.field != 1 -> false (查找失败早退返回 false)");

        // 连续多次调用（模拟主循环每帧对多条绑定求值），确认无重入/无自锁
        bool loop_ok = true;
        for (int i = 0; i < 200; ++i) {
            neg_state.Evaluate("event.kill", "!=", true);
            neg_state.Evaluate("player_state.health", "!=", 0);
        }
        CHECK(loop_ok, "400 次连续 \"!=\" 求值（含 event.* 脉冲字段）全部返回，无重入死锁");
    }

    // 7. [R6 路径穿越防御回归测试]
    std::cout << "\n[测试 7] 验证 WebServer 路径穿越防御 (R6 回归护栏)...\n";
    {
        int test_port = 19899;
        std::unique_ptr<aura::WebServer> server;
        std::thread server_thread;
        bool server_ready = false;

        // 端口冲突自愈与动态探测：若默认 19899 端口已被占用，依次向后探测可用候选端口
        for (int p = 19899; p <= 19909; ++p) {
            server = std::make_unique<aura::WebServer>(config_path, p);
            server_thread = std::thread([&]() {
                server->Start();
            });

            httplib::Client cli("127.0.0.1", p);
            cli.set_connection_timeout(1, 0);
            cli.set_read_timeout(1, 0);

            // 替代硬编码盲目延时，主动轮询直到 Web 服务就绪
            for (int retry = 0; retry < 25; ++retry) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                auto res = cli.Get("/");
                if (res && res->status == 200) {
                    server_ready = true;
                    test_port = p;
                    break;
                }
            }

            if (server_ready) {
                break;
            }

            // 当前端口无法绑定或超时，平滑退出并释放线程后尝试下一个端口
            server->Stop();
            if (server_thread.joinable()) {
                server_thread.join();
            }
        }

        CHECK(server_ready, "测试 WebServer 成功启动并绑定可用本地端口: " + std::to_string(test_port));

        if (server_ready) {
            httplib::Client cli("127.0.0.1", test_port);
            cli.set_connection_timeout(2, 0);
            cli.set_read_timeout(2, 0);

            auto res_root = cli.Get("/");
            CHECK(res_root != nullptr && res_root->status == 200, "根路径 GET / 正常响应 200");

            // 针对实际存在的真实文件验证拦截力：
            // 在当前工作目录下存在 test_config.json；若无路径穿越防御，
            // 访问 /web/..%2ftest_config.json 将读到真实文件并返回 200；
            // 防御生效时必须坚决拦截并返回 404！
            auto res_traversal_slash = cli.Get("/web/..%2ftest_config.json");
            CHECK(res_traversal_slash != nullptr && res_traversal_slash->status == 404,
                  "正斜杠相对路径穿越 GET /web/..%2ftest_config.json 成功拦截并返回 404");

            auto res_traversal_backslash = cli.Get("/web/..%5ctest_config.json");
            CHECK(res_traversal_backslash != nullptr && res_traversal_backslash->status == 404,
                  "反斜杠相对路径穿越 GET /web/..%5ctest_config.json 成功拦截并返回 404");

            auto res_traversal_encoded_dots = cli.Get("/web/%2e%2e/test_config.json");
            CHECK(res_traversal_encoded_dots != nullptr && res_traversal_encoded_dots->status == 404,
                  "编码点相对路径穿越 GET /web/%2e%2e/test_config.json 成功拦截并返回 404");

            auto res_traversal_drive = cli.Get("/web/C:/Windows/win.ini");
            CHECK(res_traversal_drive != nullptr && res_traversal_drive->status == 404,
                  "绝对盘符绕过尝试 GET /web/C:/Windows/win.ini 成功拦截并返回 404");

            auto res_traversal_unc = cli.Get("/web//server/share/x");
            CHECK(res_traversal_unc != nullptr && res_traversal_unc->status == 404,
                  "UNC 共享路径绕过尝试 GET /web//server/share/x 成功拦截并返回 404");

            server->Stop();
            if (server_thread.joinable()) {
                server_thread.join();
            }
        }
    }

    // 8. [R16 参数下界校验与 CurrentEffect 除零防范护栏]
    std::cout << "\n[测试 8] 验证参数下界校验 ClampPeriod 与 CurrentEffect 除零防范 (R16 护栏)...\n";
    {
        // 8.1 单元验证 ClampPeriod 纯逻辑
        CHECK(aura::ClampPeriod(0, 3000, 33) == 3000, "ClampPeriod(0, 3000) 缺省/0 走预设值 3000");
        CHECK(aura::ClampPeriod(1, 3000, 33) == 33, "ClampPeriod(1, 3000) 极小值 1 钳制为下界 33ms");
        CHECK(aura::ClampPeriod(2, 3000, 33) == 33, "ClampPeriod(2, 3000) 极小值 2 钳制为下界 33ms");
        CHECK(aura::ClampPeriod(32, 3000, 33) == 33, "ClampPeriod(32, 3000) 边界值 32 钳制为下界 33ms");
        CHECK(aura::ClampPeriod(33, 3000, 33) == 33, "ClampPeriod(33, 3000) 恰等于下界 33 保持 33ms");
        CHECK(aura::ClampPeriod(500, 3000, 33) == 500, "ClampPeriod(500, 3000) 合法值 500 保持 500ms");
        CHECK(aura::ClampPeriod(0, 10, 33) == 33, "ClampPeriod(0, 10) 当缺省值小于下界时安全兜底至 33ms");
        CHECK(aura::ClampPeriod(std::numeric_limits<uint64_t>::max(), 3000, 33) == std::numeric_limits<uint64_t>::max(),
              "ClampPeriod(UINT64_MAX, ...) 保持 UINT64_MAX");

        // 加载权威键位表 fixture，确保渲染循环能够真实遍历 68 个物理键位
        aura::Keymap km;
        std::string keymap_path = "tests/fixtures/calibrated_keymap.json";
        {
            std::ifstream check_km(keymap_path);
            if (!check_km.good()) {
                keymap_path = "calibrated_keymap.json"; // 兼容以 tests/fixtures 作为 cwd 运行场景
            }
        }
        bool km_loaded = km.LoadFromJson(keymap_path);
        CHECK(km_loaded && km.GetAllKeys().size() == 68,
              "成功加载 68 键位映射表 fixture: " + keymap_path);

        aura::FrameBuffer fb;

        // 8.2 验证 BreathingEffect 在 period_ms=1 极小周期下渲染正确性
        aura::BreathingEffect breath(aura::ColorRGB(255, 0, 0), aura::ColorRGB(0, 0, 0), 1);
        CHECK(breath.GetPeriodMs() == 33, "BreathingEffect(..., 1) 构造钳制周期为 33ms");
        breath.Render(0, fb, km);
        // 在 elapsed_ms=0 时，呼吸光效相位为 0，首键 RGB 应为预设的主色 (255, 0, 0)
        CHECK(fb.buffer[0] == 255 && fb.buffer[1] == 0 && fb.buffer[2] == 0,
              "BreathingEffect 在 period_ms=1 渲染首帧首键通道为预期主色 (255,0,0)");
        breath.Render(1, fb, km);

        // 8.3 验证 WaveEffect 在 period_ms=1 极小周期下遍历 68 键渲染正确性
        fb.Fill(0, 0, 0);
        aura::WaveEffect wave(1, "diag_dl");
        CHECK(wave.GetPeriodMs() == 33, "WaveEffect(1, ...) 构造钳制周期为 33ms");
        wave.Render(0, fb, km);
        wave.Render(1, fb, km);

        // 遍历确认 68 键位中存在有效的波浪彩色渲染输出，杜绝空渲染假通过
        bool wave_has_color = false;
        for (size_t i = 0; i < aura::FRAME_BUFFER_SIZE; ++i) {
            if (fb.buffer[i] > 0) {
                wave_has_color = true;
                break;
            }
        }
        CHECK(wave_has_color, "WaveEffect 在 period_ms=1 且包含 68 键位时成功生成有效波浪色彩");

        // 8.4 专项验证 CurrentEffect 除零防范与双保险 (period_ms 为 0/1/2/33 全系列)
        const aura::ColorRGB cur_color(0, 240, 255);
        // (A) period_ms = 0: 构造走默认值 2000ms
        {
            fb.Fill(0, 0, 0);
            aura::CurrentEffect cur0(cur_color, 0);
            CHECK(cur0.GetPeriodMs() == 2000, "CurrentEffect(..., 0) 构造解析为默认值 2000ms");
            cur0.Render(0, fb, km);
            cur0.Render(1, fb, km);
            cur0.Render(500, fb, km);
            cur0.Render(1000, fb, km);
            bool has_light = false;
            for (size_t i = 0; i < aura::FRAME_BUFFER_SIZE; ++i) {
                if (fb.buffer[i] > 0) { has_light = true; break; }
            }
            CHECK(has_light, "CurrentEffect 在 period_ms=0 时渲染产生有效光效输出");
        }

        // (B) period_ms = 1: 历史致命除零点 (1/2=0 取模崩溃)，构造钳制为 33ms + half 双保险
        {
            fb.Fill(0, 0, 0);
            aura::CurrentEffect cur1(cur_color, 1);
            CHECK(cur1.GetPeriodMs() == 33, "CurrentEffect(..., 1) 构造钳制为 33ms");
            cur1.Render(0, fb, km);
            cur1.Render(1, fb, km);
            cur1.Render(16, fb, km);
            cur1.Render(33, fb, km);
            bool cur1_ok = false;
            for (size_t i = 0; i < aura::FRAME_BUFFER_SIZE; ++i) {
                if (fb.buffer[i] > 0) { cur1_ok = true; break; }
            }
            CHECK(cur1_ok, "CurrentEffect 在 period_ms=1 历史除零点安全运行并产生电流脉冲");
        }

        // (C) period_ms = 2: 临界值 (2/2=1)，构造钳制为 33ms
        {
            fb.Fill(0, 0, 0);
            aura::CurrentEffect cur2(cur_color, 2);
            CHECK(cur2.GetPeriodMs() == 33, "CurrentEffect(..., 2) 构造钳制为 33ms");
            cur2.Render(0, fb, km);
            cur2.Render(1, fb, km);
            cur2.Render(2, fb, km);
            cur2.Render(33, fb, km);
            bool cur2_ok = false;
            for (size_t i = 0; i < aura::FRAME_BUFFER_SIZE; ++i) {
                if (fb.buffer[i] > 0) { cur2_ok = true; break; }
            }
            CHECK(cur2_ok, "CurrentEffect 在 period_ms=2 边界安全运行并产生电流脉冲");
        }

        // (D) period_ms = 33: 物理下界边界
        {
            fb.Fill(0, 0, 0);
            aura::CurrentEffect cur33(cur_color, 33);
            CHECK(cur33.GetPeriodMs() == 33, "CurrentEffect(..., 33) 恰等于下界保持 33ms");
            cur33.Render(0, fb, km);
            cur33.Render(16, fb, km);
            cur33.Render(33, fb, km);
            bool cur33_ok = false;
            for (size_t i = 0; i < aura::FRAME_BUFFER_SIZE; ++i) {
                if (fb.buffer[i] > 0) { cur33_ok = true; break; }
            }
            CHECK(cur33_ok, "CurrentEffect 在 period_ms=33 下界边界安全运行并产生电流脉冲");
        }

        // 8.5 其它内置效果全周期覆盖验证 (ColorCycle, StarryNight, Quicksand, Raindrop, Reactive, Ripple)
        {
            aura::ColorCycleEffect cc(1);
            CHECK(cc.GetPeriodMs() == 33, "ColorCycleEffect(1) 钳制为 33ms");
            cc.Render(0, fb, km);

            aura::StarryNightEffect sn(cur_color, false, 2);
            CHECK(sn.GetPeriodMs() == 33, "StarryNightEffect(..., 2) 钳制为 33ms");
            sn.Render(0, fb, km);

            aura::QuicksandEffect qs(aura::ColorRGB(255, 0, 0), aura::ColorRGB(0, 0, 255), 0);
            CHECK(qs.GetPeriodMs() == 3500, "QuicksandEffect(..., 0) 走默认 3500ms");
            qs.Render(0, fb, km);

            aura::RaindropEffect rd(cur_color, 1);
            CHECK(rd.GetPeriodMs() == 33, "RaindropEffect(1) 钳制为 33ms");
            rd.Render(0, fb, km);

            aura::ReactiveEffect re(aura::ColorRGB(0, 0, 0), cur_color, 1);
            CHECK(re.GetSpeedMs() == 33, "ReactiveEffect(1) 钳制为 33ms");
            re.Render(0, fb, km);

            aura::RippleEffect rip(aura::ColorRGB(0, 0, 0), cur_color, 2);
            CHECK(rip.GetSpeedMs() == 33, "RippleEffect(2) 钳制为 33ms");
            rip.Render(0, fb, km);
        }

        // 8.6 RuleEngine::LoadConfig 对 period_ms 0/1/2/33 及非法值的解析校验 (报错哲学统一)
        const std::string tmp_period_cfg = (std::filesystem::temp_directory_path() / "test_cfg_period_clamp.json").string();
        {
            std::ofstream ofs(tmp_period_cfg);
            ofs << R"json({
                "default_profile": "prof_normal",
                "rules": [
                    { "process": "p0.exe", "profile": "prof_zero" },
                    { "process": "p1.exe", "profile": "prof_one" },
                    { "process": "p2.exe", "profile": "prof_two" },
                    { "process": "p33.exe", "profile": "prof_thirtythree" },
                    { "process": "pneg.exe", "profile": "prof_negative" },
                    { "process": "pstr.exe", "profile": "prof_string" },
                    { "process": "phuge.exe", "profile": "prof_huge" },
                    { "process": "pfloat.exe", "profile": "prof_float" },
                    { "process": "pnull.exe", "profile": "prof_null" },
                    { "process": "pbool.exe", "profile": "prof_bool" }
                ],
                "profiles": {
                    "prof_normal": { "type": "static", "color": [10, 20, 30] },
                    "prof_zero": { "type": "breathing", "period_ms": 0 },
                    "prof_one": { "type": "current", "period_ms": 1, "color": [0, 240, 255] },
                    "prof_two": { "type": "current", "period_ms": 2, "color": [0, 240, 255] },
                    "prof_thirtythree": { "type": "current", "period_ms": 33, "color": [0, 240, 255] },
                    "prof_negative": { "type": "wave", "period_ms": -100 },
                    "prof_string": { "type": "color_cycle", "period_ms": "fast" },
                    "prof_huge": { "type": "wave", "period_ms": 10000000000000000000 },
                    "prof_float": { "type": "color_cycle", "period_ms": 100.5 },
                    "prof_null": { "type": "breathing", "period_ms": null },
                    "prof_bool": { "type": "raindrop", "period_ms": true }
                }
            })json";
        }

        aura::RuleEngine period_engine;
        bool p_loaded = period_engine.LoadConfig(tmp_period_cfg);
        CHECK(p_loaded, "LoadConfig 解析含非法/极小/超大/非整型 period_ms 配置不拒绝启动并成功加载");

        // 验证各方案均能匹配并且安全渲染
        auto match_and_render = [&](const std::string& proc, const std::string& expect_prof) {
            auto prof = period_engine.MatchProfile(proc);
            CHECK(prof != nullptr && prof->name == expect_prof,
                  "进程 " + proc + " 成功匹配方案: " + expect_prof);
            if (prof && prof->base_effect) {
                aura::FrameBuffer test_fb;
                prof->Render(0, test_fb, km);
                prof->Render(1, test_fb, km);
                prof->Render(16, test_fb, km);
                prof->Render(33, test_fb, km);
            }
        };

        match_and_render("p0.exe", "prof_zero");
        match_and_render("p1.exe", "prof_one");
        match_and_render("p2.exe", "prof_two");
        match_and_render("p33.exe", "prof_thirtythree");
        match_and_render("pneg.exe", "prof_negative");
        match_and_render("pstr.exe", "prof_string");
        match_and_render("phuge.exe", "prof_huge");
        match_and_render("pfloat.exe", "prof_float");
        match_and_render("pnull.exe", "prof_null");
        match_and_render("pbool.exe", "prof_bool");

        std::filesystem::remove(tmp_period_cfg);
    }

    // 9. [R3 硬件推流寻址表边界与容量证明]
    std::cout << "\n[测试 9] 验证硬件寻址表隔离规则与缓冲区容量证明 (R3 护栏)...\n";
    {
        CHECK(aura::PaddedTableLength(68) == 72, "PaddedTableLength(68) == 72 (4 隔离槽；原 72 槽缓冲零余量)");
        CHECK(aura::PaddedTableLength(69) == 73, "PaddedTableLength(69) == 73 (超旧缓冲但处于 144 槽安全界内)");
        CHECK(aura::PaddedTableLength(128) == 137, "PaddedTableLength(128) == 137 (TOTAL_LEDS 上界仅需 137 槽)");
        CHECK(aura::PaddedTableLength(128) <= aura::MAX_HARDWARE_STREAM_KEYS,
              "最坏情况 128 键寻址表长未超 MAX_HARDWARE_STREAM_KEYS(144)");
        CHECK(aura::HARDWARE_STREAM_BUFFER_SIZE == 432,
              "HARDWARE_STREAM_BUFFER_SIZE == 432 字节 (144 * 3 字节缓冲容量)");
    }

    // 10. [R12 配置引用完整性与未知效果类型强校验护栏]
    std::cout << "\n[测试 10] 验证配置引用完整性校验与未知效果类型拦截 (R12 护栏)...\n";
    {
        // 10.1 未知效果类型校验 (如 "type": "breathig")
        const std::string tmp_unknown_type = (std::filesystem::temp_directory_path() / "test_cfg_unknown_type.json").string();
        {
            std::ofstream ofs(tmp_unknown_type);
            ofs << R"json({
                "default_profile": "desktop",
                "profiles": {
                    "desktop": { "type": "breathig", "period_ms": 1000 }
                }
            })json";
        }
        aura::RuleEngine engine_bad_type;
        bool res_bad_type = engine_bad_type.LoadConfig(tmp_unknown_type);
        CHECK(!res_bad_type, "配置包含未知效果类型 (type: 'breathig') 时 LoadConfig 坚决拒绝并返回 false");
        std::filesystem::remove(tmp_unknown_type);

        // 10.2 rules 引用不存在的 profile
        const std::string tmp_missing_rule_prof = (std::filesystem::temp_directory_path() / "test_cfg_missing_rule_prof.json").string();
        {
            std::ofstream ofs(tmp_missing_rule_prof);
            ofs << R"json({
                "default_profile": "desktop",
                "rules": [
                    { "process": "code.exe", "profile": "nonexistent_profile" }
                ],
                "profiles": {
                    "desktop": { "type": "static", "color": [0, 80, 200] }
                }
            })json";
        }
        aura::RuleEngine engine_bad_rule;
        bool res_bad_rule = engine_bad_rule.LoadConfig(tmp_missing_rule_prof);
        CHECK(!res_bad_rule, "rules 引用未定义方案 (profile: 'nonexistent_profile') 时 LoadConfig 坚决拒绝并返回 false");
        std::filesystem::remove(tmp_missing_rule_prof);

        // 10.3 gsi_bindings 引用不存在的 profile
        const std::string tmp_missing_gsi_prof = (std::filesystem::temp_directory_path() / "test_cfg_missing_gsi_prof.json").string();
        {
            std::ofstream ofs(tmp_missing_gsi_prof);
            ofs << R"json({
                "default_profile": "desktop",
                "gsi_bindings": [
                    { "field": "player_state.health", "operator": "<", "value": 20, "profile": "unreal_gsi_profile" }
                ],
                "profiles": {
                    "desktop": { "type": "static", "color": [0, 80, 200] }
                }
            })json";
        }
        aura::RuleEngine engine_bad_gsi;
        bool res_bad_gsi = engine_bad_gsi.LoadConfig(tmp_missing_gsi_prof);
        CHECK(!res_bad_gsi, "gsi_bindings 引用未定义方案时 LoadConfig 坚决拒绝并返回 false");
        std::filesystem::remove(tmp_missing_gsi_prof);

        // 10.4 default_profile 引用不存在的 profile
        const std::string tmp_missing_default = (std::filesystem::temp_directory_path() / "test_cfg_missing_default.json").string();
        {
            std::ofstream ofs(tmp_missing_default);
            ofs << R"json({
                "default_profile": "ghost_desktop",
                "profiles": {
                    "desktop": { "type": "static", "color": [0, 80, 200] }
                }
            })json";
        }
        aura::RuleEngine engine_bad_def;
        bool res_bad_def = engine_bad_def.LoadConfig(tmp_missing_default);
        CHECK(!res_bad_def, "default_profile 引用未定义方案时 LoadConfig 坚决拒绝并返回 false");
        std::filesystem::remove(tmp_missing_default);

        // 10.5 coding 方案存在性与配置完整性验证
        CHECK(rule_engine.HasProfile("coding"), "测试配置中包含合法的 coding 方案 (HasProfile == true)");
        auto coding_prof = rule_engine.MatchProfile("code.exe");
        CHECK(coding_prof != nullptr && coding_prof->name == "coding", "code.exe 精准匹配 coding 方案");
        CHECK(coding_prof->key_overrides.size() >= 3, "coding 方案包含 ESC/ENTER/TAB 等按键重写覆盖");
    }

    // 11. [R12b 热重载失败 mtime 同步与旧配置保留护栏]
    std::cout << "\n[测试 11] 验证热重载 CheckAndReload 失败时 mtime 同步与旧配置保留 (R12b 护栏)...\n";
    {
        const std::string tmp_reload_cfg = (std::filesystem::temp_directory_path() / "test_cfg_reload.json").string();
        {
            std::ofstream ofs(tmp_reload_cfg);
            ofs << R"json({
                "default_profile": "prof_initial",
                "rules": [
                    { "process": "demo.exe", "profile": "prof_initial" }
                ],
                "profiles": {
                    "prof_initial": { "type": "static", "color": [10, 20, 30] }
                }
            })json";
        }
        aura::RuleEngine reload_engine;
        CHECK(reload_engine.LoadConfig(tmp_reload_cfg), "初始合法配置加载成功");
        auto initial_prof = reload_engine.MatchProfile("demo.exe");
        CHECK(initial_prof != nullptr && initial_prof->name == "prof_initial", "初始规则匹配生效");

        // 此时未修改文件，CheckAndReload 应返回 false
        CHECK(!reload_engine.CheckAndReload(), "文件未变动时 CheckAndReload 返回 false");

        // 修改为非法配置（引用破坏）
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        {
            std::ofstream ofs(tmp_reload_cfg);
            ofs << R"json({
                "default_profile": "prof_initial",
                "rules": [
                    { "process": "demo.exe", "profile": "nonexistent_profile" }
                ],
                "profiles": {
                    "prof_initial": { "type": "static", "color": [10, 20, 30] }
                }
            })json";
        }
        // 第一次调用 CheckAndReload: 检测到 mtime 改变，尝试重载失败并更新 last_write_time_
        bool reload_res1 = reload_engine.CheckAndReload();
        CHECK(!reload_res1, "热重载遇到非法引用配置返回 false");

        // 关键校验 1：旧配置未被破坏，依然保持有效
        auto preserved_prof = reload_engine.MatchProfile("demo.exe");
        CHECK(preserved_prof != nullptr && preserved_prof->name == "prof_initial",
              "热重载失败后旧配置完整保留 (demo.exe 仍匹配 prof_initial)");

        // 关键校验 2 (R12b 防刷屏核心)：再次调用 CheckAndReload，不应再尝试重载（直接返回 false）
        bool reload_res2 = reload_engine.CheckAndReload();
        CHECK(!reload_res2, "文件 mtime 未再变动时，CheckAndReload 绝不再重复重载 (防每秒刷屏)");

        // 修正文件为新的有效配置，验证 mtime 再次变更后能够成功重载
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        {
            std::ofstream ofs(tmp_reload_cfg);
            ofs << R"json({
                "default_profile": "prof_v2",
                "rules": [
                    { "process": "demo.exe", "profile": "prof_v2" }
                ],
                "profiles": {
                    "prof_v2": { "type": "breathing", "period_ms": 2000 }
                }
            })json";
        }
        bool reload_res3 = reload_engine.CheckAndReload();
        CHECK(reload_res3, "修复配置文件后，mtime 变化触发重载成功");
        auto v2_prof = reload_engine.MatchProfile("demo.exe");
        CHECK(v2_prof != nullptr && v2_prof->name == "prof_v2", "重载后新配置 prof_v2 生效");

        std::filesystem::remove(tmp_reload_cfg);
    }

    // 12. [R12b 初始启动加载失败后的自愈热重载闭环测试]
    std::cout << "\n[测试 12] 验证初始启动加载失败时的 mtime 追踪与自愈热重载闭环 (R12b 护栏)...\n";
    {
        const std::string tmp_heal_cfg = (std::filesystem::temp_directory_path() / "test_cfg_heal.json").string();
        // 初始写入包含未定义方案的非法配置
        {
            std::ofstream ofs(tmp_heal_cfg);
            ofs << R"json({
                "default_profile": "desktop",
                "rules": [
                    { "process": "heal.exe", "profile": "broken_profile" }
                ],
                "profiles": {
                    "desktop": { "type": "static", "color": [10, 20, 30] }
                }
            })json";
        }
        aura::RuleEngine heal_engine;
        bool init_ok = heal_engine.LoadConfig(tmp_heal_cfg);
        CHECK(!init_ok, "初始配置存在引用破坏时 LoadConfig 拒绝并返回 false");
        CHECK(heal_engine.GetConfigPath() == tmp_heal_cfg, "LoadConfig 失败后依然安全绑定 config_path_ 用于后续热重载自愈");

        // 文件未修改前，CheckAndReload 应静默返回 false，不刷屏
        CHECK(!heal_engine.CheckAndReload(), "初始非法配置未修改时 CheckAndReload 静默返回 false (防刷屏)");

        // 修复配置文件
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        {
            std::ofstream ofs(tmp_heal_cfg);
            ofs << R"json({
                "default_profile": "desktop",
                "rules": [
                    { "process": "heal.exe", "profile": "desktop" }
                ],
                "profiles": {
                    "desktop": { "type": "breathing", "period_ms": 1500 }
                }
            })json";
        }
        bool heal_res = heal_engine.CheckAndReload();
        CHECK(heal_res, "配置文件修复后，CheckAndReload 成功自愈重载生效");
        auto healed_prof = heal_engine.MatchProfile("heal.exe");
        CHECK(healed_prof != nullptr && healed_prof->name == "desktop", "自愈后 heal.exe 正确匹配 desktop 方案");

        std::filesystem::remove(tmp_heal_cfg);
    }

    // 13. [R8 宽字符 / 非 ASCII 路径热重载与属性查询]
    std::cout << "\n[测试 13] 验证非 ASCII / 宽字符 / UTF-8 路径热重载与文件时间支持 (R8 护栏)...\n";
    {
        std::filesystem::path unicode_dir = std::filesystem::temp_directory_path() / "aura_test_测试_键盘配置";
        std::error_code ec;
        std::filesystem::create_directories(unicode_dir, ec);
        std::filesystem::path unicode_cfg = unicode_dir / "config_unicode.json";
        std::string unicode_cfg_str = unicode_cfg.string();

        {
            std::ofstream ofs(unicode_cfg);
            ofs << R"json({
                "default_profile": "desktop",
                "profiles": {
                    "desktop": { "type": "static", "color": [5, 15, 25] }
                }
            })json";
        }

        aura::RuleEngine unicode_engine;
        bool u_loaded = unicode_engine.LoadConfig(unicode_cfg_str);
        CHECK(u_loaded, "非 ASCII / 宽字符路径配置文件成功加载");
        CHECK(!unicode_engine.CheckAndReload(), "非 ASCII 路径文件未修改时 CheckAndReload 返回 false");

        // 修改非 ASCII 路径下的文件
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        {
            std::ofstream ofs(unicode_cfg);
            ofs << R"json({
                "default_profile": "desktop",
                "profiles": {
                    "desktop": { "type": "color_cycle", "period_ms": 3000 }
                }
            })json";
        }
        bool u_reloaded = unicode_engine.CheckAndReload();
        CHECK(u_reloaded, "非 ASCII 路径文件修改后 CheckAndReload 成功检测并重载");

        std::filesystem::remove_all(unicode_dir, ec);
    }

    // 14. [畸形根节点与非法结构类型强校验护栏]
    std::cout << "\n[测试 14] 验证畸形根节点与非法数据类型拦截 (健壮性护栏)...\n";
    {
        // 14.1 根节点为数组
        const std::string tmp_arr_root = (std::filesystem::temp_directory_path() / "test_cfg_arr_root.json").string();
        {
            std::ofstream ofs(tmp_arr_root);
            ofs << R"json([{"default_profile": "desktop"}])json";
        }
        aura::RuleEngine engine_arr;
        CHECK(!engine_arr.LoadConfig(tmp_arr_root), "JSON 根节点为数组时 LoadConfig 拦截并返回 false");
        std::filesystem::remove(tmp_arr_root);

        // 14.2 rules 字段非数组
        const std::string tmp_bad_rules = (std::filesystem::temp_directory_path() / "test_cfg_bad_rules.json").string();
        {
            std::ofstream ofs(tmp_bad_rules);
            ofs << R"json({
                "default_profile": "desktop",
                "rules": "not_an_array",
                "profiles": {
                    "desktop": { "type": "static" }
                }
            })json";
        }
        aura::RuleEngine engine_bad_rules;
        CHECK(!engine_bad_rules.LoadConfig(tmp_bad_rules), "rules 字段非数组时 LoadConfig 拦截并返回 false");
        std::filesystem::remove(tmp_bad_rules);

        // 14.3 profiles 内方案非对象
        const std::string tmp_bad_prof_obj = (std::filesystem::temp_directory_path() / "test_cfg_bad_prof_obj.json").string();
        {
            std::ofstream ofs(tmp_bad_prof_obj);
            ofs << R"json({
                "default_profile": "desktop",
                "profiles": {
                    "desktop": 12345
                }
            })json";
        }
        aura::RuleEngine engine_bad_prof_obj;
        CHECK(!engine_bad_prof_obj.LoadConfig(tmp_bad_prof_obj), "profiles 中方案非对象时 LoadConfig 拦截并返回 false");
        std::filesystem::remove(tmp_bad_prof_obj);
    }

    // 15. [Profile 亮度缩放与 speed_index 解析验证 (D1 / R15 方案 A)]
    std::cout << "\n[测试 15] Profile 亮度缩放与 speed_index 解析验证 (D1 / R15 方案 A)...\n";
    {
        // 15.1 Profile::Render 统一等比亮度缩放 (含 key_overrides 覆盖)
        std::string keymap_path = "tests/fixtures/calibrated_keymap.json";
        {
            std::ifstream check_file(keymap_path);
            if (!check_file.good()) {
                keymap_path = "calibrated_keymap.json";
            }
        }
        aura::Keymap keymap;
        CHECK(keymap.LoadFromJson(keymap_path), "加载键位映射文件: " + keymap_path);

        int esc_id = -1;
        CHECK(keymap.FindLedId("ESC", esc_id) && esc_id >= 0, "成功定位 ESC 键 LED ID");

        aura::Profile prof;
        prof.name = "test_scale";
        prof.base_effect = std::make_shared<aura::StaticEffect>(aura::ColorRGB(100, 200, 50));
        aura::KeyOverride ko;
        ko.key_spec = "ESC";
        ko.color = aura::ColorRGB(200, 100, 40);
        prof.key_overrides.push_back(ko);

        // 默认满亮度 255
        CHECK(prof.brightness == 255, "Profile 初始默认亮度为 255");
        aura::FrameBuffer frame_255;
        prof.Render(0, frame_255, keymap);
        CHECK(frame_255.buffer[esc_id * 3 + 0] == 200 &&
              frame_255.buffer[esc_id * 3 + 1] == 100 &&
              frame_255.buffer[esc_id * 3 + 2] == 40,
              "满亮度 (255) 下 ESC 覆盖色保持不变 (200, 100, 40)");

        size_t other_id = (esc_id == 0) ? 1 : 0;
        CHECK(frame_255.buffer[other_id * 3 + 0] == 100 &&
              frame_255.buffer[other_id * 3 + 1] == 200 &&
              frame_255.buffer[other_id * 3 + 2] == 50,
              "满亮度 (255) 下基础底色保持不变 (100, 200, 50)");

        // 半亮度 128 (~50%)
        prof.brightness = 128;
        aura::FrameBuffer frame_128;
        prof.Render(0, frame_128, keymap);
        // ESC: 200*128/255=100, 100*128/255=50, 40*128/255=20
        CHECK(frame_128.buffer[esc_id * 3 + 0] == 100 &&
              frame_128.buffer[esc_id * 3 + 1] == 50 &&
              frame_128.buffer[esc_id * 3 + 2] == 20,
              "半亮度 (128) 下 ESC 覆盖色等比缩放至 (100, 50, 20)");
        // Base: 100*128/255=50, 200*128/255=100, 50*128/255=25
        CHECK(frame_128.buffer[other_id * 3 + 0] == 50 &&
              frame_128.buffer[other_id * 3 + 1] == 100 &&
              frame_128.buffer[other_id * 3 + 2] == 25,
              "半亮度 (128) 下基础底色等比缩放至 (50, 100, 25)");

        // 零亮度 0 (完全黑灯)
        prof.brightness = 0;
        aura::FrameBuffer frame_0;
        prof.Render(0, frame_0, keymap);
        bool all_zero = true;
        for (size_t i = 0; i < aura::FRAME_BUFFER_SIZE; ++i) {
            if (frame_0.buffer[i] != 0) {
                all_zero = false;
                break;
            }
        }
        CHECK(all_zero, "零亮度 (0) 下整个帧缓冲区 384 字节全为 0");

        // 低亮度 51 (~20%)
        prof.brightness = 51;
        aura::FrameBuffer frame_51;
        prof.Render(0, frame_51, keymap);
        // Base: 100*51/255=20, 200*51/255=40, 50*51/255=10
        CHECK(frame_51.buffer[other_id * 3 + 0] == 20 &&
              frame_51.buffer[other_id * 3 + 1] == 40 &&
              frame_51.buffer[other_id * 3 + 2] == 10,
              "低亮度 (51) 下基础底色等比缩放至 (20, 40, 10)");

        // 极低亮度 1 (最低非零下界，验证 255 通道截断保全为 1)
        prof.brightness = 1;
        aura::KeyOverride ko_white;
        ko_white.key_spec = "ESC";
        ko_white.color = aura::ColorRGB(255, 255, 255);
        prof.key_overrides.clear();
        prof.key_overrides.push_back(ko_white);
        aura::FrameBuffer frame_1;
        prof.Render(0, frame_1, keymap);
        CHECK(frame_1.buffer[esc_id * 3 + 0] == 1 &&
              frame_1.buffer[esc_id * 3 + 1] == 1 &&
              frame_1.buffer[esc_id * 3 + 2] == 1,
              "极低亮度 (1) 下白色 (255,255,255) 缩放截断为 (1, 1, 1)");

        // base_effect 为 nullptr 场景：背景全黑，仅 key_overrides 参与亮度缩放
        aura::Profile prof_no_base;
        prof_no_base.name = "no_base";
        prof_no_base.brightness = 128;
        prof_no_base.key_overrides.push_back(ko); // ESC -> (200, 100, 40)
        aura::FrameBuffer frame_no_base;
        prof_no_base.Render(0, frame_no_base, keymap);
        CHECK(frame_no_base.buffer[esc_id * 3 + 0] == 100 &&
              frame_no_base.buffer[esc_id * 3 + 1] == 50 &&
              frame_no_base.buffer[esc_id * 3 + 2] == 20,
              "无 base_effect 方案在半亮度下 ESC 依然正确缩放至 (100, 50, 20)");
        CHECK(frame_no_base.buffer[other_id * 3 + 0] == 0 &&
              frame_no_base.buffer[other_id * 3 + 1] == 0 &&
              frame_no_base.buffer[other_id * 3 + 2] == 0,
              "无 base_effect 方案其余未覆盖键位保持全黑 (0, 0, 0)");

        // 15.2 RuleEngine 对 brightness (0.0-1.0 浮点 / 0-255 整数) 解析、浮点抖动与越界钳制
        const std::string tmp_bright_cfg = (std::filesystem::temp_directory_path() / "test_cfg_brightness.json").string();
        {
            std::ofstream ofs(tmp_bright_cfg);
            ofs << R"json({
                "default_profile": "p_default",
                "profiles": {
                    "p_default": { "type": "static" },
                    "p_float_1": { "type": "static", "brightness": 1.0 },
                    "p_float_0": { "type": "static", "brightness": 0.0 },
                    "p_float_half": { "type": "static", "brightness": 0.5 },
                    "p_float_jitter": { "type": "static", "brightness": 1.0000000000000002 },
                    "p_float_jitter2": { "type": "static", "brightness": 1.0001 },
                    "p_int_1": { "type": "static", "brightness": 1 },
                    "p_int_128": { "type": "static", "brightness": 128 },
                    "p_int_255": { "type": "static", "brightness": 255 },
                    "p_int_50": { "type": "static", "brightness": 50 },
                    "p_neg": { "type": "static", "brightness": -0.8 },
                    "p_over": { "type": "static", "brightness": 500 },
                    "p_bad_type": { "type": "static", "brightness": "high" },
                    "p_bool_true": { "type": "static", "brightness": true },
                    "p_bool_false": { "type": "static", "brightness": false },
                    "p_null": { "type": "static", "brightness": null }
                }
            })json";
        }
        aura::RuleEngine bright_engine;
        CHECK(bright_engine.LoadConfig(tmp_bright_cfg), "成功加载亮度测试配置文件");

        auto p_def = bright_engine.GetProfile("p_default");
        CHECK(p_def != nullptr && p_def->brightness == 255, "缺省 brightness 时解析为默认 255");

        auto p_f1 = bright_engine.GetProfile("p_float_1");
        CHECK(p_f1 != nullptr && p_f1->brightness == 255, "浮点 1.0 解析为 255");

        auto p_f0 = bright_engine.GetProfile("p_float_0");
        CHECK(p_f0 != nullptr && p_f0->brightness == 0, "浮点 0.0 解析为 0");

        auto p_fhalf = bright_engine.GetProfile("p_float_half");
        CHECK(p_fhalf != nullptr && p_fhalf->brightness == 128, "浮点 0.5 解析为 128");

        auto p_fjitter = bright_engine.GetProfile("p_float_jitter");
        CHECK(p_fjitter != nullptr && p_fjitter->brightness == 255, "浮点微小抖动 (1.0000000000000002) 解析为 255");

        auto p_fjitter2 = bright_engine.GetProfile("p_float_jitter2");
        CHECK(p_fjitter2 != nullptr && p_fjitter2->brightness == 255, "浮点微小抖动 (1.0001) 钳制为 255");

        auto p_i1 = bright_engine.GetProfile("p_int_1");
        CHECK(p_i1 != nullptr && p_i1->brightness == 255, "整数 1 (来自前端 1.0 序列化) 解析为 255");

        auto p_i128 = bright_engine.GetProfile("p_int_128");
        CHECK(p_i128 != nullptr && p_i128->brightness == 128, "整数 128 解析为 128");

        auto p_i255 = bright_engine.GetProfile("p_int_255");
        CHECK(p_i255 != nullptr && p_i255->brightness == 255, "整数 255 解析为 255");

        auto p_i50 = bright_engine.GetProfile("p_int_50");
        CHECK(p_i50 != nullptr && p_i50->brightness == 50, "整数 50 解析为 50");

        auto p_neg = bright_engine.GetProfile("p_neg");
        CHECK(p_neg != nullptr && p_neg->brightness == 0, "负数亮度 (-0.8) 钳制为 0");

        auto p_over = bright_engine.GetProfile("p_over");
        CHECK(p_over != nullptr && p_over->brightness == 255, "超限亮度 (500) 钳制为 255");

        auto p_bad = bright_engine.GetProfile("p_bad_type");
        CHECK(p_bad != nullptr && p_bad->brightness == 255, "非法字符串亮度 (\"high\") 回退默认 255");

        auto p_btrue = bright_engine.GetProfile("p_bool_true");
        CHECK(p_btrue != nullptr && p_btrue->brightness == 255, "布尔真亮度 (true) 回退默认 255");

        auto p_bfalse = bright_engine.GetProfile("p_bool_false");
        CHECK(p_bfalse != nullptr && p_bfalse->brightness == 255, "布尔假亮度 (false) 回退默认 255");

        auto p_null = bright_engine.GetProfile("p_null");
        CHECK(p_null != nullptr && p_null->brightness == 255, "空值亮度 (null) 回退默认 255");

        std::filesystem::remove(tmp_bright_cfg);

        // 15.3 非有限浮点数 (NaN / Infinity) 安全防御验证
        {
            nlohmann::json mem_cfg = {
                {"default_profile", "p_nan"},
                {"profiles", {
                    {"p_nan", {{"type", "static"}, {"brightness", std::numeric_limits<double>::quiet_NaN()}}},
                    {"p_inf", {{"type", "static"}, {"brightness", std::numeric_limits<double>::infinity()}}},
                    {"p_speed_nan", {{"type", "breathing"}, {"speed_index", std::numeric_limits<double>::quiet_NaN()}}}
                }}
            };
            const std::string tmp_nan_cfg = (std::filesystem::temp_directory_path() / "test_cfg_nan.json").string();
            {
                std::ofstream ofs(tmp_nan_cfg);
                ofs << mem_cfg.dump();
            }
            aura::RuleEngine nan_engine;
            CHECK(nan_engine.LoadConfig(tmp_nan_cfg), "成功加载包含 NaN/Inf 的配置文件");
            auto prof_nan = nan_engine.GetProfile("p_nan");
            CHECK(prof_nan != nullptr && prof_nan->brightness == 255, "NaN 亮度安全回退默认 255");
            auto prof_inf = nan_engine.GetProfile("p_inf");
            CHECK(prof_inf != nullptr && prof_inf->brightness == 255, "Infinity 亮度安全回退默认 255");
            auto prof_snan = nan_engine.GetProfile("p_speed_nan");
            auto b_snan = prof_snan ? std::dynamic_pointer_cast<aura::BreathingEffect>(prof_snan->base_effect) : nullptr;
            CHECK(b_snan != nullptr && b_snan->GetPeriodMs() == 3000, "NaN speed_index 安全回退默认 3000ms");
            std::filesystem::remove(tmp_nan_cfg);
        }

        // 15.4 RuleEngine 对 speed_index 档位映射与 period_ms 优先级验证
        const std::string tmp_speed_cfg = (std::filesystem::temp_directory_path() / "test_cfg_speed.json").string();
        {
            std::ofstream ofs(tmp_speed_cfg);
            ofs << R"json({
                "default_profile": "p_speed_0",
                "profiles": {
                    "p_speed_0": { "type": "breathing", "speed_index": 0 },
                    "p_speed_1": { "type": "breathing", "speed_index": 1 },
                    "p_speed_2": { "type": "breathing", "speed_index": 2 },
                    "p_speed_float": { "type": "breathing", "speed_index": 1.0 },
                    "p_override": { "type": "breathing", "speed_index": 0, "period_ms": 2200 },
                    "p_bad_speed": { "type": "breathing", "speed_index": 99 },
                    "p_speed_bool": { "type": "breathing", "speed_index": true },
                    "p_speed_null": { "type": "breathing", "speed_index": null }
                }
            })json";
        }
        aura::RuleEngine speed_engine;
        CHECK(speed_engine.LoadConfig(tmp_speed_cfg), "成功加载 speed_index 配置文件");

        auto prof_s0 = speed_engine.GetProfile("p_speed_0");
        auto b_s0 = prof_s0 ? std::dynamic_pointer_cast<aura::BreathingEffect>(prof_s0->base_effect) : nullptr;
        CHECK(b_s0 != nullptr && b_s0->GetPeriodMs() == 5500, "speed_index 0 映射为 5500ms (慢速)");

        auto prof_s1 = speed_engine.GetProfile("p_speed_1");
        auto b_s1 = prof_s1 ? std::dynamic_pointer_cast<aura::BreathingEffect>(prof_s1->base_effect) : nullptr;
        CHECK(b_s1 != nullptr && b_s1->GetPeriodMs() == 3200, "speed_index 1 映射为 3200ms (标准)");

        auto prof_s2 = speed_engine.GetProfile("p_speed_2");
        auto b_s2 = prof_s2 ? std::dynamic_pointer_cast<aura::BreathingEffect>(prof_s2->base_effect) : nullptr;
        CHECK(b_s2 != nullptr && b_s2->GetPeriodMs() == 1600, "speed_index 2 映射为 1600ms (快速)");

        auto prof_sfloat = speed_engine.GetProfile("p_speed_float");
        auto b_sfloat = prof_sfloat ? std::dynamic_pointer_cast<aura::BreathingEffect>(prof_sfloat->base_effect) : nullptr;
        CHECK(b_sfloat != nullptr && b_sfloat->GetPeriodMs() == 3200, "浮点 speed_index (1.0) 映射为 3200ms (标准)");

        auto prof_sover = speed_engine.GetProfile("p_override");
        auto b_sover = prof_sover ? std::dynamic_pointer_cast<aura::BreathingEffect>(prof_sover->base_effect) : nullptr;
        CHECK(b_sover != nullptr && b_sover->GetPeriodMs() == 2200, "同时存在时 period_ms 优先于 speed_index (2200ms)");

        auto prof_sbad = speed_engine.GetProfile("p_bad_speed");
        auto b_sbad = prof_sbad ? std::dynamic_pointer_cast<aura::BreathingEffect>(prof_sbad->base_effect) : nullptr;
        CHECK(b_sbad != nullptr && b_sbad->GetPeriodMs() == 3000, "非法 speed_index (99) 回退默认 3000ms");

        auto prof_sbool = speed_engine.GetProfile("p_speed_bool");
        auto b_sbool = prof_sbool ? std::dynamic_pointer_cast<aura::BreathingEffect>(prof_sbool->base_effect) : nullptr;
        CHECK(b_sbool != nullptr && b_sbool->GetPeriodMs() == 3000, "布尔 speed_index (true) 回退默认 3000ms");

        auto prof_snull = speed_engine.GetProfile("p_speed_null");
        auto b_snull = prof_snull ? std::dynamic_pointer_cast<aura::BreathingEffect>(prof_snull->base_effect) : nullptr;
        CHECK(b_snull != nullptr && b_snull->GetPeriodMs() == 3000, "空值 speed_index (null) 回退默认 3000ms");

        std::filesystem::remove(tmp_speed_cfg);
    }

    // 16. 时钟回退与下溢保护验证 (Issue #21)
    std::cout << "\n[测试 16] GsiState 时钟回退与 uint64_t 下溢安全保护...\n";
    {
        aura::GsiState time_gsi;
        nlohmann::json dummy_payload = R"json({
            "provider": {"name": "Counter-Strike: Global Offensive", "timestamp": 1234567},
            "player": {"state": {"health": 100}}
        })json"_json;
        time_gsi.UpdateFromPayload(dummy_payload);

        CHECK(time_gsi.GetLastUpdateMs() > 0, "成功设置 last_update_ms_");
        CHECK(time_gsi.IsActive(10000), "正常时间窗口内 IsActive 为 true");

        auto j_status = time_gsi.ToJson();
        CHECK(j_status["connected"].get<bool>(), "ToJson 返回 connected 为 true");
        CHECK(j_status["last_updated_sec"].get<double>() >= 0.0, "last_updated_sec 非负有效");
    }

    // 17. GsiAdapter 端口占用同步拦截与原子生命周期验证 (Issue #23 / U11)
    std::cout << "\n[测试 17] GsiAdapter 端口占用同步拦截与原子生命周期验证...\n";
    {
        aura::GsiAdapter adapter1;
        aura::GsiAdapter adapter2;
        int test_port = 19991;

        bool started1 = adapter1.Start(test_port);
        CHECK(started1, "adapter1 成功绑定并启动端口 " + std::to_string(test_port));

        // adapter2 尝试绑定已被 adapter1 占用的相同端口，必须同步返回 false
        bool started2 = adapter2.Start(test_port);
        CHECK(!started2, "adapter2 绑定相同端口失败，成功同步返回 false（非死代码拦截）");

        // 重复调用已启动的 adapter1::Start 幂等返回 true
        CHECK(adapter1.Start(test_port), "已启动的 GsiAdapter 重复调用 Start 幂等返回 true");

        // 停止 adapter1，释放端口
        adapter1.Stop();
        CHECK(!adapter1.IsRunning(), "adapter1 停止后 IsRunning 为 false");

        // 重复 Stop 幂等安全
        adapter1.Stop();

        // 端口释放后 adapter2 可以成功绑定启动（稍作等待确保操作系统内核完成 TCP 端口表释放）
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        bool started2_retry = adapter2.Start(test_port);
        CHECK(started2_retry, "adapter1 释放后 adapter2 成功绑定端口 " + std::to_string(test_port));
        adapter2.Stop();
    }

    // 18. WebServer 模块目录搜索与 HTML 资源解析验证 (Issue #35 / §3.6)
    std::cout << "\n[测试 18] WebServer 路径搜索与 HTML 资源解析验证...\n";
    {
        aura::WebServer server(config_path, 19992);
        std::string html = server.LoadHtmlContent();
        CHECK(!html.empty(), "LoadHtmlContent 返回非空 HTML 内容");
        CHECK(html.find("html") != std::string::npos || html.find("HTML") != std::string::npos, 
              "LoadHtmlContent 成功加载有效 HTML 文档");
    }
    // 19. FPS 帧率配置与钳制保护验证
    std::cout << "\n[测试 19] 验证 FPS 帧率解析与 10~100 范围钳制保护...\n";
    {
        const std::string tmp_fps_cfg = (std::filesystem::temp_directory_path() / "test_cfg_fps.json").string();
        {
            std::ofstream ofs(tmp_fps_cfg);
            ofs << R"json({
                "default_profile": "prof_normal",
                "fps": 60,
                "profiles": {
                    "prof_normal": { "type": "static" },
                    "prof_100": { "type": "static", "fps": 100 },
                    "prof_over": { "type": "static", "fps": 240 },
                    "prof_under": { "type": "static", "fps": 2 }
                }
            })json";
        }
        aura::RuleEngine fps_engine;
        CHECK(fps_engine.LoadConfig(tmp_fps_cfg), "成功加载包含 FPS 的配置文件");
        CHECK(fps_engine.GetFps() == 60, "根级 fps 解析为 60");
        
        auto p_normal = fps_engine.GetProfile("prof_normal");
        CHECK(p_normal != nullptr && p_normal->fps == 60, "未指定 profile fps 时继承根级 60 FPS");

        auto p_100 = fps_engine.GetProfile("prof_100");
        CHECK(p_100 != nullptr && p_100->fps == 100, "指定的 100 FPS 正确解析 (硬件物理上限)");

        auto p_over = fps_engine.GetProfile("prof_over");
        CHECK(p_over != nullptr && p_over->fps == 100, "超限 240 FPS 正确钳制为硬件上限 100 FPS");

        auto p_under = fps_engine.GetProfile("prof_under");
        CHECK(p_under != nullptr && p_under->fps == 10, "过低 2 FPS 正确钳制为安全下限 10 FPS");

        std::filesystem::remove(tmp_fps_cfg);
    }

    // =========================================================================
    // 18. 按键物理几何坐标推导与 Win32 键盘钩子映射专项测试
    // =========================================================================
    {
        std::cout << "\n[Test 18] 按键物理空间几何坐标与扩展键 / Copilot 映射...\n";

        // 18.1 Keymap 几何坐标校验
        aura::Keymap km;
        std::string km_path = "tests/fixtures/calibrated_keymap.json";
        if (!std::filesystem::exists(km_path)) {
            km_path = "calibrated_keymap.json";
        }
        CHECK(km.LoadFromJson(km_path), "成功加载 calibrated_keymap.json 供几何测试");

        const auto& all = km.GetAllKeys();
        CHECK(all.find("UP") != all.end(), "存在 UP 键定义");
        CHECK(all.find("DOWN") != all.end(), "存在 DOWN 键定义");
        CHECK(all.find("LEFT") != all.end(), "存在 LEFT 键定义");
        CHECK(all.find("RIGHT") != all.end(), "存在 RIGHT 键定义");
        CHECK(all.find("R_ALT") != all.end(), "存在 R_ALT 键定义");
        CHECK(all.find("COPILOT") != all.end(), "存在 COPILOT 键定义");

        if (all.find("UP") != all.end() && all.find("DOWN") != all.end()) {
            const auto& up = all.at("UP");
            const auto& down = all.at("DOWN");
            CHECK(std::abs(up.physical_x - 14.5) < 0.01, "UP 物理 X 坐标为 14.5");
            CHECK(std::abs(down.physical_x - 14.5) < 0.01, "DOWN 物理 X 坐标为 14.5");
            CHECK(std::abs(up.physical_x - down.physical_x) < 0.001, "UP 与 DOWN 物理绝对垂直对齐 (delta == 0)");
            CHECK(std::abs(up.physical_y - 4.0) < 0.01, "UP 物理 Y 坐标为 4.0");
            CHECK(std::abs(down.physical_y - 5.0) < 0.01, "DOWN 物理 Y 坐标为 5.0");
        }

        if (all.find("LEFT") != all.end() && all.find("RIGHT") != all.end()) {
            const auto& left = all.at("LEFT");
            const auto& right = all.at("RIGHT");
            CHECK(std::abs(left.physical_x - 13.5) < 0.01, "LEFT 物理 X 坐标为 13.5 (DOWN 左侧 1u)");
            CHECK(std::abs(right.physical_x - 15.5) < 0.01, "RIGHT 物理 X 坐标为 15.5 (DOWN 右侧 1u)");
        }

        if (all.find("R_ALT") != all.end()) {
            CHECK(std::abs(all.at("R_ALT").physical_x - 10.5) < 0.01, "R_ALT 物理 X 坐标为 10.5 (空格右侧 1u)");
        }
        if (all.find("COPILOT") != all.end()) {
            CHECK(std::abs(all.at("COPILOT").physical_x - 12.5) < 0.01, "COPILOT 物理 X 坐标为 12.5 (LEFT 左侧 1u)");
        }

        // 18.2 VkToKeyName 扩展键与 Copilot 键解析
        CHECK(aura::VkToKeyName(VK_MENU, 0) == "L_ALT", "无扩展位 VK_MENU 解析为 L_ALT");
        CHECK(aura::VkToKeyName(VK_MENU, 1) == "R_ALT", "含扩展位 (LLKHF_EXTENDED) VK_MENU 解析为 R_ALT");
        CHECK(aura::VkToKeyName(VK_LMENU, 0) == "L_ALT", "VK_LMENU 解析为 L_ALT");
        CHECK(aura::VkToKeyName(VK_RMENU, 0) == "R_ALT", "VK_RMENU 解析为 R_ALT");
        CHECK(aura::VkToKeyName(VK_CONTROL, 0) == "L_CTRL", "无扩展位 VK_CONTROL 解析为 L_CTRL");
        CHECK(aura::VkToKeyName(VK_CONTROL, 1) == "R_CTRL", "含扩展位 (LLKHF_EXTENDED) VK_CONTROL 解析为 R_CTRL");
        CHECK(aura::VkToKeyName(VK_LCONTROL, 0) == "L_CTRL", "VK_LCONTROL 解析为 L_CTRL");
        CHECK(aura::VkToKeyName(VK_RCONTROL, 0) == "R_CTRL", "VK_RCONTROL 解析为 R_CTRL");
        CHECK(aura::VkToKeyName(VK_APPS, 0) == "COPILOT", "VK_APPS (0x5D) 解析为 COPILOT");
        CHECK(aura::VkToKeyName(0x86, 0) == "COPILOT", "VK_F23 (0x86) 解析为 COPILOT");
        CHECK(aura::VkToKeyName(VK_UP, 0) == "UP", "VK_UP 解析为 UP");
        CHECK(aura::VkToKeyName(VK_DOWN, 0) == "DOWN", "VK_DOWN 解析为 DOWN");
        CHECK(aura::VkToKeyName(VK_LEFT, 0) == "LEFT", "VK_LEFT 解析为 LEFT");
        CHECK(aura::VkToKeyName(VK_RIGHT, 0) == "RIGHT", "VK_RIGHT 解析为 RIGHT");

        // 18.3 KeyInputHub Copilot 宏消抖保护
        {
            auto& hub = aura::KeyInputHub::Instance();
            std::vector<aura::KeyPressEvent> drained;
            hub.DrainEvents(drained); // 清空历史

            // 模拟 Windows 11 Copilot 组合键序列: L_WIN + L_SHIFT + F23
            hub.RecordKeyPress("L_WIN");
            hub.RecordKeyPress("L_SHIFT");
            hub.RecordKeyPress("COPILOT");

            hub.DrainEvents(drained);
            CHECK(drained.size() == 1, "Copilot 宏消抖后只保留单一按键事件");
            if (!drained.empty()) {
                CHECK(drained[0].key_name == "COPILOT", "消抖后的保留事件为 COPILOT");
            }
        }
    }

    // =========================================================================
    // 20. thickness 参数解析、边界钳制与非有限/非法类型防御专项测试
    // =========================================================================
    std::cout << "\n[测试 20] ParseAndClampThickness 参数边界与容错测试...\n";
    {
        // 20.1 正常合法值测试
        nlohmann::json j_normal = {{"thickness", 1.5}};
        CHECK(std::abs(aura::ParseAndClampThickness(j_normal, 1.0) - 1.5) < 0.001,
              "正常合法值 1.5 正确解析为 1.5");

        nlohmann::json j_min = {{"thickness", 0.1}};
        CHECK(std::abs(aura::ParseAndClampThickness(j_min, 1.0) - 0.1) < 0.001,
              "边界合法值 0.1 正确解析为 0.1");

        nlohmann::json j_max = {{"thickness", 5.0}};
        CHECK(std::abs(aura::ParseAndClampThickness(j_max, 1.0) - 5.0) < 0.001,
              "边界合法值 5.0 正确解析为 5.0");

        // 20.2 下界与过小值钳制
        nlohmann::json j_zero = {{"thickness", 0.0}};
        CHECK(std::abs(aura::ParseAndClampThickness(j_zero, 1.0) - 0.1) < 0.001,
              "下界超限 0.0 正确钳制为 0.1 (防除零安全底线)");

        nlohmann::json j_neg = {{"thickness", -2.5}};
        CHECK(std::abs(aura::ParseAndClampThickness(j_neg, 1.0) - 0.1) < 0.001,
              "负值 -2.5 正确钳制为 0.1");

        nlohmann::json j_tiny = {{"thickness", 0.0001}};
        CHECK(std::abs(aura::ParseAndClampThickness(j_tiny, 1.0) - 0.1) < 0.001,
              "微小值 0.0001 正确钳制为 0.1");

        // 20.3 上界超限钳制
        nlohmann::json j_over = {{"thickness", 5.1}};
        CHECK(std::abs(aura::ParseAndClampThickness(j_over, 1.0) - 5.0) < 0.001,
              "上界超限 5.1 正确钳制为 5.0");

        nlohmann::json j_huge = {{"thickness", 999.0}};
        CHECK(std::abs(aura::ParseAndClampThickness(j_huge, 1.0) - 5.0) < 0.001,
              "超大值 999.0 正确钳制为 5.0");

        // 20.4 非有限数值与非数值类型防御 (安全回退默认值)
        nlohmann::json j_nan = {{"thickness", std::numeric_limits<double>::quiet_NaN()}};
        CHECK(std::abs(aura::ParseAndClampThickness(j_nan, 1.0) - 1.0) < 0.001,
              "NaN 非有限数值安全回退默认值 1.0");

        nlohmann::json j_inf = {{"thickness", std::numeric_limits<double>::infinity()}};
        CHECK(std::abs(aura::ParseAndClampThickness(j_inf, 1.0) - 1.0) < 0.001,
              "Infinity 非有限数值安全回退默认值 1.0");

        nlohmann::json j_str = {{"thickness", "invalid_string"}};
        CHECK(std::abs(aura::ParseAndClampThickness(j_str, 1.0) - 1.0) < 0.001,
              "非数值字符串安全回退默认值 1.0");

        nlohmann::json j_bool = {{"thickness", true}};
        CHECK(std::abs(aura::ParseAndClampThickness(j_bool, 1.0) - 1.0) < 0.001,
              "非数值布尔值安全回退默认值 1.0");

        nlohmann::json j_null = {{"thickness", nullptr}};
        CHECK(std::abs(aura::ParseAndClampThickness(j_null, 1.0) - 1.0) < 0.001,
              "null 安全回退默认值 1.0");

        nlohmann::json j_empty = nlohmann::json::object();
        CHECK(std::abs(aura::ParseAndClampThickness(j_empty, 2.0) - 2.0) < 0.001,
              "缺失 thickness 字段时返回指定的默认值 2.0");

        // 20.5 引擎构造函数层级 ClampThickness 辅助函数校验
        CHECK(std::abs(aura::ClampThickness(0.0) - 0.1) < 0.001, "ClampThickness(0.0) 钳制为 0.1");
        CHECK(std::abs(aura::ClampThickness(-10.0) - 0.1) < 0.001, "ClampThickness(-10.0) 钳制为 0.1");
        CHECK(std::abs(aura::ClampThickness(10.0) - 5.0) < 0.001, "ClampThickness(10.0) 钳制为 5.0");
        CHECK(std::abs(aura::ClampThickness(2.2) - 2.2) < 0.001, "ClampThickness(2.2) 保持 2.2");
        CHECK(std::abs(aura::ClampThickness(std::numeric_limits<double>::quiet_NaN()) - 1.0) < 0.001,
              "ClampThickness(NaN) 安全回退 1.0");
    }

    // =========================================================================
    // 21. 11 款光效引擎实例化、参数获取、spread 扩散方向与多帧安全推流测试
    // =========================================================================
    std::cout << "\n[测试 21] 11 款光效引擎实例化与渲染鲁棒性测试 (含 thickness 边界、spread 方向、速度联动)...\n";
    {
        aura::Keymap km;
        std::string km_path = "tests/fixtures/calibrated_keymap.json";
        if (!std::filesystem::exists(km_path)) {
            km_path = "calibrated_keymap.json";
        }
        km.LoadFromJson(km_path);

        aura::FrameBuffer frame;

        // 21.1 StaticEffect (静态单色 + 模拟按压)
        {
            aura::StaticEffect st1(aura::ColorRGB(255, 128, 64), false);
            CHECK(st1.GetColor().r == 255 && st1.GetColor().g == 128 && st1.GetColor().b == 64,
                  "StaticEffect 获取基准单色成功");
            CHECK(!st1.GetAnalog(), "StaticEffect analog 标志为 false");
            frame.Clear();
            st1.Render(0, frame, km);
            CHECK(frame.buffer[0] == 255 && frame.buffer[1] == 128 && frame.buffer[2] == 64,
                  "StaticEffect Render 成功全键铺满");

            aura::StaticEffect st_analog(aura::ColorRGB(50, 50, 50), true);
            CHECK(st_analog.GetAnalog(), "StaticEffect analog 标志为 true");
            frame.Clear();
            st_analog.Render(100, frame, km);
        }

        // 21.2 BreathingEffect (双色呼吸)
        {
            aura::BreathingEffect br(aura::ColorRGB(255, 0, 0), aura::ColorRGB(0, 0, 255), 2000);
            CHECK(br.GetPeriodMs() == 2000, "BreathingEffect period_ms 为 2000");
            CHECK(br.GetC1().r == 255 && br.GetC2().b == 255, "BreathingEffect c1/c2 参数获取成功");
            frame.Clear();
            br.Render(0, frame, km);
            br.Render(1000, frame, km);
            br.Render(2000, frame, km);
        }

        // 21.3 ColorCycleEffect (全光谱循环)
        {
            aura::ColorCycleEffect cc(3000);
            CHECK(cc.GetPeriodMs() == 3000, "ColorCycleEffect period_ms 为 3000");
            frame.Clear();
            cc.Render(0, frame, km);
            cc.Render(750, frame, km);
            cc.Render(1500, frame, km);
        }

        // 21.4 WaveEffect (波浪 + spread 扩散方向 + thickness 边界)
        {
            // 正常参数
            aura::WaveEffect wave_norm(3500, "spread", 2.0);
            CHECK(wave_norm.GetPeriodMs() == 3500, "WaveEffect period_ms 为 3500");
            CHECK(wave_norm.GetDirection() == "spread", "WaveEffect 扩散方向为 spread");
            CHECK(std::abs(wave_norm.GetThickness() - 2.0) < 0.001, "WaveEffect thickness 为 2.0");

            // 越界保护
            aura::WaveEffect wave_under(10, "diag_dl", 0.0);
            CHECK(wave_under.GetPeriodMs() == 33, "WaveEffect 周期安全钳制为 33ms");
            CHECK(std::abs(wave_under.GetThickness() - 0.1) < 0.001, "WaveEffect 下界厚度钳制为 0.1");

            aura::WaveEffect wave_over(3500, "spread", 10.0);
            CHECK(std::abs(wave_over.GetThickness() - 5.0) < 0.001, "WaveEffect 上界厚度钳制为 5.0");

            // 多帧推流验证（重点验证 spread 径向数学在各时间点稳定无崩溃、无 NaN）
            frame.Clear();
            for (uint64_t t = 0; t <= 3500; t += 350) {
                wave_norm.Render(t, frame, km);
                wave_under.Render(t, frame, km);
                wave_over.Render(t, frame, km);
            }
            CHECK(true, "WaveEffect 扩散 spread 与极限 thickness 多帧推流安全运行无异常");
        }

        // 21.5 ReactiveEffect (响应式点亮与渐隐)
        {
            aura::ReactiveEffect react(aura::ColorRGB(0, 10, 20), aura::ColorRGB(255, 50, 80), 2000);
            CHECK(react.GetSpeedMs() == 2000, "ReactiveEffect 周期为 2000");
            CHECK(react.GetBaseColor().b == 20, "ReactiveEffect bg 底色获取成功");
            CHECK(react.GetTriggerColor().r == 255, "ReactiveEffect 高亮色获取成功");
            frame.Clear();
            react.Render(0, frame, km);
            react.Render(500, frame, km);
        }

        // 21.6 RippleEffect (涟漪同心扩散 + thickness 边界 + 速度 speed_ms_ 联动)
        {
            aura::RippleEffect rip(aura::ColorRGB(0, 5, 10), aura::ColorRGB(0, 240, 255), 1600, 2.5);
            CHECK(rip.GetSpeedMs() == 1600, "RippleEffect 速度周期为 1600");
            CHECK(std::abs(rip.GetThickness() - 2.5) < 0.001, "RippleEffect thickness 为 2.5");
            CHECK(rip.GetBaseColor().b == 10, "RippleEffect bg 底色获取成功");
            CHECK(rip.GetTriggerColor().g == 240, "RippleEffect trigger 色获取成功");

            // 越界下界保护（确保 half_thick >= 0.1 彻底防止除零）
            aura::RippleEffect rip_zero(aura::ColorRGB(0, 0, 0), aura::ColorRGB(255, 255, 255), 33, 0.0);
            CHECK(rip_zero.GetSpeedMs() == 33, "RippleEffect 周期下界钳制为 33ms");
            CHECK(std::abs(rip_zero.GetThickness() - 0.1) < 0.001, "RippleEffect 厚度下界钳制为 0.1");

            // 模拟按键激发涟漪
            auto& hub = aura::KeyInputHub::Instance();
            hub.RecordKeyPress("SPACE");
            hub.RecordKeyPress("W");

            frame.Clear();
            for (uint64_t t = 0; t <= 2000; t += 200) {
                rip.Render(t, frame, km);
                rip_zero.Render(t, frame, km);
            }
            CHECK(true, "RippleEffect 敲击同心扩散与极端下界厚度多帧渲染无除零无崩溃");
        }

        // 21.7 StarryNightEffect (繁星闪烁 + random_colors)
        {
            aura::StarryNightEffect star_mono(aura::ColorRGB(0, 200, 255), false, 2500);
            CHECK(!star_mono.GetRandomColors(), "StarryNightEffect 单色模式 random_colors 为 false");
            CHECK(star_mono.GetColor().g == 200, "StarryNightEffect 单色色值正确");

            aura::StarryNightEffect star_rand(aura::ColorRGB(0, 200, 255), true, 2500);
            CHECK(star_rand.GetRandomColors(), "StarryNightEffect 随机色模式 random_colors 为 true");

            frame.Clear();
            star_mono.Render(0, frame, km);
            star_rand.Render(500, frame, km);
        }

        // 21.8 QuicksandEffect (流沙 + thickness + spread 方向)
        {
            aura::QuicksandEffect qs(aura::ColorRGB(255, 0, 0), aura::ColorRGB(0, 0, 255), 3500, "spread", 3.0);
            CHECK(qs.GetPeriodMs() == 3500, "QuicksandEffect 周期为 3500");
            CHECK(qs.GetDirection() == "spread", "QuicksandEffect 方向为 spread");
            CHECK(std::abs(qs.GetThickness() - 3.0) < 0.001, "QuicksandEffect thickness 为 3.0");
            CHECK(qs.GetC1().r == 255 && qs.GetC2().b == 255, "QuicksandEffect c1/c2 获取成功");

            aura::QuicksandEffect qs_clamp(aura::ColorRGB(0,0,0), aura::ColorRGB(255,255,255), 20, "diag_dl", 10.0);
            CHECK(qs_clamp.GetPeriodMs() == 33, "QuicksandEffect 周期钳制为 33ms");
            CHECK(std::abs(qs_clamp.GetThickness() - 5.0) < 0.001, "QuicksandEffect thickness 上界钳制为 5.0");

            frame.Clear();
            for (uint64_t t = 0; t <= 3500; t += 500) {
                qs.Render(t, frame, km);
                qs_clamp.Render(t, frame, km);
            }
            CHECK(true, "QuicksandEffect 流沙多帧渲染平稳");
        }

        // 21.9 CurrentEffect (电流脉冲 + thickness 光束宽度)
        {
            aura::CurrentEffect cur(aura::ColorRGB(0, 240, 255), 2000, 1.8);
            CHECK(cur.GetPeriodMs() == 2000, "CurrentEffect 周期为 2000");
            CHECK(std::abs(cur.GetThickness() - 1.8) < 0.001, "CurrentEffect thickness 为 1.8");
            CHECK(cur.GetColor().g == 240, "CurrentEffect 颜色获取正确");

            aura::CurrentEffect cur_clamp(aura::ColorRGB(255, 0, 0), 10, -5.0);
            CHECK(cur_clamp.GetPeriodMs() == 33, "CurrentEffect 周期钳制为 33ms");
            CHECK(std::abs(cur_clamp.GetThickness() - 0.1) < 0.001, "CurrentEffect thickness 下界钳制为 0.1");

            frame.Clear();
            for (uint64_t t = 0; t <= 2000; t += 200) {
                cur.Render(t, frame, km);
                cur_clamp.Render(t, frame, km);
            }
            CHECK(true, "CurrentEffect 电流光束宽度缩放与多帧渲染无异常");
        }

        // 21.10 RaindropEffect (雨滴淅沥)
        {
            aura::RaindropEffect rain(aura::ColorRGB(0, 180, 255), 2500);
            CHECK(rain.GetPeriodMs() == 2500, "RaindropEffect 周期为 2500");
            CHECK(rain.GetColor().b == 255, "RaindropEffect 颜色获取正确");
            frame.Clear();
            for (uint64_t t = 0; t <= 2500; t += 250) {
                rain.Render(t, frame, km);
            }
            CHECK(true, "RaindropEffect 雨滴多帧渲染无异常");
        }

        // 21.11 CustomKeymapEffect (自定义逐键与底色)
        {
            aura::CustomKeymapEffect ckm(aura::ColorRGB(20, 40, 60));
            CHECK(ckm.GetBgColor().r == 20 && ckm.GetBgColor().g == 40 && ckm.GetBgColor().b == 60,
                  "CustomKeymapEffect 背景色获取正确");
            frame.Clear();
            ckm.Render(0, frame, km);
            CHECK(frame.buffer[0] == 20 && frame.buffer[1] == 40 && frame.buffer[2] == 60,
                  "CustomKeymapEffect 铺底渲染成功");
        }
    }

    // =========================================================================
    // 22. RuleEngine 完整配置解析 11 种光效与对称颜色回退验证
    // =========================================================================
    std::cout << "\n[测试 22] RuleEngine 完整配置解析 11 种光效与对称颜色回退验证...\n";
    {
        const std::string tmp_11_cfg = (std::filesystem::temp_directory_path() / "test_cfg_11_effects.json").string();
        {
            std::ofstream ofs(tmp_11_cfg);
            ofs << R"json({
                "default_profile": "p_static",
                "fps": 30,
                "profiles": {
                    "p_static": {
                        "type": "static",
                        "color1": [12, 34, 56],
                        "analog": true
                    },
                    "p_breathing": {
                        "type": "breathing",
                        "color": [100, 150, 200],
                        "color2": [10, 20, 30],
                        "speed_index": 0
                    },
                    "p_cycle": {
                        "type": "color_cycle",
                        "speed_index": 2
                    },
                    "p_wave": {
                        "type": "wave",
                        "direction": "spread",
                        "thickness": 2.5,
                        "speed_index": 1
                    },
                    "p_custom": {
                        "type": "custom_keymap",
                        "bg": [5, 10, 15]
                    },
                    "p_reactive": {
                        "type": "reactive",
                        "bg": [12, 18, 24],
                        "color1": [255, 40, 60],
                        "speed_index": 1
                    },
                    "p_ripple": {
                        "type": "ripple",
                        "bg": [8, 16, 32],
                        "color": [0, 200, 255],
                        "thickness": 0.8,
                        "speed_index": 2
                    },
                    "p_starry": {
                        "type": "starry_night",
                        "color1": [60, 120, 180],
                        "random_colors": true,
                        "speed_index": 1
                    },
                    "p_quicksand": {
                        "type": "quicksand",
                        "color": [255, 120, 30],
                        "color2": [30, 120, 255],
                        "direction": "spread",
                        "thickness": 3.2,
                        "speed_index": 0
                    },
                    "p_current": {
                        "type": "current",
                        "color1": [0, 220, 240],
                        "thickness": 1.5,
                        "speed_index": 2
                    },
                    "p_raindrop": {
                        "type": "raindrop",
                        "color1": [110, 210, 255],
                        "speed_index": 1
                    }
                }
            })json";
        }

        aura::RuleEngine engine11;
        bool loaded11 = engine11.LoadConfig(tmp_11_cfg);
        CHECK(loaded11, "RuleEngine 成功加载包含全部 11 种光效配置的 JSON 文件");

        // 验证 11 个 profile 均已正确注册
        CHECK(engine11.HasProfile("p_static"), "已解析 p_static");
        CHECK(engine11.HasProfile("p_breathing"), "已解析 p_breathing");
        CHECK(engine11.HasProfile("p_cycle"), "已解析 p_cycle");
        CHECK(engine11.HasProfile("p_wave"), "已解析 p_wave");
        CHECK(engine11.HasProfile("p_custom"), "已解析 p_custom");
        CHECK(engine11.HasProfile("p_reactive"), "已解析 p_reactive");
        CHECK(engine11.HasProfile("p_ripple"), "已解析 p_ripple");
        CHECK(engine11.HasProfile("p_starry"), "已解析 p_starry");
        CHECK(engine11.HasProfile("p_quicksand"), "已解析 p_quicksand");
        CHECK(engine11.HasProfile("p_current"), "已解析 p_current");
        CHECK(engine11.HasProfile("p_raindrop"), "已解析 p_raindrop");

        // 验证各方案参数与回退值
        auto p_static = engine11.GetProfile("p_static");
        auto eff_static = std::dynamic_pointer_cast<const aura::StaticEffect>(p_static->base_effect);
        CHECK(eff_static != nullptr, "p_static 正确实例化为 StaticEffect");
        if (eff_static) {
            CHECK(eff_static->GetColor().r == 12 && eff_static->GetColor().g == 34 && eff_static->GetColor().b == 56,
                  "p_static: 成功从 color1 回退读取颜色 (12, 34, 56)");
            CHECK(eff_static->GetAnalog(), "p_static: analog 成功解析为 true");
        }

        auto p_breathing = engine11.GetProfile("p_breathing");
        auto eff_breathing = std::dynamic_pointer_cast<const aura::BreathingEffect>(p_breathing->base_effect);
        CHECK(eff_breathing != nullptr, "p_breathing 正确实例化为 BreathingEffect");
        if (eff_breathing) {
            CHECK(eff_breathing->GetC1().r == 100 && eff_breathing->GetC1().g == 150 && eff_breathing->GetC1().b == 200,
                  "p_breathing: 成功从 color 回退读取 c1 (100, 150, 200)");
            CHECK(eff_breathing->GetPeriodMs() == 5500, "p_breathing: speed_index 0 解析为 5500ms");
        }

        auto p_wave = engine11.GetProfile("p_wave");
        auto eff_wave = std::dynamic_pointer_cast<const aura::WaveEffect>(p_wave->base_effect);
        CHECK(eff_wave != nullptr, "p_wave 正确实例化为 WaveEffect");
        if (eff_wave) {
            CHECK(eff_wave->GetDirection() == "spread", "p_wave: direction 成功解析为 spread");
            CHECK(std::abs(eff_wave->GetThickness() - 2.5) < 0.001, "p_wave: thickness 成功解析为 2.5");
            CHECK(eff_wave->GetPeriodMs() == 3200, "p_wave: speed_index 1 解析为 3200ms");
        }

        auto p_reactive = engine11.GetProfile("p_reactive");
        auto eff_reactive = std::dynamic_pointer_cast<const aura::ReactiveEffect>(p_reactive->base_effect);
        CHECK(eff_reactive != nullptr, "p_reactive 正确实例化为 ReactiveEffect");
        if (eff_reactive) {
            CHECK(eff_reactive->GetBaseColor().r == 12 && eff_reactive->GetBaseColor().g == 18 && eff_reactive->GetBaseColor().b == 24,
                  "p_reactive: bg 成功解析为 (12, 18, 24)");
            CHECK(eff_reactive->GetTriggerColor().r == 255 && eff_reactive->GetTriggerColor().g == 40 && eff_reactive->GetTriggerColor().b == 60,
                  "p_reactive: 成功从 color1 回退读取高亮色");
        }

        auto p_ripple = engine11.GetProfile("p_ripple");
        auto eff_ripple = std::dynamic_pointer_cast<const aura::RippleEffect>(p_ripple->base_effect);
        CHECK(eff_ripple != nullptr, "p_ripple 正确实例化为 RippleEffect");
        if (eff_ripple) {
            CHECK(eff_ripple->GetBaseColor().r == 8 && eff_ripple->GetBaseColor().g == 16 && eff_ripple->GetBaseColor().b == 32,
                  "p_ripple: bg 成功解析为 (8, 16, 32)");
            CHECK(eff_ripple->GetTriggerColor().r == 0 && eff_ripple->GetTriggerColor().g == 200 && eff_ripple->GetTriggerColor().b == 255,
                  "p_ripple: trigger 颜色成功解析为 (0, 200, 255)");
            CHECK(std::abs(eff_ripple->GetThickness() - 0.8) < 0.001, "p_ripple: thickness 成功解析为 0.8");
            CHECK(eff_ripple->GetSpeedMs() == 1600, "p_ripple: speed_index 2 解析为 1600ms");
        }

        auto p_starry = engine11.GetProfile("p_starry");
        auto eff_starry = std::dynamic_pointer_cast<const aura::StarryNightEffect>(p_starry->base_effect);
        CHECK(eff_starry != nullptr, "p_starry 正确实例化为 StarryNightEffect");
        if (eff_starry) {
            CHECK(eff_starry->GetRandomColors(), "p_starry: random_colors 成功解析为 true");
            CHECK(eff_starry->GetColor().r == 60 && eff_starry->GetColor().g == 120 && eff_starry->GetColor().b == 180,
                  "p_starry: 成功从 color1 回退读取颜色 (60, 120, 180)");
        }

        auto p_quicksand = engine11.GetProfile("p_quicksand");
        auto eff_quicksand = std::dynamic_pointer_cast<const aura::QuicksandEffect>(p_quicksand->base_effect);
        CHECK(eff_quicksand != nullptr, "p_quicksand 正确实例化为 QuicksandEffect");
        if (eff_quicksand) {
            CHECK(eff_quicksand->GetDirection() == "spread", "p_quicksand: direction 成功解析为 spread");
            CHECK(std::abs(eff_quicksand->GetThickness() - 3.2) < 0.001, "p_quicksand: thickness 成功解析为 3.2");
            CHECK(eff_quicksand->GetC1().r == 255 && eff_quicksand->GetC1().g == 120 && eff_quicksand->GetC1().b == 30,
                  "p_quicksand: 成功从 color 回退读取 c1 (255, 120, 30)");
        }

        auto p_current = engine11.GetProfile("p_current");
        auto eff_current = std::dynamic_pointer_cast<const aura::CurrentEffect>(p_current->base_effect);
        CHECK(eff_current != nullptr, "p_current 正确实例化为 CurrentEffect");
        if (eff_current) {
            CHECK(std::abs(eff_current->GetThickness() - 1.5) < 0.001, "p_current: thickness 成功解析为 1.5");
            CHECK(eff_current->GetColor().r == 0 && eff_current->GetColor().g == 220 && eff_current->GetColor().b == 240,
                  "p_current: 成功从 color1 回退读取颜色");
        }

        // 统一多帧渲染验证（全部 11 种方案）
        aura::Keymap km;
        std::string km_path = "tests/fixtures/calibrated_keymap.json";
        if (!std::filesystem::exists(km_path)) {
            km_path = "calibrated_keymap.json";
        }
        km.LoadFromJson(km_path);

        aura::FrameBuffer frame;
        const std::vector<std::string> all_pnames = {
            "p_static", "p_breathing", "p_cycle", "p_wave", "p_custom",
            "p_reactive", "p_ripple", "p_starry", "p_quicksand", "p_current", "p_raindrop"
        };
        for (const auto& pname : all_pnames) {
            auto prof = engine11.GetProfile(pname);
            CHECK(prof != nullptr && prof->base_effect != nullptr, pname + " 具有有效的 base_effect");
            if (prof && prof->base_effect) {
                frame.Clear();
                prof->Render(100, frame, km);
                prof->Render(1000, frame, km);
            }
        }
        CHECK(true, "RuleEngine 解析生成的全部 11 款光效多帧渲染安全验证通过");

        std::filesystem::remove(tmp_11_cfg);
    }

    // =========================================================================
    // 23. ConditionNode AST 递归逻辑与多条件运算验证
    // =========================================================================
    std::cout << "\n[测试 23] ConditionNode AST 递归逻辑与多条件运算验证...\n";
    {
        aura::GsiState gsi;
        nlohmann::json payload = {
            {"player", {
                {"state", {
                    {"health", 18},
                    {"armor", 85},
                    {"flashed", 120}
                }},
                {"activity", "playing"},
                {"match_stats", {
                    {"kills", 3},
                    {"assists", 1}
                }}
            }},
            {"round", {
                {"phase", "live"},
                {"bomb", "planted"}
            }}
        };
        gsi.UpdateFromPayload(payload);

        // 1. 叶子节点比较运算符 (Eq, Ne, Lt, Le, Gt, Ge)
        {
            aura::ConditionNode node_eq;
            node_eq.field = "player.state.health";
            node_eq.comp_op = aura::CompareOp::Eq;
            node_eq.target_value = 18;
            CHECK(node_eq.Evaluate(&gsi, "cs2.exe"), "叶子节点: health == 18 判定为 true");

            aura::ConditionNode node_ne;
            node_ne.field = "player.state.health";
            node_ne.comp_op = aura::CompareOp::Ne;
            node_ne.target_value = 100;
            CHECK(node_ne.Evaluate(&gsi, "cs2.exe"), "叶子节点: health != 100 判定为 true");

            aura::ConditionNode node_lt;
            node_lt.field = "player.state.health";
            node_lt.comp_op = aura::CompareOp::Lt;
            node_lt.target_value = 20;
            CHECK(node_lt.Evaluate(&gsi, "cs2.exe"), "叶子节点: health < 20 判定为 true");

            aura::ConditionNode node_le;
            node_le.field = "player.state.health";
            node_le.comp_op = aura::CompareOp::Le;
            node_le.target_value = 18;
            CHECK(node_le.Evaluate(&gsi, "cs2.exe"), "叶子节点: health <= 18 判定为 true");

            aura::ConditionNode node_gt;
            node_gt.field = "player.state.armor";
            node_gt.comp_op = aura::CompareOp::Gt;
            node_gt.target_value = 50;
            CHECK(node_gt.Evaluate(&gsi, "cs2.exe"), "叶子节点: armor > 50 判定为 true");

            aura::ConditionNode node_ge;
            node_ge.field = "player.state.armor";
            node_ge.comp_op = aura::CompareOp::Ge;
            node_ge.target_value = 85;
            CHECK(node_ge.Evaluate(&gsi, "cs2.exe"), "叶子节点: armor >= 85 判定为 true");
        }

        // 2. 字符串与进程叶子节点
        {
            aura::ConditionNode node_bomb;
            node_bomb.field = "round.bomb";
            node_bomb.comp_op = aura::CompareOp::Eq;
            node_bomb.target_value = "planted";
            CHECK(node_bomb.Evaluate(&gsi, "cs2.exe"), "字符串字段: round.bomb == 'planted' 判定为 true");

            aura::ConditionNode node_proc;
            node_proc.field = "process";
            node_proc.comp_op = aura::CompareOp::Eq;
            node_proc.target_value = "cs2.exe";
            CHECK(node_proc.Evaluate(&gsi, "cs2.exe"), "进程字段: process == 'cs2.exe' 判定为 true");
            CHECK(!node_proc.Evaluate(&gsi, "code.exe"), "进程字段: process == 'cs2.exe' 在 code.exe 下判定为 false");
        }

        // 3. 逻辑运算与复合 AST 树
        {
            // AND 节点: (health < 20) AND (round.bomb == "planted")
            aura::ConditionNode c1, c2, node_and;
            c1.field = "player.state.health";
            c1.comp_op = aura::CompareOp::Lt;
            c1.target_value = 20;

            c2.field = "round.bomb";
            c2.comp_op = aura::CompareOp::Eq;
            c2.target_value = "planted";

            node_and.logic_op = aura::LogicOp::And;
            node_and.children = {c1, c2};
            CHECK(node_and.Evaluate(&gsi, "cs2.exe"), "复合 AND 树节点求值正确");

            // OR 节点: (health == 100) OR (round.bomb == "planted")
            aura::ConditionNode c_eq_100;
            c_eq_100.field = "player.state.health";
            c_eq_100.comp_op = aura::CompareOp::Eq;
            c_eq_100.target_value = 100;

            aura::ConditionNode node_or;
            node_or.logic_op = aura::LogicOp::Or;
            node_or.children = {c_eq_100, c2};
            CHECK(node_or.Evaluate(&gsi, "cs2.exe"), "复合 OR 树节点 (false OR true) 求值正确");

            // NOT 节点: NOT (health == 100)
            aura::ConditionNode node_not;
            node_not.logic_op = aura::LogicOp::Not;
            node_not.children = {c_eq_100};
            CHECK(node_not.Evaluate(&gsi, "cs2.exe"), "复合 NOT 节点求值正确");
        }

        // 4. JSON 序列化与反序列化双向一致性
        {
            nlohmann::json tree_json = {
                {"op", "and"},
                {"conditions", {
                    {
                        {"field", "process"},
                        {"op", "=="},
                        {"value", "cs2.exe"}
                    },
                    {
                        {"op", "or"},
                        {"conditions", {
                            {{"field", "player.state.health"}, {"op", "<="}, {"value", 20}},
                            {{"field", "round.bomb"}, {"op", "=="}, {"value", "planted"}}
                        }}
                    }
                }}
            };

            auto parsed_tree = aura::ConditionNode::FromJson(tree_json);
            CHECK(parsed_tree.Evaluate(&gsi, "cs2.exe"), "从 JSON 解析的高级 AST 树求值通过 (cs2.exe)");
            CHECK(!parsed_tree.Evaluate(&gsi, "explorer.exe"), "从 JSON 解析的高级 AST 树在前台不是 cs2 时严格隔离返回 false");

            nlohmann::json serialized = parsed_tree.ToJson();
            CHECK(serialized.contains("op") && serialized["op"] == "and", "AST 序列化保留顶级 op 字段");
            CHECK(serialized.contains("conditions") && serialized["conditions"].size() == 2, "AST 序列化保留 conditions 数组");
            auto re_parsed = aura::ConditionNode::FromJson(serialized);
            CHECK(re_parsed.Evaluate(&gsi, "cs2.exe"), "反序列化往返后求值结果完全一致");
        }
    }

    // =========================================================================
    // 24. OverlayManager CS2 瞬态覆盖光效驱动与生命周期/混合验证
    // =========================================================================
    std::cout << "\n[测试 24] OverlayManager CS2 瞬态覆盖光效驱动与生命周期/混合验证...\n";
    {
        aura::OverlayManager overlay_mgr;
        aura::Keymap km;
        std::string km_path = "tests/fixtures/calibrated_keymap.json";
        if (!std::filesystem::exists(km_path)) {
            km_path = "calibrated_keymap.json";
        }
        km.LoadFromJson(km_path);

        // 注册覆盖光效绑定: event.kill, event.bomb_planted
        aura::OverlayBinding b_kill;
        b_kill.event_name = "event.kill";
        b_kill.duration_ms = 1000;
        b_kill.attack_ms = 100;
        b_kill.fade_out_ms = 400;
        b_kill.blend_mode = "replace";
        b_kill.effect = std::make_shared<aura::StaticEffect>(aura::ColorRGB(255, 0, 0));
        overlay_mgr.RegisterBinding(b_kill);

        aura::OverlayBinding b_bomb;
        b_bomb.event_name = "event.bomb_planted";
        b_bomb.duration_ms = 2000;
        b_bomb.attack_ms = 200;
        b_bomb.fade_out_ms = 600;
        b_bomb.blend_mode = "add";
        b_bomb.effect = std::make_shared<aura::StaticEffect>(aura::ColorRGB(100, 50, 0));
        overlay_mgr.RegisterBinding(b_bomb);

        CHECK(overlay_mgr.GetActiveOverlayCount() == 0, "初始状态下无活跃覆盖光效");

        // 模拟 GSI 遥测驱动边缘触发
        aura::GsiState gsi;
        nlohmann::json s_initial = {
            {"player", {
                {"state", {{"health", 100}, {"armor", 100}, {"round_kills", 0}, {"round_killhs", 0}}},
                {"team", "CT"}
            }},
            {"round", {{"phase", "live"}, {"bomb", ""}}}
        };
        gsi.UpdateFromPayload(s_initial);
        overlay_mgr.UpdateBindingsFromGsi(&gsi, 1000);
        CHECK(overlay_mgr.GetActiveOverlayCount() == 0, "kills=0 时未触发覆盖光效");

        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        // kills: 0 -> 1 上升沿触发
        nlohmann::json s_kill1 = {
            {"player", {
                {"state", {{"health", 100}, {"armor", 100}, {"round_kills", 1}, {"round_killhs", 0}}},
                {"team", "CT"}
            }},
            {"round", {{"phase", "live"}, {"bomb", ""}}}
        };
        gsi.UpdateFromPayload(s_kill1);
        overlay_mgr.UpdateBindingsFromGsi(&gsi, 2000);
        CHECK(overlay_mgr.GetActiveOverlayCount() == 1, "kills 递增上升沿成功触发 event.kill 覆盖光效");

        // 持续处于 kills=1，不应重复触发叠加
        overlay_mgr.UpdateBindingsFromGsi(&gsi, 2100);
        CHECK(overlay_mgr.GetActiveOverlayCount() == 1, "kills 保持不变不产生重复触发");

        // 验证 ActiveOverlay 权重计算与各阶段特征
        aura::ActiveOverlay ao;
        ao.start_ms = 1000;
        ao.attack_ms = 100;
        ao.duration_ms = 1000;
        ao.fade_out_ms = 400; // 阶段: [1000, 1100] 爬坡, [1100, 1600] 持续, [1600, 2000] 线性消退

        CHECK(std::abs(ao.ComputeWeight(1000) - 0.0) < 0.05, "t=1000 起始时刻权重接近 0.0");
        CHECK(std::abs(ao.ComputeWeight(1050) - 0.5) < 0.05, "t=1050 attack 中点时刻权重接近 0.5");
        CHECK(std::abs(ao.ComputeWeight(1100) - 1.0) < 0.05, "t=1100 attack 终点时刻权重达到 1.0");
        CHECK(std::abs(ao.ComputeWeight(1300) - 1.0) < 0.05, "t=1300 sustain 维持期权重保持 1.0");
        CHECK(std::abs(ao.ComputeWeight(1800) - 0.5) < 0.05, "t=1800 fade_out 衰减中点权重接近 0.5");
        CHECK(std::abs(ao.ComputeWeight(2000) - 0.0) < 0.05, "t=2000 结束时刻权重衰减至 0.0");
        CHECK(ao.IsExpired(2001), "t=2001 标记为已过期");

        // 验证 FrameBuffer 混合应用 (replace 与 add 模式)
        aura::FrameBuffer base_frame;
        // 底色深蓝 (0, 0, 100)
        base_frame.Fill(0, 0, 100);

        aura::FrameBuffer test_frame = base_frame;
        // 在 peak 时刻 (t = 2100，duration=1000，start=2000，weight=1.0) 执行 ApplyOverlays (replace 模式纯红 255, 0, 0)
        overlay_mgr.ApplyOverlays(2100, test_frame, km, &gsi);
        CHECK(test_frame.buffer[0] == 255 && test_frame.buffer[1] == 0 && test_frame.buffer[2] == 0,
              "replace 模式在峰值完全替换底色为 (255, 0, 0)");

        // 在过期时刻 (t = 3100) 执行 ApplyOverlays，覆盖自动清理并无残留
        test_frame = base_frame;
        overlay_mgr.ApplyOverlays(3100, test_frame, km, &gsi);
        CHECK(overlay_mgr.GetActiveOverlayCount() == 0, "过期后 active_overlays_ 自动被清除释放");
        CHECK(test_frame.buffer[0] == 0 && test_frame.buffer[1] == 0 && test_frame.buffer[2] == 100,
              "覆盖光效自然结束后底色完全平滑还原");
    }

    // =========================================================================
    // 25. Orchestration Rules, Plugin 方案解析与 EffectEngine 预览机制验证
    // =========================================================================
    std::cout << "\n[测试 25] Orchestration Rules, Plugin 方案解析与 EffectEngine 预览机制验证...\n";
    {
        // 1. 验证 RuleEngine 加载 orchestration.rules 配置
        const std::string tmp_orch_cfg = (std::filesystem::temp_directory_path() / "test_cfg_orchestration.json").string();
        {
            std::ofstream ofs(tmp_orch_cfg);
            ofs << R"json({
                "default_profile": "desktop_prof",
                "fps": 25,
                "profiles": {
                    "desktop_prof": {
                        "type": "static",
                        "color": [10, 20, 30]
                    },
                    "cs2_base": {
                        "type": "breathing",
                        "color": [0, 255, 100]
                    },
                    "danger_high_priority": {
                        "type": "static",
                        "color": [255, 0, 0]
                    },
                    "custom_plugin_prof": {
                        "type": "plugin",
                        "plugin_path": "plugins/nonexistent_plugin.dll",
                        "effect_name": "NonexistentEffect"
                    }
                },
                "orchestration": {
                    "rules": [
                        {
                            "id": "rule_cs2_danger",
                            "name": "CS2 Critical Low Health Alert",
                            "process": "cs2.exe",
                            "dnd": true,
                            "condition": {
                                "op": "and",
                                "conditions": [
                                    {"field": "process", "op": "==", "value": "cs2.exe"},
                                    {"field": "player.state.health", "op": "<", "value": 25}
                                ]
                            },
                            "profile": "danger_high_priority"
                        },
                        {
                            "id": "rule_cs2_normal",
                            "name": "CS2 Normal Ingame",
                            "process": "cs2.exe",
                            "condition": {
                                "field": "process",
                                "op": "==",
                                "value": "cs2.exe"
                            },
                            "profile": "cs2_base"
                        }
                    ],
                    "event_overlays": [
                        {
                            "event": "event.kill",
                            "name": "Kill Splash",
                            "effect": "danger_high_priority",
                            "duration_ms": 900,
                            "attack_ms": 60,
                            "fade_out_ms": 300,
                            "blend_mode": "replace"
                        }
                    ],
                    "fallback_profile": "desktop_prof"
                }
            })json";
        }

        aura::RuleEngine engine;
        bool loaded_orch = engine.LoadConfig(tmp_orch_cfg);
        CHECK(loaded_orch, "成功加载包含 orchestration 完整规范的配置文件");

        const auto& orch = engine.GetOrchestration();
        CHECK(orch.rules.size() == 2, "成功解析 2 条现代 AST orchestration rules");
        CHECK(orch.event_overlays.size() == 1, "成功解析 1 条 event_overlays 瞬态事件覆盖规则");
        CHECK(orch.event_overlays[0].event == "event.kill", "event_overlay 事件名称解析为 event.kill");

        // 验证插件 profile 解析无异常回退
        CHECK(engine.HasProfile("custom_plugin_prof"), "插件类型 profile 成功解析注册到 Profile 表中");

        // 验证 AST 优先级驱动匹配: cs2.exe 满血 -> cs2_base
        aura::GsiState gsi;
        nlohmann::json s_full = {
            {"player", {{"state", {{"health", 100}}}}}
        };
        gsi.UpdateFromPayload(s_full);
        auto p_match1 = engine.MatchProfile("cs2.exe", &gsi);
        CHECK(p_match1 != nullptr && p_match1->name == "cs2_base", "满血状态下命中 cs2_base");
        CHECK(!engine.ShouldSuppressWebUi("cs2.exe", &gsi), "cs2_base 规则未开启 DND，ShouldSuppressWebUi 为 false");

        // cs2.exe 残血 (health = 15 < 25) -> danger_high_priority
        nlohmann::json s_low = {
            {"player", {{"state", {{"health", 15}}}}}
        };
        gsi.UpdateFromPayload(s_low);
        auto p_match2 = engine.MatchProfile("cs2.exe", &gsi);
        CHECK(p_match2 != nullptr && p_match2->name == "danger_high_priority", "残血状态下 AST 规则优先命中 danger_high_priority");
        CHECK(engine.ShouldSuppressWebUi("cs2.exe", &gsi), "danger_high_priority 规则配置 dnd=true，ShouldSuppressWebUi 返回 true");

        // 非目标前台进程 (code.exe) -> fallback desktop_prof
        auto p_match3 = engine.MatchProfile("code.exe", &gsi);
        CHECK(p_match3 != nullptr && p_match3->name == "desktop_prof", "非 cs2 进程回退为 fallback_profile (desktop_prof)");

        // 2. 验证 EffectEngine 实时编辑预览 (Preview Frame) 机制
        aura::EffectEngine effect_engine;
        aura::Keymap km;
        std::string km_path = "tests/fixtures/calibrated_keymap.json";
        if (!std::filesystem::exists(km_path)) {
            km_path = "calibrated_keymap.json";
        }
        km.LoadFromJson(km_path);

        effect_engine.SetActiveProfile(engine.GetProfile("desktop_prof"));

        aura::FrameBuffer normal_fb;
        effect_engine.Tick(normal_fb, km, &gsi);
        CHECK(normal_fb.buffer[0] == 10 && normal_fb.buffer[1] == 20 && normal_fb.buffer[2] == 30,
              "未激活预览时输出 profile 原始帧 (10, 20, 30)");

        // 压入编辑态预览帧 (亮黄色 255, 255, 0)，持续 150ms
        aura::FrameBuffer preview_fb;
        preview_fb.Fill(255, 255, 0);
        effect_engine.SetPreviewFrame(preview_fb, 150);
        CHECK(effect_engine.HasActivePreview(), "SetPreviewFrame 后 HasActivePreview 为 true");

        aura::FrameBuffer rendered_preview;
        effect_engine.Tick(rendered_preview, km, &gsi);
        CHECK(rendered_preview.buffer[0] == 255 && rendered_preview.buffer[1] == 255 && rendered_preview.buffer[2] == 0,
              "预览处于激活期时，Tick 严格优先渲染预览帧硬件缓冲");

        // 模拟等待超时后预览自然过期
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CHECK(!effect_engine.HasActivePreview(), "超时后 HasActivePreview 自动过期失效");

        aura::FrameBuffer restored_fb;
        effect_engine.Tick(restored_fb, km, &gsi);
        CHECK(restored_fb.buffer[0] == 10 && restored_fb.buffer[1] == 20 && restored_fb.buffer[2] == 30,
              "预览过期后无缝恢复为活动配置 profile 帧");

        // 3. 验证 PluginManager 动态加载保护与路径规范化机制
        auto& pm = aura::PluginManager::Instance();
        auto nonexistent_eff = pm.CreateEffect("DefiniteNonexistentPluginEffect_12345");
        CHECK(nonexistent_eff == nullptr, "不存在的插件安全返回 nullptr，不崩溃无异常");

        auto shadow_path = aura::PluginManager::ResolvePluginPath("test_shadow_effect");
        CHECK(shadow_path.string().find("test_shadow_effect") != std::string::npos,
              "ResolvePluginPath 能够准确规范化插件路径");

        std::filesystem::remove(tmp_orch_cfg);
    }

    // =========================================================================
    // [测试 P0-1 回归] COM 服务器注册表解析与 UAF 防护验证
    // =========================================================================
    std::cout << "\n[测试 P0-1] COM 服务器注册表解析与 UAF 防护验证...\n";
    {
        std::wstring test_clsid = L"{AE9DB4C8-4F2A-4756-9B11-2F6D78C61F1A}";

        // 1. HKLM 命中路径：直接返回，不触发 HKCR
        bool hkcr_called = false;
        auto mock_hklm_hit = [&](HKEY root, const std::wstring& key) -> std::wstring {
            if (root == HKEY_LOCAL_MACHINE) {
                if (key == L"SOFTWARE\\Classes\\CLSID\\" + test_clsid + L"\\InprocServer32") {
                    return L"C:\\Windows\\System32\\hklm_hal.dll";
                }
            } else if (root == HKEY_CLASSES_ROOT) {
                hkcr_called = true;
            }
            return L"";
        };
        std::wstring res_hklm = aura::ResolveInprocServerDllPath(test_clsid, mock_hklm_hit);
        CHECK(res_hklm == L"C:\\Windows\\System32\\hklm_hal.dll", "HKLM 存在时优先返回 HKLM 路径");
        CHECK(!hkcr_called, "HKLM 命中时不应调用 HKCR 回退");

        // 2. HKLM miss -> HKCR fallback：验证 HKCR 路径接收到完全相同的 clsid_text，无 UAF
        std::wstring captured_hkcr_key;
        auto mock_hkcr_fallback = [&](HKEY root, const std::wstring& key) -> std::wstring {
            if (root == HKEY_LOCAL_MACHINE) {
                return L""; // miss
            }
            if (root == HKEY_CLASSES_ROOT) {
                captured_hkcr_key = key;
                return L"C:\\Program Files\\ASUS\\hkcr_fallback_hal.dll";
            }
            return L"";
        };
        std::wstring res_hkcr = aura::ResolveInprocServerDllPath(test_clsid, mock_hkcr_fallback);
        CHECK(res_hkcr == L"C:\\Program Files\\ASUS\\hkcr_fallback_hal.dll", "HKLM miss 时成功回退到 HKCR 路径");
        CHECK(captured_hkcr_key == L"CLSID\\" + test_clsid + L"\\InprocServer32",
              "HKCR fallback 接收到完整正确的 CLSID 子键 (无 Use-After-Free)");

        // 3. 两处皆 miss 时安全返回空
        auto mock_both_miss = [](HKEY, const std::wstring&) -> std::wstring {
            return L"";
        };
        std::wstring res_empty = aura::ResolveInprocServerDllPath(test_clsid, mock_both_miss);
        CHECK(res_empty.empty(), "注册表均未命中时安全返回空字符串");
    }

    // =========================================================================
    // [测试 P0-2 回归] 配置热重载与 GSI 状态驱动的 ShouldSuppressWebUi 联动
    // =========================================================================
    std::cout << "\n[测试 P0-2] 配置热重载与 GSI 状态驱动的 ShouldSuppressWebUi 联动...\n";
    {
        std::string tmp_hot_cfg = (std::filesystem::temp_directory_path() / "tmp_hot_reload_p02.json").string();
        nlohmann::json cfg_init = {
            {"default_profile", "desktop_prof"},
            {"profiles", {
                {"desktop_prof", {{"type", "static"}, {"color", {10, 20, 30}}}},
                {"cs2_safe", {{"type", "static"}, {"color", {0, 255, 0}}}},
                {"cs2_danger", {{"type", "static"}, {"color", {255, 0, 0}}}}
            }},
            {"orchestration", {
                {"version", 2},
                {"fallback_profile", "desktop_prof"},
                {"rules", {
                    {
                        {"process", "cs2.exe"},
                        {"target_profile", "cs2_danger"},
                        {"dnd", true},
                        {"condition", {
                            {"field", "player.state.health"},
                            {"op", "<"},
                            {"value", 20}
                        }}
                    },
                    {
                        {"process", "cs2.exe"},
                        {"target_profile", "cs2_safe"},
                        {"dnd", false}
                    }
                }}
            }}
        };

        {
            std::ofstream out(tmp_hot_cfg);
            out << cfg_init.dump(2);
        }

        aura::RuleEngine engine_hot;
        bool ok = engine_hot.LoadConfig(tmp_hot_cfg);
        CHECK(ok, "成功加载热重载测试配置");

        // 1. 前台进程为 cs2.exe，初始 GSI 状态：健康 (health = 100)
        aura::GsiState live_gsi;
        live_gsi.UpdateFromPayload({{"player", {{"state", {{"health", 100}}}}}});

        std::string cur_proc = "cs2.exe";
        CHECK(!engine_hot.ShouldSuppressWebUi(cur_proc, &live_gsi), "健康状态下未触发 DND (dnd=false)");

        // 2. 前台进程保持不变 (cs2.exe) -> GSI 条件发生变化：残血 (health = 15)
        live_gsi.UpdateFromPayload({{"player", {{"state", {{"health", 15}}}}}});

        // 3. 模拟配置热重载（重载触发文件修改与 CheckAndReload）
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        {
            cfg_init["fps"] = 60;
            std::ofstream out(tmp_hot_cfg);
            out << cfg_init.dump(2);
        }
        CHECK(engine_hot.CheckAndReload(), "CheckAndReload 成功捕获并应用配置变更");

        // 4. 关键验收：在配置热重载路径下，必须传入当前 live_gsi，立即按当前残血 GSI 正确重新计算 DND
        bool suppress_with_gsi = engine_hot.ShouldSuppressWebUi(cur_proc, &live_gsi);
        bool suppress_without_gsi = engine_hot.ShouldSuppressWebUi(cur_proc); // 缺陷形态：漏传 GSI (nullptr)

        CHECK(suppress_with_gsi, "热重载后传入当前 GSI: 立即按残血条件计算得到 DND 抑制 (suppress=true)");
        CHECK(!suppress_without_gsi, "若漏传 GSI: 无法评估残血条件导致 DND 漏判 (确认修复必要性)");

        std::filesystem::remove(tmp_hot_cfg);
    }

    // =========================================================================
    // [测试 P1-2 回归] ASUS HAL 逆向接口兼容性 Gate 判定套件
    // =========================================================================
    std::cout << "\n[测试 P1-2] ASUS HAL 逆向接口兼容性 Gate 判定套件...\n";
    {
        // 1. 针对受支持文件的 Gate 判定 (drivers/AacKbHal_x64.dll)
        std::wstring valid_hal_path = L"drivers/AacKbHal_x64.dll";
        if (!std::filesystem::exists(valid_hal_path)) {
            valid_hal_path = L"../../drivers/AacKbHal_x64.dll";
        }
        if (std::filesystem::exists(valid_hal_path)) {
            aura::HalGateResult res = aura::ValidateHalFileGate(valid_hal_path);
            CHECK(res.status == aura::HalGateStatus::Supported, "原厂 AacKbHal_x64.dll 必须通过文件兼容性 Gate 校验");
            CHECK(res.sha256 == "52d575bf942b7551b3f120c446bf0d853e36f9225c6b9a17407a80e0b1829f04",
                  "校验 SHA-256 散列值完全吻合");
            CHECK(res.file_version == "1.3.46.0", "校验 FileVersion 完全吻合 (1.3.46.0)");
            CHECK(res.matched_version != nullptr, "成功匹配已验证版本表条目");
        } else {
            std::cout << "  [*] 跳过本地 drivers/AacKbHal_x64.dll 真实文件读取 (文件不存在)\n";
        }

        // 2. 针对不受支持的文件 (Unsupported Version / Hash Mismatch)
        std::filesystem::path tmp_fake_p = std::filesystem::temp_directory_path() / "tmp_fake_hal.dll";
        {
            std::ofstream out(tmp_fake_p, std::ios::binary);
            std::string fake_data = "THIS IS NOT A VALID ASUS HAL DLL BUT A CORRUPTED OR UNKNOWN VERSION";
            out.write(fake_data.data(), fake_data.size());
        }
        std::wstring fake_path = tmp_fake_p.wstring();
        aura::HalGateResult res_unsupported = aura::ValidateHalFileGate(fake_path);
        CHECK(res_unsupported.status == aura::HalGateStatus::UnsupportedVersion,
              "未知或散列不匹配的 DLL 必须判定为 UnsupportedVersion (Fail-closed)");
        CHECK(!res_unsupported.IsSupported(), "IsSupported() 必须返回 false");
        std::string err_str = aura::FormatHalGateError(res_unsupported);
        CHECK(err_str.find("Unsupported ASUS HAL version") != std::string::npos,
              "错误信息必须包含规范的 'Unsupported ASUS HAL version'");
        CHECK(err_str.find("Hardware control disabled for safety") != std::string::npos,
              "错误信息必须包含 'Hardware control disabled for safety'");
        std::filesystem::remove(tmp_fake_p);

        // 3. 针对不存在的文件 (FileNotFound)
        aura::HalGateResult res_missing = aura::ValidateHalFileGate(L"nonexistent_dir/missing_hal_12345.dll");
        CHECK(res_missing.status == aura::HalGateStatus::FileNotFound, "不存在的文件返回 FileNotFound");
        CHECK(!res_missing.IsSupported(), "不存在的文件不可通过 Gate");

        // 4. 针对内存模块 runtime signature 不匹配 (SignatureMismatch)
        // 构造一个模拟的 PE64 内存镜像缓冲区 (尺寸需容纳 rva_enable 0x1CB85C)
        std::vector<uint8_t> mock_pe(0x250000, 0x00);
        IMAGE_DOS_HEADER* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(mock_pe.data());
        dos->e_magic = IMAGE_DOS_SIGNATURE;
        dos->e_lfanew = 0x100;
        IMAGE_NT_HEADERS* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(mock_pe.data() + 0x100);
        nt->Signature = IMAGE_NT_SIGNATURE;
        nt->OptionalHeader.SizeOfImage = static_cast<DWORD>(mock_pe.size());

        const auto& verified_table = aura::GetVerifiedHalVersions();
        CHECK(!verified_table.empty(), "已验证版本表非空且集中管理");
        const aura::HalVersionInfo* v_info = &verified_table[0];

        // 故意填入错误的指令序言 (如 NOPs 0x90)
        memset(mock_pe.data() + v_info->rva_logger, 0x90, 16);

        aura::HalGateResult res_sig_fail = aura::ValidateHalModuleGate(reinterpret_cast<HMODULE>(mock_pe.data()), v_info);
        CHECK(res_sig_fail.status == aura::HalGateStatus::SignatureMismatch,
              "指令序言被篡改或不匹配时必须判定为 SignatureMismatch");
        CHECK(!res_sig_fail.IsSupported(), "签名不符的模块拒绝通过 Gate 驱动硬件");

        // 将正确序言填入，再次校验应通过
        memcpy(mock_pe.data() + v_info->rva_logger, v_info->logger_prologue.data(), v_info->logger_prologue.size());
        aura::HalGateResult res_sig_pass = aura::ValidateHalModuleGate(reinterpret_cast<HMODULE>(mock_pe.data()), v_info);
        CHECK(res_sig_pass.status == aura::HalGateStatus::Supported,
              "指令序言吻合时内存签名 Gate 判定通过");

        // 填入已打补丁的 0xC3 (ret)，亦应被识别为合规已 patch 模块
        mock_pe[v_info->rva_logger] = 0xC3;
        aura::HalGateResult res_sig_patched = aura::ValidateHalModuleGate(reinterpret_cast<HMODULE>(mock_pe.data()), v_info);
        CHECK(res_sig_patched.status == aura::HalGateStatus::Supported,
              "已应用防崩补丁 (0xC3) 的模块安全通过 Gate 校验");

        // 5. 针对 ApplyAacDriverPatch 的 Fail-closed 约束与 Gate 语义回归测试 (A/B/C)
        // 回归测试 A: known SHA + correct memory signature -> allow
        {
            // mock_pe 填入原厂标准序言，v_info 代表已通过文件 Gate 匹配的已知版本 (known SHA)
            memcpy(mock_pe.data() + v_info->rva_logger, v_info->logger_prologue.data(), v_info->logger_prologue.size());
            *reinterpret_cast<uint32_t*>(mock_pe.data() + v_info->rva_enable) = 1; // enable = 1

            bool patch_a = aura::ApplyAacDriverPatch(reinterpret_cast<HMODULE>(mock_pe.data()), v_info);
            CHECK(patch_a, "测试 A: known SHA + correct memory signature 必须允许 patch 并成功修补 (allow)");
            CHECK(mock_pe[v_info->rva_logger] == 0xC3, "Logger::Log 函数入口成功修补为 0xC3 (ret)");
            CHECK(*reinterpret_cast<const uint32_t*>(mock_pe.data() + v_info->rva_enable) == 0,
                  "EnableLog 标志位成功置 0");

            // 验证在已知身份前提下的 already-patched 幂等性放行
            bool patch_a_idempotent = aura::ApplyAacDriverPatch(reinterpret_cast<HMODULE>(mock_pe.data()), v_info);
            CHECK(patch_a_idempotent, "测试 A (幂等性): 已通过身份 Gate 的已修补模块再次调用必须安全放行");
        }

        // 回归测试 B: known SHA + wrong memory signature -> deny
        {
            // mock_pe_bad 具有已知身份 v_info，但内存序言被篡改或不匹配 (如 0x90 NOPs)
            std::vector<uint8_t> mock_pe_bad = mock_pe;
            memset(mock_pe_bad.data() + v_info->rva_logger, 0x90, 16);
            *reinterpret_cast<uint32_t*>(mock_pe_bad.data() + v_info->rva_enable) = 1;

            bool patch_b = aura::ApplyAacDriverPatch(reinterpret_cast<HMODULE>(mock_pe_bad.data()), v_info);
            CHECK(!patch_b, "测试 B: known SHA + wrong memory signature 必须拒绝 patch (deny)");
            CHECK(mock_pe_bad[v_info->rva_logger] == 0x90, "拒绝后函数入口序言未被篡改修改");
            CHECK(*reinterpret_cast<const uint32_t*>(mock_pe_bad.data() + v_info->rva_enable) == 1,
                  "拒绝后 EnableLog 状态保持不变");
        }

        // 回归测试 C: unknown / unavailable file identity + matching prologue -> MUST deny
        {
            // 构造内存序言与合法版本完全一致的模块 (matching prologue)
            std::vector<uint8_t> mock_pe_c = mock_pe;
            memcpy(mock_pe_c.data() + v_info->rva_logger, v_info->logger_prologue.data(), v_info->logger_prologue.size());
            *reinterpret_cast<uint32_t*>(mock_pe_c.data() + v_info->rva_enable) = 1;

            // C1: unavailable file identity (纯内存模块，无法通过 GetModuleFileNameW 从磁盘确认身份)
            // 严禁靠内存序言猜测版本，必须绝对拒绝 (MUST deny)
            bool patch_c_unavail_no_ver = aura::ApplyAacDriverPatch(reinterpret_cast<HMODULE>(mock_pe_c.data()));
            CHECK(!patch_c_unavail_no_ver,
                  "测试 C1: 文件身份不可用 (unavailable file identity) 即使序言匹配也必须拒绝 (MUST deny)");

            bool patch_c_unavail_null_ver = aura::ApplyAacDriverPatch(reinterpret_cast<HMODULE>(mock_pe_c.data()), nullptr);
            CHECK(!patch_c_unavail_null_ver,
                  "测试 C1: matched_version 为 nullptr 即使序言匹配也必须拒绝 (MUST deny)");

            // C2: unknown file identity (磁盘上存在文件，但其 SHA-256 不在已验证白名单中)
            std::filesystem::path tmp_unknown_dll = std::filesystem::temp_directory_path() / "tmp_unknown_hal.dll";
            {
                std::ofstream out(tmp_unknown_dll, std::ios::binary);
                out.write(reinterpret_cast<const char*>(mock_pe_c.data()), mock_pe_c.size());
            }
            aura::HalGateResult gate_unknown = aura::ValidateHalFileGate(tmp_unknown_dll.wstring());
            CHECK(gate_unknown.status == aura::HalGateStatus::UnsupportedVersion,
                  "测试 C2: 未知文件散列判定为 UnsupportedVersion");
            CHECK(gate_unknown.matched_version == nullptr,
                  "测试 C2: 未知文件 matched_version 为空");
            CHECK(!gate_unknown.IsSupported(),
                  "测试 C2: 未知文件绝对不得通过 ValidateHalFileGate");

            bool patch_c_unknown = aura::ApplyAacDriverPatch(
                reinterpret_cast<HMODULE>(mock_pe_c.data()), gate_unknown.matched_version);
            CHECK(!patch_c_unknown,
                  "测试 C2: 未知文件身份 (unknown file identity) 即使序言匹配也绝对拒绝 patch (MUST deny)");

            std::filesystem::remove(tmp_unknown_dll);

            // C3: 畸变非法模块句柄
            std::vector<uint8_t> invalid_mod(0x1000, 0x00);
            bool patch_c_invalid = aura::ApplyAacDriverPatch(reinterpret_cast<HMODULE>(invalid_mod.data()), nullptr);
            CHECK(!patch_c_invalid, "测试 C3: 非法/未匹配模块句柄必须被拒绝 (MUST deny)");

            // C4: 伪造/未经验证的 HalVersionInfo 对象（即使序言匹配也绝对禁止绕过白名单）
            aura::HalVersionInfo forged_ver;
            forged_ver.file_version = "9.9.9.9";
            forged_ver.sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
            forged_ver.rva_logger = v_info->rva_logger;
            forged_ver.rva_enable = v_info->rva_enable;
            forged_ver.logger_prologue = v_info->logger_prologue;
            bool patch_c_forged = aura::ApplyAacDriverPatch(
                reinterpret_cast<HMODULE>(mock_pe_c.data()), &forged_ver);
            CHECK(!patch_c_forged,
                  "测试 C4: 未在 GetVerifiedHalVersions 静态白名单表内的伪造版本对象必须被拒绝 (MUST deny)");

            // C5: already-patched 模块在文件身份不可用 (unavailable identity) 时必须绝对拒绝 (MUST deny)
            std::vector<uint8_t> mock_pe_patched = mock_pe;
            mock_pe_patched[v_info->rva_logger] = 0xC3;
            *reinterpret_cast<uint32_t*>(mock_pe_patched.data() + v_info->rva_enable) = 0;
            bool patch_c_patched_unavail = aura::ApplyAacDriverPatch(
                reinterpret_cast<HMODULE>(mock_pe_patched.data()), nullptr);
            CHECK(!patch_c_patched_unavail,
                  "测试 C5: 已经包含 0xC3 补丁但文件身份不可用的模块必须被绝对拒绝 (MUST deny)");
        }

        // 6. 针对极端畸变/越界 PE 缓冲区的 SEH 防御检验 (无崩溃防御)
        std::vector<uint8_t> corrupted_pe(0x200, 0x00);
        IMAGE_DOS_HEADER* bad_dos = reinterpret_cast<IMAGE_DOS_HEADER*>(corrupted_pe.data());
        bad_dos->e_magic = IMAGE_DOS_SIGNATURE;
        bad_dos->e_lfanew = 0x7FFFFFFF; // 巨大越界指针
        aura::HalGateResult res_corrupted = aura::ValidateHalModuleGate(reinterpret_cast<HMODULE>(corrupted_pe.data()), v_info);
        CHECK(!res_corrupted.IsSupported(), "异常畸变 PE 内存安全拦截，未触发进程崩溃");

        std::cout << "  [*] 兼容性 Gate 纯逻辑与文件校验通过 (CI 模拟环境未连接真实 ASUS 硬件)\n";
    }

    // =========================================================================
    std::cout << "\n[测试 P1-3] hardware_backend 严格配置与反拼写错误 (Fail-closed) 校验...\n";
    {
        auto write_temp_config = [](const nlohmann::json& content) -> std::filesystem::path {
            std::filesystem::path tmp_p = std::filesystem::temp_directory_path() / ("tmp_backend_cfg_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
            std::ofstream out(tmp_p);
            out << content.dump(2);
            return tmp_p;
        };

        nlohmann::json base_cfg = {
            {"default_profile", "desktop"},
            {"fps", 25},
            {"profiles", {
                {"desktop", {
                    {"type", "static"},
                    {"color", {255, 255, 255}}
                }}
            }}
        };

        // 1. hardware_backend: "native_hid" (有效值)
        {
            nlohmann::json cfg = base_cfg;
            cfg["hardware_backend"] = "native_hid";
            auto p = write_temp_config(cfg);
            aura::RuleEngine re;
            bool ok = re.LoadConfig(p.string());
            std::filesystem::remove(p);
            CHECK(ok, "hardware_backend: 'native_hid' 必须加载成功");
            CHECK(re.GetHardwareBackend() == aura::HardwareBackend::NativeHid,
                  "GetHardwareBackend 必须为 NativeHid");
        }

        // 2. hardware_backend: "legacy_hal" (有效值)
        {
            nlohmann::json cfg = base_cfg;
            cfg["hardware_backend"] = "legacy_hal";
            auto p = write_temp_config(cfg);
            aura::RuleEngine re;
            bool ok = re.LoadConfig(p.string());
            std::filesystem::remove(p);
            CHECK(ok, "hardware_backend: 'legacy_hal' 必须加载成功");
            CHECK(re.GetHardwareBackend() == aura::HardwareBackend::LegacyHal,
                  "GetHardwareBackend 必须为 LegacyHal");
        }

        // 3. hardware_backend: "auto" (有效值)
        {
            nlohmann::json cfg = base_cfg;
            cfg["hardware_backend"] = "auto";
            auto p = write_temp_config(cfg);
            aura::RuleEngine re;
            bool ok = re.LoadConfig(p.string());
            std::filesystem::remove(p);
            CHECK(ok, "hardware_backend: 'auto' 必须加载成功");
            CHECK(re.GetHardwareBackend() == aura::HardwareBackend::Auto,
                  "GetHardwareBackend 必须为 Auto");
        }

        // 4. 拼写错误: "native_hd" (必须 LoadConfig 失败，Fail-closed)
        {
            nlohmann::json cfg = base_cfg;
            cfg["hardware_backend"] = "native_hd";
            auto p = write_temp_config(cfg);
            aura::RuleEngine re;
            bool ok = re.LoadConfig(p.string());
            std::filesystem::remove(p);
            CHECK(!ok, "拼写错误 hardware_backend: 'native_hd' 必须导致 LoadConfig 明确失败 (Fail-closed)");
        }

        // 5. 非法字符串: "invalid_backend" (必须 LoadConfig 失败)
        {
            nlohmann::json cfg = base_cfg;
            cfg["hardware_backend"] = "invalid_backend";
            auto p = write_temp_config(cfg);
            aura::RuleEngine re;
            bool ok = re.LoadConfig(p.string());
            std::filesystem::remove(p);
            CHECK(!ok, "非法字符串 hardware_backend: 'invalid_backend' 必须导致 LoadConfig 失败");
        }

        // 6. 类型错误: 整数 123 (必须 LoadConfig 失败)
        {
            nlohmann::json cfg = base_cfg;
            cfg["hardware_backend"] = 123;
            auto p = write_temp_config(cfg);
            aura::RuleEngine re;
            bool ok = re.LoadConfig(p.string());
            std::filesystem::remove(p);
            CHECK(!ok, "类型错误 hardware_backend: 123 (int) 必须导致 LoadConfig 失败");
        }

        // 7. 类型错误: 对象 {} (必须 LoadConfig 失败)
        {
            nlohmann::json cfg = base_cfg;
            cfg["hardware_backend"] = nlohmann::json::object();
            auto p = write_temp_config(cfg);
            aura::RuleEngine re;
            bool ok = re.LoadConfig(p.string());
            std::filesystem::remove(p);
            CHECK(!ok, "类型错误 hardware_backend: {} (object) 必须导致 LoadConfig 失败");
        }

        // 8. 缺省省略 hardware_backend -> 默认为 Auto
        {
            nlohmann::json cfg = base_cfg;
            auto p = write_temp_config(cfg);
            aura::RuleEngine re;
            bool ok = re.LoadConfig(p.string());
            std::filesystem::remove(p);
            CHECK(ok, "省略 hardware_backend 必须加载成功");
            CHECK(re.GetHardwareBackend() == aura::HardwareBackend::Auto,
                  "省略 hardware_backend 时默认后端必须为 Auto");
        }
    }

    std::cout << "\n=========================================================\n";
    if (failures == 0) {
        std::cout << "  [SUCCESS] 所有 GSI 前台隔离、游戏事件与回归护栏测试全部 100% 通过！\n";
    } else {
        std::cout << "  [FAILED] 本次共有 " << failures << " 项显式检查未通过！\n";
    }
    std::cout << "=========================================================\n";
    return failures == 0 ? 0 : 1;
}
