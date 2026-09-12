#include "gsi/gsi_adapter.h"
#include "utils/logger.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>

namespace aura {

namespace {

std::string ToLowerStr(const std::string& s) {
    std::string res = s;
    std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return res;
}

uint64_t GetCurrentEpochMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string FormatEpochMs(uint64_t ms) {
    std::time_t sec = static_cast<std::time_t>(ms / 1000);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &sec);
#else
    localtime_r(&sec, &tm_buf);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
    return buf;
}

} // namespace

void GsiState::FlattenJsonRecursive(const std::string& prefix, 
                                    const nlohmann::json& node, 
                                    std::unordered_map<std::string, GsiValue>& out_map) {
    if (node.is_object()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            std::string key = prefix.empty() ? it.key() : (prefix + "." + it.key());
            FlattenJsonRecursive(key, it.value(), out_map);
        }
    } else if (node.is_array()) {
        for (size_t i = 0; i < node.size(); ++i) {
            std::string key = prefix + "[" + std::to_string(i) + "]";
            FlattenJsonRecursive(key, node[i], out_map);
        }
    } else if (node.is_number()) {
        out_map[prefix] = GsiValue(node.get<double>());
    } else if (node.is_string()) {
        out_map[prefix] = GsiValue(node.get<std::string>());
    } else if (node.is_boolean()) {
        out_map[prefix] = GsiValue(node.get<bool>());
    }
}

void GsiState::UpdateFromPayload(const nlohmann::json& payload) {
    if (!payload.is_object()) return;

    std::lock_guard<std::mutex> lock(mutex_);

    // 针对 payload 包含的顶层分类节点，先清除旧的对应字段，再写入新数据
    // 杜绝跨回合瞬态字段（如 round.bomb）残存导致的假阳性
    for (auto it = payload.begin(); it != payload.end(); ++it) {
        const std::string& top_key = it.key();
        if (top_key == "previously" || top_key == "added" || top_key == "auth") {
            continue; // 忽略差量跟踪与鉴权字段
        }

        std::string prefix = top_key + ".";
        for (auto s_it = flat_state_.begin(); s_it != flat_state_.end();) {
            if (s_it->first.rfind(prefix, 0) == 0) {
                s_it = flat_state_.erase(s_it);
            } else {
                ++s_it;
            }
        }

        // 清理对应的官方别名前缀 (例如 top_key == "player" -> 清理 player_state., player_weapons. 等)
        if (top_key == "player") {
            for (auto s_it = flat_state_.begin(); s_it != flat_state_.end();) {
                if (s_it->first.rfind("player_state.", 0) == 0 ||
                    s_it->first.rfind("player_weapons.", 0) == 0 ||
                    s_it->first.rfind("player_match_stats.", 0) == 0 ||
                    s_it->first.rfind("player_id.", 0) == 0) {
                    s_it = flat_state_.erase(s_it);
                } else {
                    ++s_it;
                }
            }
        } else if (top_key == "allplayers") {
            for (auto s_it = flat_state_.begin(); s_it != flat_state_.end();) {
                if (s_it->first.rfind("allplayers_state.", 0) == 0 ||
                    s_it->first.rfind("allplayers_weapons.", 0) == 0 ||
                    s_it->first.rfind("allplayers_match_stats.", 0) == 0) {
                    s_it = flat_state_.erase(s_it);
                } else {
                    ++s_it;
                }
            }
        }

        // 扁平化该子树
        std::unordered_map<std::string, GsiValue> subtree_map;
        FlattenJsonRecursive(top_key, it.value(), subtree_map);

        // 写入 flat_state_ 并自动建立官方规范别名
        for (const auto& [k, v] : subtree_map) {
            flat_state_[k] = v;

            // 1. player.state.* <-> player_state.*
            constexpr const char* P_STATE = "player.state.";
            if (k.rfind(P_STATE, 0) == 0) {
                std::string alias = "player_state." + k.substr(sizeof("player.state.") - 1);
                flat_state_[alias] = v;
            }
            // 2. player.weapons.* <-> player_weapons.*
            constexpr const char* P_WEAPONS = "player.weapons.";
            if (k.rfind(P_WEAPONS, 0) == 0) {
                std::string alias = "player_weapons." + k.substr(sizeof("player.weapons.") - 1);
                flat_state_[alias] = v;
            }
            // 3. player.match_stats.* <-> player_match_stats.*
            constexpr const char* P_STATS = "player.match_stats.";
            if (k.rfind(P_STATS, 0) == 0) {
                std::string alias = "player_match_stats." + k.substr(sizeof("player.match_stats.") - 1);
                flat_state_[alias] = v;
            }
            // 4. allplayers.<id>.state.* <-> allplayers_state.<id>.*
            constexpr const char* ALL_PLAYERS = "allplayers.";
            if (k.rfind(ALL_PLAYERS, 0) == 0) {
                size_t second_dot = k.find('.', sizeof("allplayers.") - 1);
                if (second_dot != std::string::npos) {
                    std::string id = k.substr(sizeof("allplayers.") - 1, second_dot - (sizeof("allplayers.") - 1));
                    std::string rest = k.substr(second_dot + 1);
                    if (rest.rfind("state.", 0) == 0) {
                        flat_state_["allplayers_state." + id + "." + rest.substr(6)] = v;
                    } else if (rest.rfind("weapons.", 0) == 0) {
                        flat_state_["allplayers_weapons." + id + "." + rest.substr(8)] = v;
                    } else if (rest.rfind("match_stats.", 0) == 0) {
                        flat_state_["allplayers_match_stats." + id + "." + rest.substr(12)] = v;
                    }
                }
            }
        }
    }

    uint64_t now_ms = GetCurrentEpochMs();
    last_update_ms_ = now_ms;
    packet_count_++;

    // 完整的游戏事件状态跃迁比对与触发
    DetectGameEvents(payload, now_ms);

    // 将动态事件脉冲字段同步写入 flat_state_
    SyncEventFieldsToFlatState(now_ms);
}

