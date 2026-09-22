#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include "aura/aura_types.h"

namespace aura {

constexpr uint16_t ASUS_VID = 0x0B05;
constexpr uint16_t FALCHION_ACE_HFX_PID = 0x1B7E;
constexpr uint16_t LIGHTING_USAGE_PAGE = 0xFF00;
constexpr uint16_t LIGHTING_USAGE = 0x0001;
constexpr uint16_t HID_REPORT_SIZE = 65;
constexpr size_t MAX_LEDS_PER_HID_PACKET = 15;

struct NativeHidDeviceInfo {
    std::wstring path;
    uint16_t vid = 0;
    uint16_t pid = 0;
    uint16_t version = 0;
    uint16_t usagePage = 0;
    uint16_t usage = 0;
    uint16_t inputReportByteLength = 0;
    uint16_t outputReportByteLength = 0;
    uint16_t featureReportByteLength = 0;
    bool isMatch = false;
};

class NativeHidBackend {
public:
    NativeHidBackend();
    ~NativeHidBackend();

    // Enumerate endpoints and connect to the Falchion Ace HFX lighting interface (MI_01)
    bool Connect();

    // Close device handle and release resources
    void Disconnect();

    // Connection state
    bool IsConnected() const;

    // Send a frame to hardware mapping across padded hardware table
    bool PushFrame(const FrameBuffer& frame, const std::vector<uint8_t>& padded_table);

    // Build a 65-byte Windows HID output report (Report ID 0x00 + 0xC0 0x81 header + count + up to 15 LEDs)
    // Exposed statically for unit testing
    static std::array<uint8_t, HID_REPORT_SIZE> BuildReport(
        const uint8_t* indices,
        const uint8_t* colors_rgb,
        size_t count);

    // Filter helper: check if device info matches target lighting endpoint
    static bool IsTargetLightingEndpoint(
        uint16_t vid, uint16_t pid,
        uint16_t usagePage, uint16_t usage,
        uint16_t outputReportLen,
        const std::wstring& devPath);

    std::string GetDevicePath() const;
    std::wstring GetDevicePathW() const { return device_path_; }
    std::string GetLastError() const { return last_error_; }

private:
    bool SendReport(const std::array<uint8_t, HID_REPORT_SIZE>& report);

    HANDLE hDevice_ = INVALID_HANDLE_VALUE;
    HANDLE hEvent_ = nullptr;
    std::wstring device_path_;
    std::string last_error_;
};

} // namespace aura
