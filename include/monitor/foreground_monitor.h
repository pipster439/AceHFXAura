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
    using ForegroundProvider = std::function<HWND()>;
    using ProcessResolver = std::function<std::string(HWND)>;

    ForegroundMonitor(ForegroundProvider foreground_provider = {}, ProcessResolver process_resolver = {});
    ~ForegroundMonitor();

    void SetCallback(ForegroundCallback cb);

    bool Start();
    void Stop();

    std::string GetCurrentProcessName() const;

private:
    friend class ForegroundMonitorTestPeer;

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
    enum class ForegroundSource { Event, Reconciled };
    // Both WinEvent and periodic reconciliation use this resolver and publisher.
    std::string ResolveForeground(HWND hwnd) const;
    void PublishForeground(const std::string& process_name, HWND hwnd, ForegroundSource source);
    void ObserveForegroundEvent(HWND hwnd);
    void ReconcileForeground();
    static std::string GetProcessNameFromHwnd(HWND hwnd);

    ForegroundProvider foreground_provider_;
    ProcessResolver process_resolver_;

    ForegroundCallback callback_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<DWORD> thread_id_{0};
    HWINEVENTHOOK hook_handle_{nullptr};

    // current_process_name_ 由监控线程（WinEventProc / MonitorThreadProc）写入、
    // 由主线程经 GetCurrentProcessName() 读取，故必须加锁保护。
    // （此前这里声明过一个从未被使用的 atomic<const char*> 缓存字段，已删除。）
    mutable std::mutex name_mutex_;
    std::string current_process_name_;
    unsigned int unresolved_reconciliations_{0};
    bool invalid_event_logged_{false};
};

} // namespace aura
