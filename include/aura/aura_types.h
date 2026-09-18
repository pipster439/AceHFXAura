#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <string>

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

enum class HardwareBackend {
    Auto,
    NativeHid,
    LegacyHal
};

inline const char* HardwareBackendToString(HardwareBackend b) {
    switch (b) {
        case HardwareBackend::Auto: return "auto";
        case HardwareBackend::NativeHid: return "native_hid";
        case HardwareBackend::LegacyHal: return "legacy_hal";
        default: return "unknown";
    }
}

inline HardwareBackend StringToHardwareBackend(const std::string& s) {
    if (s == "native_hid" || s == "native" || s == "hid") return HardwareBackend::NativeHid;
    if (s == "legacy_hal" || s == "hal" || s == "legacy") return HardwareBackend::LegacyHal;
    return HardwareBackend::Auto;
}

constexpr size_t TOTAL_LEDS = 128;
constexpr size_t RGB_CHANNELS = 3;
constexpr size_t FRAME_BUFFER_SIZE = TOTAL_LEDS * RGB_CHANNELS; // 384 bytes

// Padded hardware streaming parameters:
// 68 physical keys padded so that no physical key lands on the 15th slot of any 64-byte USB HID report.
// HARDWARE_STREAM_KEYS 是【隔离规则】的语义常量：每 15 槽最多放 14 个物理键。
// 68 键时按规则算出的实际表长恰为 72（+4 个 0xFF 隔离槽），与之相等。
constexpr size_t HARDWARE_STREAM_KEYS = 72;
constexpr uint8_t DUMMY_PADDING_LED_ID = 0xFF;

// 缓冲区容量必须与"隔离规则"【解耦】：容量只是本地存储上界，而实际发送长度由设备对象的
// +0x6C 字段（= 表长）决定，两者不是同一个量。
// 表长推导（按 size % 15 == 14 时先插一个隔离槽的规则逐点计算，已用脚本验证）：
//    68 键 ->  72  (+4 隔离槽；恰好占满旧的 216 字节缓冲，零余量)
//    69 键 ->  73  (会越界 3 字节 —— 这正是原实现潜伏的隐患)
//   127 键 -> 136
//   128 键 -> 137  (+9 隔离槽，411 字节；TOTAL_LEDS 场景下的最坏情况)
// 故上界取 144（余量 7 槽）。BuildPaddedHardwareTable / PushFrame / 驱动表写入
// 三处都会以此上界做运行期校验，超限即报错拒绝，而不是静默越界。
constexpr size_t MAX_HARDWARE_STREAM_KEYS = 144;
constexpr size_t HARDWARE_STREAM_BUFFER_SIZE = MAX_HARDWARE_STREAM_KEYS * RGB_CHANNELS; // 432 bytes

// 按隔离规则计算 N 个物理键时的表长（与 BuildPaddedHardwareTable 的插入规则严格一致）。
// 编译期即可求值，用于把"容量是否足够"从运行期猜测变成编译期证明。
constexpr size_t PaddedTableLength(size_t physical_keys) {
    size_t entries = 0;
    for (size_t i = 0; i < physical_keys; ++i) {
        if (entries % 15 == 14) {
            ++entries;   // 插入 0xFF 隔离槽
        }
        ++entries;
    }
    return entries;
}

// 关键不变量：BuildPaddedHardwareTable 只收 led_id ∈ [0, TOTAL_LEDS) 且去重，
// 因此表长的最坏情况就是 PaddedTableLength(TOTAL_LEDS) = 137（已由脚本逐点验证）。
// 这个静态断言把"缓冲区必定够用"钉死在编译期——若将来有人改小容量或改大 TOTAL_LEDS，
// 会直接编译失败，而不是留到运行期越界。
static_assert(PaddedTableLength(TOTAL_LEDS) <= MAX_HARDWARE_STREAM_KEYS,
              "HARDWARE_STREAM_BUFFER_SIZE 容量不足：隔离规则下的最坏表长已超出上界");
static_assert(PaddedTableLength(68) == 72,
              "隔离规则或 HARDWARE_STREAM_KEYS 被改动，请同步复核 USB 64 字节边界隔离假设");

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

    void Fill(const ColorRGB& color) {
        Fill(color.r, color.g, color.b);
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
