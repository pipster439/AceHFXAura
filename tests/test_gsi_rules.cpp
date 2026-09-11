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
#include "engine/builtin_effects.h"

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

    std::cout << "\n=========================================================\n";
    if (failures == 0) {
        std::cout << "  [SUCCESS] 所有 GSI 前台隔离、游戏事件与回归护栏测试全部 100% 通过！\n";
    } else {
        std::cout << "  [FAILED] 本次共有 " << failures << " 项显式检查未通过！\n";
    }
    std::cout << "=========================================================\n";
    return failures == 0 ? 0 : 1;
}
