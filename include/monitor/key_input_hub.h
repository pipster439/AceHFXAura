#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <unordered_set>
#include <windows.h>
#include "engine/plugin_interface.h"

namespace aura {

enum class KeyEventType { Down, Up };

struct KeyInputEvent {
    std::string key_name;
    KeyEventType type;
    uint64_t sequence{0};
    uint64_t timestamp_ms{0};
};

class KeyInputHub {
public:
    static KeyInputHub& Instance() {
        // Native plugin DestroyEffect/DllMain may run during process static
        // teardown. Keep this bounded host callback context valid until exit,
        // independently of singleton construction/destruction order.
        static KeyInputHub* instance = new KeyInputHub;
        return *instance;
    }

    void RecordKeyEvent(const std::string& key_name, KeyEventType type) {
        RecordKeyEventAt(key_name, type, MonotonicMs());
    }

    // Called by the existing logical keyboard observer on hook start/stop. A
    // registered source is distinct from a previously observed key press.
    void SetSourceAvailable(bool available) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (source_available_ == available) return;
        const uint64_t now = MonotonicMs();
        for (const auto& key : held_) events_.push_back({key, KeyEventType::Up, ++sequence_, now});
        held_.clear();
        TrimEvents();
        source_available_ = available;
        ++source_epoch_;
        logical_snapshot_ = {};
        snapshot_overflow_ = false;
    }

    void ReadLogicalSnapshot(AuraLogicalKeyboardSnapshotV1& output) {
        std::lock_guard<std::mutex> lock(mutex_);
        output = logical_snapshot_;
        output.struct_size = sizeof(output);
        output.version = AURA_LOGICAL_KEYBOARD_VERSION;
        output.source_available = source_available_ && !snapshot_overflow_ ? 1u : 0u;
        output.observed_at_ms = MonotonicMs();
        output.sequence = sequence_;
        output.source_epoch = source_epoch_;
        if (!output.source_available) output.count = 0;
        // Snapshot consumers have observed these Down states too: preserve the
        // existing Copilot rule that an observed Down must retain its paired Up.
        if (output.source_available) observed_sequence_ = sequence_;
    }

    void ReadSince(uint64_t& cursor, std::vector<KeyInputEvent>& out_events,
                   std::unordered_set<std::string>& held, uint64_t* observed_at_ms = nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        out_events.clear();
        for (const auto& event : events_) {
            if (event.sequence > cursor) out_events.push_back(event);
        }
        cursor = sequence_;
        observed_sequence_ = sequence_;
        held = held_;
        if (observed_at_ms) *observed_at_ms = MonotonicMs();
    }

    uint64_t CurrentSequence() {
        std::lock_guard<std::mutex> lock(mutex_);
        return sequence_;
    }

    void Reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        const uint64_t now = MonotonicMs();
        for (const auto& key : held_) {
            events_.push_back({key, KeyEventType::Up, ++sequence_, now});
        }
        held_.clear();
        logical_snapshot_ = {};
        snapshot_overflow_ = false;
        ++source_epoch_;
        TrimEvents();
    }