void GsiState::DetectGameEvents(const nlohmann::json& payload, uint64_t now_ms) {
    (void)payload;
    (void)now_ms;

    auto get_int = [&](const std::string& k1, const std::string& k2 = "") -> int {
        auto it = flat_state_.find(k1);
        if (it != flat_state_.end() && it->second.type == GsiValue::Type::Number) {
            return static_cast<int>(it->second.num_val);
        }
        if (!k2.empty()) {
            it = flat_state_.find(k2);
            if (it != flat_state_.end() && it->second.type == GsiValue::Type::Number) {
                return static_cast<int>(it->second.num_val);
            }
        }
        return -1;
    };

    auto get_str = [&](const std::string& k1, const std::string& k2 = "") -> std::string {
        auto it = flat_state_.find(k1);
        if (it != flat_state_.end() && it->second.type == GsiValue::Type::String) {
            return it->second.str_val;
        }
        if (!k2.empty()) {
            it = flat_state_.find(k2);
            if (it != flat_state_.end() && it->second.type == GsiValue::Type::String) {
                return it->second.str_val;
            }
        }
        return "";
    };

    int curr_health = get_int("player_state.health", "player.state.health");
    int curr_armor = get_int("player_state.armor", "player.state.armor");
    int curr_round_kills = get_int("player_state.round_kills", "player.state.round_kills");
    int curr_round_killhs = get_int("player_state.round_killhs", "player.state.round_killhs");
    int curr_match_kills = get_int("player_match_stats.kills", "player.match_stats.kills");
    int curr_flashed = get_int("player_state.flashed", "player.state.flashed");
    int curr_burning = get_int("player_state.burning", "player.state.burning");

    std::string curr_bomb_state = get_str("bomb.state", "round.bomb");
    std::string curr_round_phase = get_str("round.phase");
    std::string curr_win_team = get_str("round.win_team");
    std::string curr_map_phase = get_str("map.phase");
    std::string player_team = get_str("player.team");

    // 首次接收报文：仅记录初始状态，避免初始化时虚假触发伤害或胜负事件
    if (prev_health_ == -1 && curr_health >= 0) {
        prev_health_ = curr_health;
        prev_armor_ = curr_armor;
        prev_round_kills_ = curr_round_kills;
        prev_round_killhs_ = curr_round_killhs;
        prev_match_kills_ = curr_match_kills;
        prev_flashed_ = curr_flashed;
        prev_burning_ = curr_burning;
        prev_bomb_state_ = curr_bomb_state;
        prev_round_phase_ = curr_round_phase;
        prev_win_team_ = curr_win_team;
        prev_map_phase_ = curr_map_phase;
        return;
    }

    // 1. 击杀与暴头事件
    if (curr_round_kills > prev_round_kills_ && prev_round_kills_ >= 0) {
        int diff = curr_round_kills - prev_round_kills_;
        TriggerEvent("PlayerGotKill", "combat", "☠️ 击杀敌人", 
                     "消灭了敌人 (本回合第 " + std::to_string(curr_round_kills) + " 杀)", 
                     {{"round_kills", curr_round_kills}, {"diff", diff}});
    } else if (curr_match_kills > prev_match_kills_ && prev_match_kills_ >= 0) {
        TriggerEvent("PlayerGotKill", "combat", "☠️ 击杀敌人", 
                     "消灭了敌人 (累计击杀: " + std::to_string(curr_match_kills) + ")", 
                     {{"match_kills", curr_match_kills}});
    }

    if (curr_round_killhs > prev_round_killhs_ && prev_round_killhs_ >= 0) {
        TriggerEvent("PlayerGotHeadshotKill", "combat", "🎯 爆头击杀", 
                     "精准爆头消灭敌人 (本回合爆头 " + std::to_string(curr_round_killhs) + ")", 
                     {{"round_killhs", curr_round_killhs}});
    }

    if (curr_round_kills == 5 && prev_round_kills_ < 5 && prev_round_kills_ >= 0) {
        TriggerEvent("PlayerAce", "combat", "👑 五杀团灭 ACE", 
                     "完成单回合五杀团灭全场 ACE!", {{"round_kills", 5}});
    }

    // 2. 玩家伤害与阵亡/复活事件
    if (curr_health < prev_health_ && prev_health_ > 0 && curr_health > 0) {
        int dmg = prev_health_ - curr_health;
        TriggerEvent("PlayerTookDamage", "combat", "🩸 受到伤害", 
                     "生命值扣减 -" + std::to_string(dmg) + " (剩余生命: " + std::to_string(curr_health) + ")", 
                     {{"damage", dmg}, {"remaining_health", curr_health}});
    }
    if (curr_health == 0 && prev_health_ > 0) {
        TriggerEvent("PlayerDied", "combat", "💀 玩家阵亡", "玩家在交火中阵亡", {});
    }
    if (curr_health > 0 && prev_health_ == 0) {
        TriggerEvent("PlayerRespawned", "combat", "✨ 玩家复活", 
                     "玩家复活重新加入对局 (生命值: " + std::to_string(curr_health) + ")", 
                     {{"health", curr_health}});
    }

    // 3. 闪光致盲与火烧灼伤
    if (curr_flashed > 50 && (prev_flashed_ <= 50 || curr_flashed > prev_flashed_)) {
        TriggerEvent("PlayerFlashed", "combat", "⚡ 闪光致盲", 
                     "被闪光弹致盲 (致盲浓度: " + std::to_string(curr_flashed) + "/255)", 
                     {{"flashed", curr_flashed}});
    }
    if (curr_burning > 0 && prev_burning_ <= 0) {
        TriggerEvent("PlayerBurning", "combat", "🔥 燃烧弹灼烧", 
                     "受到燃烧弹火海伤害 (灼烧度: " + std::to_string(curr_burning) + ")", 
                     {{"burning", curr_burning}});
    }

    // 4. C4 炸弹事件
    if (curr_bomb_state == "planting" && prev_bomb_state_ != "planting") {
        TriggerEvent("BombPlanting", "bomb", "⏱️ 正在安放炸弹", "恐怖分子正在安装 C4 炸弹...", {});
    }
    if (curr_bomb_state == "planted" && prev_bomb_state_ != "planted") {
        TriggerEvent("BombPlanted", "bomb", "💣 炸弹安放成功", 
                     "The bomb has been planted! C4 进入引爆倒计时", {});
    }
    if (curr_bomb_state == "defusing" && prev_bomb_state_ != "defusing") {
        TriggerEvent("BombDefusing", "bomb", "🔧 正在拆除炸弹", "反恐精英正在拆除 C4 炸弹...", {});
    }
    if (curr_bomb_state == "defused" && prev_bomb_state_ != "defused") {
        TriggerEvent("BombDefused", "bomb", "✅ 炸弹成功拆除", 
                     "The bomb has been defused! 拆除成功，反恐精英获胜", {});
    }
    if (curr_bomb_state == "exploded" && prev_bomb_state_ != "exploded") {
        TriggerEvent("BombExploded", "bomb", "💥 炸弹爆炸", 
                     "Target destroyed! C4 炸弹引爆，恐怖分子获胜", {});
    }
    if (curr_bomb_state == "dropped" && prev_bomb_state_ != "dropped") {
        TriggerEvent("BombDropped", "bomb", "📦 炸弹掉落", "C4 炸弹遗落在地面", {});
    }
    if (curr_bomb_state == "carried" && prev_bomb_state_ == "dropped") {
        TriggerEvent("BombPickedup", "bomb", "🎒 拾起炸弹", "玩家捡起了遗落的 C4 炸弹", {});
    }

    // 5. 回合与胜负事件
    if (curr_round_phase == "freezetime" && prev_round_phase_ != "freezetime") {
        TriggerEvent("FreezetimeStarted", "round", "🛒 冻结购买时间", "回合整备阶段，打开商店购买装备", {});
    }
    if (curr_round_phase == "live" && prev_round_phase_ != "live") {
        TriggerEvent("RoundStarted", "round", "⚔️ 回合开始交火", "冻结解除，交火正式开始！", {});
    }
    if (curr_round_phase == "over" && prev_round_phase_ != "over") {
        TriggerEvent("RoundConcluded", "round", "🏁 回合交火结束", "本回合交火结束，进入结算", {});
    }
    if (!curr_win_team.empty() && curr_win_team != prev_win_team_) {
        if (!player_team.empty() && curr_win_team == player_team) {
            TriggerEvent("TeamRoundVictory", "round", "🏆 我方回合胜利", 
                         "我方阵营 (" + curr_win_team + ") 赢得本回合交火胜利！", {{"win_team", curr_win_team}});
        } else if (!player_team.empty()) {
            TriggerEvent("TeamRoundLoss", "round", "❌ 我方回合失败", 
                         "敌方阵营 (" + curr_win_team + ") 赢得本回合胜利", {{"win_team", curr_win_team}});
        }
    }

    // 6. 比赛全局事件
    if (curr_map_phase == "warmup" && prev_map_phase_ != "warmup") {
        TriggerEvent("WarmupStarted", "match", "🎮 热身赛阶段", "等待各方玩家就绪与热身练习", {});
    }
    if (curr_map_phase == "live" && prev_map_phase_ == "warmup") {
        TriggerEvent("MatchStarted", "match", "🔥 正式比赛开始", "全场正式比赛打响！", {});
    }
    if (curr_map_phase == "intermission" && prev_map_phase_ != "intermission") {
        TriggerEvent("IntermissionStarted", "match", "🔄 中场换边", "半场比赛结束，双方阵营攻守互换", {});
    }
    if (curr_map_phase == "gameover" && prev_map_phase_ != "gameover") {
        TriggerEvent("Gameover", "match", "🏆 比赛全场结束", "全场比赛结束，决出最终胜负！", {});
    }

    // 更新追踪值
    if (curr_health >= 0) prev_health_ = curr_health;
    if (curr_armor >= 0) prev_armor_ = curr_armor;
    if (curr_round_kills >= 0) prev_round_kills_ = curr_round_kills;
    if (curr_round_killhs >= 0) prev_round_killhs_ = curr_round_killhs;
    if (curr_match_kills >= 0) prev_match_kills_ = curr_match_kills;
    if (curr_flashed >= 0) prev_flashed_ = curr_flashed;
    if (curr_burning >= 0) prev_burning_ = curr_burning;
    if (!curr_bomb_state.empty()) prev_bomb_state_ = curr_bomb_state;
    if (!curr_round_phase.empty()) prev_round_phase_ = curr_round_phase;
    if (!curr_win_team.empty()) prev_win_team_ = curr_win_team;
    if (!curr_map_phase.empty()) prev_map_phase_ = curr_map_phase;
}

