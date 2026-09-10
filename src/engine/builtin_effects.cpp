#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "engine/builtin_effects.h"
#include "monitor/key_input_hub.h"
#include <cmath>
#include <algorithm>

namespace aura {

static constexpr double PI = 3.14159265358979323846;

static ColorRGB HsvToRgb(double h, double s, double v) {
    while (h < 0.0) h += 360.0;
    while (h >= 360.0) h -= 360.0;

    double c = v * s;
    double x = c * (1.0 - std::abs(std::fmod(h / 60.0, 2.0) - 1.0));
    double m = v - c;

    double r_prime = 0, g_prime = 0, b_prime = 0;
    if (h < 60.0) {
        r_prime = c; g_prime = x; b_prime = 0;
    } else if (h < 120.0) {
        r_prime = x; g_prime = c; b_prime = 0;
    } else if (h < 180.0) {
        r_prime = 0; g_prime = c; b_prime = x;
    } else if (h < 240.0) {
        r_prime = 0; g_prime = x; b_prime = c;
    } else if (h < 300.0) {
        r_prime = x; g_prime = 0; b_prime = c;
    } else {
        r_prime = c; g_prime = 0; b_prime = x;
    }

    uint8_t r = static_cast<uint8_t>(std::clamp((r_prime + m) * 255.0, 0.0, 255.0));
    uint8_t g = static_cast<uint8_t>(std::clamp((g_prime + m) * 255.0, 0.0, 255.0));
    uint8_t b = static_cast<uint8_t>(std::clamp((b_prime + m) * 255.0, 0.0, 255.0));
    return ColorRGB(r, g, b);
}

static void GetDirVector(const std::string& dir, double& out_x, double& out_y) {
    if (dir == "up")          { out_x =  0.0; out_y = -1.0; }
    else if (dir == "down")   { out_x =  0.0; out_y =  1.0; }
    else if (dir == "left")   { out_x = -1.0; out_y =  0.0; }
    else if (dir == "right")  { out_x =  1.0; out_y =  0.0; }
    else if (dir == "diag_ur"){ out_x =  0.707; out_y = -0.707; }
    else if (dir == "diag_dl"){ out_x = -0.707; out_y =  0.707; }
    else if (dir == "diag_ul"){ out_x = -0.707; out_y = -0.707; }
    else if (dir == "diag_dr"){ out_x =  0.707; out_y =  0.707; }
    else                      { out_x = -0.707; out_y =  0.707; } // default diag_dl
}

// 1. 纯静态单色 (支持模拟触发压感增亮)
void StaticEffect::Render(uint64_t /*elapsed_ms*/, FrameBuffer& out_frame, const Keymap& keymap) {
    if (!analog_) {
        out_frame.Fill(color_.r, color_.g, color_.b);
        return;
    }

    // 模拟灯效：读取真实按键，按下的键及周围爆发额外白炽光晕
    std::vector<KeyPressEvent> events;
    KeyInputHub::Instance().DrainEvents(events);
    for (const auto& ev : events) {
        analog_decays_[ev.key_name] = 1.0;
    }

    for (const auto& [name, info] : keymap.GetAllKeys()) {
        if (info.led_id < 0 || info.led_id >= static_cast<int>(TOTAL_LEDS)) continue;

        double extra = 0.0;
        auto it = analog_decays_.find(name);
        if (it != analog_decays_.end()) {
            extra = it->second;
            it->second = std::max(0.0, it->second - 0.035);
        }

        uint8_t r = static_cast<uint8_t>(std::min(255.0, color_.r + extra * (255.0 - color_.r)));
        uint8_t g = static_cast<uint8_t>(std::min(255.0, color_.g + extra * (255.0 - color_.g)));
        uint8_t b = static_cast<uint8_t>(std::min(255.0, color_.b + extra * (255.0 - color_.b)));
        out_frame.SetKey(info.led_id, ColorRGB(r, g, b));
    }
}

// 2. 双色呼吸渐变
void BreathingEffect::Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& /*keymap*/) {
    double phase = static_cast<double>(elapsed_ms % period_ms_) / static_cast<double>(period_ms_);
    double factor = 0.5 - 0.5 * std::cos(2.0 * PI * phase);

    uint8_t r = static_cast<uint8_t>(c1_.r * (1.0 - factor) + c2_.r * factor);
    uint8_t g = static_cast<uint8_t>(c1_.g * (1.0 - factor) + c2_.g * factor);
    uint8_t b = static_cast<uint8_t>(c1_.b * (1.0 - factor) + c2_.b * factor);

    out_frame.Fill(r, g, b);
}

