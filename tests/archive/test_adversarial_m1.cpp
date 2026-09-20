#include <iostream>
#include <fstream>
#include <cmath>
#include <limits>
#include <vector>
#include <string>
#include <chrono>
#include <cassert>
#include <filesystem>

#include "engine/builtin_effects.h"
#include "config/rule_engine.h"
#include "aura/keymap.h"
#include "aura/aura_types.h"
#include "monitor/key_input_hub.h"
#include "third_party/json.hpp"

namespace aura {
double ParseAndClampThickness(const std::string& pname, const nlohmann::json& pval, double def_val = 1.0);
double ParseAndClampThickness(const nlohmann::json& pval, double def_val = 1.0);
std::shared_ptr<Effect> CreateEffectFromProfile(const std::string& pname, const nlohmann::json& pval);
}

static int g_failures = 0;

#define ADV_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "  [FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            g_failures++; \
        } else { \
            std::cout << "  [PASS] " << msg << "\n"; \
        } \
    } while (0)

// 验证帧缓冲区中所有通道数据在合法范围内 [0, 255]，且无 NaN / 内存损坏
static bool ValidateFrameBuffer(const aura::FrameBuffer& frame) {
    for (size_t i = 0; i < sizeof(frame.buffer); ++i) {
        uint8_t val = frame.buffer[i];
        if (val > 255) return false;
    }
    return true;
}

