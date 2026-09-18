#include "aura/native_hid_backend.h"
#include "aura/aura_adapter.h"
#include "aura/aura_types.h"
#include "aura/keymap.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <array>
#include <string>
#include <thread>
#include <chrono>
#include <cmath>
#include <windows.h>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << #cond << ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

// 1. MI_01 endpoint matching test
bool TestEndpointMatching() {
    std::cout << "[Test 1] MI_01 Endpoint Matching..." << std::endl;

    // Positive match
    TEST_ASSERT(aura::NativeHidBackend::IsTargetLightingEndpoint(
        0x0B05, 0x1B7E, 0xFF00, 0x0001, 65,
        L"\\\\?\\hid#vid_0b05&pid_1b7e&mi_01#8&1f730e0c&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}"
    ), "Exact target endpoint must match");

    // Case-insensitive path matching
    TEST_ASSERT(aura::NativeHidBackend::IsTargetLightingEndpoint(
        0x0B05, 0x1B7E, 0xFF00, 0x0001, 65,
        L"\\\\?\\HID#VID_0B05&PID_1B7E&MI_01#8&1F730E0C&0&0000#{4D1E55B2-F16F-11CF-88CB-001111000030}"
    ), "Uppercase device path must match");

    // Negative matches: wrong VID
    TEST_ASSERT(!aura::NativeHidBackend::IsTargetLightingEndpoint(
        0x0B06, 0x1B7E, 0xFF00, 0x0001, 65,
        L"\\\\?\\hid#vid_0b06&pid_1b7e&mi_01#..."
    ), "Different VID must be rejected");

    // Negative match: wrong PID
    TEST_ASSERT(!aura::NativeHidBackend::IsTargetLightingEndpoint(
        0x0B05, 0x1B7F, 0xFF00, 0x0001, 65,
        L"\\\\?\\hid#vid_0b05&pid_1b7f&mi_01#..."
    ), "Different PID must be rejected");

    // Negative match: wrong UsagePage
    TEST_ASSERT(!aura::NativeHidBackend::IsTargetLightingEndpoint(
        0x0B05, 0x1B7E, 0xFF01, 0x0001, 65,
        L"\\\\?\\hid#vid_0b05&pid_1b7e&mi_01#..."
    ), "Different UsagePage must be rejected");

    // Negative match: wrong Usage
    TEST_ASSERT(!aura::NativeHidBackend::IsTargetLightingEndpoint(
        0x0B05, 0x1B7E, 0xFF00, 0x0002, 65,
        L"\\\\?\\hid#vid_0b05&pid_1b7e&mi_01#..."
    ), "Different Usage must be rejected");

    // Negative match: wrong OutputReportByteLength
    TEST_ASSERT(!aura::NativeHidBackend::IsTargetLightingEndpoint(
        0x0B05, 0x1B7E, 0xFF00, 0x0001, 64,
        L"\\\\?\\hid#vid_0b05&pid_1b7e&mi_01#..."
    ), "Different report byte length must be rejected");

    // Negative match: wrong interface (e.g. MI_00, MI_02, MI_03, MI_04)
    TEST_ASSERT(!aura::NativeHidBackend::IsTargetLightingEndpoint(
        0x0B05, 0x1B7E, 0xFF00, 0x0001, 65,
        L"\\\\?\\hid#vid_0b05&pid_1b7e&mi_00#..."
    ), "MI_00 must be rejected");

    TEST_ASSERT(!aura::NativeHidBackend::IsTargetLightingEndpoint(
        0x0B05, 0x1B7E, 0xFF00, 0x0001, 65,
        L"\\\\?\\hid#vid_0b05&pid_1b7e&mi_04#..."
    ), "MI_04 must be rejected");

    std::cout << "  [PASS] Endpoint matching verified." << std::endl;
    return true;
}

