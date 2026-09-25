#include "config/magnetic_host_profile.h"
#include "aura/hardware/m605_key_mapping.h"
#include "third_party/json.hpp"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>
#include <windows.h>

namespace aura {
namespace {
using Json = nlohmann::json;

std::optional<std::string> ReadBounded(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > 4 * 1024 * 1024) return std::nullopt;
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(input), {});
}

std::optional<std::string> DecodeFileData(const std::string& xml,
                                          const std::string& expected_name) {
    const auto name_tag = "<file_name>" + expected_name + "</file_name>";
    const auto name = xml.find(name_tag);
    if (name == std::string::npos || xml.find(name_tag, name + name_tag.size()) != std::string::npos)
        return std::nullopt;
    const auto begin = xml.find("<file_data>");
    if (begin == std::string::npos || name > begin ||
        xml.find("<file_data>", begin + 1) != std::string::npos) return std::nullopt;
    const auto start = begin + std::string_view("<file_data>").size();
    const auto end = xml.find("</file_data>", start);
    if (end == std::string::npos || end - start > 4 * 1024 * 1024) return std::nullopt;
    const auto encoded = std::string_view(xml).substr(start, end - start);
    std::string escaped;
    int bits = 0;
    unsigned value = 0;
    for (const unsigned char c : encoded) {
        if (c == '=') break;
        int digit = -1;
        if (c >= 'A' && c <= 'Z') digit = c - 'A';
        else if (c >= 'a' && c <= 'z') digit = c - 'a' + 26;
        else if (c >= '0' && c <= '9') digit = c - '0' + 52;
        else if (c == '+') digit = 62;
        else if (c == '/') digit = 63;
        if (digit < 0) return std::nullopt;
        value = ((value << 6) | static_cast<unsigned>(digit)) & 0xffffu;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            escaped.push_back(static_cast<char>((value >> bits) & 0xff));
        }
    }
    std::string decoded;
    decoded.reserve(escaped.size());
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < escaped.size(); ++i) {
        if (escaped[i] == '%') {
            if (i + 2 >= escaped.size()) return std::nullopt;
            const int hi = hex(escaped[i + 1]), lo = hex(escaped[i + 2]);
            if (hi < 0 || lo < 0) return std::nullopt;
            decoded.push_back(static_cast<char>((hi << 4) | lo));
            i += 2;
        } else {
            decoded.push_back(escaped[i]);
        }
    }
    return decoded;
}

std::optional<int> Integer(const Json& object, const char* field, int minimum, int maximum) {
    if (!object.is_object() || !object.contains(field)) return std::nullopt;
    const auto& value = object.at(field);
    int number = -1;
    if (value.is_number_integer()) {
        const auto wide = value.get<int64_t>();
        if (wide < minimum || wide > maximum) return std::nullopt;
        number = static_cast<int>(wide);
    } else if (value.is_string()) {
        const auto& text = value.get_ref<const std::string&>();
        if (text.empty() || text.size() > 4 ||
            !std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
            return std::nullopt;
        try { number = std::stoi(text); } catch (...) { return std::nullopt; }
    }
    return number >= minimum && number <= maximum ? std::optional<int>(number) : std::nullopt;
}

std::optional<uint8_t> Raw(const Json& object, const char* field, int minimum, int maximum) {
    const auto value = Integer(object, field, minimum, maximum);
    if (!value) return std::nullopt;
    return static_cast<uint8_t>(*value);
}

std::optional<int> SelectedProfileId(const Json& config) {
    if (!config.is_object() || !config.contains("currProfile") ||
        !config.contains("profileList") || !config.at("profileList").is_array()) return std::nullopt;
    const auto selected = Integer(config.at("currProfile"), "id", 1, 99);
    if (!selected) return std::nullopt;
    int matches = 0;
    for (const auto& profile : config.at("profileList"))
        if (Integer(profile, "id", 1, 99) == selected) ++matches;
    return matches == 1 ? selected : std::nullopt;
}
} // namespace