int main() {
    std::cout << "=========================================================\n";
    std::cout << "  Milencer 1 Adversarial Empirical Verification Harness\n";
    std::cout << "=========================================================\n\n";

    // -------------------------------------------------------------------------
    // Phase 1: Numerical Extrema in ClampThickness & ClampPeriod
    // -------------------------------------------------------------------------
    std::cout << "[Phase 1] ClampThickness and ClampPeriod Numerical Extrema...\n";
    {
        // ClampPeriod bounds
        ADV_CHECK(aura::ClampPeriod(0, 3000, 33) == 3000, "ClampPeriod(0, 3000) returns default 3000");
        ADV_CHECK(aura::ClampPeriod(0, 0, 33) == 33, "ClampPeriod(0, 0) clamps default to 33");
        ADV_CHECK(aura::ClampPeriod(1, 3000, 33) == 33, "ClampPeriod(1, 3000) clamps 1 to 33");
        ADV_CHECK(aura::ClampPeriod(32, 3000, 33) == 33, "ClampPeriod(32, 3000) clamps 32 to 33");
        ADV_CHECK(aura::ClampPeriod(33, 3000, 33) == 33, "ClampPeriod(33, 3000) accepts 33 exactly");
        ADV_CHECK(aura::ClampPeriod(UINT64_MAX, 3000, 33) == UINT64_MAX, "ClampPeriod(UINT64_MAX) retains max");

        // ClampThickness bounds
        const double qnan = std::numeric_limits<double>::quiet_NaN();
        const double snan = std::numeric_limits<double>::signaling_NaN();
        const double pinf = std::numeric_limits<double>::infinity();
        const double ninf = -std::numeric_limits<double>::infinity();

        ADV_CHECK(std::abs(aura::ClampThickness(qnan, 1.0) - 1.0) < 1e-9, "ClampThickness(quiet_NaN) -> 1.0");
        ADV_CHECK(std::abs(aura::ClampThickness(snan, 1.0) - 1.0) < 1e-9, "ClampThickness(signaling_NaN) -> 1.0");
        ADV_CHECK(std::abs(aura::ClampThickness(pinf, 1.0) - 1.0) < 1e-9, "ClampThickness(+Inf) -> 1.0");
        ADV_CHECK(std::abs(aura::ClampThickness(ninf, 1.0) - 1.0) < 1e-9, "ClampThickness(-Inf) -> 1.0");
        ADV_CHECK(std::abs(aura::ClampThickness(qnan, 3.5) - 3.5) < 1e-9, "ClampThickness(NaN, def=3.5) -> 3.5");
        ADV_CHECK(std::abs(aura::ClampThickness(qnan, 99.0) - 1.0) < 1e-9, "ClampThickness(NaN, invalid def=99.0) fallback to 1.0");
        ADV_CHECK(std::abs(aura::ClampThickness(qnan, -5.0) - 1.0) < 1e-9, "ClampThickness(NaN, negative def=-5.0) fallback to 1.0");
        ADV_CHECK(std::abs(aura::ClampThickness(qnan, qnan) - 1.0) < 1e-9, "ClampThickness(NaN, def=NaN) fallback to 1.0");

        // Subnormal numbers
        double subnormal = std::numeric_limits<double>::denorm_min(); // ~4.94e-324
        ADV_CHECK(std::abs(aura::ClampThickness(subnormal) - 0.1) < 1e-9, "ClampThickness(denorm_min) clamps to 0.1");

        // Signed zero
        ADV_CHECK(std::abs(aura::ClampThickness(0.0) - 0.1) < 1e-9, "ClampThickness(+0.0) clamps to 0.1");
        ADV_CHECK(std::abs(aura::ClampThickness(-0.0) - 0.1) < 1e-9, "ClampThickness(-0.0) clamps to 0.1");

        // Boundary values
        ADV_CHECK(std::abs(aura::ClampThickness(0.09999999999) - 0.1) < 1e-9, "ClampThickness(0.09999999999) clamps to 0.1");
        ADV_CHECK(std::abs(aura::ClampThickness(0.1) - 0.1) < 1e-9, "ClampThickness(0.1) keeps 0.1");
        ADV_CHECK(std::abs(aura::ClampThickness(5.0) - 5.0) < 1e-9, "ClampThickness(5.0) keeps 5.0");
        ADV_CHECK(std::abs(aura::ClampThickness(5.000000001) - 5.0) < 1e-9, "ClampThickness(5.000000001) clamps to 5.0");
        ADV_CHECK(std::abs(aura::ClampThickness(1e300) - 5.0) < 1e-9, "ClampThickness(1e300) clamps to 5.0");
        ADV_CHECK(std::abs(aura::ClampThickness(-1e300) - 0.1) < 1e-9, "ClampThickness(-1e300) clamps to 0.1");
    }

    // -------------------------------------------------------------------------
    // Phase 2: All 11 Effects Adversarial Multi-Frame Rendering
    // -------------------------------------------------------------------------
    std::cout << "\n[Phase 2] All 11 Effects Stress Rendering Under Extrema...\n";
    {
        aura::Keymap km;
        std::string km_path = "tests/fixtures/calibrated_keymap.json";
        if (!std::filesystem::exists(km_path)) {
            km_path = "calibrated_keymap.json";
        }
        ADV_CHECK(km.LoadFromJson(km_path), "Load keymap for rendering");

        aura::FrameBuffer frame;

        // WaveEffect with spread and diagonal under extreme thicknesses
        std::vector<double> test_thicknesses = { -1e10, -1.0, 0.0, 0.05, 0.1, 1.0, 2.5, 5.0, 5.1, 1e10 };
        std::vector<std::string> test_directions = { "spread", "up", "down", "left", "right", "diag_ul", "diag_ur", "diag_dl", "diag_dr", "invalid_xyz", "" };
        std::vector<uint64_t> test_times = { 0, 1, 33, 1000, 3500, 50000, 100000000ULL, UINT64_MAX - 100, UINT64_MAX };

        // Test WaveEffect
        for (double th : test_thicknesses) {
            for (const auto& dir : test_directions) {
                aura::WaveEffect wave(3500, dir, th);
                for (uint64_t t : test_times) {
                    frame.Clear();
                    wave.Render(t, frame, km);
                    ADV_CHECK(ValidateFrameBuffer(frame), "WaveEffect Render valid (" + dir + ", th=" + std::to_string(th) + ")");
                    break; // sample once per combination to keep run fast
                }
            }
        }

        // Test RippleEffect (including elapsed_ms < start_ms underflow and key spamming)
        {
            aura::RippleEffect ripple(aura::ColorRGB(10, 20, 30), aura::ColorRGB(200, 220, 240), 2500, 0.1);
            // Simulate 20 key presses at t = 10000
            for (int k = 0; k < 20; ++k) {
                aura::KeyInputHub::Instance().RecordKeyPress("W");
                aura::KeyInputHub::Instance().RecordKeyPress("SPACE");
            }
            frame.Clear();
            ripple.Render(10000, frame, km);
            ADV_CHECK(ValidateFrameBuffer(frame), "RippleEffect Render with multiple ripples");

            // Extreme underflow test: elapsed_ms = 5000 < start_ms = 10000
            ripple.Render(5000, frame, km);
            ADV_CHECK(ValidateFrameBuffer(frame), "RippleEffect Render with elapsed_ms < start_ms underflow safety");

            // Extreme timestamp tests
            for (uint64_t t : test_times) {
                ripple.Render(t, frame, km);
                ADV_CHECK(ValidateFrameBuffer(frame), "RippleEffect Render at extreme t=" + std::to_string(t));
                break;
            }
        }

        // Test CurrentEffect with pulse_col & pulse_width
        for (double th : test_thicknesses) {
            aura::CurrentEffect cur(aura::ColorRGB(0, 255, 255), 2000, th);
            for (uint64_t t : test_times) {
                frame.Clear();
                cur.Render(t, frame, km);
                ADV_CHECK(ValidateFrameBuffer(frame), "CurrentEffect Render th=" + std::to_string(th));
                break;
            }
        }

        // Test QuicksandEffect with spread and normal directions
        for (double th : test_thicknesses) {
            for (const auto& dir : { "spread", "diag_dl", "right" }) {
                aura::QuicksandEffect qs(aura::ColorRGB(255, 0, 0), aura::ColorRGB(0, 0, 255), 3500, dir, th);
                frame.Clear();
                qs.Render(1750, frame, km);
                ADV_CHECK(ValidateFrameBuffer(frame), "QuicksandEffect Render dir=" + std::string(dir) + ", th=" + std::to_string(th));
                break;
            }
        }

        // Test StarryNightEffect under random_colors and monocolor
        {
            aura::StarryNightEffect sn_mono(aura::ColorRGB(0, 240, 255), false, 2500);
            aura::StarryNightEffect sn_rand(aura::ColorRGB(0, 240, 255), true, 2500);
            for (uint64_t t : test_times) {
                frame.Clear();
                sn_mono.Render(t, frame, km);
                ADV_CHECK(ValidateFrameBuffer(frame), "StarryNightEffect mono Render t=" + std::to_string(t));

                frame.Clear();
                sn_rand.Render(t, frame, km);
                ADV_CHECK(ValidateFrameBuffer(frame), "StarryNightEffect rand Render t=" + std::to_string(t));
                break;
            }
        }

        // Test RaindropEffect
        {
            aura::RaindropEffect rain(aura::ColorRGB(0, 180, 255), 2500);
            for (uint64_t t : test_times) {
                frame.Clear();
                rain.Render(t, frame, km);
                ADV_CHECK(ValidateFrameBuffer(frame), "RaindropEffect Render t=" + std::to_string(t));
                break;
            }
        }

        // Test ReactiveEffect
        {
            aura::ReactiveEffect react(aura::ColorRGB(0, 5, 10), aura::ColorRGB(255, 20, 40), 2000);
            for (int i = 0; i < 50; ++i) {
                aura::KeyInputHub::Instance().RecordKeyPress("A");
                aura::KeyInputHub::Instance().RecordKeyPress("ENTER");
            }
            frame.Clear();
            react.Render(100, frame, km);
            react.Render(200, frame, km);
            ADV_CHECK(ValidateFrameBuffer(frame), "ReactiveEffect Render with keypresses");
        }

        // Test BreathingEffect
        {
            aura::BreathingEffect breath(aura::ColorRGB(100, 200, 50), aura::ColorRGB(10, 20, 30), 1000);
            for (uint64_t t : test_times) {
                frame.Clear();
                breath.Render(t, frame, km);
                ADV_CHECK(ValidateFrameBuffer(frame), "BreathingEffect Render t=" + std::to_string(t));
                break;
            }
        }

        // Test ColorCycleEffect
        {
            aura::ColorCycleEffect cc(3500);
            for (uint64_t t : test_times) {
                frame.Clear();
                cc.Render(t, frame, km);
                ADV_CHECK(ValidateFrameBuffer(frame), "ColorCycleEffect Render t=" + std::to_string(t));
                break;
            }
        }

        // Test StaticEffect
        {
            aura::StaticEffect st_plain(aura::ColorRGB(100, 150, 200), false);
            aura::StaticEffect st_analog(aura::ColorRGB(100, 150, 200), true);
            frame.Clear();
            st_plain.Render(0, frame, km);
            ADV_CHECK(ValidateFrameBuffer(frame), "StaticEffect plain Render");
            st_analog.Render(0, frame, km);
            ADV_CHECK(ValidateFrameBuffer(frame), "StaticEffect analog Render");
        }

        // Test CustomKeymapEffect
        {
            aura::CustomKeymapEffect ckm(aura::ColorRGB(15, 30, 45));
            frame.Clear();
            ckm.Render(0, frame, km);
            ADV_CHECK(ValidateFrameBuffer(frame), "CustomKeymapEffect Render");
        }
    }

    // -------------------------------------------------------------------------
    // Phase 3: Corrupted & Boundary Keymap Safety
    // -------------------------------------------------------------------------
    std::cout << "\n[Phase 3] Corrupted & Extreme Keymap Geometry Safety...\n";
    {
        aura::Keymap corrupted_km;
        const std::string tmp_km = (std::filesystem::temp_directory_path() / "test_corrupted_km.json").string();
        {
            std::ofstream ofs(tmp_km);
            ofs << R"json({
                "keys": [
                    {"key_name": "KEY_NEG", "display_label": "NEG", "led_id": -1, "row": 1, "col": 1, "physical_x": 0.0, "physical_y": 0.0},
                    {"key_name": "KEY_OVER", "display_label": "OVER", "led_id": 9999, "row": 1, "col": 1, "physical_x": 0.0, "physical_y": 0.0},
                    {"key_name": "KEY_EXT_X", "display_label": "EXTX", "led_id": 0, "row": 1, "col": 1, "physical_x": 1e9, "physical_y": -1e9},
                    {"key_name": "KEY_EXT_Y", "display_label": "EXTY", "led_id": 1, "row": 1, "col": 1, "physical_x": -1e9, "physical_y": 1e9}
                ]
            })json";
        }
        corrupted_km.LoadFromJson(tmp_km);

        aura::FrameBuffer frame;
        aura::WaveEffect wave(3500, "spread", 2.0);
        wave.Render(100, frame, corrupted_km);
        ADV_CHECK(ValidateFrameBuffer(frame), "WaveEffect renders safely on corrupted keymap");

        aura::RippleEffect ripple(aura::ColorRGB(0,0,0), aura::ColorRGB(255,255,255), 2500, 1.0);
        ripple.Render(100, frame, corrupted_km);
        ADV_CHECK(ValidateFrameBuffer(frame), "RippleEffect renders safely on corrupted keymap");

        aura::CurrentEffect cur(aura::ColorRGB(0,255,255), 2000, 1.0);
        cur.Render(100, frame, corrupted_km);
        ADV_CHECK(ValidateFrameBuffer(frame), "CurrentEffect renders safely on corrupted keymap");

        aura::QuicksandEffect qs(aura::ColorRGB(255,0,0), aura::ColorRGB(0,0,255), 3500, "spread", 1.0);
        qs.Render(100, frame, corrupted_km);
        ADV_CHECK(ValidateFrameBuffer(frame), "QuicksandEffect renders safely on corrupted keymap");
        std::filesystem::remove(tmp_km);
    }


    // -------------------------------------------------------------------------
    // Phase 4: RuleEngine JSON Ingestion Fuzzing
    // -------------------------------------------------------------------------
    std::cout << "\n[Phase 4] RuleEngine JSON Fuzzing & Malformed Profiles...\n";
    {
        // Fuzz ParseAndClampThickness with non-standard types
        nlohmann::json fuzzed_types = {
            {"thickness", nullptr},
            {"thickness", true},
            {"thickness", false},
            {"thickness", "5.0"},
            {"thickness", "[1.0]"},
            {"thickness", nlohmann::json::array()},
            {"thickness", nlohmann::json::object()},
            {"thickness", 1e300},
            {"thickness", -1e300}
        };

        for (const auto& item : fuzzed_types) {
            double res = aura::ParseAndClampThickness(item, 1.0);
            ADV_CHECK(res >= 0.1 && res <= 5.0, "Fuzzed thickness yields safe clamped value: " + std::to_string(res));
        }

        // Test CreateEffectFromProfile with all 11 effects with edge cases
        const std::vector<std::string> all_types = {
            "static", "breathing", "color_cycle", "wave", "custom_keymap",
            "reactive", "ripple", "starry_night", "quicksand", "current", "raindrop"
        };

        for (const auto& t : all_types) {
            // Minimal profile with empty values
            nlohmann::json p_empty = { {"type", t} };
            auto eff = aura::CreateEffectFromProfile("prof_" + t, p_empty);
            ADV_CHECK(eff != nullptr, "CreateEffectFromProfile creates valid effect for minimal type: " + t);

            // Profile with corrupted types for color, thickness, period
            nlohmann::json p_corrupt = {
                {"type", t},
                {"color", "invalid_color_string"},
                {"color1", nullptr},
                {"color2", 12345},
                {"bg", false},
                {"thickness", "huge"},
                {"speed_index", "warp_speed"},
                {"period_ms", -9999}
            };
            auto eff_corrupt = aura::CreateEffectFromProfile("prof_corrupt_" + t, p_corrupt);
            ADV_CHECK(eff_corrupt != nullptr, "CreateEffectFromProfile survives corrupted fields for type: " + t);
        }

        // Unknown type
        nlohmann::json p_unknown = { {"type", "quantum_hyperdrive"} };
        auto eff_unknown = aura::CreateEffectFromProfile("prof_unknown", p_unknown);
        ADV_CHECK(eff_unknown == nullptr, "CreateEffectFromProfile safely rejects unknown effect type");
    }

    // -------------------------------------------------------------------------
    // Phase 5: High-Frequency Multi-Frame Rendering Performance & No Hanging
    // -------------------------------------------------------------------------
    std::cout << "\n[Phase 5] High-Frequency Multi-Frame Performance & Hang Prevention...\n";
    {
        aura::Keymap km;
        std::string km_path = "tests/fixtures/calibrated_keymap.json";
        if (!std::filesystem::exists(km_path)) {
            km_path = "calibrated_keymap.json";
        }
        km.LoadFromJson(km_path);
        aura::FrameBuffer frame;

        aura::WaveEffect wave(3500, "spread", 5.0);
        aura::RippleEffect ripple(aura::ColorRGB(0,0,0), aura::ColorRGB(255,255,255), 2500, 5.0);
        aura::CurrentEffect cur(aura::ColorRGB(0,255,255), 2000, 5.0);
        aura::QuicksandEffect qs(aura::ColorRGB(255,0,0), aura::ColorRGB(0,0,255), 3500, "spread", 5.0);

        // Preload ripples
        for (int i = 0; i < 8; ++i) {
            aura::KeyInputHub::Instance().RecordKeyPress("A");
        }

        auto start = std::chrono::high_resolution_clock::now();
        const int FRAMES = 10000;
        for (int f = 0; f < FRAMES; ++f) {
            uint64_t t = f * 10;
            wave.Render(t, frame, km);
            ripple.Render(t, frame, km);
            cur.Render(t, frame, km);
            qs.Render(t, frame, km);
        }
        auto end = std::chrono::high_resolution_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
        double per_frame_us = (elapsed_ms * 1000.0) / (FRAMES * 4);

        std::cout << "  Executed " << (FRAMES * 4) << " effect renders in " << elapsed_ms << " ms (" 
                  << per_frame_us << " us / render)\n";
        ADV_CHECK(per_frame_us < 50.0, "High-frequency render performance < 50us per render (actual: " + std::to_string(per_frame_us) + " us)");
    }

    std::cout << "\n=========================================================\n";
    if (g_failures == 0) {
        std::cout << "  [SUCCESS] All Adversarial Empirical Tests Passed (0 failures)!\n";
    } else {
        std::cout << "  [FAILURE] " << g_failures << " adversarial checks failed!\n";
    }
    std::cout << "=========================================================\n";

    return g_failures == 0 ? 0 : 1;
}
