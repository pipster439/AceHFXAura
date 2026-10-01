#pragma once
#include "aura/hardware/m605_rt_gate.h"
#include <windows.h>
#include <atomic>
#include <functional>
#include <thread>
#include <condition_variable>
#include <mutex>

namespace aura {
// Passive vendor-input collection only. No HID handle, ReadFile/WriteFile,
// feature report, query, M605 command, Profile callback or keyboard hook.
class HardwareRtGateMonitor {
public:
    using Callback = std::function<void(const HardwareRtGateObservation&)>;
    explicit HardwareRtGateMonitor(Callback callback) : callback_(std::move(callback)) {}
    ~HardwareRtGateMonitor() { Stop(); }
    bool Start();
    void Stop();
private:
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    void Run();
    void ReconcileCollection();
    void Receive(HRAWINPUT);
    void Publish(HardwareRtGateState, const char* reason);
    Callback callback_;
    std::thread thread_;
    std::atomic<DWORD> thread_id_{0};
    std::mutex startup_mutex_;
    std::condition_variable startup_ready_;
    HANDLE device_ = nullptr; // Raw Input identity only; never a file handle
    HardwareRtGateObservation observation_;
};
}