void GsiState::TriggerEvent(const std::string& name, 
                            const std::string& category, 
                            const std::string& label, 
                            const std::string& desc, 
                            const nlohmann::json& details) {
    uint64_t now_ms = GetCurrentEpochMs();
    event_timestamps_[name] = now_ms;
    last_event_name_ = name;
    last_event_label_ = label;
    last_event_sync_ms_ = 0;

    GameEventRecord rec;
    rec.name = name;
    rec.category = category;
    rec.label = label;
    rec.desc = desc;
    rec.timestamp_ms = now_ms;
    rec.time_str = FormatEpochMs(now_ms);
    rec.details = details;

    recent_events_.push_front(rec);
    while (recent_events_.size() > 50) {
        recent_events_.pop_back();
    }
}

void GsiState::SyncEventFieldsToFlatState(uint64_t now_ms) const {
    // 节流：event.* 脉冲字段只是 (event_timestamps_, flat_state_ 中少量源字段, now_ms) 的纯函数，
    // 同一毫秒内重复调用结果必然相同。主循环 25FPS × 多条绑定会对 flat_state_ 做大量重复全量写入，
    // 故同毫秒直接跳过。最坏情况：同一毫秒内源字段二次变化时，event.* 滞后 1ms（脉冲窗口 1000~4000ms，可忽略）。
    if (now_ms == last_event_sync_ms_) return;
    last_event_sync_ms_ = now_ms;

    auto is_active_pulse = [&](const std::string& ev, uint64_t duration_ms) -> bool {
        auto it = event_timestamps_.find(ev);
        if (it == event_timestamps_.end()) return false;
        return (now_ms >= it->second && (now_ms - it->second) <= duration_ms);
    };

    flat_state_["event.last_event"] = GsiValue(last_event_name_);
    flat_state_["event.last_label"] = GsiValue(last_event_label_);
    flat_state_["event.kill"] = GsiValue(is_active_pulse("PlayerGotKill", 1500));
    flat_state_["event.headshot"] = GsiValue(is_active_pulse("PlayerGotHeadshotKill", 1500));
    flat_state_["event.ace"] = GsiValue(is_active_pulse("PlayerAce", 3000));
    flat_state_["event.damage"] = GsiValue(is_active_pulse("PlayerTookDamage", 1000));

    // 阵亡状态：血量为 0 时持续有效
    int h = -1;
    auto it_h = flat_state_.find("player_state.health");
    if (it_h != flat_state_.end() && it_h->second.type == GsiValue::Type::Number) {
        h = static_cast<int>(it_h->second.num_val);
    }
    flat_state_["event.death"] = GsiValue(h == 0);

    flat_state_["event.respawn"] = GsiValue(is_active_pulse("PlayerRespawned", 1500));

    // 炸弹状态
    std::string b_state;
    auto it_b = flat_state_.find("bomb.state");
    if (it_b != flat_state_.end() && it_b->second.type == GsiValue::Type::String) {
        b_state = it_b->second.str_val;
    } else {
        auto it_rb = flat_state_.find("round.bomb");
        if (it_rb != flat_state_.end() && it_rb->second.type == GsiValue::Type::String) {
            b_state = it_rb->second.str_val;
        }
    }
    std::string r_phase;
    auto it_rp = flat_state_.find("round.phase");
    if (it_rp != flat_state_.end() && it_rp->second.type == GsiValue::Type::String) {
        r_phase = it_rp->second.str_val;
    }

    flat_state_["event.bomb_planting"] = GsiValue(b_state == "planting");
    flat_state_["event.bomb_planted"] = GsiValue(b_state == "planted" && r_phase != "over");
    flat_state_["event.bomb_defusing"] = GsiValue(b_state == "defusing");
    flat_state_["event.bomb_defused"] = GsiValue(is_active_pulse("BombDefused", 3000));
    flat_state_["event.bomb_exploded"] = GsiValue(is_active_pulse("BombExploded", 3000));

    flat_state_["event.round_started"] = GsiValue(is_active_pulse("RoundStarted", 2000));
    flat_state_["event.freezetime"] = GsiValue(r_phase == "freezetime");
    flat_state_["event.round_victory"] = GsiValue(is_active_pulse("TeamRoundVictory", 4000));
    flat_state_["event.round_loss"] = GsiValue(is_active_pulse("TeamRoundLoss", 4000));

    std::string m_phase;
    auto it_mp = flat_state_.find("map.phase");
    if (it_mp != flat_state_.end() && it_mp->second.type == GsiValue::Type::String) {
        m_phase = it_mp->second.str_val;
    }
    flat_state_["event.warmup"] = GsiValue(m_phase == "warmup");
    flat_state_["event.gameover"] = GsiValue(m_phase == "gameover");

    // 闪光与燃烧
    int fl = 0;
    auto it_fl = flat_state_.find("player_state.flashed");
    if (it_fl != flat_state_.end() && it_fl->second.type == GsiValue::Type::Number) {
        fl = static_cast<int>(it_fl->second.num_val);
    }
    flat_state_["event.flashed"] = GsiValue(fl > 50);

    int brn = 0;
    auto it_brn = flat_state_.find("player_state.burning");
    if (it_brn != flat_state_.end() && it_brn->second.type == GsiValue::Type::Number) {
        brn = static_cast<int>(it_brn->second.num_val);
    }
    flat_state_["event.burning"] = GsiValue(brn > 0);
}

