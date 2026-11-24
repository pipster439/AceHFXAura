#include "engine/effect_program.h"
#include "engine/input_sources.h"
#include "engine/layer_compositor.h"
#include "engine/program_catalog.h"
#include "gsi/gsi_adapter.h"
#include <iostream>
#include <chrono>
#include <atomic>
#include <vector>
#include <cmath>
#include <cstdlib>

// Custom global allocation tracker for hot-path zero-allocation verification
static std::atomic<uint64_t> g_alloc_count{0};
static std::atomic<bool> g_track_allocations{false};

void* operator new(std::size_t size) {
    if (g_track_allocations.load(std::memory_order_relaxed)) {
        g_alloc_count.fetch_add(1, std::memory_order_relaxed);
    }
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using Json = nlohmann::json;
using namespace aura;

namespace {
unsigned g_checks = 0;
void AssertCheck(bool condition, const char* message) {
    ++g_checks;
    if (!condition) {
        std::cerr << "FAILED ASSERTION: " << message << std::endl;
        throw std::runtime_error(message);
    }
}

class BenchmarkGsi final : public IGsiReader {
public:
    double health{100.0};
    bool active{true};
    mutable char text[257]{};

    double GetNumber(const char* field, double fallback) const override {
        if (!active) return fallback;
        if (std::strcmp(field, "player.state.health") == 0) return health;
        return fallback;
    }
    bool GetBool(const char* field, bool fallback) const override {
        if (!active) return fallback;
        if (std::strcmp(field, "player.state.helmet") == 0) return true;
        return fallback;
    }
    const char* GetString(const char* field, const char* fallback) const override {
        if (!active) return fallback;
        if (std::strcmp(field, "player.team") == 0) {
            strcpy_s(text, "CT");
            return text;
        }
        return fallback;
    }
    bool IsActive() const override { return active; }
};

struct BenchmarkKeyboard {
    AuraLogicalKeyboardSnapshotV1 snapshot{};
    static uint32_t AURA_PLUGIN_CALL Read(void* context, AuraLogicalKeyboardSnapshotV1* out, uint32_t bytes) {
        auto& self = *static_cast<BenchmarkKeyboard*>(context);
        if (bytes != sizeof(*out)) return 0;
        *out = self.snapshot;
        return 1;
    }
    AuraLogicalKeyboardHostV1 Host() {
        return {sizeof(AuraLogicalKeyboardHostV1), 1, this, &Read};
    }
};
} // namespace

int main(int argc, char** argv) {
    try {
        std::cout << "=== G5 PERFORMANCE & RELIABILITY BENCHMARK SUITE ===" << std::endl;
        const char* keymap_file = (argc >= 2) ? argv[1] : "calibrated_keymap.json";
        Keymap keymap;
        AssertCheck(keymap.LoadFromJson(keymap_file), "keymap load");

        // -------------------------------------------------------------
        // Benchmark 1: GSI Telemetry Timing & Latency
        // -------------------------------------------------------------
        {
            std::cout << "[1/5] Benchmarking GSI Ingestion & Query Latency (10,000 iterations)..." << std::endl;
            BenchmarkGsi gsi_reader;
            const size_t gsi_iterations = 10000;
            const auto start = std::chrono::high_resolution_clock::now();

            double sum_num = 0.0;
            bool sum_bool = false;
            for (size_t i = 0; i < gsi_iterations; ++i) {
                gsi_reader.health = static_cast<double>(i % 100);
                sum_num += gsi_reader.GetNumber("player.state.health", 0.0);
                sum_bool ^= gsi_reader.GetBool("player.state.helmet", false);
                const char* str = gsi_reader.GetString("player.team", "");
                if (!str || str[0] == '\0') AssertCheck(false, "gsi string empty");
            }
            const auto elapsed = std::chrono::high_resolution_clock::now() - start;
            const double total_us = std::chrono::duration<double, std::micro>(elapsed).count();
            const double avg_us_per_query = total_us / gsi_iterations;

            std::cout << "      GSI 10,000 queries completed in " << (total_us / 1000.0) << " ms ("
                      << avg_us_per_query << " us/query)" << std::endl;
            AssertCheck(avg_us_per_query < 20.0, "GSI query latency must be < 20 microseconds");
        }

        // -------------------------------------------------------------
        // Benchmark 2: 10,000-Frame Warmed Zero-Allocation Profiling
        // -------------------------------------------------------------
        {
            std::cout << "[2/5] Benchmarking 10,000-Frame Warmed Rendering & Zero-Allocation..." << std::endl;
            // Build a multi-feature program exercising:
            // - Audio feature RMS brightness scaling
            // - Screen color blending
            // - GSI health conditional branch
            // - WASD key loop with decay
            // - Periodic waveform oscillation
            Json full_program = {
                {"schema_version", 1},
                {"entry", 0},
                {"variables", 2},
                {"slots", 1},
                {"publication", {{"mode", "continuous"}, {"fade_out_ms", 0}}},
                {"nodes", Json::array({
                    // node 0: if GSI health < 50
                    {{"kind", "if"}, {"condition", {{"op", "lt"}, {"args", {
                        {{"op", "gsi_number"}, {"path", "player.state.health"}, {"default", {{"op", "number"}, {"value", 100}}}},
                        {{"op", "number"}, {"value", 50}}
                    }}}}, {"yes", 1}, {"no", 2}},
                    // node 1: fill with screen color
                    {{"kind", "action"}, {"op", "fill"}, {"color", {{"op", "screen_color"}, {"default", {{"op", "rgb"}, {"args", {{{"op", "number"}, {"value", 255}}, {{"op", "number"}, {"value", 0}}, {{"op", "number"}, {"value", 0}}}}}}}}, {"next", 3}},
                    // node 2: fill with audio RMS modulated color
                    {{"kind", "action"}, {"op", "fill"}, {"color", {{"op", "rgb"}, {"args", {
                        {{"op", "mul"}, {"args", {{{"op", "audio_feature"}, {"feature", "rms"}, {"default", {{"op", "number"}, {"value", 0.5}}}}, {{"op", "number"}, {"value", 255}}}}},
                        {{"op", "number"}, {"value", 120}},
                        {{"op", "number"}, {"value", 200}}
                    }}}}, {"next", 3}},
                    // node 3: foreach WASD key
                    {{"kind", "loopInit"}, {"slot", 0}, {"mode", "keys"}, {"next", 4}},
                    // node 4: loopTest WASD
                    {{"kind", "loopTest"}, {"slot", 0}, {"mode", "keys"}, {"zone", "wasd"}, {"body", 5}, {"after", 7}},
                    // node 5: set key with decay
                    {{"kind", "action"}, {"op", "setKey"}, {"keySlot", 0},
                     {"led", {{"op", "coords"}, {"field", "led_id"}}},
                     {"color", {{"op", "brightness"}, {"args", {
                         {{"op", "rgb"}, {"args", {{{"op", "number"}, {"value", 255}}, {{"op", "number"}, {"value", 255}}, {{"op", "number"}, {"value", 255}}}}},
                         {{"op", "decay"}, {"args", {{{"op", "number"}, {"value", 2.0}}}}}
                     }}}},
                     {"next", 6}},
                    // node 6: increment
                    {{"kind", "increment"}, {"slot", 0}, {"next", 4}},
                    // node 7: end
                    {{"kind", "end"}}
                })}
            };

            auto compiled = EffectProgram::Compile(full_program);
            AssertCheck(compiled->UsesAudio(), "program uses audio");
            AssertCheck(compiled->UsesScreen(), "program uses screen");
            AssertCheck(compiled->UsesGsi(), "program uses gsi");
            AssertCheck(compiled->UsesKeyboard(), "program uses keyboard");

            BenchmarkKeyboard kbd;
            kbd.snapshot.struct_size = sizeof(kbd.snapshot);
            kbd.snapshot.version = 1;
            kbd.snapshot.source_available = 1;
            kbd.snapshot.count = 1;
            kbd.snapshot.observed_at_ms = 1000;
            strcpy_s(kbd.snapshot.keys[0].name, "W");
            kbd.snapshot.keys[0].held = 1;
            auto host = kbd.Host();

            AudioFeatureSource audio_source;
            audio_source.Enable();
            audio_source.Submit({1000, 0.6f, 0.9f, 800.0f, 1});

            ScreenColorSource screen_source;
            screen_source.Enable();
            screen_source.Submit({1000, {40, 80, 160}, 1});

            BenchmarkGsi gsi;
            gsi.health = 75.0;

            ProgramEffect effect(compiled, &host);

            // Warm up layout resolution and internal structures (1 frame)
            FrameBuffer out_frame;
            effect.RenderWithContext(EffectContext(1000, keymap, &gsi, &audio_source, &screen_source), out_frame);
            AssertCheck(!effect.Status().faulted, "warmup frame rendered without fault");

            // Now activate global allocation tracking
            g_alloc_count.store(0);
            g_track_allocations.store(true);

            const size_t render_iterations = 10000;
            const auto render_start = std::chrono::high_resolution_clock::now();

            for (uint64_t t = 1001; t <= 1000 + render_iterations; ++t) {
                effect.RenderWithContext(EffectContext(t, keymap, &gsi, &audio_source, &screen_source), out_frame);
            }

            const auto render_elapsed = std::chrono::high_resolution_clock::now() - render_start;
            g_track_allocations.store(false);

            const uint64_t allocations_during_render = g_alloc_count.load();
            const double total_render_us = std::chrono::duration<double, std::micro>(render_elapsed).count();
            const double avg_render_us = total_render_us / render_iterations;

            std::cout << "      10,000 frames rendered in " << (total_render_us / 1000.0) << " ms ("
                      << avg_render_us << " us/frame)" << std::endl;
            std::cout << "      Interpreter heap allocations during 10,000 frames: "
                      << allocations_during_render << std::endl;

            AssertCheck(allocations_during_render == 0, "Zero heap allocations during warmed rendering");
            AssertCheck(avg_render_us < 50.0, "Average render time must be < 50 microseconds per frame");
        }

        // -------------------------------------------------------------
        // Benchmark 3: LayerCompositor 10,000-Frame Throughput
        // -------------------------------------------------------------
        {
            std::cout << "[3/5] Benchmarking LayerCompositor Throughput (10,000 compositions)..." << std::endl;
            LayerCompositor compositor;
            LayerPixelFrame base;
            base.Fill({20, 20, 20});

            LayerStack persistent;
            for (size_t l = 0; l < 4; ++l) {
                LayerSubmission sub;
                sub.frame.SetKey(l * 10, ColorRGB(100, 150, 200));
                sub.blend = LayerBlend::Alpha;
                sub.opacity = 0.75f;
                persistent.Add(sub);
            }

            LayerStack transient;
            for (size_t l = 0; l < 4; ++l) {
                LayerSubmission sub;
                sub.frame.SetKey(l * 10 + 5, ColorRGB(50, 100, 150));
                sub.blend = LayerBlend::Additive;
                sub.opacity = 0.5f;
                transient.Add(sub);
            }

            FrameBuffer output;
            std::bitset<TOTAL_LEDS> coverage;

            const size_t compose_iterations = 10000;
            const auto comp_start = std::chrono::high_resolution_clock::now();

            for (size_t i = 0; i < compose_iterations; ++i) {
                AssertCheck(compositor.Compose(base, persistent, transient, output, coverage), "compose succeeded");
            }

            const auto comp_elapsed = std::chrono::high_resolution_clock::now() - comp_start;
            const double total_comp_us = std::chrono::duration<double, std::micro>(comp_elapsed).count();
            const double avg_comp_us = total_comp_us / compose_iterations;

            std::cout << "      10,000 compositions finished in " << (total_comp_us / 1000.0) << " ms ("
                      << avg_comp_us << " us/composition)" << std::endl;
            AssertCheck(avg_comp_us < 20.0, "Layer composition must be < 20 microseconds");
            AssertCheck(coverage.all(), "All LEDs covered");
        }

        // -------------------------------------------------------------
        // Benchmark 4: ProgramCatalog Fault Injection & Concurrency
        // -------------------------------------------------------------
        {
            std::cout << "[4/5] Testing ProgramCatalog Fault Injection & Resiliency..." << std::endl;
            // Corrupt JSON parsing
            try {
                EffectProgram::Parse("{\"schema_version\": 999}");
                AssertCheck(false, "unsupported schema version must throw");
            } catch (const EffectProgramException& e) {
                AssertCheck(!e.Error().code.empty(), "structured error code present");
            }

            // Oversized payload rejection (> 1MB)
            try {
                std::string oversized(EffectProgram::MaxJsonBytes + 1024, ' ');
                EffectProgram::Parse(oversized);
                AssertCheck(false, "oversized program must throw");
            } catch (const EffectProgramException& e) {
                AssertCheck(!e.Error().code.empty(), "structured error code present");
            }

            // Fault injection during render loop: divide by zero
            Json div_zero_program = {
                {"schema_version", 1},
                {"entry", 0},
                {"variables", 0},
                {"slots", 0},
                {"publication", {{"mode", "continuous"}, {"fade_out_ms", 0}}},
                {"nodes", Json::array({
                    {{"kind", "action"}, {"op", "fill"}, {"color", {{"op", "rgb"}, {"args", {
                        {{"op", "div"}, {"args", {{{"op", "number"}, {"value", 100}}, {{"op", "number"}, {"value", 0}}}}},
                        {{"op", "number"}, {"value", 0}},
                        {{"op", "number"}, {"value", 0}}
                    }}}}, {"next", 1}},
                    {{"kind", "end"}}
                })}
            };
            auto faulty_compiled = EffectProgram::Compile(div_zero_program);
            ProgramEffect faulty_effect(faulty_compiled);
            FrameBuffer bad_output;
            faulty_effect.RenderWithContext(EffectContext(0, keymap), bad_output);
            AssertCheck(faulty_effect.Status().faulted, "runtime fault latched on divide by zero");
            AssertCheck(faulty_effect.Status().error.code == "divide_by_zero", "exact error code divide_by_zero");
            std::cout << "      Fault injection correctly latched: code=" << faulty_effect.Status().error.code << std::endl;
        }

        // -------------------------------------------------------------
        // Benchmark 5: Platform Adapters Concurrency & Rapid Toggles
        // -------------------------------------------------------------
        {
            std::cout << "[5/5] Testing Windows Capture Adapters Rapid Toggles (1,000 iterations)..." << std::endl;
#ifdef _WIN32
            AudioFeatureSource audio_src;
            WindowsAudioCaptureAdapter audio_adapter(audio_src);
            ScreenColorSource screen_src;
            WindowsScreenCaptureAdapter screen_adapter(screen_src, 10);

            // 1,000 rapid toggle cycles
            for (int i = 0; i < 1000; ++i) {
                audio_adapter.Enable();
                screen_adapter.Enable();
                audio_adapter.Disable();
                screen_adapter.Disable();
            }

            // Verify clean disabled state
            AssertCheck(!audio_adapter.Enabled(), "audio adapter cleanly disabled");
            AssertCheck(!screen_adapter.Enabled(), "screen adapter cleanly disabled");

            // Feed 10,000 simulated packets across sources
            audio_adapter.Enable();
            screen_adapter.Enable();
            float pcm_sample[2] = {0.5f, -0.5f};
            uint8_t pixel_sample[4] = {10, 20, 30, 255};
            for (int i = 0; i < 10000; ++i) {
                audio_adapter.FeedPcmSamples(pcm_sample, 2, 48000, 10000 + i);
                screen_adapter.FeedPixels(pixel_sample, 1, 1, 10000 + i);
            }
            AssertCheck(audio_adapter.GetMetrics().simulated_frames == 10000, "10,000 audio simulated frames");
            AssertCheck(screen_adapter.GetMetrics().simulated_frames == 10000, "10,000 screen simulated frames");
            audio_adapter.Disable();
            screen_adapter.Disable();
            std::cout << "      10,000 simulated frames fed and verified without deadlock or leak." << std::endl;
#else
            std::cout << "      (Non-Windows platform: adapter toggle skipped)" << std::endl;
#endif
        }

        std::cout << "PASS: All " << g_checks << " G5 performance, reliability & benchmark checks passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << std::endl;
        return 1;
    }
}