// 2. 65-byte packet builder, report ID offset, C0 81 header, little-endian count, RGB layout
bool TestPacketBuilder() {
    std::cout << "[Test 2] Packet Builder & Layout..." << std::endl;

    // Single LED packet
    uint8_t indices_1[1] = { 11 }; // 'A' key
    uint8_t colors_1[3] = { 255, 128, 64 };
    auto r1 = aura::NativeHidBackend::BuildReport(indices_1, colors_1, 1);

    TEST_ASSERT(r1.size() == 65, "Report must be exactly 65 bytes");
    TEST_ASSERT(r1[0] == 0x00, "Report ID must be 0x00 at byte 0");
    TEST_ASSERT(r1[1] == 0xC0, "Command header byte 0 must be 0xC0 at byte 1");
    TEST_ASSERT(r1[2] == 0x81, "Command header byte 1 must be 0x81 at byte 2");
    TEST_ASSERT(r1[3] == 0x01, "Count low byte must be 1 at byte 3");
    TEST_ASSERT(r1[4] == 0x00, "Count high byte must be 0 at byte 4");
    TEST_ASSERT(r1[5] == 11, "Entry 0 LED ID must be 11 at byte 5");
    TEST_ASSERT(r1[6] == 255, "Entry 0 Red must be 255 at byte 6");
    TEST_ASSERT(r1[7] == 128, "Entry 0 Green must be 128 at byte 7");
    TEST_ASSERT(r1[8] == 64, "Entry 0 Blue must be 64 at byte 8");

    // Trailing bytes must be padded with 0
    for (size_t i = 9; i < 65; ++i) {
        TEST_ASSERT(r1[i] == 0x00, "Trailing bytes must be 0x00");
    }

    // 15 LEDs packet (full packet)
    uint8_t indices_15[15];
    uint8_t colors_15[15 * 3];
    for (size_t i = 0; i < 15; ++i) {
        indices_15[i] = static_cast<uint8_t>(i + 1);
        colors_15[i * 3 + 0] = static_cast<uint8_t>(10 + i);
        colors_15[i * 3 + 1] = static_cast<uint8_t>(20 + i);
        colors_15[i * 3 + 2] = static_cast<uint8_t>(30 + i);
    }
    auto r15 = aura::NativeHidBackend::BuildReport(indices_15, colors_15, 15);

    TEST_ASSERT(r15[0] == 0x00, "Report ID must be 0x00");
    TEST_ASSERT(r15[1] == 0xC0 && r15[2] == 0x81, "Header must be C0 81");
    TEST_ASSERT(r15[3] == 15, "Count low byte must be 15");
    TEST_ASSERT(r15[4] == 0, "Count high byte must be 0");

    // Check last entry (Slot 14, bytes 61..64)
    TEST_ASSERT(r15[5 + 14 * 4 + 0] == 15, "Slot 14 LED ID must be 15");
    TEST_ASSERT(r15[5 + 14 * 4 + 1] == 10 + 14, "Slot 14 Red");
    TEST_ASSERT(r15[5 + 14 * 4 + 2] == 20 + 14, "Slot 14 Green");
    TEST_ASSERT(r15[5 + 14 * 4 + 3] == 30 + 14, "Slot 14 Blue (Byte 64 / Payload Byte 63)");

    std::cout << "  [PASS] Packet builder and layout verified." << std::endl;
    return true;
}

// 3. Byte63 workaround and 68-key padded table verification
bool TestByte63PaddingAndHardwareTable() {
    std::cout << "[Test 3] Byte 63 Padding & Hardware Table..." << std::endl;

    aura::Keymap keymap;
    bool loaded = keymap.LoadFromJson("calibrated_keymap.json");
    TEST_ASSERT(loaded, "Must load calibrated_keymap.json");

    std::vector<uint8_t> table = aura::AuraAdapter::GeneratePaddedHardwareTable(&keymap);

    // 68 physical keys must produce exactly 72 entries (68 + 4 padding slots)
    TEST_ASSERT(table.size() == 72, "Padded table for 68 keys must have 72 entries (got: " + std::to_string(table.size()) + ")");

    // Workaround invariant: every slot where index % 15 == 14 must be DUMMY_PADDING_LED_ID (0xFF)
    for (size_t i = 0; i < table.size(); ++i) {
        if (i % 15 == 14) {
            TEST_ASSERT(table[i] == aura::DUMMY_PADDING_LED_ID,
                        "Slot " + std::to_string(i) + " must be DUMMY_PADDING_LED_ID (0xFF)");
        } else {
            TEST_ASSERT(table[i] != aura::DUMMY_PADDING_LED_ID,
                        "Slot " + std::to_string(i) + " should NOT be padding");
        }
    }

    // Verify exactly 4 padding slots: 14, 29, 44, 59
    TEST_ASSERT(table[14] == 0xFF, "Slot 14 must be 0xFF");
    TEST_ASSERT(table[29] == 0xFF, "Slot 29 must be 0xFF");
    TEST_ASSERT(table[44] == 0xFF, "Slot 44 must be 0xFF");
    TEST_ASSERT(table[59] == 0xFF, "Slot 59 must be 0xFF");

    // Total chunks for 72 entries with 15 LEDs per packet:
    // Packet 0: entries 0..14 (15 entries)
    // Packet 1: entries 15..29 (15 entries)
    // Packet 2: entries 30..44 (15 entries)
    // Packet 3: entries 45..59 (15 entries)
    // Packet 4: entries 60..71 (12 entries)
    // Total: 5 packets
    size_t packet_count = 0;
    for (size_t chunkStart = 0; chunkStart < table.size(); chunkStart += 15) {
        packet_count++;
    }
    TEST_ASSERT(packet_count == 5, "Multi-packet stream must chunk into exactly 5 packets");

    std::cout << "  [PASS] Byte 63 padding and 68-key table verified." << std::endl;
    return true;
}