std::vector<GameEventRecord> GsiState::GetRecentEvents(size_t max_count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<GameEventRecord> result;
    size_t count = std::min(max_count, recent_events_.size());
    for (size_t i = 0; i < count; ++i) {
        result.push_back(recent_events_[i]);
    }
    return result;
}

bool GsiState::IsEventActive(const std::string& event_name, uint64_t pulse_window_ms) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = event_timestamps_.find(event_name);
    if (it == event_timestamps_.end()) return false;
    uint64_t now_ms = GetCurrentEpochMs();
    return (now_ms >= it->second && (now_ms - it->second) <= pulse_window_ms);
}

bool GsiState::Evaluate(const std::string& field, const std::string& op, const nlohmann::json& target_val) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return EvaluateLocked(field, op, target_val);
}

// 本函数为 Evaluate 的锁内实现。前置条件：调用方已持有 mutex_。
// 拆分的唯一目的是让 op=="!=" 能安全递归（std::mutex 不可重入，直接递归 Evaluate 会死锁）。
bool GsiState::EvaluateLocked(const std::string& field, const std::string& op, const nlohmann::json& target_val) const {
    // 如果检查的是事件脉冲字段，即时刷新脉冲时效
    if (field.rfind("event.", 0) == 0) {
        SyncEventFieldsToFlatState(GetCurrentEpochMs());
    }

    auto it = flat_state_.find(field);
    if (it == flat_state_.end()) {
        // 尝试别名查找 (如果用户配的是 player.state.health 也能命中 player_state.health)
        if (field.rfind("player_state.", 0) == 0) {
            std::string alt = "player.state." + field.substr(13);
            it = flat_state_.find(alt);
        } else if (field.rfind("player.state.", 0) == 0) {
            std::string alt = "player_state." + field.substr(13);
            it = flat_state_.find(alt);
        }
    }

    if (it == flat_state_.end()) {
        return false;
    }

    const GsiValue& actual = it->second;

    // 解析目标比较值
    double target_num = 0.0;
    bool has_target_num = false;
    std::string target_str;
    bool has_target_str = false;
    bool target_bool = false;
    bool has_target_bool = false;

    if (target_val.is_number()) {
        target_num = target_val.get<double>();
        has_target_num = true;
    } else if (target_val.is_string()) {
        target_str = target_val.get<std::string>();
        has_target_str = true;
        // 尝试解析字符串中的数值
        try {
            size_t idx = 0;
            target_num = std::stod(target_str, &idx);
            if (idx == target_str.size()) has_target_num = true;
        } catch (...) {}
    } else if (target_val.is_boolean()) {
        target_bool = target_val.get<bool>();
        has_target_bool = true;
    }

    if (op == "<") {
        if (actual.type == GsiValue::Type::Number && has_target_num) {
            return actual.num_val < target_num;
        }
        return false;
    } else if (op == "<=") {
        if (actual.type == GsiValue::Type::Number && has_target_num) {
            return actual.num_val <= target_num;
        }
        return false;
    } else if (op == ">") {
        if (actual.type == GsiValue::Type::Number && has_target_num) {
            return actual.num_val > target_num;
        }
        return false;
    } else if (op == ">=") {
        if (actual.type == GsiValue::Type::Number && has_target_num) {
            return actual.num_val >= target_num;
        }
        return false;
    } else if (op == "==") {
        if (actual.type == GsiValue::Type::Number && has_target_num) {
            return std::abs(actual.num_val - target_num) < 1e-4;
        }
        if (actual.type == GsiValue::Type::Boolean && has_target_bool) {
            return actual.bool_val == target_bool;
        }
        if (actual.type == GsiValue::Type::String && has_target_str) {
            return ToLowerStr(actual.str_val) == ToLowerStr(target_str);
        }
        return false;
    } else if (op == "!=") {
        // 递归到锁内实现：不可调用公开的 Evaluate()，否则会二次锁定不可重入的 mutex_ → 死锁
        return !EvaluateLocked(field, "==", target_val);
    } else if (op == "contains") {
        if (actual.type == GsiValue::Type::String && has_target_str) {
            return ToLowerStr(actual.str_val).find(ToLowerStr(target_str)) != std::string::npos;
        }
        return false;
    }

    return false;
}

