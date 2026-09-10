#pragma once

#include "aura/aura_types.h"
#include "aura/keymap.h"
#include <string>
#include <memory>
#include <chrono>
#include <windows.h>
#include <objbase.h>

namespace aura {

enum class AdapterState {
    Uninitialized,
    Connecting,
    Connected,
    Disconnected,
    BackoffWait,
    Error
};

class AuraAdapter {
public:
    explicit AuraAdapter(bool dry_run = false);
    ~AuraAdapter();

    // Must be called on the dedicated hardware stream thread
    bool Initialize(const Keymap* keymap = nullptr);

    // Push a frame to the hardware (maps frame LEDs to padded hardware table)
    bool PushFrame(const FrameBuffer& frame);

    // Forces a reset / blackout to clear any dirty state from prior crashes
    bool ForceReset();

    // Reconnection tick: called by stream thread if disconnected
    bool CheckReconnect();

    // Shutdown and release all COM resources
    void Shutdown();

    // Memory-safety stress test: repeatedly init and release HAL
    bool RunInitStressTest(size_t iterations);

    AdapterState GetState() const { return state_; }
    bool IsConnected() const { return state_ == AdapterState::Connected; }
    bool IsDryRun() const { return dry_run_; }

private:
    bool ConnectHardwareInternal();
    void ReleaseHardwareInternal();
    LONG CallSetSingleSafe(void* pDev, void* buffer);
    // 构建隔离寻址表。返回 false 表示表长超过 MAX_HARDWARE_STREAM_KEYS 上界
    // （调用方必须据此失败，绝不能用可能越界的表去驱动硬件）。
    bool BuildPaddedHardwareTable(const Keymap* keymap);

    bool dry_run_ = false;
    AdapterState state_ = AdapterState::Uninitialized;
    // 契约：COM 生命周期由调用方（如 main 中的 ComScope）管理，AuraAdapter 不自行初始化或反初始化 COM

    HMODULE hHalMod_ = nullptr;
    IClassFactory* pFactory_ = nullptr;
    void* pHal_ = nullptr;
    void* pDev_ = nullptr;
    PFN_SetSingle fn_set_single_ = nullptr;


    const Keymap* keymap_ = nullptr;
    std::vector<uint8_t> padded_hardware_table_;
    uint8_t stream_buffer_[HARDWARE_STREAM_BUFFER_SIZE]{0};

    std::chrono::steady_clock::time_point last_reconnect_attempt_;
    size_t failed_push_count_;
    uint64_t dry_run_frame_count_ = 0;
};

} // namespace aura
