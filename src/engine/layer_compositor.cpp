#include "engine/layer_compositor.h"
#include <algorithm>
#include <chrono>

namespace aura {
LayerPixelFrame LayerPixelFrame::FromFrame(const FrameBuffer& frame, bool opaque) {
    LayerPixelFrame result; for (size_t led = 0; led < TOTAL_LEDS; ++led) {
        result.pixels[led] = ColorRGB(frame.buffer[led*3], frame.buffer[led*3+1], frame.buffer[led*3+2]);
        if (opaque || frame.buffer[led*3] || frame.buffer[led*3+1] || frame.buffer[led*3+2]) result.coverage.set(led);
    } return result;
}
FrameBuffer LayerPixelFrame::ToFrame() const { FrameBuffer result; for (size_t led=0;led<TOTAL_LEDS;++led) result.SetKey(led,pixels[led]); return result; }
bool LayerStack::Add(LayerSubmission submission) { if (count_ >= Capacity) return false; entries_[count_++] = std::move(submission); return true; }
void LayerCompositor::Apply(const LayerSubmission& layer, FrameBuffer& output, std::bitset<TOTAL_LEDS>& coverage) {
    const float opacity = std::clamp(layer.opacity, 0.0f, 1.0f); ++metrics_.layers;
    for (size_t led=0; led<TOTAL_LEDS; ++led) if (layer.frame.coverage.test(led)) {
        ++metrics_.covered_pixels; ++metrics_.blend_operations; const auto source=layer.frame.pixels[led];
        for (size_t channel=0;channel<RGB_CHANNELS;++channel) {
            auto& destination=output.buffer[led*RGB_CHANNELS+channel]; const auto value=channel==0?source.r:channel==1?source.g:source.b;
            if(layer.blend==LayerBlend::Replace) destination=(opacity>=1.0f)?static_cast<uint8_t>(value):static_cast<uint8_t>(std::clamp((1.0f-opacity)*destination+opacity*value,0.0f,255.0f));
            else if(layer.blend==LayerBlend::Additive) destination=static_cast<uint8_t>(std::clamp(destination+opacity*value,0.0f,255.0f));
            else destination=static_cast<uint8_t>(std::clamp((1.0f-opacity)*destination+opacity*value,0.0f,255.0f));
        }
        coverage.set(led);
    }
}
bool LayerCompositor::Compose(const LayerPixelFrame& base,const LayerStack& persistent,const LayerStack& transient,
                              FrameBuffer& output,std::bitset<TOTAL_LEDS>& coverage) {
    const auto start=std::chrono::steady_clock::now();output=base.ToFrame();coverage=base.coverage;metrics_.layers=metrics_.covered_pixels=metrics_.blend_operations=0;
    auto ordered=[&](const LayerStack& stack){std::array<size_t,LayerStack::Capacity> order{};for(size_t i=0;i<stack.Size();++i)order[i]=i;
        std::sort(order.begin(),order.begin()+stack.Size(),[&](size_t a,size_t b){return std::tie(stack[a].priority,stack[a].order,stack[a].sequence)<std::tie(stack[b].priority,stack[b].order,stack[b].sequence);});
        for(size_t i=0;i<stack.Size();++i)Apply(stack[order[i]],output,coverage);};
    ordered(persistent);ordered(transient);++metrics_.compose_calls;metrics_.last_compose_us=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count());return true;
}
} // namespace aura