nlohmann::json GsiState::ToJson() const {
    std::lock_guard<std::mutex> lock(mutex_);

    uint64_t now_ms = GetCurrentEpochMs();
    // 导出前先刷新事件脉冲时效（SyncEventFieldsToFlatState 为 const，不再需要 const_cast）
    SyncEventFieldsToFlatState(now_ms);

    nlohmann::json j_data = nlohmann::json::object();
    for (const auto& [k, v] : flat_state_) {
        switch (v.type) {
            case GsiValue::Type::Number:
                // 如果是整数则输出整型，保持整洁
                if (std::floor(v.num_val) == v.num_val && !std::isinf(v.num_val) && !std::isnan(v.num_val)) {
                    j_data[k] = static_cast<int64_t>(v.num_val);
                } else {
                    j_data[k] = v.num_val;
                }
                break;
            case GsiValue::Type::String:
                j_data[k] = v.str_val;
                break;
            case GsiValue::Type::Boolean:
                j_data[k] = v.bool_val;
                break;
            default:
                break;
        }
    }

    nlohmann::json j_events = nlohmann::json::array();
    for (const auto& ev : recent_events_) {
        j_events.push_back({
            {"name", ev.name},
            {"category", ev.category},
            {"label", ev.label},
            {"desc", ev.desc},
            {"timestamp_ms", ev.timestamp_ms},
            {"time_str", ev.time_str},
            {"details", ev.details}
        });
    }

    const uint64_t diff_ms = (now_ms >= last_update_ms_) ? (now_ms - last_update_ms_) : (last_update_ms_ - now_ms);
    double last_updated_sec = (last_update_ms_ > 0) ? (static_cast<double>(diff_ms) / 1000.0) : -1.0;

    bool active = (last_update_ms_ > 0 && diff_ms < 10000);

    std::string lower_proc = ToLowerStr(foreground_process_);
    bool is_cs2 = (lower_proc == "cs2.exe" || lower_proc == "cs2" || lower_proc == "csgo.exe" || lower_proc == "csgo");

    return {
        {"connected", active},
        {"last_update_ms", last_update_ms_},
        {"last_updated_sec", last_updated_sec},
        {"packet_count", packet_count_},
        {"field_count", flat_state_.size()},
        {"foreground_process", foreground_process_},
        {"is_cs2_foreground", is_cs2},
        {"data", j_data},
        {"events", j_events}
    };
}

