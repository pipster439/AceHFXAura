#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>
#include <unordered_map>
#include <memory>
#include <optional>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <vector>
#include "third_party/json.hpp"
#include "third_party/httplib.h"
#include "engine/plugin_interface.h"
#include "aura/runtime_status.h"
#include "gsi/automation_input.h"

namespace aura {

class LightingControlService;
class AutomationControlService;

// 通用 GSI 字段值容器
struct GsiValue {
    enum class Type { None, Number, String, Boolean };
    Type type{Type::None};
    double num_val{0.0};
    std::string str_val;
    bool bool_val{false};

    GsiValue() = default;
    explicit GsiValue(double n) : type(Type::Number), num_val(n) {}
    explicit GsiValue(const std::string& s) : type(Type::String), str_val(s) {}
    explicit GsiValue(bool b) : type(Type::Boolean), bool_val(b) {}
};

// 完整游戏事件记录 (派生自 CounterStrike2GSI 规范)
struct GameEventRecord {
    std::string name;           // 如 "PlayerGotKill", "BombPlanted", "PlayerTookDamage"
    std::string category;       // 如 "combat", "bomb", "round", "match", "weapon"
    std::string label;          // 如 "☠️ 击杀敌人", "💣 炸弹安放"
    std::string desc;           // 如 "玩家消灭了 1 名敌人 (当前第 2 杀)"
    uint64_t timestamp_ms{0};   // 毫秒时间戳
    std::string time_str;       // "HH:MM:SS"
    nlohmann::json details;     // 参数详情
};

// 线程安全的 GSI 扁平化状态存储、事件推导与条件判断引擎
// 核心纪律：该状态仅在内存中维护键值对，绝对不涉及任何 COM/HAL 或硬件调用
class GsiState : public IGsiReader {
public:
    GsiState() = default;
    ~GsiState() override = default;

    // IGsiReader 接口实现
    double GetNumber(const char* field, double def_val = 0.0) const override;
    bool GetBool(const char* field, bool def_val = false) const override;
    const char* GetString(const char* field, const char* def_val = "") const override;
    bool IsActive() const override { return IsActive(10000); }

    // 解析并扁平化来自 CS2 的 JSON Payload，同时执行状态跃迁比对，触发完整游戏事件
    void UpdateFromPayload(const nlohmann::json& payload);
    // Explicit monotonic time makes observation/freshness conformance deterministic.
    void UpdateFromPayloadAt(const nlohmann::json& payload, uint64_t received_at_ms);
    AutomationInputDrain DrainAutomationInputs();
    std::shared_ptr<const AutomationTelemetry> GetAutomationTelemetry() const;

    // 评估某条绑定条件是否满足 (支持常规字段与 event.* 动态脉冲事件)
    bool Evaluate(const std::string& field, const std::string& op, const nlohmann::json& target_val) const;

    // 导出当前所有扁平化字段为 JSON (含活跃事件及最近事件队列)
    nlohmann::json ToJson() const;

    // 检查 GSI 是否在指定时间内收到过有效心跳
    bool IsActive(uint64_t timeout_ms) const;

    // 获取最新更新时间戳 (毫秒)
    uint64_t GetLastUpdateMs() const;

    // 获取最近触发的游戏事件流
    std::vector<GameEventRecord> GetRecentEvents(size_t max_count = 30) const;

    // 检查某个事件是否在指定脉冲窗口期内活跃
    bool IsEventActive(const std::string& event_name, uint64_t pulse_window_ms = 1500) const;

    // 手动触发指定事件
    void TriggerEvent(const std::string& name, 
                      const std::string& category, 
                      const std::string& label, 
                      const std::string& desc, 
                      const nlohmann::json& details = {});

    // 重置/清除当前状态
    void Clear();

    // 更新与获取当前系统前台进程名
    void SetForegroundProcess(const std::string& proc);
    std::string GetForegroundProcess() const;

private:
    void UpdateFromPayloadImpl(const nlohmann::json& payload, std::optional<uint64_t> receipt_override);
    static void FlattenJsonRecursive(const std::string& prefix,
                                     const nlohmann::json& node, 
                                     std::unordered_map<std::string, GsiValue>& out_map);

    void DetectGameEvents(const nlohmann::json& payload, uint64_t now_ms);

