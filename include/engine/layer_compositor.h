#pragma once
#include "aura/aura_types.h"
#include <array>
#include <bitset>
#include <cstdint>

namespace aura {
enum class LayerBlend { Replace, Alpha, Additive };
struct LayerPixelFrame {
    std::array<ColorRGB, TOTAL_LEDS> pixels{};
    std::bitset<TOTAL_LEDS> coverage;
    static LayerPixelFrame FromFrame(const FrameBuffer& frame, bool opaque);
    FrameBuffer ToFrame() const;
    void Clear() { pixels.fill({}); coverage.reset(); }
    void Fill(ColorRGB color) { pixels.fill(color); coverage.set(); }
    void SetKey(size_t led, ColorRGB color) { if (led < TOTAL_LEDS) { pixels[led] = color; coverage.set(led); } }
};
struct LayerSubmission { LayerPixelFrame frame; LayerBlend blend{LayerBlend::Alpha}; float opacity{1.0f}; int priority{0}; size_t order{0}; uint64_t sequence{0}; };
class LayerStack {
public:
    static constexpr size_t Capacity = 32;
    bool Add(LayerSubmission submission);
    void Clear() { count_ = 0; }
    size_t Size() const { return count_; }
    const LayerSubmission& operator[](size_t index) const { return entries_[index]; }
private:
    std::array<LayerSubmission, Capacity> entries_{};
    size_t count_{0};
};
struct CompositionMetrics { uint64_t layers{0}, covered_pixels{0}, blend_operations{0}, compose_calls{0}, last_compose_us{0}; };
class LayerCompositor {
public:
    static constexpr size_t MaxLayers = LayerStack::Capacity;
    bool Compose(const LayerPixelFrame& base, const LayerStack& persistent, const LayerStack& transient,
                 FrameBuffer& output, std::bitset<TOTAL_LEDS>& coverage);
    CompositionMetrics Metrics() const { return metrics_; }
private:
    void Apply(const LayerSubmission& layer, FrameBuffer& output, std::bitset<TOTAL_LEDS>& coverage);
    CompositionMetrics metrics_{};
};
} // namespace aura
