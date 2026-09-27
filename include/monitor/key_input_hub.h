#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <unordered_set>
#include <windows.h>

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
        static KeyInputHub instance;
        return instance;
    }

    void RecordKeyEvent(const std::string& key_name, KeyEventType type) {
        RecordKeyEventAt(key_name, type, MonotonicMs());
    }

    void ReadSince(uint64_t& cursor, std::vector<KeyInputEvent>& out_events,
                   std::unordered_set<std::string>& held, uint64_t* observed_at_ms = nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        out_events.clear();
        for (const auto& event : events_) {
            if (event.sequence > cursor) out_events.push_back(event);
        }
        cursor = sequence_;
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
            // Windows 11 Copilot 键硬件发送合成宏: Win + Shift + F23
            // 若 50ms 内刚压入了由该宏产生的合成 L_WIN 或 L_SHIFT，则消除前置假事件，避免左下角误闪
            while (!events_.empty() && 
                   (events_.back().key_name == "L_SHIFT" || events_.back().key_name == "L_WIN") &&
                   (now - events_.back().timestamp_ms < 50) &&
                   events_.back().type == KeyEventType::Down) {
                held_.erase(events_.back().key_name);
                events_.pop_back();
            }
        }
        events_.push_back({key_name, type, ++sequence_, now});
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