private:
    friend class KeyInputHubTestPeer;
    static uint64_t MonotonicMs() {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    void RecordKeyEventAt(const std::string& key_name, KeyEventType type, uint64_t now) {
        if (key_name.empty()) return;

        std::lock_guard<std::mutex> lock(mutex_);
        if (type == KeyEventType::Down && !held_.insert(key_name).second) return;
        if (type == KeyEventType::Up && held_.erase(key_name) == 0) return;
        if (type == KeyEventType::Down && key_name == "COPILOT") {
            // Logical Win/Shift/F23 may be a macro or a real chord; this
            // heuristic cannot establish physical origin. Already observed
            // events must retain held state until Up for paired release/fade.
            while (!events_.empty() && 
                   (events_.back().key_name == "L_SHIFT" || events_.back().key_name == "L_WIN") &&
                   (now - events_.back().timestamp_ms < 50) &&
                   events_.back().type == KeyEventType::Down) {
                if (events_.back().sequence <= observed_sequence_) break;
                if (auto* state = LogicalKey(events_.back().key_name)) {
                    state->held = 0; state->has_release = 0;
                    state->last_down_ms = state->last_up_ms = 0;
                    state->down_sequence = state->up_sequence = 0;
                }
                held_.erase(events_.back().key_name);
                events_.pop_back();
            }
        }
        events_.push_back({key_name, type, ++sequence_, now});
        if (auto* state = LogicalKey(key_name)) {
            state->held = type == KeyEventType::Down ? 1u : 0u;
            if (type == KeyEventType::Down) {
                state->last_down_ms = now; state->down_sequence = sequence_; state->has_release = 0;
            } else {
                state->last_up_ms = now; state->up_sequence = sequence_; state->has_release = 1;
            }
        }
        // 环形上限保护：当活跃灯效不消费按键事件时 (常亮/呼吸/波浪等)，
        // 防止长时间运行下事件队列无界增长；仅保留最近 MAX_PENDING_EVENTS 条
        TrimEvents();
    }

    static constexpr size_t MAX_PENDING_EVENTS = 1024;
    KeyInputHub() = default;
    std::mutex mutex_;
    std::vector<KeyInputEvent> events_;
    std::unordered_set<std::string> held_;
    uint64_t sequence_{0};
    uint64_t observed_sequence_{0};
    bool source_available_{false};
    bool snapshot_overflow_{false};
    uint64_t source_epoch_{1};
    AuraLogicalKeyboardSnapshotV1 logical_snapshot_{};
    AuraLogicalKeyStateV1* LogicalKey(const std::string& name) {
        if (name.empty() || name.size() >= AURA_LOGICAL_KEYBOARD_NAME_BYTES || name.find('\0') != std::string::npos)
            return nullptr;
        for (uint32_t i = 0; i < logical_snapshot_.count; ++i)
            if (name == logical_snapshot_.keys[i].name) return &logical_snapshot_.keys[i];
        if (logical_snapshot_.count == AURA_LOGICAL_KEYBOARD_MAX_KEYS) { snapshot_overflow_ = true; return nullptr; }
        auto& state = logical_snapshot_.keys[logical_snapshot_.count++];
        std::memcpy(state.name, name.c_str(), name.size() + 1);
        return &state;
    }
    void TrimEvents() {
        if (events_.size() > MAX_PENDING_EVENTS)
            events_.erase(events_.begin(), events_.begin() + (events_.size() - MAX_PENDING_EVENTS));
    }
};

inline std::string VkToKeyName(DWORD vk, DWORD flags = 0) {
    bool is_extended = (flags & 0x01) != 0; // LLKHF_EXTENDED

    if (vk >= 'A' && vk <= 'Z') return std::string(1, static_cast<char>(vk));
    if (vk >= '0' && vk <= '9') return std::string(1, static_cast<char>(vk));

    switch (vk) {
        case VK_ESCAPE: return "ESC";
        case VK_TAB: return "TAB";
        case VK_CAPITAL: return "CAPS";
        case VK_LSHIFT: return "L_SHIFT";
        case VK_RSHIFT: return "R_SHIFT";
        case VK_SHIFT: return is_extended ? "R_SHIFT" : "L_SHIFT";
        case VK_LCONTROL: return "L_CTRL";
        case VK_RCONTROL: return "R_CTRL";
        case VK_CONTROL: return is_extended ? "R_CTRL" : "L_CTRL";
        case VK_LWIN: return "L_WIN";
        case VK_RWIN: return "L_WIN"; // 键盘物理配列为单 Win 键 (L_WIN)
        case VK_LMENU: return "L_ALT";
        case VK_RMENU: return "R_ALT";
        case VK_MENU: return is_extended ? "R_ALT" : "L_ALT";
        case VK_SPACE: return "SPACE";
        case VK_BACK: return "BACKSPACE";
        case VK_RETURN: return "ENTER";
        case VK_INSERT: return "INS";
        case VK_DELETE: return "DEL";
        case VK_PRIOR: return "PGUP";
        case VK_NEXT: return "PGDN";
        case VK_UP: return "UP";
        case VK_DOWN: return "DOWN";
        case VK_LEFT: return "LEFT";
        case VK_RIGHT: return "RIGHT";
        case VK_APPS: return "COPILOT"; // 0x5D: Context Menu / App 键 (被 Copilot 键复用)
        case 0x86: /* VK_F23 */ return "COPILOT"; // Windows 11 Copilot 物理键专属键码
        case VK_OEM_MINUS: return "-";
        case VK_OEM_PLUS: return "=";
        case VK_OEM_4: return "[";
        case VK_OEM_6: return "]";
        case VK_OEM_5: return "\\";
        case VK_OEM_1: return ";";
        case VK_OEM_7: return "'";
        case VK_OEM_COMMA: return ",";
        case VK_OEM_PERIOD: return ".";
        case VK_OEM_2: return "/";
        default: return "";
    }
}

} // namespace aura
