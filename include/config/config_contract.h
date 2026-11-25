#pragma once

#include "config/automation_contract.h"
#include "aura/aura_types.h"
#include "third_party/json.hpp"
#include <cstdint>
#include <cmath>
#include <stdexcept>
#include <string>

namespace aura {

struct HallLightingConfig {
    bool enabled{false};
    std::string mode{"glow"}; // "glow" | "gradient" | "halo"
    ColorRGB color{255, 255, 255};
    ColorRGB secondary_color{220, 40, 255};
    double intensity{0.85};
    double min_depth_mm{0.10};

    bool operator==(const HallLightingConfig& other) const {
        return enabled == other.enabled && mode == other.mode &&
               color.r == other.color.r && color.g == other.color.g && color.b == other.color.b &&
               secondary_color.r == other.secondary_color.r && secondary_color.g == other.secondary_color.g &&
               secondary_color.b == other.secondary_color.b &&
               intensity == other.intensity && min_depth_mm == other.min_depth_mm;
    }
};

inline HallLightingConfig ParseHallLightingConfig(const nlohmann::json& root) {
    HallLightingConfig cfg;
    if (!root.is_object() || !root.contains("hall_lighting") || !root.at("hall_lighting").is_object()) {
        return cfg;
    }
    const auto& hl = root.at("hall_lighting");
    if (hl.contains("enabled") && hl.at("enabled").is_boolean()) cfg.enabled = hl.at("enabled").get<bool>();
    if (hl.contains("mode") && hl.at("mode").is_string()) {
        const auto m = hl.at("mode").get<std::string>();
        if (m == "glow" || m == "gradient" || m == "halo") cfg.mode = m;
    }
    auto parse_rgb = [](const nlohmann::json& j, const char* key, ColorRGB& dst) {
        if (j.contains(key) && j.at(key).is_array() && j.at(key).size() >= 3) {
            dst = ColorRGB(
                static_cast<uint8_t>(std::clamp(j.at(key)[0].get<int>(), 0, 255)),
                static_cast<uint8_t>(std::clamp(j.at(key)[1].get<int>(), 0, 255)),
                static_cast<uint8_t>(std::clamp(j.at(key)[2].get<int>(), 0, 255)));
        }
    };
    parse_rgb(hl, "color", cfg.color);
    parse_rgb(hl, "secondary_color", cfg.secondary_color);
    if (hl.contains("intensity") && hl.at("intensity").is_number())
        cfg.intensity = std::clamp(hl.at("intensity").get<double>(), 0.0, 1.0);
    if (hl.contains("min_depth_mm") && hl.at("min_depth_mm").is_number())
        cfg.min_depth_mm = std::clamp(hl.at("min_depth_mm").get<double>(), 0.05, 2.0);
    return cfg;
}

inline nlohmann::json HallLightingToJson(const HallLightingConfig& cfg) {
    return {
        {"enabled", cfg.enabled},
        {"mode", cfg.mode},
        {"color", {cfg.color.r, cfg.color.g, cfg.color.b}},
        {"secondary_color", {cfg.secondary_color.r, cfg.secondary_color.g, cfg.secondary_color.b}},
        {"intensity", std::round(cfg.intensity * 100.0) / 100.0},
        {"min_depth_mm", std::round(cfg.min_depth_mm * 100.0) / 100.0}
    };
}

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
    if (root.contains("hall_lighting")) {
        const auto& hl = root.at("hall_lighting");
        if (!hl.is_object()) throw std::runtime_error("/hall_lighting must be an object");
        for (auto it = hl.begin(); it != hl.end(); ++it) {
            const auto& k = it.key();
            if (k == "enabled") {
                if (!it.value().is_boolean()) throw std::runtime_error("/hall_lighting/enabled must be boolean");
            } else if (k == "mode") {
                if (!it.value().is_string()) throw std::runtime_error("/hall_lighting/mode must be a string");
                const auto& m = it.value().get_ref<const std::string&>();
                if (m != "glow" && m != "gradient" && m != "halo")
                    throw std::runtime_error("/hall_lighting/mode must be glow, gradient, or halo");
            } else if (k == "color" || k == "secondary_color") {
                if (!it.value().is_array() || it.value().size() != 3)
                    throw std::runtime_error("/hall_lighting/" + k + " must be RGB");
                for (const auto& ch : it.value())
                    if (!ch.is_number_integer() || ch.get<int64_t>() < 0 || ch.get<int64_t>() > 255)
                        throw std::runtime_error("/hall_lighting/" + k + " must contain integers in [0,255]");
            } else if (k == "intensity") {
                if (!it.value().is_number() || !std::isfinite(it.value().get<double>()) ||
                    it.value().get<double>() < 0.0 || it.value().get<double>() > 1.0)
                    throw std::runtime_error("/hall_lighting/intensity must be in [0,1]");
            } else if (k == "min_depth_mm") {
                if (!it.value().is_number() || !std::isfinite(it.value().get<double>()) ||
                    it.value().get<double>() < 0.05 || it.value().get<double>() > 2.0)
                    throw std::runtime_error("/hall_lighting/min_depth_mm must be in [0.05,2.0]");
            } else {
                throw std::runtime_error("/hall_lighting has unexpected field '" + k + "'");
            }
        }
    }
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
