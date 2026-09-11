#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <windows.h>

namespace aura {

struct KeyPressEvent {
    std::string key_name;
    uint64_t timestamp_ms{0};
};

class KeyInputHub {
public:
    static KeyInputHub& Instance() {
        static KeyInputHub instance;
        return instance;
    }

    void RecordKeyPress(const std::string& key_name) {
        if (key_name.empty()) return;
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        std::lock_guard<std::mutex> lock(mutex_);
        if (key_name == "COPILOT") {
            // Windows 11 Copilot 键硬件发送合成宏: Win + Shift + F23
            // 若 50ms 内刚压入了由该宏产生的合成 L_WIN 或 L_SHIFT，则消除前置假事件，避免左下角误闪
            while (!events_.empty() && 
                   (events_.back().key_name == "L_SHIFT" || events_.back().key_name == "L_WIN") &&
                   (now - events_.back().timestamp_ms < 50)) {
                events_.pop_back();
            }
        }
        events_.push_back({key_name, static_cast<uint64_t>(now)});
        // 环形上限保护：当活跃灯效不消费按键事件时 (常亮/呼吸/波浪等)，
        // 防止长时间运行下事件队列无界增长；仅保留最近 MAX_PENDING_EVENTS 条
        if (events_.size() > MAX_PENDING_EVENTS) {
            events_.erase(events_.begin(), events_.begin() + (events_.size() - MAX_PENDING_EVENTS));
        }
    }

    void DrainEvents(std::vector<KeyPressEvent>& out_events) {
        std::lock_guard<std::mutex> lock(mutex_);
        out_events = std::move(events_);
        events_.clear();
    }

private:
    static constexpr size_t MAX_PENDING_EVENTS = 64;
    KeyInputHub() = default;
    std::mutex mutex_;
    std::vector<KeyPressEvent> events_;
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
