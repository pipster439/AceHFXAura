#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <memory>
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

    // 8. [R16 参数下界校验护栏]
    std::cout << "\n[测试 8] 验证灯效引擎极小周期 period_ms=1 真实渲染有效性 (R16 护栏)...\n";
    {
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

        // 验证 BreathingEffect 在 period_ms=1 极小周期下渲染正确性
        aura::BreathingEffect breath(aura::ColorRGB(255, 0, 0), aura::ColorRGB(0, 0, 0), 1);
        breath.Render(0, fb, km);
        // 在 elapsed_ms=0 时，呼吸光效相位为 0，首键 RGB 应为预设的主色 (255, 0, 0)
        CHECK(fb.buffer[0] == 255 && fb.buffer[1] == 0 && fb.buffer[2] == 0,
              "BreathingEffect 在 period_ms=1 渲染首帧首键通道为预期主色 (255,0,0)");
        breath.Render(1, fb, km);

        // 验证 WaveEffect 在 period_ms=1 极小周期下遍历 68 键渲染正确性
        fb.Fill(0, 0, 0);
        aura::WaveEffect wave(1, "diag_dl");
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

    std::cout << "\n=========================================================\n";
    if (failures == 0) {
        std::cout << "  [SUCCESS] 所有 GSI 前台隔离、游戏事件与回归护栏测试全部 100% 通过！\n";
    } else {
        std::cout << "  [FAILED] 本次共有 " << failures << " 项显式检查未通过！\n";
    }
    std::cout << "=========================================================\n";
    return failures == 0 ? 0 : 1;
}
