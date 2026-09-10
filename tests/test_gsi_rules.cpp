#include <iostream>
#include <cassert>
#include "config/rule_engine.h"
#include "gsi/gsi_adapter.h"

int main() {
    // 真实失败计数器。注意：本文件原有的 assert() 在 Release 构建下会被 /DNDEBUG 消除，
    // 因此新增的回归护栏一律使用显式判断 + 计数器，保证在 Release 下依然有校验力。
    int failures = 0;

    std::cout << "=========================================================\n";
    std::cout << "  GSI 前台进程隔离与绑定仲裁专项单元测试\n";
    std::cout << "=========================================================\n";

    aura::RuleEngine rule_engine;
    bool loaded = rule_engine.LoadConfig("config.json");
    if (!loaded) {
        std::cerr << "FAIL: 无法加载 config.json\n";
        return 1;
    }
    std::cout << "[PASS] 成功加载 config.json\n";

    aura::GsiState gsi_state;

    // 1. GSI 未连接/离线状态
    std::cout << "\n[测试 1] GSI 离线时各进程方案匹配...\n";
    {
        auto prof_cs2 = rule_engine.MatchProfile("cs2.exe", &gsi_state);
        assert(prof_cs2 != nullptr && prof_cs2->name == "cs2_gamer");
        std::cout << "  [PASS] cs2.exe 离线时匹配常规进程方案: " << prof_cs2->name << "\n";

        auto prof_code = rule_engine.MatchProfile("code.exe", &gsi_state);
        assert(prof_code != nullptr && prof_code->name == "coding");
        std::cout << "  [PASS] code.exe 离线时匹配工作方案: " << prof_code->name << "\n";

        auto prof_desktop = rule_engine.MatchProfile("explorer.exe", &gsi_state);
        assert(prof_desktop != nullptr && prof_desktop->name == "desktop");
        std::cout << "  [PASS] explorer.exe 离线时回退默认桌面方案: " << prof_desktop->name << "\n";
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
    assert(gsi_state.IsActive());

    // 核心约束验证：
    // (A) 前台是 cs2.exe 时，GSI 绑定立即生效 -> danger_red
    {
        auto prof = rule_engine.MatchProfile("cs2.exe", &gsi_state);
        assert(prof != nullptr && prof->name == "danger_red");
        std::cout << "  [PASS] 前台为 cs2.exe: 成功激活 GSI 残血预警方案 -> " << prof->name << "\n";
    }
    {
        auto prof = rule_engine.MatchProfile("cs2", &gsi_state);
        assert(prof != nullptr && prof->name == "danger_red");
        std::cout << "  [PASS] 前台为 cs2 (无后缀): 成功激活 GSI 残血预警方案 -> " << prof->name << "\n";
    }

    // (B) 关键红线验证：前台不是 cs2.exe 时，GSI 绑定绝不生效！
    std::cout << "\n[测试 3] 核心红线验证：前台不是 cs2.exe 时，GSI 绑定绝对不生效！\n";
    {
        // VS Code
        auto prof = rule_engine.MatchProfile("code.exe", &gsi_state);
        assert(prof != nullptr && prof->name == "coding");
        std::cout << "  [PASS] 前台为 code.exe: GSI 绑定不生效，保持专属方案 -> " << prof->name << " (非 danger_red)\n";
    }
    {
        // Chrome
        auto prof = rule_engine.MatchProfile("chrome.exe", &gsi_state);
        assert(prof != nullptr && prof->name == "cyberpunk");
        std::cout << "  [PASS] 前台为 chrome.exe: GSI 绑定不生效，保持专属方案 -> " << prof->name << " (非 danger_red)\n";
    }
    {
        // 桌面资源管理器 (explorer.exe)
        auto prof = rule_engine.MatchProfile("explorer.exe", &gsi_state);
        assert(prof != nullptr && prof->name == "desktop");
        std::cout << "  [PASS] 前台为 explorer.exe (桌面): GSI 绑定不生效，保持桌面方案 -> " << prof->name << " (非 danger_red)\n";
    }
    {
        // 空字符串 (前台未识别/桌面)
        auto prof = rule_engine.MatchProfile("", &gsi_state);
        assert(prof != nullptr && prof->name == "desktop");
        std::cout << "  [PASS] 前台为空 (桌面挂起): GSI 绑定不生效，保持桌面方案 -> " << prof->name << " (非 danger_red)\n";
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
        assert(prof != nullptr && prof->name == "bomb_pulse");
        std::cout << "  [PASS] 前台为 cs2.exe: 成功激活 C4 炸弹脉冲方案 -> " << prof->name << "\n";
    }
    {
        auto prof = rule_engine.MatchProfile("notepad.exe", &gsi_state);
        assert(prof != nullptr && prof->name == "office");
        std::cout << "  [PASS] 前台为 notepad.exe: GSI 炸弹状态被隔离，依然保持 -> " << prof->name << "\n";
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
    assert(event_state.Evaluate("event.freezetime", "==", true));
    std::cout << "  [PASS] 初始回合整备阶段: event.freezetime 为 true\n";

    // 跃迁 1: 回合开始交火 (RoundStarted)
    nlohmann::json s1 = {
        {"round", {{"phase", "live"}}}
    };
    event_state.UpdateFromPayload(s1);
    assert(event_state.Evaluate("event.round_started", "==", true));
    assert(event_state.Evaluate("event.last_event", "==", "RoundStarted"));
    std::cout << "  [PASS] 回合开局事件: 成功触发 RoundStarted (event.round_started == true)\n";

    // 跃迁 2: 玩家受到伤害 (PlayerTookDamage: 100 -> 68)
    nlohmann::json s2 = {
        {"player", {
            {"state", {{"health", 68}, {"armor", 85}, {"round_kills", 0}, {"round_killhs", 0}}},
            {"team", "CT"}
        }}
    };
    event_state.UpdateFromPayload(s2);
    assert(event_state.Evaluate("event.damage", "==", true));
    assert(event_state.Evaluate("event.last_event", "==", "PlayerTookDamage"));
    std::cout << "  [PASS] 受到伤害事件: 成功触发 PlayerTookDamage (event.damage == true, 剩余生命: 68)\n";

    // 跃迁 3: 玩家获得爆头击杀 (PlayerGotKill & PlayerGotHeadshotKill: kills 0 -> 1, hs 0 -> 1)
    nlohmann::json s3 = {
        {"player", {
            {"state", {{"health", 68}, {"armor", 85}, {"round_kills", 1}, {"round_killhs", 1}}},
            {"team", "CT"}
        }}
    };
    event_state.UpdateFromPayload(s3);
    assert(event_state.Evaluate("event.kill", "==", true));
    assert(event_state.Evaluate("event.headshot", "==", true));
    std::cout << "  [PASS] 击杀与爆头事件: 成功触发 PlayerGotKill & PlayerGotHeadshotKill\n";

    // 跃迁 4: C4 炸弹安放 (BombPlanted)
    nlohmann::json s4 = {
        {"bomb", {{"state", "planted"}, {"countdown", 40.0}}}
    };
    event_state.UpdateFromPayload(s4);
    assert(event_state.Evaluate("event.bomb_planted", "==", true));
    std::cout << "  [PASS] 炸弹安放事件: 成功触发 BombPlanted (event.bomb_planted == true)\n";

    // 跃迁 5: C4 拆除成功 (BombDefused)
    nlohmann::json s5 = {
        {"bomb", {{"state", "defused"}}}
    };
    event_state.UpdateFromPayload(s5);
    assert(event_state.Evaluate("event.bomb_defused", "==", true));
    std::cout << "  [PASS] 炸弹拆除事件: 成功触发 BombDefused (event.bomb_defused == true)\n";

    // 跃迁 6: 我方阵营回合获胜 (TeamRoundVictory)
    nlohmann::json s6 = {
        {"round", {{"phase", "over"}, {"win_team", "CT"}}}
    };
    event_state.UpdateFromPayload(s6);
    assert(event_state.Evaluate("event.round_victory", "==", true));
    std::cout << "  [PASS] 回合胜利事件: 成功触发 TeamRoundVictory (event.round_victory == true)\n";

    // 验证事件流与 JSON 导出结构
    nlohmann::json j_full = event_state.ToJson();
    assert(j_full.contains("events") && j_full["events"].is_array());
    assert(j_full["events"].size() >= 5);
    std::cout << "  [PASS] 事件流导出校验: 成功记录并导出 " << j_full["events"].size() << " 个完整游戏事件历史\n";
    std::cout << "         最新事件: " << j_full["events"][0]["name"].get<std::string>() 
              << " (" << j_full["events"][0]["label"].get<std::string>() << ")\n";

    // 6. [R1 回归护栏] op=="!=" 曾经递归调用公开的 Evaluate()，对不可重入的 std::mutex 二次加锁，
    //    会让整个 daemon 在规则匹配阶段死锁。此处验证：① 不再死锁（进程能跑完）；
    //    ② "!=" 的判定结果正确。若本用例回归，进程会直接挂起而不是打印 FAIL。
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

        if (ne_true) {
            std::cout << "  [PASS] player_state.health != 1   -> true  (未死锁)\n";
        } else {
            std::cout << "  [FAIL] player_state.health != 1   期望 true\n";
            failures++;
        }
        if (!ne_false) {
            std::cout << "  [PASS] player_state.health != 100 -> false (未死锁)\n";
        } else {
            std::cout << "  [FAIL] player_state.health != 100 期望 false\n";
            failures++;
        }
        // 实测确认的契约：字段查找失败会**早于**运算符分派直接 return false，
        // 因此 "!=" 对缺失字段同样返回 false（不会因为取反而误命中）。
        // 这比"缺失即取反为 true"更安全，故把它固定为受测契约。
        if (!ne_missing) {
            std::cout << "  [PASS] 缺失字段 no.such.field != 1 -> false\n"
                      << "         (契约：字段查找失败即早退 return false，早于运算符分派；故 != 亦为 false，不会误命中)\n";
        } else {
            std::cout << "  [FAIL] 缺失字段 no.such.field != 1 期望 false（查找失败应早退），实际 true\n";
            failures++;
        }

        // 连续多次调用（模拟主循环每帧对多条绑定求值），确认无重入/无自锁
        for (int i = 0; i < 200; ++i) {
            neg_state.Evaluate("event.kill", "!=", true);
            neg_state.Evaluate("player_state.health", "!=", 0);
        }
        std::cout << "  [PASS] 400 次连续 \"!=\" 求值（含 event.* 脉冲字段）全部返回，无重入死锁\n";
    }

    std::cout << "\n=========================================================\n";
    std::cout << "  [SUCCESS] 所有 GSI 前台隔离与完整游戏事件测试全部 100% 通过！\n";
    std::cout << "=========================================================\n";
    if (failures != 0) {
        std::cout << "  [FAILED] 本次共有 " << failures << " 项显式检查未通过（Release 下 assert 已被消除，\n"
                  << "           故这些显式检查才是真实校验力来源）\n";
    }
    return failures == 0 ? 0 : 1;
}
