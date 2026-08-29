#pragma once

#include "engine/effect.h"
#include <cmath>
#include <vector>
#include <unordered_map>
#include <string>

namespace aura {

// 1. 纯静态单色 (支持模拟按压触发增亮)
class StaticEffect : public Effect {
public:
    explicit StaticEffect(const ColorRGB& color, bool analog = false)
        : color_(color), analog_(analog) {}

    void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) override;

private:
    ColorRGB color_;
    bool analog_{false};
    std::unordered_map<std::string, double> analog_decays_;
};

// 2. 双色呼吸渐变
class BreathingEffect : public Effect {
public:
    BreathingEffect(const ColorRGB& c1, const ColorRGB& c2, uint64_t period_ms = 3000)
        : c1_(c1), c2_(c2), period_ms_(period_ms > 0 ? period_ms : 3000) {}

    void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) override;

private:
    ColorRGB c1_;
    ColorRGB c2_;
    uint64_t period_ms_;
};

// 3. 彩色循环 (全光谱平滑循环)
class ColorCycleEffect : public Effect {
public:
    explicit ColorCycleEffect(uint64_t period_ms = 3500)
        : period_ms_(period_ms > 0 ? period_ms : 3500) {}

    void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) override;

private:
    uint64_t period_ms_;
};

// 4. 动态彩虹光谱波浪 (支持 8 方向向量与周期)
class WaveEffect : public Effect {
public:
    explicit WaveEffect(uint64_t period_ms = 3500, const std::string& direction = "diag_dl")
        : period_ms_(period_ms > 0 ? period_ms : 3500), direction_(direction) {}

    void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) override;

private:
    uint64_t period_ms_;
    std::string direction_;
};

// 5. 按键触发响应 (仅被按下的键发光并渐隐，无涟漪扩散射线，无自动化假演示)
class ReactiveEffect : public Effect {
public:
    ReactiveEffect(const ColorRGB& base_color, const ColorRGB& trigger_color, uint64_t speed_ms = 2500)
        : base_color_(base_color), trigger_color_(trigger_color), speed_ms_(speed_ms > 0 ? speed_ms : 2500) {}

    void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) override;

private:
    ColorRGB base_color_;
    ColorRGB trigger_color_;
    uint64_t speed_ms_;
    std::unordered_map<std::string, double> key_decays_;
};

// 6. 涟漪扩散光效 (敲击按键向四周激发同心扩散波浪)
class RippleEffect : public Effect {
public:
    RippleEffect(const ColorRGB& base_color, const ColorRGB& trigger_color, uint64_t speed_ms = 2500)
        : base_color_(base_color), trigger_color_(trigger_color), speed_ms_(speed_ms > 0 ? speed_ms : 2500) {}

    void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) override;

private:
    ColorRGB base_color_;
    ColorRGB trigger_color_;
    uint64_t speed_ms_;

    struct RippleWave {
        double col{0.0};
        double row{0.0};
        uint64_t start_ms{0};
    };
    std::vector<RippleWave> ripples_;
};

// 7. 星空 (繁星微光随机闪烁)
class StarryNightEffect : public Effect {
public:
    StarryNightEffect(const ColorRGB& color, bool random_colors = false, uint64_t period_ms = 2500)
        : color_(color), random_colors_(random_colors), period_ms_(period_ms > 0 ? period_ms : 2500) {}

    void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) override;

private:
    ColorRGB color_;
    bool random_colors_{false};
    uint64_t period_ms_;
};

// 8. 流沙涌动 (温润正弦曲线流动)
class QuicksandEffect : public Effect {
public:
    QuicksandEffect(const ColorRGB& c1, const ColorRGB& c2, uint64_t period_ms = 3500, const std::string& direction = "diag_dl")
        : c1_(c1), c2_(c2), period_ms_(period_ms > 0 ? period_ms : 3500), direction_(direction) {}

    void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) override;

private:
    ColorRGB c1_;
    ColorRGB c2_;
    uint64_t period_ms_;
    std::string direction_;
};

// 9. 电流涌动 (高能闪电电光脉冲)
class CurrentEffect : public Effect {
public:
    explicit CurrentEffect(const ColorRGB& color, uint64_t period_ms = 2000)
        : color_(color), period_ms_(period_ms > 0 ? period_ms : 2000) {}

    void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) override;

private:
    ColorRGB color_;
    uint64_t period_ms_;
};

// 10. 雨滴淅沥 (雨点降落随机激荡)
class RaindropEffect : public Effect {
public:
    explicit RaindropEffect(const ColorRGB& color, uint64_t period_ms = 2500)
        : color_(color), period_ms_(period_ms > 0 ? period_ms : 2500) {}

    void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) override;

private:
    ColorRGB color_;
    uint64_t period_ms_;
};

// 11. 自定义逐键布局 (背景色 + 明确按键清单)
class CustomKeymapEffect : public Effect {
public:
    explicit CustomKeymapEffect(const ColorRGB& bg_color = ColorRGB(0, 0, 0))
        : bg_color_(bg_color) {}

    void Render(uint64_t /*elapsed_ms*/, FrameBuffer& out_frame, const Keymap& /*keymap*/) override {
        out_frame.Fill(bg_color_.r, bg_color_.g, bg_color_.b);
    }

private:
    ColorRGB bg_color_;
};

} // namespace aura
