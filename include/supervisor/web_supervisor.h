#pragma once

#include <windows.h>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <filesystem>

namespace aura {

class WebUiSupervisor {
public:
    WebUiSupervisor();
    ~WebUiSupervisor();

    // 禁用拷贝与移动
    WebUiSupervisor(const WebUiSupervisor&) = delete;
    WebUiSupervisor& operator=(const WebUiSupervisor&) = delete;

    // 启动监护器后台工线程（主线程零阻塞）
    void StartSupervisor(const std::filesystem::path& config_path = "config.json", int port = 19898,
        const std::filesystem::path& runtime_root = {}, const std::filesystem::path& web_root = {},
        const std::filesystem::path& sdk_include = {}, const std::string& instance_id = "");

    // 停止监护器并安全回收子进程与工线程
    void Shutdown();

    // 设置是否抑制（非阻塞原子通知：前台切换时调用）
    void SetSuppressed(bool suppressed);

    // 查询当前运行状态
    bool IsRunning() const { return is_running_.load(std::memory_order_acquire); }
    bool IsSuppressed() const { return target_suppressed_.load(std::memory_order_acquire); }

private:
    void WorkerLoop();
    bool StartChildProcess();
    void StopChildProcess();
    std::wstring FindExecutablePath() const;

    std::wstring config_path_{L"config.json"};
    int port_{19898};
    std::filesystem::path runtime_root_, web_root_, sdk_include_;
    std::string instance_id_;

    // 状态与同步
    std::atomic<bool> target_suppressed_{false};
    std::atomic<bool> is_running_{false};
    std::atomic<bool> stop_requested_{false};

    std::thread worker_thread_;
    std::mutex cv_mutex_;
    std::condition_variable cv_;

    // Windows 进程与 Job Object 资源
    HANDLE hJob_{nullptr};
    PROCESS_INFORMATION pi_{};
    HANDLE hShutdownEvent_{nullptr};
    uint64_t seq_{0};

    // 连续失败退避机制
    int consecutive_failures_{0};
    bool in_backoff_{false};
    std::chrono::steady_clock::time_point backoff_until_{};
};

} // namespace aura
