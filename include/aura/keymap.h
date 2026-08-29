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

private:
    void SetupAliases();
    static std::string ToUpper(const std::string& str);

    std::unordered_map<std::string, KeyInfo> keys_;
    std::unordered_map<std::string, int> name_to_id_;
};

} // namespace aura
