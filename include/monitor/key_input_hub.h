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
        events_.push_back({key_name, static_cast<uint64_t>(now)});
    }

    void DrainEvents(std::vector<KeyPressEvent>& out_events) {
        std::lock_guard<std::mutex> lock(mutex_);
        out_events = std::move(events_);
        events_.clear();
    }

private:
    KeyInputHub() = default;
    std::mutex mutex_;
    std::vector<KeyPressEvent> events_;
};

inline std::string VkToKeyName(DWORD vk) {
    if (vk >= 'A' && vk <= 'Z') return std::string(1, static_cast<char>(vk));
    if (vk >= '0' && vk <= '9') return std::string(1, static_cast<char>(vk));

    switch (vk) {
        case VK_ESCAPE: return "ESC";
        case VK_TAB: return "TAB";
        case VK_CAPITAL: return "CAPS";
        case VK_LSHIFT: case VK_SHIFT: return "L_SHIFT";
        case VK_RSHIFT: return "R_SHIFT";
        case VK_LCONTROL: case VK_CONTROL: return "L_CTRL";
        case VK_RCONTROL: return "R_CTRL";
        case VK_LWIN: case VK_RWIN: return "L_WIN";
        case VK_LMENU: case VK_MENU: return "L_ALT";
        case VK_RMENU: return "R_ALT";
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