    // 前置条件：调用方必须已持有 mutex_。
    // 公开入口 Evaluate() 负责加锁后转调此处，使 op=="!=" 可以安全地递归调用自身，
    // 而不会对【非重入】的 std::mutex 二次加锁（原实现正是因此会死锁）。
    bool EvaluateLocked(const std::string& field, 
                        const std::string& op, 
                        const nlohmann::json& target_val) const;

    // 已改为 const：内部只写入 mutable 的 flat_state_ / last_event_sync_ms_，
    // 因此 Evaluate() 与 ToJson() 不再需要 const_cast。
    void SyncEventFieldsToFlatState(uint64_t now_ms) const;

    mutable std::mutex mutex_;
    mutable std::unordered_map<std::string, GsiValue> flat_state_;
    // 事件脉冲刷新节流：同一毫秒内只重算一次（见 SyncEventFieldsToFlatState）
    mutable uint64_t last_event_sync_ms_{0};
    uint64_t last_update_ms_{0};
    uint64_t packet_count_{0};
    uint64_t automation_epoch_{1}, automation_sequence_{0}, automation_received_ms_{0};
    uint64_t automation_dropped_{0};
    bool automation_overflow_{false};
    std::shared_ptr<const AutomationTelemetry> automation_latest_;
    std::deque<std::shared_ptr<const AutomationObservation>> automation_batches_;
    std::vector<std::string> packet_occurrences_;
    bool collecting_occurrences_{false};
    std::string foreground_process_;

    // 完整游戏事件历史与脉冲时钟
    std::deque<GameEventRecord> recent_events_;
    std::unordered_map<std::string, uint64_t> event_timestamps_;
    std::unordered_map<std::string, uint64_t> event_sequences_;
    std::string last_event_name_;
    std::string last_event_label_;

    // 历史状态跃迁比较追踪器
    int prev_health_{-1};
    int prev_armor_{-1};
    int prev_round_kills_{-1};
    int prev_round_killhs_{-1};
    int prev_match_kills_{-1};
    int prev_flashed_{-1};
    int prev_burning_{-1};
    std::string prev_bomb_state_;
    std::string prev_round_phase_;
    std::string prev_win_team_;
    std::string prev_map_phase_;
    std::string prev_active_weapon_;
    int prev_ct_score_{-1};
    int prev_t_score_{-1};
};

// CS2 GSI HTTP 接收适配器
// 严格绑定 127.0.0.1:19897
// 核心纪律：HTTP 服务线程完全独立，仅负责接收网络请求并更新 GsiState 内存字典，
// 严禁在此线程中直接或间接调用任何 COM/HAL/Aura 硬件驱动接口！
class GsiAdapter {
public:
    GsiAdapter();
    ~GsiAdapter();

    // 启动 HTTP 接收服务 (非阻塞，后台拉起专属 I/O 线程)
    bool Start(int port = 19897);

    // 平滑停止服务并释放端口
    void Stop();

    // 获取只读 GSI 状态引用
    const GsiState& GetState() const { return state_; }
    GsiState& GetState() { return state_; }

    bool IsRunning() const { return is_running_.load(std::memory_order_acquire); }
    int GetPort() const { return port_; }

    void SetPluginReloadHandler(std::function<bool(const std::string&, bool)> handler) {
        on_reload_plugin_ = std::move(handler);
    }
    void SetPreviewHandler(std::function<bool(const std::string&)> handler) {
        on_preview_frame_ = std::move(handler);
    }
    void SetStatusStore(std::shared_ptr<const RuntimeStatusStore> store) {
        status_store_ = std::move(store);
    }
    void SetLightingService(std::shared_ptr<LightingControlService> service) {
        lighting_service_ = std::move(service);
    }
    void SetAutomationService(std::shared_ptr<AutomationControlService> service) {
        automation_service_ = std::move(service);
    }

private:
    void SetupRoutes();

    std::function<bool(const std::string&, bool)> on_reload_plugin_;
    std::function<bool(const std::string&)> on_preview_frame_;
    std::shared_ptr<const RuntimeStatusStore> status_store_;
    std::shared_ptr<LightingControlService> lighting_service_;
    std::shared_ptr<AutomationControlService> automation_service_;

    int port_{19897};
    std::unique_ptr<httplib::Server> svr_;
    std::thread worker_thread_;
    std::atomic<bool> is_running_{false};
    GsiState state_;
};

} // namespace aura