// 4. Backend config parsing and string conversions
bool TestBackendConfigParse() {
    std::cout << "[Test 4] Backend Config Parse & Strict TryParse..." << std::endl;

    aura::HardwareBackend b = aura::HardwareBackend::Auto;

    // Strict TryParseHardwareBackend: valid values
    TEST_ASSERT(aura::TryParseHardwareBackend("auto", b) && b == aura::HardwareBackend::Auto, "auto parse");
    TEST_ASSERT(aura::TryParseHardwareBackend("Auto", b) && b == aura::HardwareBackend::Auto, "Auto parse");
    TEST_ASSERT(aura::TryParseHardwareBackend("AUTO", b) && b == aura::HardwareBackend::Auto, "AUTO parse");

    TEST_ASSERT(aura::TryParseHardwareBackend("native_hid", b) && b == aura::HardwareBackend::NativeHid, "native_hid parse");
    TEST_ASSERT(aura::TryParseHardwareBackend("NATIVE_HID", b) && b == aura::HardwareBackend::NativeHid, "NATIVE_HID parse");
    TEST_ASSERT(aura::TryParseHardwareBackend("native", b) && b == aura::HardwareBackend::NativeHid, "native parse");
    TEST_ASSERT(aura::TryParseHardwareBackend("Native", b) && b == aura::HardwareBackend::NativeHid, "Native parse");
    TEST_ASSERT(aura::TryParseHardwareBackend("hid", b) && b == aura::HardwareBackend::NativeHid, "hid parse");
    TEST_ASSERT(aura::TryParseHardwareBackend("HID", b) && b == aura::HardwareBackend::NativeHid, "HID parse");

    TEST_ASSERT(aura::TryParseHardwareBackend("legacy_hal", b) && b == aura::HardwareBackend::LegacyHal, "legacy_hal parse");
    TEST_ASSERT(aura::TryParseHardwareBackend("LEGACY_HAL", b) && b == aura::HardwareBackend::LegacyHal, "LEGACY_HAL parse");
    TEST_ASSERT(aura::TryParseHardwareBackend("legacy", b) && b == aura::HardwareBackend::LegacyHal, "legacy parse");
    TEST_ASSERT(aura::TryParseHardwareBackend("hal", b) && b == aura::HardwareBackend::LegacyHal, "hal parse");
    TEST_ASSERT(aura::TryParseHardwareBackend("HAL", b) && b == aura::HardwareBackend::LegacyHal, "HAL parse");

    // Strict TryParseHardwareBackend: invalid values (must return false)
    TEST_ASSERT(!aura::TryParseHardwareBackend("native_hd", b), "typo native_hd must fail");
    TEST_ASSERT(!aura::TryParseHardwareBackend("asdf", b), "random string must fail");
    TEST_ASSERT(!aura::TryParseHardwareBackend("", b), "empty string must fail");
    TEST_ASSERT(!aura::TryParseHardwareBackend("123", b), "numeric string must fail");
    TEST_ASSERT(!aura::TryParseHardwareBackend("autoo", b), "autoo must fail");
    TEST_ASSERT(!aura::TryParseHardwareBackend("legacy_ha", b), "prefix typo must fail");

    // StringToHardwareBackend
    TEST_ASSERT(aura::StringToHardwareBackend("native_hid") == aura::HardwareBackend::NativeHid, "StringToHardwareBackend valid");
    TEST_ASSERT(aura::StringToHardwareBackend("unknown_val") == aura::HardwareBackend::Auto, "StringToHardwareBackend fallback to auto");

    // HardwareBackendToString
    TEST_ASSERT(std::string(aura::HardwareBackendToString(aura::HardwareBackend::Auto)) == "auto", "to string auto");
    TEST_ASSERT(std::string(aura::HardwareBackendToString(aura::HardwareBackend::NativeHid)) == "native_hid", "to string native_hid");
    TEST_ASSERT(std::string(aura::HardwareBackendToString(aura::HardwareBackend::LegacyHal)) == "legacy_hal", "to string legacy_hal");

    std::cout << "  [PASS] Backend config parse verified." << std::endl;
    return true;
}

