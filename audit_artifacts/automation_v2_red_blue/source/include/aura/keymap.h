#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace aura {

struct KeyInfo {
    std::string key_name;
    std::string display_label;
    int led_id = -1;
    int physical_row = 0;
    int physical_col = 0;
    double physical_x = 0.0;
    double physical_y = 0.0;
    std::string category;
};

class Keymap {
public:
    Keymap() = default;

    bool LoadFromJson(const std::string& json_path);

    bool FindLedId(const std::string& key_name, int& out_led_id) const;

    // Resolves key or compound/group string (e.g. "WASD", "ARROWS", "QWER", "1", "ESC")
    bool ResolveKeys(const std::string& key_spec, std::vector<int>& out_led_ids) const;

    const std::unordered_map<std::string, KeyInfo>& GetAllKeys() const {
        return keys_;
    }

    size_t KeyCount() const {
        return keys_.size();
    }

    // 68 键 ANSI 65% 标准物理配列几何顺序列表（唯一权威顺序，用于 Web/推流与硬件矩阵映射）
    static const std::vector<std::string>& GetStandardLayoutKeys();

private:
    void SetupAliases();
    static std::string ToUpper(const std::string& str);

    std::unordered_map<std::string, KeyInfo> keys_;
    std::unordered_map<std::string, int> name_to_id_;
};

} // namespace aura
