#pragma once

#include "config/automation_contract.h"
#include "aura/aura_types.h"
#include "third_party/json.hpp"
#include <cstdint>
#include <cmath>
#include <stdexcept>
#include <string>

namespace aura {

inline constexpr int kDefaultConfigFps = 25;
inline constexpr int kMinConfigFps = 10;
inline constexpr int kMaxConfigFps = 100;

inline bool IsValidConfigFps(const nlohmann::json& value) {
    if (!value.is_number_integer()) return false;
    if (value.is_number_unsigned()) {
        const auto fps = value.get<uint64_t>();
        return fps >= kMinConfigFps && fps <= kMaxConfigFps;
    }
    const auto fps = value.get<int64_t>();
    return fps >= kMinConfigFps && fps <= kMaxConfigFps;
}

// Validate the persisted document before an official writer replaces it. Unknown
// fields remain opaque so narrow edits preserve authoring and future client data.
inline void ValidateConfigDocument(const nlohmann::json& root) {
    ValidateAutomationContainers(root);
    if (root.contains("fps") && !IsValidConfigFps(root.at("fps")))
        throw std::runtime_error("/fps must be an integer in [10,100]");
    if (root.contains("blockly_effects") && !root.at("blockly_effects").is_object())
        throw std::runtime_error("/blockly_effects must be an object");
    if (root.contains("hardware_backend")) {
        if (!root.at("hardware_backend").is_string())
            throw std::runtime_error("/hardware_backend must be a string");
        HardwareBackend parsed = HardwareBackend::Auto;
        if (!TryParseHardwareBackend(root.at("hardware_backend").get<std::string>(), parsed))
            throw std::runtime_error("/hardware_backend has an unsupported value");
    }
    const auto name = root.value("default_profile", std::string("desktop"));
    if (name.empty() || !root.contains("profiles") || !root.at("profiles").is_object() ||
        root.at("profiles").empty() || !root.at("profiles").contains(name))
        throw std::runtime_error("/default_profile must reference a profile in /profiles");
    if (root.contains("orchestration")) {
        const auto& orchestration = root.at("orchestration");
        if (orchestration.contains("fallback_profile")) {
            if (!orchestration.at("fallback_profile").is_string() ||
                (!orchestration.at("fallback_profile").get<std::string>().empty() &&
                 !root.at("profiles").contains(orchestration.at("fallback_profile").get<std::string>())))
                throw std::runtime_error("/orchestration/fallback_profile must reference a profile");
        }
        if (orchestration.contains("automation_freshness_ms") &&
            (!orchestration.at("automation_freshness_ms").is_number_integer() ||
             orchestration.at("automation_freshness_ms").get<int64_t>() <= 0))
            throw std::runtime_error("/orchestration/automation_freshness_ms must be a positive integer");
    }
    for (const auto& [profile_name, profile] : root.at("profiles").items()) {
        const auto path = "/profiles/" + profile_name;
        if (!profile.is_object()) throw std::runtime_error(path + " must be an object");
        const auto type = profile.value("type", std::string("static"));
        if (type != "static" && type != "breathing" && type != "color_cycle" &&
            type != "wave" && type != "custom_keymap" && type != "reactive" &&
            type != "ripple" && type != "starry_night" && type != "quicksand" &&
            type != "current" && type != "raindrop" && type != "plugin")
            throw std::runtime_error(path + "/type is unsupported");
        if (profile.contains("fps") && !IsValidConfigFps(profile.at("fps")))
            throw std::runtime_error(path + "/fps must be an integer in [10,100]");
        if (profile.contains("period_ms") &&
            (!profile.at("period_ms").is_number_unsigned() || profile.at("period_ms").get<uint64_t>() < 33))
            throw std::runtime_error(path + "/period_ms must be an integer >= 33");
        if (profile.contains("brightness")) {
            const auto& value = profile.at("brightness");
            if (!value.is_number() || !std::isfinite(value.get<double>()) ||
                value.get<double>() < 0 || value.get<double>() > 255)
                throw std::runtime_error(path + "/brightness must be in [0,255]");
        }
        for (const auto* key : {"analog", "random_colors"})
            if (profile.contains(key) && !profile.at(key).is_boolean())
                throw std::runtime_error(path + "/" + key + " must be boolean");
        if (profile.contains("thickness")) {
            const auto& value = profile.at("thickness");
            if (!value.is_number() || !std::isfinite(value.get<double>()) ||
                value.get<double>() < 0.1 || value.get<double>() > 5.0)
                throw std::runtime_error(path + "/thickness must be in [0.1,5]");
        }
        if (profile.contains("direction")) {
            if (!profile.at("direction").is_string())
                throw std::runtime_error(path + "/direction must be a string");
            const auto direction = profile.at("direction").get<std::string>();
            if (direction != "diag_dl" && direction != "diag_dr" && direction != "diag_ul" &&
                direction != "diag_ur" && direction != "left" && direction != "right" &&
                direction != "up" && direction != "down" && direction != "spread")
                throw std::runtime_error(path + "/direction is unsupported");
        }
        for (const auto* key : {"color", "color1", "color2", "bg"}) {
            if (!profile.contains(key)) continue;
            const auto& color = profile.at(key);
            if (!color.is_array() || color.size() != 3)
                throw std::runtime_error(path + "/" + key + " must be RGB");
            for (const auto& channel : color)
                if (!channel.is_number_integer() || channel.get<int64_t>() < 0 || channel.get<int64_t>() > 255)
                    throw std::runtime_error(path + "/" + key + " must contain integers in [0,255]");
        }
        if (profile.contains("keys")) {
            if (!profile.at("keys").is_object()) throw std::runtime_error(path + "/keys must be an object");
            for (const auto& [key_name, color] : profile.at("keys").items()) {
                if (!color.is_array() || color.size() != 3)
                    throw std::runtime_error(path + "/keys/" + key_name + " must be RGB");
                for (const auto& channel : color)
                    if (!channel.is_number_integer() || channel.get<int64_t>() < 0 || channel.get<int64_t>() > 255)
                        throw std::runtime_error(path + "/keys/" + key_name + " must contain integers in [0,255]");
            }
        }
        if (type == "plugin") {
            bool found = false;
            for (const auto* key : {"plugin_name", "plugin", "effect", "effect_name", "plugin_path"}) {
                if (!profile.contains(key)) continue;
                if (!profile.at(key).is_string()) throw std::runtime_error(path + "/" + key + " must be a string");
                found = found || !profile.at(key).get<std::string>().empty();
            }
            if (!found) throw std::runtime_error(path + " requires plugin_name");
        }
    }
}

} // namespace aura
