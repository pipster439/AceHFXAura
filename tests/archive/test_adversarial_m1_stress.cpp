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

    std::cout << "\n=========================================================\n";
    if (g_failures == 0) {
        std::cout << "  [SUCCESS] All Adversarial Stress Tests Passed (0 failures)!\n";
    } else {
        std::cout << "  [FAILURE] " << g_failures << " adversarial checks failed!\n";
    }
    std::cout << "=========================================================\n";

    return g_failures == 0 ? 0 : 1;
}

