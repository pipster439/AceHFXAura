#pragma once

#include "engine/effect.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <string>

namespace aura {

// 通用灯效刷新周期钳制函数 (下界取 33ms ≈ 30FPS，低于此无物理意义)
inline constexpr uint64_t ClampPeriod(uint64_t v, uint64_t def, uint64_t min = 33) {
    if (v == 0) {
        return def < min ? min : def;
    }
    return v < min ? min : v;
}

// 通用灯效厚度 (thickness) 钳制函数 [0.1, 5.0]
inline double ClampThickness(double t, double def = 1.0, double min_t = 0.1, double max_t = 5.0) {
    if (!std::isfinite(t)) {
        return (std::isfinite(def) && def >= min_t && def <= max_t) ? def : 1.0;
    }
    return std::clamp(t, min_t, max_t);
}

// 1. 纯静态单色 (支持模拟按压触发增亮)
class StaticEffect : public Effect {
public:
    explicit StaticEffect(const ColorRGB& color, bool analog = false)
        : color_(color), analog_(analog) {}

    void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) override;
    const ColorRGB& GetColor() const { return color_; }
    bool GetAnalog() const { return analog_; }

private:
    ColorRGB color_;
    bool analog_{false};
    std::unordered_map<std::string, double> analog_decays_;
};

// 2. 双色呼吸渐变
class BreathingEffect : public Effect {
public:
    BreathingEffect(const ColorRGB& c1, const ColorRGB& c2, uint64_t period_ms = 3000)
        : c1_(c1), c2_(c2), period_ms_(ClampPeriod(period_ms, 3000)) {}

    void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) override;
    uint64_t GetPeriodMs() const { return period_ms_; }
    const ColorRGB& GetC1() const { return c1_; }
    const ColorRGB& GetC2() const { return c2_; }

private:
    ColorRGB c1_;
    ColorRGB c2_;
    uint64_t period_ms_;
};

// 3. 彩色循环 (全光谱平滑循环)
class ColorCycleEffect : public Effect {
public:
    explicit ColorCycleEffect(uint64_t period_ms = 3500)
        : period_ms_(ClampPeriod(period_ms, 3500)) {}

    void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) override;
    uint64_t GetPeriodMs() const { return period_ms_; }

private:
    uint64_t period_ms_;
};

// 4. 动态彩虹光谱波浪 (支持 8 方向向量、扩散 spread、周期与厚度)
class WaveEffect : public Effect {
public:
    explicit WaveEffect(uint64_t period_ms = 3500, const std::string& direction = "diag_dl", double thickness = 1.0)
        : period_ms_(ClampPeriod(period_ms, 3500)), direction_(direction),
          thickness_(ClampThickness(thickness, 1.0)) {}

    void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) override;
    uint64_t GetPeriodMs() const { return period_ms_; }
    const std::string& GetDirection() const { return direction_; }
    double GetThickness() const { return thickness_; }

private:
    uint64_t period_ms_;
    std::string direction_;
    double thickness_{1.0};
};

// 5. 按键触发响应 (仅被按下的键发光并渐隐，无涟漪扩散射线，无自动化假演示)
class ReactiveEffect : public Effect {
public:
    ReactiveEffect(const ColorRGB& base_color, const ColorRGB& trigger_color, uint64_t speed_ms = 2500)
        : base_color_(base_color), trigger_color_(trigger_color), speed_ms_(ClampPeriod(speed_ms, 2500)) {}

    void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) override;
    uint64_t GetSpeedMs() const { return speed_ms_; }
    const ColorRGB& GetBaseColor() const { return base_color_; }
    const ColorRGB& GetTriggerColor() const { return trigger_color_; }

private:
    ColorRGB base_color_;
    ColorRGB trigger_color_;
    uint64_t speed_ms_;
    std::unordered_map<std::string, double> key_decays_;
};

// 6. 涟漪扩散光效 (敲击按键向四周激发同心扩散波浪)
class RippleEffect : public Effect {
public:
    RippleEffect(const ColorRGB& base_color, const ColorRGB& trigger_color, uint64_t speed_ms = 2500, double thickness = 1.0)
        : base_color_(base_color), trigger_color_(trigger_color), speed_ms_(ClampPeriod(speed_ms, 2500)),
          thickness_(ClampThickness(thickness, 1.0)) {}

    void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) override;
    uint64_t GetSpeedMs() const { return speed_ms_; }
    const ColorRGB& GetBaseColor() const { return base_color_; }
    const ColorRGB& GetTriggerColor() const { return trigger_color_; }
    double GetThickness() const { return thickness_; }

private:
    ColorRGB base_color_;
    ColorRGB trigger_color_;
    uint64_t speed_ms_;
    double thickness_{1.0};

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
        : color_(color), random_colors_(random_colors), period_ms_(ClampPeriod(period_ms, 2500)) {}

    void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) override;
    uint64_t GetPeriodMs() const { return period_ms_; }
    const ColorRGB& GetColor() const { return color_; }
    bool GetRandomColors() const { return random_colors_; }

private:
    ColorRGB color_;
    bool random_colors_{false};
    uint64_t period_ms_;
};

// 8. 流沙涌动 (温润正弦曲线流动)
class QuicksandEffect : public Effect {
public:
    QuicksandEffect(const ColorRGB& c1, const ColorRGB& c2, uint64_t period_ms = 3500, const std::string& direction = "diag_dl", double thickness = 1.0)
        : c1_(c1), c2_(c2), period_ms_(ClampPeriod(period_ms, 3500)), direction_(direction),
          thickness_(ClampThickness(thickness, 1.0)) {}

    void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) override;
    uint64_t GetPeriodMs() const { return period_ms_; }
    const std::string& GetDirection() const { return direction_; }
    double GetThickness() const { return thickness_; }
    const ColorRGB& GetC1() const { return c1_; }
    const ColorRGB& GetC2() const { return c2_; }

private:
    ColorRGB c1_;
    ColorRGB c2_;
    uint64_t period_ms_;
    std::string direction_;
    double thickness_{1.0};
};

// 9. 电流涌动 (高能闪电电光脉冲)
class CurrentEffect : public Effect {
public:
    explicit CurrentEffect(const ColorRGB& color, uint64_t period_ms = 2000, double thickness = 1.0)
        : color_(color), period_ms_(ClampPeriod(period_ms, 2000)),
          thickness_(ClampThickness(thickness, 1.0)) {}

    void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) override;
    uint64_t GetPeriodMs() const { return period_ms_; }
    const ColorRGB& GetColor() const { return color_; }
    double GetThickness() const { return thickness_; }

private:
    ColorRGB color_;
    uint64_t period_ms_;
    double thickness_{1.0};
};

// 10. 雨滴淅沥 (雨点降落随机激荡)
class RaindropEffect : public Effect {
public:
    explicit RaindropEffect(const ColorRGB& color, uint64_t period_ms = 2500)
        : color_(color), period_ms_(ClampPeriod(period_ms, 2500)) {}

    void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) override;
    uint64_t GetPeriodMs() const { return period_ms_; }
    const ColorRGB& GetColor() const { return color_; }

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

    const ColorRGB& GetBgColor() const { return bg_color_; }

private:
    ColorRGB bg_color_;
};

} // namespace aura
