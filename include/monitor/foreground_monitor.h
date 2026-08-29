#pragma once

#include <windows.h>
#include <string>
#include <functional>
#include <thread>
#include <atomic>

namespace aura {

using ForegroundCallback = std::function<void(const std::string& process_name, HWND hwnd)>;

class ForegroundMonitor {
public:
    ForegroundMonitor();
    ~ForegroundMonitor();

    void SetCallback(ForegroundCallback cb);

    bool Start();
    void Stop();

    std::string GetCurrentProcessName() const;

private:
    static void CALLBACK WinEventProc(
        HWINEVENTHOOK hWinEventHook,
        DWORD event,
        HWND hwnd,
        LONG idObject,
        LONG idChild,
        DWORD dwEventThread,
        DWORD dwmsEventTime
    );

    void MonitorThreadProc();
    static std::string GetProcessNameFromHwnd(HWND hwnd);

    ForegroundCallback callback_;
    std::thread thread_;
    std::atomic<bool> running_;
    DWORD thread_id_;
    HWINEVENTHOOK hook_handle_;

    mutable std::atomic<const char*> cached_proc_name_; // lightweight readout
    std::string current_process_name_;
};

} // namespace aura