// 5. Backend resolution precedence (CLI > config > default)
bool TestResolvedBackendPrecedence() {
    std::cout << "[Test 5] Resolved Backend Precedence..." << std::endl;

    // CLI overrides config
    TEST_ASSERT(aura::ResolveHardwareBackend(true, aura::HardwareBackend::NativeHid, aura::HardwareBackend::LegacyHal) == aura::HardwareBackend::NativeHid,
                "CLI native_hid must override config legacy_hal");
    TEST_ASSERT(aura::ResolveHardwareBackend(true, aura::HardwareBackend::LegacyHal, aura::HardwareBackend::NativeHid) == aura::HardwareBackend::LegacyHal,
                "CLI legacy_hal must override config native_hid");

    // Config used when no CLI override
    TEST_ASSERT(aura::ResolveHardwareBackend(false, aura::HardwareBackend::Auto, aura::HardwareBackend::NativeHid) == aura::HardwareBackend::NativeHid,
                "Config native_hid used when no CLI");
    TEST_ASSERT(aura::ResolveHardwareBackend(false, aura::HardwareBackend::Auto, aura::HardwareBackend::LegacyHal) == aura::HardwareBackend::LegacyHal,
                "Config legacy_hal used when no CLI");
    TEST_ASSERT(aura::ResolveHardwareBackend(false, aura::HardwareBackend::Auto, aura::HardwareBackend::Auto) == aura::HardwareBackend::Auto,
                "Default auto when neither specified");

    std::cout << "  [PASS] Resolved backend precedence verified." << std::endl;
    return true;
}

// 5. Adapter backend selection semantics
bool TestAdapterBackendSelection() {
    std::cout << "[Test 5] AuraAdapter Backend Selection..." << std::endl;

    // Default configuration must be Auto
    aura::AuraAdapter adapter_default;
    TEST_ASSERT(adapter_default.GetConfiguredBackend() == aura::HardwareBackend::Auto, "Default backend must be Auto");

    // Explicit native_hid
    aura::AuraAdapter adapter_native(false, aura::HardwareBackend::NativeHid);
    TEST_ASSERT(adapter_native.GetConfiguredBackend() == aura::HardwareBackend::NativeHid, "Configured backend must be NativeHid");

    // Explicit legacy_hal
    aura::AuraAdapter adapter_hal(false, aura::HardwareBackend::LegacyHal);
    TEST_ASSERT(adapter_hal.GetConfiguredBackend() == aura::HardwareBackend::LegacyHal, "Configured backend must be LegacyHal");

    // Dry-run mode
    aura::AuraAdapter adapter_dry(true);
    TEST_ASSERT(adapter_dry.IsDryRun(), "Dry run must be true");
    TEST_ASSERT(adapter_dry.Initialize(nullptr), "Dry run initialize must succeed");
    TEST_ASSERT(adapter_dry.IsConnected(), "Dry run must be connected");
    TEST_ASSERT(adapter_dry.GetActiveBackendName() == "dry_run", "Dry run active backend name must be 'dry_run'");

    aura::FrameBuffer fb;
    fb.Fill(255, 0, 0);
    TEST_ASSERT(adapter_dry.PushFrame(fb), "Dry run PushFrame must succeed");
    TEST_ASSERT(adapter_dry.ForceReset(), "Dry run ForceReset must succeed");

    adapter_dry.Shutdown();
    TEST_ASSERT(!adapter_dry.IsConnected(), "After shutdown must be disconnected");

    std::cout << "  [PASS] Adapter backend selection verified." << std::endl;
    return true;
}