bool GsiState::IsActive(uint64_t timeout_ms) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (last_update_ms_ == 0) return false;
    uint64_t now_ms = GetCurrentEpochMs();
    uint64_t diff_ms = (now_ms >= last_update_ms_) ? (now_ms - last_update_ms_) : (last_update_ms_ - now_ms);
    return diff_ms <= timeout_ms;
}

uint64_t GsiState::GetLastUpdateMs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_update_ms_;
}

void GsiState::SetForegroundProcess(const std::string& proc) {
    std::lock_guard<std::mutex> lock(mutex_);
    foreground_process_ = proc;
}

std::string GsiState::GetForegroundProcess() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return foreground_process_;
}

void GsiState::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    flat_state_.clear();
    recent_events_.clear();
    event_timestamps_.clear();
    last_event_name_.clear();
    last_event_label_.clear();
    foreground_process_.clear();
    last_update_ms_ = 0;
    packet_count_ = 0;
    last_event_sync_ms_ = 0;   // 复位事件脉冲节流，避免 Clear 后同毫秒内跳过首次刷新

    prev_health_ = -1;
    prev_armor_ = -1;
    prev_round_kills_ = -1;
    prev_round_killhs_ = -1;
    prev_match_kills_ = -1;
    prev_flashed_ = -1;
    prev_burning_ = -1;
    prev_bomb_state_.clear();
    prev_round_phase_.clear();
    prev_win_team_.clear();
    prev_map_phase_.clear();
    prev_active_weapon_.clear();
    prev_ct_score_ = -1;
    prev_t_score_ = -1;
}

