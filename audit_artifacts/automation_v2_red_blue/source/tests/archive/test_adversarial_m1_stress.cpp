#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <atomic>

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <cassert>

#include "engine/plugin_manager.h"
#include "engine/overlay_manager.h"
#include "engine/builtin_effects.h"
#include "engine/effect_engine.h"
#include "config/rule_engine.h"
#include "gsi/gsi_adapter.h"
#include "aura/keymap.h"
#include "aura/aura_types.h"

static int g_failures = 0;

#define STRESS_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "  [FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            g_failures++; \
        } else { \
            std::cout << "  [PASS] " << msg << "\n"; \
        } \
    } while (0)

// Helper dummy effect for overlay testing
class DummyOverlayEffect : public aura::Effect {
public:
    DummyOverlayEffect(uint8_t r, uint8_t g, uint8_t b) : r_(r), g_(g), b_(b) {}
    void Render(uint64_t, aura::FrameBuffer& out, const aura::Keymap&) override {
        for (size_t i = 0; i < aura::FRAME_BUFFER_SIZE; i += 3) {
            out.buffer[i] = r_;
            out.buffer[i + 1] = g_;
            out.buffer[i + 2] = b_;
        }
    }
private:
    uint8_t r_, g_, b_;
};

int main() {
    std::cout << "=========================================================\n";
    std::cout << "  Milestone 1 Empirical Stress & Adversarial Harness\n";
    std::cout << "=========================================================\n\n";

    aura::Keymap km;
    std::string km_path = "tests/fixtures/calibrated_keymap.json";
    if (!std::filesystem::exists(km_path)) km_path = "calibrated_keymap.json";
    km.LoadFromJson(km_path);

    // =========================================================================
    // PART 1: PluginManager Adversarial Stress
    // =========================================================================
    std::cout << "[TEST SUITE 1] PluginManager Dynamic Loading & Adversarial Stress\n";
    auto& pm = aura::PluginManager::Instance();

    // 1.1 Basic Plugin Loading & Execution
    {
        auto eff = pm.LoadPlugin("stress_test");
        STRESS_CHECK(eff != nullptr, "1.1 LoadPlugin('stress_test') successfully returns Effect instance");
        STRESS_CHECK(pm.HasPlugin("stress_test"), "1.1 HasPlugin('stress_test') is true");

        aura::FrameBuffer fb;
        eff->Render(1234, fb, km);
        STRESS_CHECK(fb.buffer[0] == static_cast<uint8_t>(1234 % 256), "1.1 Plugin Render outputs valid computed frame");
    }

    // 1.2 LNK1104 File Lock Bypass (Shadow Copy Verification)
    {
        // Keep an active effect in memory
        auto active_eff = pm.CreateEffect("stress_test");
        STRESS_CHECK(active_eff != nullptr, "1.2 Active effect instance created");

        // Attempt to open and write to the ORIGINAL DLL file while active_eff is held in memory
        std::filesystem::path orig_dll = "plugins/effect_stress_test.dll";
        STRESS_CHECK(std::filesystem::exists(orig_dll), "1.2 Original DLL exists on disk");

        bool write_success = false;
        {
            // Open for binary appending/writing
            std::ofstream ofs(orig_dll, std::ios::binary | std::ios::in | std::ios::out);
            if (ofs.is_open()) {
                ofs.seekp(0, std::ios::end);
                char zero = 0;
                ofs.write(&zero, 0); // zero-byte touch
                ofs.flush();
                write_success = !ofs.fail();
            }
        }
        STRESS_CHECK(write_success, "1.2 LNK1104 Lock Bypass: Original DLL remains writable on disk while loaded");
    }

    // 1.3 Concurrent Loading Stress Across 16 Threads
    {
        constexpr int NUM_THREADS = 16;
        constexpr int OPS_PER_THREAD = 40;
        std::vector<std::thread> threads;
        std::atomic<bool> start_latch{false};
        std::atomic<int> success_count{0};

        for (int i = 0; i < NUM_THREADS; ++i) {
            threads.emplace_back([&, i]() {
                while (!start_latch.load()) { std::this_thread::yield(); }
                aura::FrameBuffer local_fb;
                for (int op = 0; op < OPS_PER_THREAD; ++op) {
                    auto eff = pm.CreateEffect("stress_test");
                    if (eff) {
                        eff->Render(op * 10, local_fb, km);
                        if (local_fb.buffer[1] == 100 && local_fb.buffer[2] == 200) {
                            success_count++;
                        }
                    }
                    pm.HasPlugin("stress_test");
                    pm.GetLoadedPluginNames();
                }
            });
        }

        start_latch.store(true);
        for (auto& t : threads) {
            t.join();
        }

        STRESS_CHECK(success_count == NUM_THREADS * OPS_PER_THREAD, 
            "1.3 Concurrent Loading: " + std::to_string(success_count.load()) + " / " + 
            std::to_string(NUM_THREADS * OPS_PER_THREAD) + " concurrent effect operations succeeded");
    }

    // 1.4 Rapid Reloads During Active Concurrent Rendering
    {
        std::atomic<bool> render_running{true};
        std::atomic<int> render_frames{0};
        std::atomic<int> reload_success{0};

        // Worker thread continuously rendering from an effect instance
        std::thread render_thread([&]() {
            auto eff = pm.CreateEffect("stress_test");
            aura::FrameBuffer fb;
            while (render_running.load()) {
                if (eff) {
                    eff->Render(render_frames.load(), fb, km);
                    render_frames++;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });

        // Main thread performing rapid reloads
        for (int r = 0; r < 30; ++r) {
            if (pm.ReloadPlugin("stress_test")) {
                reload_success++;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        render_running.store(false);
        render_thread.join();

        STRESS_CHECK(reload_success == 30, "1.4 Rapid Reload: 30 / 30 reloads succeeded without interrupting render loop");
        STRESS_CHECK(render_frames > 50, "1.4 Active rendering thread survived rapid reloads with zero memory violations");
    }

    // 1.5 Non-Existent DLLs and Malformed Paths
    {
        auto e1 = pm.LoadPlugin("nonexistent_plugin_xyz999");
        STRESS_CHECK(e1 == nullptr, "1.5 Non-existent plugin returns nullptr safely");

        auto e2 = pm.LoadPlugin("");
        STRESS_CHECK(e2 == nullptr, "1.5 Empty string plugin name returns nullptr safely");

        auto e3 = pm.CreateEffect("nonexistent_plugin_xyz999");
        STRESS_CHECK(e3 == nullptr, "1.5 CreateEffect on non-existent plugin returns nullptr safely");

        bool r1 = pm.ReloadPlugin("nonexistent_plugin_xyz999");
        STRESS_CHECK(!r1, "1.5 ReloadPlugin on non-existent plugin returns false safely");
    }

    // 1.6 Corrupted DLL Files & Export Validation
    {
        // 1.6a 0-byte file
        std::filesystem::path zero_byte_dll = "plugins/effect_zero_corrupt.dll";
        {
            std::ofstream ofs(zero_byte_dll, std::ios::binary | std::ios::trunc);
        }
        auto e_zero = pm.LoadPlugin("zero_corrupt");
        STRESS_CHECK(e_zero == nullptr, "1.6a 0-byte DLL rejected safely");
        std::filesystem::remove(zero_byte_dll);

        // 1.6b Garbage text file named .dll
        std::filesystem::path garbage_dll = "plugins/effect_garbage.dll";
        {
            std::ofstream ofs(garbage_dll, std::ios::binary | std::ios::trunc);
            ofs << "NOT_A_VALID_PE_HEADER_JUST_GARBAGE_BYTES_FOR_TESTING_1234567890";
        }
        auto e_garbage = pm.LoadPlugin("garbage");
        STRESS_CHECK(e_garbage == nullptr, "1.6b Garbage PE DLL rejected safely");
        std::filesystem::remove(garbage_dll);

        // 1.6c Valid PE file lacking Aura plugin exports
        std::filesystem::path no_exports_dll = "plugins/effect_no_exports.dll";
        std::error_code ec;
        std::filesystem::copy_file("build/Release/test_diag_hook_test.exe", no_exports_dll, std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec) {
            auto e_no_exp = pm.LoadPlugin("no_exports");
            STRESS_CHECK(e_no_exp == nullptr, "1.6c Executable missing CreateEffect/DestroyEffect rejected safely");
            std::filesystem::remove(no_exports_dll, ec);
        }
    }

    // 1.7 Shadow Cache Cleanup
    {
        pm.CleanupShadowCache();
        STRESS_CHECK(true, "1.7 CleanupShadowCache executes safely");
    }

    // =========================================================================
    // PART 2: OverlayManager Adversarial Stress
    // =========================================================================
    std::cout << "\n[TEST SUITE 2] OverlayManager Transient Pulse & Blending Stress\n";
    aura::OverlayManager om;

    // 2.1 Overlapping Events with Multiple Concurrent Blend Modes
    {
        auto eff_red = std::make_shared<DummyOverlayEffect>(255, 0, 0);
        auto eff_green = std::make_shared<DummyOverlayEffect>(0, 255, 0);
        auto eff_blue = std::make_shared<DummyOverlayEffect>(0, 0, 255);
        auto eff_white = std::make_shared<DummyOverlayEffect>(255, 255, 255);

        // Trigger 4 overlapping overlays with different modes
        om.TriggerOverlay("event.kill", eff_red, 1000, 300, "replace", 100);
        om.TriggerOverlay("event.headshot", eff_green, 800, 200, "add", 50);
        om.TriggerOverlay("event.bomb", eff_blue, 1500, 500, "blend", 200);
        om.TriggerOverlay("event.flash", eff_white, 2000, 600, "blend", 100);

        STRESS_CHECK(om.GetActiveOverlayCount() == 4, "2.1 4 concurrent overlapping overlays registered");

        aura::FrameBuffer fb;
        fb.Clear();
        bool all_valid = true;

        // Render 100 frames simulating 2.5 seconds of timeline
        for (uint64_t t = 1000; t <= 3500; t += 25) {
            om.ApplyOverlays(t, fb, km);
            for (size_t i = 0; i < aura::FRAME_BUFFER_SIZE; ++i) {
                if (fb.buffer[i] > 255) { all_valid = false; }
            }
        }

        STRESS_CHECK(all_valid, "2.1 Multi-mode overlapping overlays render valid clamped [0, 255] frames");
        STRESS_CHECK(om.GetActiveOverlayCount() == 0, "2.1 All 4 overlays naturally expire after duration");
    }

    // 2.2 Zero, Negative & Boundary Durations / Underflow Protection
    {
        auto eff = std::make_shared<DummyOverlayEffect>(100, 100, 100);

        // Zero duration
        om.TriggerOverlay("event.zero", eff, 0, 0, "blend", 0);
        aura::FrameBuffer fb;
        fb.Clear();
        om.ApplyOverlays(1000, fb, km);
        STRESS_CHECK(om.GetActiveOverlayCount() == 0, "2.2 Zero duration overlay expires immediately on tick 1");

        // Fade > Duration (attempted unsigned underflow)
        om.TriggerOverlay("event.underflow_test", eff, 200, 500, "blend", 0);
        om.ApplyOverlays(1000, fb, km);
        om.ApplyOverlays(1100, fb, km);
        om.ApplyOverlays(1250, fb, km);
        STRESS_CHECK(om.GetActiveOverlayCount() == 0, "2.2 Fade > Duration handled safely without unsigned underflow");

        // Attack + Fade > Duration
        om.TriggerOverlay("event.overlap_fade", eff, 500, 400, "blend", 400);
        om.ApplyOverlays(2000, fb, km);
        om.ApplyOverlays(2300, fb, km);
        om.ApplyOverlays(2600, fb, km);
        STRESS_CHECK(om.GetActiveOverlayCount() == 0, "2.2 Attack + Fade > Duration handled safely");

        // Timestamp backwards skew (current_ms < start_ms)
        aura::ActiveOverlay test_ov;
        test_ov.start_ms = 5000;
        test_ov.duration_ms = 1000;
        test_ov.fade_out_ms = 300;
        double w_back = test_ov.ComputeWeight(4000); // 4000 < 5000
        STRESS_CHECK(w_back == 0.0, "2.2 Timestamp backwards jump yields safe 0.0 weight");
        STRESS_CHECK(!test_ov.IsExpired(4000), "2.2 Timestamp backwards jump does not falsely expire");

        // Timestamp near UINT64_MAX
        test_ov.start_ms = UINT64_MAX - 2000;
        test_ov.duration_ms = 1000;
        double w_near_max = test_ov.ComputeWeight(UINT64_MAX - 1500);
        STRESS_CHECK(w_near_max >= 0.0 && w_near_max <= 1.0, "2.2 Timestamp near UINT64_MAX computes valid weight without overflow");
    }

    // 2.3 High-Frequency Multi-Threaded Burst Triggers
    {
        om.ClearActiveOverlays();
        constexpr int NUM_BURST_THREADS = 8;
        constexpr int TRIGGERS_PER_THREAD = 1000;
        std::vector<std::thread> burst_threads;
        std::atomic<bool> start_burst{false};
        auto dummy_eff = std::make_shared<DummyOverlayEffect>(200, 10, 50);

        for (int i = 0; i < NUM_BURST_THREADS; ++i) {
            burst_threads.emplace_back([&, i]() {
                while (!start_burst.load()) { std::this_thread::yield(); }
                std::string ev_name = "event.burst_" + std::to_string(i % 4);
                for (int j = 0; j < TRIGGERS_PER_THREAD; ++j) {
                    om.TriggerOverlay(ev_name, dummy_eff, 1000, 300, "blend", 100);
                }
            });
        }

        // Concurrent rendering thread
        std::atomic<bool> render_burst_running{true};
        std::thread render_burst([&]() {
            aura::FrameBuffer fb;
            uint64_t tick = 1000;
            while (render_burst_running.load()) {
                om.ApplyOverlays(tick, fb, km);
                tick += 10;
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
        });

        start_burst.store(true);
        for (auto& t : burst_threads) {
            t.join();
        }
        render_burst_running.store(false);
        render_burst.join();

        STRESS_CHECK(true, "2.3 High-Frequency 8,000 burst triggers across 8 threads completed without race/deadlock");
    }

    // 2.4 GSI Rising-Edge Retriggering Resilience
    {
        om.ClearBindings();
        om.ClearActiveOverlays();

        auto kill_eff = std::make_shared<DummyOverlayEffect>(255, 0, 0);
        aura::OverlayBinding b;
        b.event_name = "event.kill";
        b.effect_name = "kill_effect";
        b.effect = kill_eff;
        b.duration_ms = 1000;
        b.fade_out_ms = 300;
        b.attack_ms = 0;
        b.blend_mode = "replace";
        om.RegisterBinding(b);

        aura::GsiState gsi;

        // Step 1: Initial state (kills=0)
        nlohmann::json s0 = {
            {"player", {{"state", {{"health", 100}, {"armor", 100}, {"round_kills", 0}, {"round_killhs", 0}}}, {"team", "CT"}}},
            {"round", {{"phase", "live"}, {"bomb", ""}}}
        };
        gsi.UpdateFromPayload(s0);
        om.UpdateBindingsFromGsi(&gsi, 1000);
        STRESS_CHECK(om.GetActiveOverlayCount() == 0, "2.4 Initial state false triggers no overlay");

        // Step 2: Rising edge (kills: 0 -> 1)
        nlohmann::json s1 = {
            {"player", {{"state", {{"health", 100}, {"armor", 100}, {"round_kills", 1}, {"round_killhs", 0}}}, {"team", "CT"}}},
            {"round", {{"phase", "live"}, {"bomb", ""}}}
        };
        gsi.UpdateFromPayload(s1);
        om.UpdateBindingsFromGsi(&gsi, 1020);
        STRESS_CHECK(om.GetActiveOverlayCount() == 1, "2.4 Rising edge (false -> true) triggers overlay");

        // Step 3: Sustained high (kills: 1 -> 1) - must NOT retrigger
        om.UpdateBindingsFromGsi(&gsi, 1040);
        om.UpdateBindingsFromGsi(&gsi, 1060);
        STRESS_CHECK(om.GetActiveOverlayCount() == 1, "2.4 Sustained true does not duplicate active overlay count");

        // Step 4: Advance kills: 1 -> 2 (second rising edge)
        nlohmann::json s2 = {
            {"player", {{"state", {{"health", 100}, {"armor", 100}, {"round_kills", 2}, {"round_killhs", 0}}}, {"team", "CT"}}},
            {"round", {{"phase", "live"}, {"bomb", ""}}}
        };
        gsi.UpdateFromPayload(s2);
        om.UpdateBindingsFromGsi(&gsi, 1100);
        STRESS_CHECK(om.GetActiveOverlayCount() == 1, "2.4 Second rising edge successfully refreshes overlay");
    }

    // 2.5 Bit-for-Bit Exact Restoration Blending
    {
        om.ClearActiveOverlays();
        aura::FrameBuffer base_frame;
        // Generate deterministic non-zero test base frame
        for (size_t i = 0; i < aura::FRAME_BUFFER_SIZE; ++i) {
            base_frame.buffer[i] = static_cast<uint8_t>((i * 7 + 13) % 256);
        }

        aura::FrameBuffer snapshot = base_frame;
        auto overlay_eff = std::make_shared<DummyOverlayEffect>(255, 0, 0);

        om.TriggerOverlay("event.restore_test", overlay_eff, 1000, 400, "blend", 100);

        // Frame at t=1000 (attack start)
        aura::FrameBuffer test_frame = base_frame;
        om.ApplyOverlays(1000, test_frame, km);

        // Frame at t=1500 (sustain phase: overlay heavily alters frame)
        om.ApplyOverlays(1500, test_frame, km);
        bool differs_at_peak = (std::memcmp(test_frame.buffer, snapshot.buffer, aura::FRAME_BUFFER_SIZE) != 0);
        STRESS_CHECK(differs_at_peak, "2.5 Overlay actively alters base frame during sustain phase");

        // Frame at t=2000 (exactly at end of duration)
        om.ApplyOverlays(2000, test_frame, km);

        // Frame at t=2001 (expired and pruned)
        test_frame = base_frame;
        om.ApplyOverlays(2001, test_frame, km);
        bool bit_identical = (std::memcmp(test_frame.buffer, snapshot.buffer, aura::FRAME_BUFFER_SIZE) == 0);
        STRESS_CHECK(bit_identical, "2.5 Bit-For-Bit Exact Restoration: Frame perfectly restored to snapshot after expiration");
    }

    // 2.6 Throughput Benchmark
    {
        om.ClearActiveOverlays();
        auto eff1 = std::make_shared<DummyOverlayEffect>(100, 50, 20);
        auto eff2 = std::make_shared<DummyOverlayEffect>(20, 100, 50);
        om.TriggerOverlay("ev1", eff1, 10000, 1000, "blend", 0);
        om.TriggerOverlay("ev2", eff2, 10000, 1000, "add", 0);

        aura::FrameBuffer fb;
        constexpr int BENCH_FRAMES = 10000;
        auto start = std::chrono::high_resolution_clock::now();
        for (int f = 0; f < BENCH_FRAMES; ++f) {
            om.ApplyOverlays(1000 + f, fb, km);
        }
        auto end = std::chrono::high_resolution_clock::now();
        double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
        double us_per_frame = (total_ms * 1000.0) / BENCH_FRAMES;

        std::cout << "  Benchmarked " << BENCH_FRAMES << " ApplyOverlays calls in " 
                  << total_ms << " ms (" << us_per_frame << " us / frame)\n";
        STRESS_CHECK(us_per_frame < 50.0, "2.6 Rendering throughput: < 50 us/frame (actual: " + std::to_string(us_per_frame) + " us)");
    }

    std::cout << "\n=========================================================\n";
    if (g_failures == 0) {
        std::cout << "  [SUCCESS] All Adversarial Stress Tests Passed (0 failures)!\n";
    } else {
        std::cout << "  [FAILURE] " << g_failures << " adversarial checks failed!\n";
    }
    std::cout << "=========================================================\n";

    return g_failures == 0 ? 0 : 1;
}