// 6. Native backend error propagation when disconnected
bool TestNativeBackendErrorPropagation() {
    std::cout << "[Test 6] Native Backend Error Propagation..." << std::endl;

    aura::NativeHidBackend backend;
    TEST_ASSERT(!backend.IsConnected(), "Fresh backend must be disconnected");

    aura::FrameBuffer fb;
    std::vector<uint8_t> table = { 1, 2, 3 };
    TEST_ASSERT(!backend.PushFrame(fb, table), "PushFrame on disconnected backend must fail");

    std::cout << "  [PASS] Error propagation verified." << std::endl;
    return true;
}

// 7. Live Physical Hardware Acceptance Test
bool TestLiveHardwareAcceptance() {
    std::cout << "\n=========================================================" << std::endl;
    std::cout << " [Acceptance] Live Physical Hardware Test (ROG Falchion Ace HFX)" << std::endl;
    std::cout << "=========================================================" << std::endl;

    // Step 0: Ensure AacKbHal_x64.dll is NOT loaded in this process
    TEST_ASSERT(GetModuleHandleW(L"AacKbHal_x64.dll") == nullptr, "AacKbHal_x64.dll must NOT be loaded initially");
    std::cout << "  [PASS] Pre-check: AacKbHal_x64.dll is not loaded." << std::endl;

    // Step 1: Load calibrated keymap
    aura::Keymap keymap;
    TEST_ASSERT(keymap.LoadFromJson("calibrated_keymap.json"), "Must load calibrated_keymap.json");

    // Step 2: Initialize AuraAdapter with HardwareBackend::NativeHid
    aura::AuraAdapter adapter(false, aura::HardwareBackend::NativeHid);
    bool init_ok = adapter.Initialize(&keymap);
    TEST_ASSERT(init_ok, "Native HID adapter Initialize must succeed");
    TEST_ASSERT(adapter.IsConnected(), "Adapter must report Connected");
    TEST_ASSERT(adapter.GetActiveBackend() == aura::HardwareBackend::NativeHid, "Active backend must be NativeHid");
    std::cout << "  [PASS] Device connected via Native HID (" << adapter.GetDevicePath() << ")" << std::endl;

    // Step 3: Verify AacKbHal_x64.dll was NOT loaded by adapter init
    TEST_ASSERT(GetModuleHandleW(L"AacKbHal_x64.dll") == nullptr, "AacKbHal_x64.dll must NOT be loaded during Native HID init");
    std::cout << "  [PASS] Module audit: AacKbHal_x64.dll is NOT loaded into process." << std::endl;

    // Step 4: All Black
    std::cout << "  [Step 4] Setting All Black (0, 0, 0)..." << std::endl;
    aura::FrameBuffer fb;
    fb.Fill(0, 0, 0);
    TEST_ASSERT(adapter.PushFrame(fb), "PushFrame All Black must succeed");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Step 5: Pure Red
    std::cout << "  [Step 5] Setting Pure Red (255, 0, 0)..." << std::endl;
    fb.Fill(255, 0, 0);
    TEST_ASSERT(adapter.PushFrame(fb), "PushFrame Pure Red must succeed");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Step 6: Pure Blue (verifying Byte63 / slot 14 padding)
    std::cout << "  [Step 6] Setting Pure Blue (0, 0, 255)..." << std::endl;
    fb.Fill(0, 0, 255);
    TEST_ASSERT(adapter.PushFrame(fb), "PushFrame Pure Blue must succeed");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Step 7: Single Key Test: Esc (green), A (magenta), Space (yellow)
    std::cout << "  [Step 7] Single Key Test: Esc=Green, A=Magenta, Space=Yellow..." << std::endl;
    fb.Fill(0, 0, 0);
    int esc_id = -1, a_id = -1, space_id = -1;
    TEST_ASSERT(keymap.FindLedId("Escape", esc_id) || keymap.FindLedId("Esc", esc_id), "Find Esc key");
    TEST_ASSERT(keymap.FindLedId("A", a_id), "Find A key");
    TEST_ASSERT(keymap.FindLedId("Space", space_id), "Find Space key");
    fb.SetKey(static_cast<size_t>(esc_id), 0, 255, 0);
    fb.SetKey(static_cast<size_t>(a_id), 255, 0, 255);
    fb.SetKey(static_cast<size_t>(space_id), 255, 255, 0);
    TEST_ASSERT(adapter.PushFrame(fb), "PushFrame Single Key must succeed");
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    // Step 8: Dynamic 30-second continuous push frame (25 FPS = 750 frames)
    std::cout << "  [Step 8] Streaming dynamic rainbow effect for 30s at 25 FPS (750 frames)..." << std::endl;
    const size_t total_frames = 750;
    size_t pushed_frames = 0;
    auto start_time = std::chrono::steady_clock::now();
    for (size_t f = 0; f < total_frames; ++f) {
        for (size_t k = 0; k < aura::TOTAL_LEDS; ++k) {
            double hue = std::fmod((f * 4.0) + (k * 5.0), 360.0);
            double c = 1.0;
            double x = c * (1.0 - std::fabs(std::fmod(hue / 60.0, 2.0) - 1.0));
            double r = 0, g = 0, b = 0;
            if (hue < 60) { r = c; g = x; b = 0; }
            else if (hue < 120) { r = x; g = c; b = 0; }
            else if (hue < 180) { r = 0; g = c; b = x; }
            else if (hue < 240) { r = 0; g = x; b = c; }
            else if (hue < 300) { r = x; g = 0; b = c; }
            else { r = c; g = 0; b = x; }
            fb.SetKey(k, static_cast<uint8_t>(r * 255), static_cast<uint8_t>(g * 255), static_cast<uint8_t>(b * 255));
        }
        if (!adapter.PushFrame(fb)) {
            std::cerr << "[FAIL] PushFrame failed at frame " << f << std::endl;
            return false;
        }
        pushed_frames++;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count();
    std::cout << "  [PASS] Streamed " << pushed_frames << " frames in " << elapsed_ms << " ms with 0 errors/drops." << std::endl;

    // Step 9: Verify AacKbHal_x64.dll was NEVER loaded
    TEST_ASSERT(GetModuleHandleW(L"AacKbHal_x64.dll") == nullptr, "AacKbHal_x64.dll must still NOT be loaded after streaming");
    std::cout << "  [PASS] Post-check: AacKbHal_x64.dll was never loaded." << std::endl;

    // Step 10: Reset to black & shutdown
    std::cout << "  [Step 10] Resetting hardware to blackout and shutting down..." << std::endl;
    adapter.ForceReset();
    adapter.Shutdown();
    TEST_ASSERT(!adapter.IsConnected(), "Adapter must be disconnected after shutdown");

    std::cout << "  [PASS] Live physical hardware acceptance test completely passed!" << std::endl;
    return true;
}

int main(int argc, char* argv[]) {
    bool run_live_hardware = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--live-hardware" || arg == "-l") {
            run_live_hardware = true;
        }
    }

    std::cout << "=========================================================" << std::endl;
    std::cout << " AceHFXAura Native Win32 HID Backend Unit Tests" << std::endl;
    std::cout << "=========================================================" << std::endl;

    bool all_passed = true;
    all_passed &= TestEndpointMatching();
    all_passed &= TestPacketBuilder();
    all_passed &= TestByte63PaddingAndHardwareTable();
    all_passed &= TestBackendConfigParse();
    all_passed &= TestResolvedBackendPrecedence();
    all_passed &= TestAdapterBackendSelection();
    all_passed &= TestNativeBackendErrorPropagation();

    if (run_live_hardware) {
        all_passed &= TestLiveHardwareAcceptance();
    }

    std::cout << "=========================================================" << std::endl;
    if (all_passed) {
        std::cout << "[ALL TESTS PASSED] Native HID backend tests successful!" << std::endl;
        return 0;
    } else {
        std::cerr << "[TEST FAILURE] One or more tests failed." << std::endl;
        return 1;
    }
}