MagneticHostProfile MagneticHostProfileProvider::Parse(
    const std::string& config_xml, const std::string& profile_xml, const std::string& serial) {
    MagneticHostProfile result;
    if (serial.empty() || !std::all_of(serial.begin(), serial.end(),
        [](unsigned char c) { return c >= '0' && c <= '9'; })) return result;
    const auto config_data = DecodeFileData(config_xml, "config_" + serial + ".xml");
    if (!config_data) return result;
    const auto config = Json::parse(*config_data, nullptr, false);
    if (config.is_discarded()) return result;
    const auto profile_id = SelectedProfileId(config);
    if (!profile_id) return result;
    const auto profile_data = DecodeFileData(profile_xml,
        "fp_" + std::to_string(*profile_id) + "_config_" + serial + ".xml");
    if (!profile_data) return result;
    const auto profile = Json::parse(*profile_data, nullptr, false);
    if (profile.is_discarded() || !profile.is_object() || !profile.contains("button")) return result;
    result.active_profile_id = *profile_id;
    const auto& button = profile.at("button");
    if (button.contains("analogTrigger")) {
        const auto& trigger = button.at("analogTrigger");
        result.global_actuation_raw = Raw(trigger, "actuation", 1, 40);
        result.global_rt_press_raw = Raw(trigger, "rapidTriggerPress", 1, 25);
        result.global_rt_release_raw = Raw(trigger, "rapidTriggerRelease", 1, 25);
        if (trigger.is_object() && trigger.contains("preKeyRapidTriggerList") &&
            trigger.at("preKeyRapidTriggerList").is_array()) {
            std::map<uint16_t, bool> enabled;
            bool valid_list = true;
            for (const auto& key : trigger.at("preKeyRapidTriggerList")) {
                Json wrapped = {{"id", key}};
                const auto id = Integer(wrapped, "id", 0, 65535);
                if (!id || !m605::WireIdForLogicalKey(static_cast<uint16_t>(*id)) ||
                    !enabled.emplace(static_cast<uint16_t>(*id), true).second) {
                    valid_list = false;
                    break;
                }
            }
            result.per_key_rt_list_known = valid_list;
            if (valid_list) result.per_key_rt_enabled = std::move(enabled);
        }
    }
    if (button.contains("deadZone")) {
        const auto& deadzone = button.at("deadZone");
        result.global_deadzone_top_raw = Raw(deadzone, "deadZoneTop", 0, 5);
        result.global_deadzone_bottom_raw = Raw(deadzone, "deadZoneBottom", 0, 5);
    }
    if (button.contains("speedTap") && button.at("speedTap").is_array()) {
        bool valid_list = true;
        for (const auto& pair : button.at("speedTap")) {
            if (!pair.is_object() || !pair.contains("keys") || !pair.at("keys").is_array()) {
                valid_list = false; break;
            }
            if (pair.at("keys").empty()) continue;
            if (pair.at("keys").size() != 2) { valid_list = false; break; }
            const auto& keys = pair.at("keys");
            Json first = {{"id", keys[0]}}, second = {{"id", keys[1]}};
            const auto a = Integer(first, "id", 0, 65535);
            const auto b = Integer(second, "id", 0, 65535);
            if (!a || !b || a == b || !m605::WireIdForLogicalKey(static_cast<uint16_t>(*a)) ||
                !m605::WireIdForLogicalKey(static_cast<uint16_t>(*b)) ||
                !result.saved_speedtap_pairs.emplace(
                    std::make_pair(static_cast<uint16_t>(*a), static_cast<uint16_t>(*b)), true).second) {
                valid_list = false;
                break;
            }
        }
        result.saved_speedtap_pairs_known = valid_list;
        if (!valid_list) result.saved_speedtap_pairs.clear();
    }
    if (profile.contains("lighting") && profile.at("lighting").is_object() &&
        profile.at("lighting").contains("keyboard")) {
        const auto& lighting = profile.at("lighting").at("keyboard");
        if (Integer(lighting, "effectID", 0, 0) &&
            Integer(lighting, "analogEffect", 0, 1))
            result.static_analog_effect = *Integer(lighting, "analogEffect", 0, 1) == 1;
    }
    return result;
}

MagneticHostProfile MagneticHostProfileProvider::LoadFromDirectory(
    const std::filesystem::path& directory) {
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) return {};
    std::vector<std::filesystem::path> configs;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        const auto name = it->path().filename().string();
        if (name.compare(0, 7, "config_") == 0 && name.size() > 11 &&
            name.compare(name.size() - 4, 4, ".xml") == 0)
            configs.push_back(it->path());
    }
    if (error || configs.size() != 1) return {};
    const auto filename = configs[0].filename().string();
    const auto serial = filename.substr(7, filename.size() - 11);
    const auto config_xml = ReadBounded(configs[0]);
    if (!config_xml) return {};
    const auto data = DecodeFileData(*config_xml, filename);
    if (!data) return {};
    const auto config = Json::parse(*data, nullptr, false);
    if (config.is_discarded()) return {};
    const auto profile_id = SelectedProfileId(config);
    if (!profile_id) return {};
    const auto profile_file = directory /
        ("fp_" + std::to_string(*profile_id) + "_config_" + serial + ".xml");
    const auto profile_xml = ReadBounded(profile_file);
    return profile_xml ? Parse(*config_xml, *profile_xml, serial) : MagneticHostProfile{};
}

MagneticHostProfile MagneticHostProfileProvider::LoadProduction() {
    wchar_t path[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"ProgramData", path, 32768);
    if (!length || length >= 32768) return {};
    return LoadFromDirectory(std::filesystem::path(std::wstring(path, length)) /
        L"ASUS\\Framework\\keyboard\\ROG FALCHION ACE HFX");
}
} // namespace aura