double GsiState::GetNumber(const char* field, double def_val) const {
    if (!field || !*field) return def_val;
    std::lock_guard<std::mutex> lock(mutex_);
    if (std::strncmp(field, "event.", 6) == 0) {
        SyncEventFieldsToFlatState(GetCurrentEpochMs());
    }
    auto it = flat_state_.find(field);
    if (it == flat_state_.end()) {
        std::string f(field);
        if (f.rfind("player.state.", 0) == 0) {
            it = flat_state_.find("player_state." + f.substr(13));
        } else if (f.rfind("player_state.", 0) == 0) {
            it = flat_state_.find("player.state." + f.substr(13));
        }
    }
    if (it != flat_state_.end()) {
        if (it->second.type == GsiValue::Type::Number) return it->second.num_val;
        if (it->second.type == GsiValue::Type::Boolean) return it->second.bool_val ? 1.0 : 0.0;
        if (it->second.type == GsiValue::Type::String) {
            try { return std::stod(it->second.str_val); } catch (...) { return def_val; }
        }
    }
    return def_val;
}

bool GsiState::GetBool(const char* field, bool def_val) const {
    if (!field || !*field) return def_val;
    std::lock_guard<std::mutex> lock(mutex_);
    if (std::strncmp(field, "event.", 6) == 0) {
        SyncEventFieldsToFlatState(GetCurrentEpochMs());
    }
    auto it = flat_state_.find(field);
    if (it == flat_state_.end()) {
        std::string f(field);
        if (f.rfind("player.state.", 0) == 0) {
            it = flat_state_.find("player_state." + f.substr(13));
        } else if (f.rfind("player_state.", 0) == 0) {
            it = flat_state_.find("player.state." + f.substr(13));
        }
    }
    if (it != flat_state_.end()) {
        if (it->second.type == GsiValue::Type::Boolean) return it->second.bool_val;
        if (it->second.type == GsiValue::Type::Number) return it->second.num_val != 0.0;
        if (it->second.type == GsiValue::Type::String) {
            return !it->second.str_val.empty() && it->second.str_val != "false" && it->second.str_val != "0";
        }
    }
    return def_val;
}

const char* GsiState::GetString(const char* field, const char* def_val) const {
    if (!field || !*field) return def_val;
    thread_local static std::string tl_buf;
    std::lock_guard<std::mutex> lock(mutex_);
    if (std::strncmp(field, "event.", 6) == 0) {
        SyncEventFieldsToFlatState(GetCurrentEpochMs());
    }
    auto it = flat_state_.find(field);
    if (it == flat_state_.end()) {
        std::string f(field);
        if (f.rfind("player.state.", 0) == 0) {
            it = flat_state_.find("player_state." + f.substr(13));
        } else if (f.rfind("player_state.", 0) == 0) {
            it = flat_state_.find("player.state." + f.substr(13));
        }
    }
    if (it != flat_state_.end()) {
        if (it->second.type == GsiValue::Type::String) {
            tl_buf = it->second.str_val;
            return tl_buf.c_str();
        }
        if (it->second.type == GsiValue::Type::Number) {
            tl_buf = std::to_string(it->second.num_val);
            return tl_buf.c_str();
        }
        if (it->second.type == GsiValue::Type::Boolean) {
            return it->second.bool_val ? "true" : "false";
        }
    }
    return def_val;
}

// ========================================================
// GsiAdapter 实现
// ========================================================
GsiAdapter::GsiAdapter() = default;

