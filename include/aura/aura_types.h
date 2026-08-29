#pragma once

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace aura {

// CLSID_ClaymoreHal = {AE9DB4C8-4F2A-4756-9B11-2F6D78C61F1A}
inline const GUID CLSID_ClaymoreHal = {
    0xAE9DB4C8, 0x4F2A, 0x4756,
    { 0x9B, 0x11, 0x2F, 0x6D, 0x78, 0xC6, 0x1F, 0x1A }
};

// IID_IAsusAacLedDeviceHal = {F2C8D5B4-3854-4325-8A4F-FD7C5072E3BA}
inline const GUID IID_IAsusAacLedDeviceHal = {
    0xF2C8D5B4, 0x3854, 0x4325,
    { 0x8A, 0x4F, 0xFD, 0x7C, 0x50, 0x72, 0xE3, 0xBA }
};

constexpr size_t TOTAL_LEDS = 128;
constexpr size_t RGB_CHANNELS = 3;
constexpr size_t FRAME_BUFFER_SIZE = TOTAL_LEDS * RGB_CHANNELS; // 384 bytes

// Padded hardware streaming parameters:
// 68 physical keys padded so that no physical key lands on the 15th slot of any 64-byte USB HID report
constexpr size_t HARDWARE_STREAM_KEYS = 72;
constexpr size_t HARDWARE_STREAM_BUFFER_SIZE = HARDWARE_STREAM_KEYS * RGB_CHANNELS; // 216 bytes
constexpr uint8_t DUMMY_PADDING_LED_ID = 0xFF;

// Memory safety structure for CreateLedDevice
// Emulates MSVC std::vector<void*> ABI layout (first, last, end)
struct alignas(void*) FakeVector {
    void** first;
    void** last;
    void** end;
};

// VTable index definitions based on verified binary analysis
constexpr size_t VTABLE_HAL_RELEASE = 2;
constexpr size_t VTABLE_HAL_CREATE_LED_DEVICE = 5;

constexpr size_t VTABLE_DEV_RELEASE = 2;
constexpr size_t VTABLE_DEV_SET_SINGLE = 19; // Set_L_STD_SINGLE_XY
constexpr size_t VTABLE_DEV_INIT_TABLE = 20; // Authentic table initializer


// Function pointer types
using PFN_Access = LONG (__stdcall *)(void* this_ptr);
using PFN_CreateLedDevice = LONG (__stdcall *)(void* this_ptr, FakeVector* vec);
using PFN_SetSingle = LONG (__stdcall *)(void* this_ptr, void* rgb_buffer);
using PFN_Release = ULONG (__stdcall *)(void* this_ptr);

struct ColorRGB {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    constexpr ColorRGB() = default;
    constexpr ColorRGB(uint8_t red, uint8_t green, uint8_t blue)
        : r(red), g(green), b(blue) {}
};

struct FrameBuffer {
    uint8_t buffer[FRAME_BUFFER_SIZE]{0};

    void Clear() {
        std::memset(buffer, 0, FRAME_BUFFER_SIZE);
    }

    void Fill(uint8_t r, uint8_t g, uint8_t b) {
        for (size_t i = 0; i < TOTAL_LEDS; ++i) {
            buffer[i * 3 + 0] = r;
            buffer[i * 3 + 1] = g;
            buffer[i * 3 + 2] = b;
        }
    }

    void SetKey(size_t led_id, uint8_t r, uint8_t g, uint8_t b) {
        if (led_id < TOTAL_LEDS) {
            buffer[led_id * 3 + 0] = r;
            buffer[led_id * 3 + 1] = g;
            buffer[led_id * 3 + 2] = b;
        }
    }

    void SetKey(size_t led_id, const ColorRGB& color) {
        SetKey(led_id, color.r, color.g, color.b);
    }

    const uint8_t* Data() const {
        return buffer;
    }

    uint8_t* Data() {
        return buffer;
    }
};

} // namespace aura