// 3. 彩色循环
void ColorCycleEffect::Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& /*keymap*/) {
    double phase = static_cast<double>(elapsed_ms % period_ms_) / static_cast<double>(period_ms_);
    ColorRGB rgb = HsvToRgb(phase * 360.0, 1.0, 1.0);
    out_frame.Fill(rgb.r, rgb.g, rgb.b);
}

// 4. 彩虹光谱波浪 (支持 8 方向向量)
void WaveEffect::Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) {
    double time_phase = static_cast<double>(elapsed_ms % period_ms_) / static_cast<double>(period_ms_);
    double dir_x, dir_y;
    GetDirVector(direction_, dir_x, dir_y);

    for (const auto& [name, info] : keymap.GetAllKeys()) {
        if (info.led_id >= 0 && info.led_id < static_cast<int>(TOTAL_LEDS)) {
            double norm_x = static_cast<double>(info.physical_col - 1) / 15.0;
            double norm_y = static_cast<double>(info.physical_row - 1) / 4.0;
            double proj = norm_x * dir_x + norm_y * dir_y;
            double hue = std::fmod((proj - time_phase + 100.0) * 360.0, 360.0);
            ColorRGB rgb = HsvToRgb(hue, 1.0, 1.0);
            out_frame.SetKey(info.led_id, rgb);
        }
    }
}

// 5. 按键触发响应 (仅被敲击键点亮并自然衰减，无涟漪扩散，无自动化假演示)
void ReactiveEffect::Render(uint64_t /*elapsed_ms*/, FrameBuffer& out_frame, const Keymap& keymap) {
    // 1. 摄取真实物理敲击
    std::vector<KeyPressEvent> events;
    KeyInputHub::Instance().DrainEvents(events);
    for (const auto& ev : events) {
        key_decays_[ev.key_name] = 1.0;
    }

    // 2. 逐键渲染
    for (const auto& [name, info] : keymap.GetAllKeys()) {
        if (info.led_id < 0 || info.led_id >= static_cast<int>(TOTAL_LEDS)) continue;

        double decay = 0.0;
        auto it = key_decays_.find(name);
        if (it != key_decays_.end()) {
            decay = it->second;
            it->second = std::max(0.0, it->second - 0.035);
        }

        if (decay > 0.01) {
            uint8_t r = static_cast<uint8_t>(base_color_.r + decay * (trigger_color_.r - base_color_.r));
            uint8_t g = static_cast<uint8_t>(base_color_.g + decay * (trigger_color_.g - base_color_.g));
            uint8_t b = static_cast<uint8_t>(base_color_.b + decay * (trigger_color_.b - base_color_.b));
            out_frame.SetKey(info.led_id, ColorRGB(r, g, b));
        } else {
            out_frame.SetKey(info.led_id, base_color_);
        }
    }
}

// 6. 涟漪扩散光效 (敲击按键激荡同心水波向四周扩散，无自动化假演示)
void RippleEffect::Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) {
    // 1. 摄取真实物理按键
    std::vector<KeyPressEvent> events;
    KeyInputHub::Instance().DrainEvents(events);
    const auto& all_keys = keymap.GetAllKeys();

    for (const auto& ev : events) {
        auto it = all_keys.find(ev.key_name);
        if (it != all_keys.end()) {
            ripples_.push_back({static_cast<double>(it->second.physical_col), static_cast<double>(it->second.physical_row), elapsed_ms});
        }
    }

    if (ripples_.size() > 8) {
        ripples_.erase(ripples_.begin(), ripples_.begin() + (ripples_.size() - 8));
    }

    // 2. 背景底色
    out_frame.Fill(base_color_.r, base_color_.g, base_color_.b);

    const double speed = 0.014; // physical columns per ms

    for (const auto& [name, info] : all_keys) {
        if (info.led_id < 0 || info.led_id >= static_cast<int>(TOTAL_LEDS)) continue;

        double total_glow = 0.0;
        double k_col = static_cast<double>(info.physical_col);
        double k_row = static_cast<double>(info.physical_row);

        for (const auto& r : ripples_) {
            double current_r = static_cast<double>(elapsed_ms - r.start_ms) * speed;
            if (current_r > 18.0) continue;

            double dx = k_col - r.col;
            double dy = (k_row - r.row) * 2.2;
            double dist = std::sqrt(dx * dx + dy * dy);

            double diff = std::abs(dist - current_r);
            if (diff < 1.8) {
                double wave_factor = (1.0 - diff / 1.8) * (1.0 - current_r / 18.0);
                total_glow += wave_factor;
            }
        }

        if (total_glow > 0.01) {
            double factor = std::min(1.0, total_glow);
            uint8_t r = static_cast<uint8_t>(base_color_.r + factor * (trigger_color_.r - base_color_.r));
            uint8_t g = static_cast<uint8_t>(base_color_.g + factor * (trigger_color_.g - base_color_.g));
            uint8_t b = static_cast<uint8_t>(base_color_.b + factor * (trigger_color_.b - base_color_.b));
            out_frame.SetKey(info.led_id, ColorRGB(r, g, b));
        }
    }
}

