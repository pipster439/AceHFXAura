#pragma once

#include <windows.h>
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

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

    // current_process_name_ 由监控线程（WinEventProc / MonitorThreadProc）写入、
    // 由主线程经 GetCurrentProcessName() 读取，故必须加锁保护。
    // （此前这里声明过一个从未被使用的 atomic<const char*> 缓存字段，已删除。）
    mutable std::mutex name_mutex_;
    std::string current_process_name_;
};

} // namespace aura
