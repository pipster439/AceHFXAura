#pragma once

#include "aura/aura_types.h"
#include "engine/plugin_interface.h"
#include "monitor/key_input_hub.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <thread>

namespace aura {

enum class InputAvailability { Disabled, Unknown, Fresh, Stale };

struct InputHealth {
    InputAvailability availability{InputAvailability::Unknown};
    uint64_t observed_at_ms{0};
    uint64_t age_ms{0};
    uint64_t sequence{0};
};

struct AudioFeatureSample {
    uint64_t observed_at_ms{0};
    float rms{0.0f};
    float peak{0.0f};
    float spectral_centroid_hz{0.0f};
    uint64_t sequence{0};
};

struct ScreenColorSample {
    uint64_t observed_at_ms{0};
    ColorRGB color{};
    uint64_t sequence{0};
};

struct AudioFeatureReading {
    InputHealth health{};
    AudioFeatureSample sample{};
};

struct ScreenColorReading {
    InputHealth health{};
    ScreenColorSample sample{};
};

// Shared bounded source metrics. Counters are diagnostic only; they never
// authorize capture, hardware writes, or render-thread waits.
struct InputSourceMetrics {
    uint64_t accepted_samples{0};
    uint64_t rejected_samples{0};
    uint64_t reads{0};
};

InputHealth EvaluateInputHealth(bool enabled, uint64_t observed_at_ms,
                                uint64_t sequence, uint64_t now_ms,
                                uint64_t freshness_ms);

class AudioFeatureSource {
public:
    explicit AudioFeatureSource(uint64_t freshness_ms = 250) : freshness_ms_(freshness_ms) {}

    void Enable();
    void Disable();
    bool Enabled() const { return enabled_.load(std::memory_order_acquire); }
    // Owner-enabled adapter seam / deterministic fake input. No capture occurs.
    bool Submit(AudioFeatureSample sample);
    AudioFeatureReading Read(uint64_t now_ms) const;
    InputSourceMetrics Metrics() const;

private:
    static bool Valid(const AudioFeatureSample& sample);
    const uint64_t freshness_ms_;
    std::atomic<bool> enabled_{false};
    std::atomic<uint64_t> observed_at_ms_{0}, sequence_{0};
    std::atomic<uint32_t> rms_bits_{0}, peak_bits_{0}, centroid_bits_{0};
    mutable std::atomic<uint64_t> accepted_{0}, rejected_{0}, reads_{0};
};

class ScreenColorSource {
public:
    explicit ScreenColorSource(uint64_t freshness_ms = 500) : freshness_ms_(freshness_ms) {}

    void Enable();
    void Disable();
    bool Enabled() const { return enabled_.load(std::memory_order_acquire); }
    // Owner-enabled adapter seam / deterministic fake input. No GDI capture occurs.
    bool Submit(ScreenColorSample sample);
    ScreenColorReading Read(uint64_t now_ms) const;
    InputSourceMetrics Metrics() const;

private:
    static bool Valid(const ScreenColorSample& sample);
    const uint64_t freshness_ms_;
    std::atomic<bool> enabled_{false};
    std::atomic<uint64_t> observed_at_ms_{0}, sequence_{0};
    std::atomic<uint32_t> color_{0};
    mutable std::atomic<uint64_t> accepted_{0}, rejected_{0}, reads_{0};
};

class GsiInputSource {
public:
    explicit GsiInputSource(uint64_t freshness_ms = 3000) : freshness_ms_(freshness_ms) {}
    void Enable() { enabled_.store(true, std::memory_order_release); }
    void Disable() { enabled_.store(false, std::memory_order_release); }
    void Observe(uint64_t observed_at_ms, uint64_t sequence = 0);
    InputHealth Health(uint64_t now_ms) const;

private:
    const uint64_t freshness_ms_;
    std::atomic<bool> enabled_{true};
    std::atomic<uint64_t> observed_at_ms_{0}, sequence_{0};
};

struct KeyboardInputReading {
    InputHealth health{};
    AuraLogicalKeyboardSnapshotV1 snapshot{};
};

class KeyboardInputSource {
public:
    explicit KeyboardInputSource(uint64_t freshness_ms = 250) : freshness_ms_(freshness_ms) {}
    KeyboardInputReading Read(uint64_t now_ms) const;

private:
    const uint64_t freshness_ms_;
};

#ifdef _WIN32
// Optional Windows WASAPI loopback audio capture adapter.
// Defaults to disabled. Capture requires explicit Enable() call.
class WindowsAudioCaptureAdapter {
public:
    explicit WindowsAudioCaptureAdapter(AudioFeatureSource& target);
    ~WindowsAudioCaptureAdapter();

    WindowsAudioCaptureAdapter(const WindowsAudioCaptureAdapter&) = delete;
    WindowsAudioCaptureAdapter& operator=(const WindowsAudioCaptureAdapter&) = delete;

    bool Enable();
    void Disable();
    bool Enabled() const { return enabled_.load(std::memory_order_acquire); }

    // Simulation / offline seam: feed raw PCM samples to calculate RMS, Peak, Centroid
    // without requiring physical audio hardware or COM initialization.
    bool FeedPcmSamples(const float* samples, size_t count, uint32_t sample_rate, uint64_t now_ms);

    struct Metrics {
        uint64_t frames_captured{0};
        uint64_t capture_errors{0};
        uint64_t simulated_frames{0};
    };
    Metrics GetMetrics() const;

private:
    void WorkerLoop();

    AudioFeatureSource& target_;
    std::atomic<bool> enabled_{false};
    std::atomic<bool> running_{false};
    std::thread worker_;
    mutable std::atomic<uint64_t> frames_captured_{0};
    mutable std::atomic<uint64_t> capture_errors_{0};
    mutable std::atomic<uint64_t> simulated_frames_{0};
};

// Optional Windows GDI / Desktop screen color capture adapter.
// Defaults to disabled. Capture requires explicit Enable() call.
class WindowsScreenCaptureAdapter {
public:
    explicit WindowsScreenCaptureAdapter(ScreenColorSource& target, uint32_t sample_interval_ms = 33);
    ~WindowsScreenCaptureAdapter();

    WindowsScreenCaptureAdapter(const WindowsScreenCaptureAdapter&) = delete;
    WindowsScreenCaptureAdapter& operator=(const WindowsScreenCaptureAdapter&) = delete;

    bool Enable();
    void Disable();
    bool Enabled() const { return enabled_.load(std::memory_order_acquire); }

    // Simulation / offline seam: feed raw pixel buffer to compute average color
    // without requiring display monitor or GDI device context.
    bool FeedPixels(const uint8_t* bgra, int width, int height, uint64_t now_ms);

    struct Metrics {
        uint64_t frames_captured{0};
        uint64_t capture_errors{0};
        uint64_t simulated_frames{0};
    };
    Metrics GetMetrics() const;

private:
    void WorkerLoop();

    ScreenColorSource& target_;
    const uint32_t sample_interval_ms_;
    std::atomic<bool> enabled_{false};
    std::atomic<bool> running_{false};
    std::thread worker_;
    mutable std::atomic<uint64_t> frames_captured_{0};
    mutable std::atomic<uint64_t> capture_errors_{0};
    mutable std::atomic<uint64_t> simulated_frames_{0};
};
#endif

} // namespace aura
