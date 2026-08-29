#include "aura/keymap.h"
#include "third_party/json.hpp"
#include "utils/logger.h"
#include <fstream>
#include <algorithm>
#include <cctype>

namespace aura {

std::string Keymap::ToUpper(const std::string& str) {
    std::string res = str;
    std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return res;
}

bool Keymap::LoadFromJson(const std::string& json_path) {
    std::ifstream file(json_path);
    if (!file.is_open()) {
        LOG_ERROR("无法打开键位映射文件: " << json_path);
        return false;
    }

    try {
        nlohmann::json j;
        file >> j;

        if (!j.contains("keys") || !j["keys"].is_object()) {
            LOG_ERROR("键位映射文件缺少有效的 'keys' 节点: " << json_path);
            return false;
        }

        keys_.clear();
        name_to_id_.clear();

        for (auto& [k, v] : j["keys"].items()) {
            KeyInfo info;
            info.key_name = k;
            if (v.contains("display_label")) info.display_label = v["display_label"].get<std::string>();
            if (v.contains("led_id")) info.led_id = v["led_id"].get<int>();
            if (v.contains("physical_row")) info.physical_row = v["physical_row"].get<int>();
            if (v.contains("physical_col")) info.physical_col = v["physical_col"].get<int>();
            if (v.contains("category")) info.category = v["category"].get<std::string>();

            std::string upper_k = ToUpper(k);
            keys_[upper_k] = info;
            name_to_id_[upper_k] = info.led_id;
        }

        SetupAliases();

        LOG_INFO("成功加载权威键位映射表: " << json_path << " (共 " << keys_.size() << " 个键位)");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("解析键位映射文件异常: " << e.what());
        return false;
    }
}

void Keymap::SetupAliases() {
    // 建立标准别名匹配
    auto add_alias = [this](const std::string& alias, const std::string& standard) {
        std::string std_upper = ToUpper(standard);
        auto it = name_to_id_.find(std_upper);
        if (it != name_to_id_.end()) {
            name_to_id_[ToUpper(alias)] = it->second;
        }
    };

    add_alias("ESCAPE", "ESC");
    add_alias("SPACEBAR", "SPACE");
    add_alias("RETURN", "ENTER");
    add_alias("BS", "BACKSPACE");
    add_alias("CAPSLOCK", "CAPS");
    
    add_alias("SHIFT", "L_SHIFT");
    add_alias("LSHIFT", "L_SHIFT");
    add_alias("RSHIFT", "R_SHIFT");
    
    add_alias("CTRL", "L_CTRL");
    add_alias("LCTRL", "L_CTRL");
    add_alias("RCTRL", "R_CTRL");

    add_alias("WIN", "L_WIN");
    add_alias("LWIN", "L_WIN");
    add_alias("WINDOWS", "L_WIN");

    add_alias("ALT", "L_ALT");
    add_alias("LALT", "L_ALT");
    add_alias("RALT", "R_ALT");

    add_alias("INSERT", "INS");
    add_alias("DELETE", "DEL");
    add_alias("PAGEUP", "PGUP");
    add_alias("PAGEDOWN", "PGDN");

    add_alias("ARROWUP", "UP");
    add_alias("ARROWDOWN", "DOWN");
    add_alias("ARROWLEFT", "LEFT");
    add_alias("ARROWRIGHT", "RIGHT");
}

bool Keymap::FindLedId(const std::string& key_name, int& out_led_id) const {
    std::string upper = ToUpper(key_name);
    auto it = name_to_id_.find(upper);
    if (it != name_to_id_.end()) {
        out_led_id = it->second;
        return true;
    }

    // 检查是否直接传入的是数字字符串 (0~127)
    try {
        size_t idx = 0;
        int num = std::stoi(upper, &idx);
        if (idx == upper.size() && num >= 0 && num < 128) {
            out_led_id = num;
            return true;
        }
    } catch (...) {}

    return false;
}

bool Keymap::ResolveKeys(const std::string& key_spec, std::vector<int>& out_led_ids) const {
    std::string upper = ToUpper(key_spec);
    out_led_ids.clear();

    if (upper == "WASD") {
        std::vector<std::string> wasd = {"W", "A", "S", "D"};
        for (const auto& k : wasd) {
            int id = -1;
            if (FindLedId(k, id)) out_led_ids.push_back(id);
        }
        return !out_led_ids.empty();
    }

    if (upper == "ARROWS") {
        std::vector<std::string> arr = {"UP", "DOWN", "LEFT", "RIGHT"};
        for (const auto& k : arr) {
            int id = -1;
            if (FindLedId(k, id)) out_led_ids.push_back(id);
        }
        return !out_led_ids.empty();
    }

    // 单个键名直接查找
    int single_id = -1;
    if (FindLedId(upper, single_id)) {
        out_led_ids.push_back(single_id);
        return true;
    }

    // 连写多字符拆解 (如 "QWER", "ZXCV", "TYGF", "1234")
    if (upper.size() > 1) {
        bool all_matched = true;
        std::vector<int> temp_ids;
        for (char c : upper) {
            std::string s(1, c);
            int id = -1;
            if (FindLedId(s, id)) {
                temp_ids.push_back(id);
            } else {
                all_matched = false;
                break;
            }
        }
        if (all_matched) {
            out_led_ids = std::move(temp_ids);
            return true;
        }
    }

    return false;
}

} // namespace aura