// 7. 星空 (繁星微光随机闪烁)
void StarryNightEffect::Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) {
    for (const auto& [name, info] : keymap.GetAllKeys()) {
        if (info.led_id < 0 || info.led_id >= static_cast<int>(TOTAL_LEDS)) continue;

        uint32_t seed = (info.physical_col * 17 + info.physical_row * 31) % 100;
        double star_phase = static_cast<double>((elapsed_ms + seed * 45) % period_ms_) / static_cast<double>(period_ms_);
        double glow = std::max(0.0, std::sin(star_phase * PI) * 1.5 - 0.5);

        if (random_colors_) {
            double hue = seed * 3.6;
            out_frame.SetKey(info.led_id, HsvToRgb(hue, 1.0, glow));
        } else {
            uint8_t r = static_cast<uint8_t>(color_.r * glow);
            uint8_t g = static_cast<uint8_t>(color_.g * glow);
            uint8_t b = static_cast<uint8_t>(color_.b * glow);
            out_frame.SetKey(info.led_id, ColorRGB(r, g, b));
        }
    }
}

// 8. 流沙涌动
void QuicksandEffect::Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) {
    double time_phase = static_cast<double>(elapsed_ms % period_ms_) / static_cast<double>(period_ms_);
    double dir_x, dir_y;
    GetDirVector(direction_, dir_x, dir_y);

    for (const auto& [name, info] : keymap.GetAllKeys()) {
        if (info.led_id < 0 || info.led_id >= static_cast<int>(TOTAL_LEDS)) continue;

        double wave = std::sin((info.physical_col * 0.4 * dir_x + info.physical_row * 0.8 * dir_y) + (time_phase * 2.0 * PI)) * 0.5 + 0.5;
        uint8_t r = static_cast<uint8_t>(c1_.r * wave + c2_.r * (1.0 - wave));
        uint8_t g = static_cast<uint8_t>(c1_.g * wave + c2_.g * (1.0 - wave));
        uint8_t b = static_cast<uint8_t>(c1_.b * wave + c2_.b * (1.0 - wave));
        out_frame.SetKey(info.led_id, ColorRGB(r, g, b));
    }
}

// 9. 电流涌动
void CurrentEffect::Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) {
    const uint64_t half = period_ms_ / 2 ? period_ms_ / 2 : 1;
    double time_phase = static_cast<double>(elapsed_ms % half) / static_cast<double>(half);
    double pulse_col = time_phase * 16.0;

    for (const auto& [name, info] : keymap.GetAllKeys()) {
        if (info.led_id < 0 || info.led_id >= static_cast<int>(TOTAL_LEDS)) continue;

        double diff = std::abs(static_cast<double>(info.physical_col) - pulse_col);
        if (diff < 1.6) {
            out_frame.SetKey(info.led_id, ColorRGB(255, 255, 255)); // 核心白炽
        } else {
            out_frame.SetKey(info.led_id, ColorRGB(static_cast<uint8_t>(color_.r * 0.15),
                                                   static_cast<uint8_t>(color_.g * 0.15),
                                                   static_cast<uint8_t>(color_.b * 0.15)));
        }
    }
}

// 10. 雨滴淅沥
void RaindropEffect::Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) {
    for (const auto& [name, info] : keymap.GetAllKeys()) {
        if (info.led_id < 0 || info.led_id >= static_cast<int>(TOTAL_LEDS)) continue;

        uint32_t seed = (info.physical_col * 23 + info.physical_row * 47) % 80;
        double drop_phase = static_cast<double>((elapsed_ms + seed * 80) % period_ms_) / static_cast<double>(period_ms_);
        double drop_glow = std::pow(std::max(0.0, 1.0 - drop_phase * 4.0), 2.0);

        uint8_t r = static_cast<uint8_t>(color_.r * (0.15 + 0.85 * drop_glow));
        uint8_t g = static_cast<uint8_t>(color_.g * (0.15 + 0.85 * drop_glow));
        uint8_t b = static_cast<uint8_t>(color_.b * (0.15 + 0.85 * drop_glow));
        out_frame.SetKey(info.led_id, ColorRGB(r, g, b));
    }
}

} // namespace aura