GsiAdapter::~GsiAdapter() {
    Stop();
}

void GsiAdapter::SetupRoutes() {
    if (!svr_) return;
    // 限制请求体上限为 256KB，防止超大 payload 引发内存拒绝服务 (DoS)
    svr_->set_payload_max_length(256 * 1024);
    svr_->set_error_handler([](const httplib::Request& /*req*/, httplib::Response& res) {
        if (res.status == 413) {
            res.set_content(R"json({"status":"error","error":"Payload Too Large","message":"请求体超过 256KB 上限"})json", "application/json; charset=utf-8");
        }
    });

    // 处理来自 CS2 的 GSI POST Payload
    auto gsi_post_handler = [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = nlohmann::json::parse(req.body);
            // 严格纪律：HTTP 线程仅更新内存状态，绝对不触碰任何 COM/HAL 或驱动
            state_.UpdateFromPayload(j);
            res.status = 200;
            res.set_content("{\"status\":\"ok\"}", "application/json; charset=utf-8");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string("{\"status\":\"error\",\"message\":") + nlohmann::json(e.what()).dump() + "}", "application/json; charset=utf-8");
        }
    };

    svr_->Post("/", gsi_post_handler);
    svr_->Post("/gsi", gsi_post_handler);

    // 查询当前扁平化状态
    auto gsi_get_handler = [this](const httplib::Request&, httplib::Response& res) {
        std::string json_str = state_.ToJson().dump();
        res.set_content(json_str, "application/json; charset=utf-8");
    };

    svr_->Get("/", gsi_get_handler);
    svr_->Get("/api/gsi/current", gsi_get_handler);

    // 守护进程插件热重载 IPC 入口
    svr_->Post("/api/plugin/reload", [this](const httplib::Request& req, httplib::Response& res) {
        std::string name;
        try {
            auto j = nlohmann::json::parse(req.body);
            name = j.value("name", j.value("plugin_name", ""));
        } catch (...) {}
        bool ok = true;
        if (on_reload_plugin_) {
            ok = on_reload_plugin_(name);
        }
        res.status = ok ? 200 : 400;
        res.set_content(ok ? "{\"status\":\"ok\",\"message\":\"Plugin reloaded\"}" : "{\"status\":\"error\",\"message\":\"Plugin reload failed\"}", "application/json; charset=utf-8");
    });

    // 守护进程编辑态硬件推流预览 IPC 入口
    svr_->Post("/api/preview", [this](const httplib::Request& req, httplib::Response& res) {
        bool ok = true;
        if (on_preview_frame_) {
            ok = on_preview_frame_(req.body);
        }
        res.status = ok ? 200 : 400;
        res.set_content(ok ? "{\"status\":\"ok\"}" : "{\"status\":\"error\"}", "application/json; charset=utf-8");
    });
}

bool GsiAdapter::Start(int port) {
    if (is_running_.load(std::memory_order_acquire)) {
        return true;
    }

    port_ = port;
    svr_ = std::make_unique<httplib::Server>();
    SetupRoutes();

    // 严格设置独占地址绑定（Windows 下杜绝 SO_REUSEADDR 端口劫持与伪成功），避免多实例冲突
    svr_->set_socket_options([](socket_t sock) {
#ifdef _WIN32
        int opt = 1;
        ::setsockopt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
        int opt = 1;
        ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const void*>(&opt), sizeof(opt));
#endif
    });
    svr_->set_keep_alive_max_count(100);

    // 显式同步绑定端口，以同步检测端口是否可用/被占用，杜绝假阳性成功
    if (!svr_->bind_to_port("127.0.0.1", port_)) {
        LOG_ERROR("[GSI] 无法绑定并监听 127.0.0.1:" + std::to_string(port_) + " (端口可能已被占用)");
        svr_.reset();
        return false;
    }

    is_running_.store(true, std::memory_order_release);

    worker_thread_ = std::thread([this]() {
        LOG_INFO("[GSI] 接收服务专属 I/O 线程启动，正在监听 127.0.0.1:" + std::to_string(port_));
        bool ok = svr_->listen_after_bind();
        if (!ok) {
            LOG_ERROR("[GSI] listen_after_bind 异常退出: 127.0.0.1:" + std::to_string(port_));
        }
        is_running_.store(false, std::memory_order_release);
        LOG_INFO("[GSI] 接收服务专属 I/O 线程已安全退出");
    });

    svr_->wait_until_ready();
    return true;
}

void GsiAdapter::Stop() {
    if (is_running_.exchange(false, std::memory_order_acq_rel)) {
        LOG_INFO("[GSI] 正在请求平滑停止 GSI HTTP 接收服务...");
        if (svr_) {
            svr_->wait_until_ready();
            svr_->stop();
        }
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        svr_.reset();
        LOG_INFO("[+] GSI HTTP 接收服务已完全释放");
    }
}

} // namespace aura
