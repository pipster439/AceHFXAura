#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cfgmgr32.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <mutex>
#include "aura/aura_types.h"

namespace aura {

class M605Runtime;
class M605NativeTransport;

constexpr uint16_t ASUS_VID = 0x0B05;
constexpr uint16_t FALCHION_ACE_HFX_PID = 0x1B7E;
constexpr uint16_t LIGHTING_USAGE_PAGE = 0xFF00;
constexpr uint16_t LIGHTING_USAGE = 0x0001;
constexpr uint16_t HID_REPORT_SIZE = 65;
constexpr size_t MAX_LEDS_PER_HID_PACKET = 15;
static_assert(HID_REPORT_SIZE >= 5, "HID report must fit the RGB header");
static_assert(MAX_LEDS_PER_HID_PACKET <= (HID_REPORT_SIZE - 5) / 4,
              "RGB packet capacity must fit inside the HID report");

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

    // Read-only transport guard; writes remain private to the backend/runtime.
    static bool IsSupportedOutputReport(const std::array<uint8_t, HID_REPORT_SIZE>& report);
    static bool IsSupportedOutputReport(const uint8_t* report, size_t length);

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
    friend class M605Runtime;
    friend class M605NativeTransport;
    // Shared across MI_01 handles so a staged magnetic setting and its apply
    // cannot be interleaved with another runtime operation or a lighting frame.
    static std::mutex& DeviceWriteMutex();
    bool SendReport(const std::array<uint8_t, HID_REPORT_SIZE>& report);
    // DeviceWriteMutex held. Only the fixed12 00 query, never arbitrary reads/commands.
    bool QueryBasicInfo(std::array<uint8_t, HID_REPORT_SIZE>& response);
    bool ProbeCurrentM605Transport() const; // read-only; DeviceWriteMutex held
    bool OpenDevice();
    static DWORD CALLBACK OnInterfaceNotification(HCMNOTIFICATION, PVOID context,
        CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA data, DWORD size);

    HANDLE hDevice_ = INVALID_HANDLE_VALUE;
    HANDLE hEvent_ = nullptr;
    bool validated_target_ = false;
    std::wstring device_path_;
    std::string last_error_;
    HCMNOTIFICATION interface_notification_ = nullptr;
    std::atomic<bool> interface_changed_{true};
    std::mutex notification_mutex_;
    std::wstring observed_path_; // callback-only identity, protected independently
};

} // namespace aura
