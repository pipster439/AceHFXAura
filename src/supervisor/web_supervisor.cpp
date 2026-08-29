#include "supervisor/web_supervisor.h"
#include "utils/logger.h"
#include <vector>
#include <filesystem>

namespace aura {

WebUiSupervisor::WebUiSupervisor() {
    // 创建 Windows Job Object 并配置 KILL_ON_JOB_CLOSE
    // 确保若 daemon 发生任何非正常终止或被强杀，操作系统内核必定自动回收子进程
    hJob_ = CreateJobObjectW(nullptr, nullptr);
    if (hJob_) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(hJob_, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    } else {
        LOG_WARN("[WebUI 监护] 创建 JobObject 失败: 0x" + std::to_string(GetLastError()));
    }
}

WebUiSupervisor::~WebUiSupervisor() {
    Shutdown();
    if (hJob_) {
        CloseHandle(hJob_);
        hJob_ = nullptr;
    }
}

void WebUiSupervisor::StartSupervisor(const std::string& config_path, int port) {
    config_path_ = config_path;
    port_ = port;
    stop_requested_.store(false, std::memory_order_release);

    worker_thread_ = std::thread(&WebUiSupervisor::WorkerLoop, this);
    LOG_INFO("[WebUI 监护] 网页服务后台监护线程已就绪");
}

void WebUiSupervisor::Shutdown() {
    if (stop_requested_.exchange(true, std::memory_order_acq_rel)) {
        return; // 已经关闭过
    }

    {
        std::lock_guard<std::mutex> lock(cv_mutex_);
        cv_.notify_all();
    }

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    // 确保子进程完全关闭
    StopChildProcess();
}

void WebUiSupervisor::SetSuppressed(bool suppressed) {
    bool old_val = target_suppressed_.exchange(suppressed, std::memory_order_acq_rel);
    if (old_val != suppressed) {
        std::lock_guard<std::mutex> lock(cv_mutex_);
        cv_.notify_one();
    }
}

std::string WebUiSupervisor::FindExecutablePath() const {
    wchar_t mod_path[MAX_PATH];
    if (GetModuleFileNameW(nullptr, mod_path, MAX_PATH)) {
        std::filesystem::path current_exe(mod_path);
        auto candidate = current_exe.parent_path() / "aura_web_ui.exe";
        if (std::filesystem::exists(candidate)) {
            return candidate.string();
        }
        auto candidate_build = current_exe.parent_path() / "Release" / "aura_web_ui.exe";
        if (std::filesystem::exists(candidate_build)) {
            return candidate_build.string();
        }
    }

    if (std::filesystem::exists("aura_web_ui.exe")) {
        return "aura_web_ui.exe";
    }
    if (std::filesystem::exists("build/Release/aura_web_ui.exe")) {
        return "build/Release/aura_web_ui.exe";
    }

    return "aura_web_ui.exe";
}

bool WebUiSupervisor::StartChildProcess() {
    if (is_running_.load(std::memory_order_acquire)) {
        return true;
    }

    // 退避状态检查
    if (in_backoff_) {
        if (std::chrono::steady_clock::now() < backoff_until_) {
            return false;
        }
        in_backoff_ = false;
        LOG_INFO("[WebUI 监护] 退出静默退避期，尝试重新启动网页服务...");
    }

    std::string exe_path = FindExecutablePath();
    if (!std::filesystem::exists(exe_path)) {
        consecutive_failures_++;
        LOG_ERROR("[WebUI 监护] 未找到网页服务可执行文件: " + exe_path);
        if (consecutive_failures_ >= 3) {
            in_backoff_ = true;
            backoff_until_ = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            LOG_WARN("[WebUI 监护] 网页服务已连续失败 3 次，进入 30 秒静默退避期，避免重试风暴");
        }
        return false;
    }

    // 创建命名 Shutdown Event，用于"先礼后兵"平滑通知
    seq_++;
    std::string event_name = "Local\\Aura_Web_UI_Shutdown_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(seq_);
    hShutdownEvent_ = CreateEventA(nullptr, TRUE, FALSE, event_name.c_str());

    std::string cmdline = "\"" + exe_path + "\" --port " + std::to_string(port_) + 
                          " --config \"" + config_path_ + "\" --shutdown-event \"" + event_name + "\"";

    std::wstring wcmdline(cmdline.begin(), cmdline.end());

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    ZeroMemory(&pi_, sizeof(pi_));

    BOOL ok = CreateProcessW(
        nullptr,
        &wcmdline[0],
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi_
    );

    if (!ok) {
        DWORD err = GetLastError();
        consecutive_failures_++;
        LOG_ERROR("[WebUI 监护] CreateProcessW 失败: 0x" + std::to_string(err) + " (命令: " + cmdline + ")");

        if (hShutdownEvent_) {
            CloseHandle(hShutdownEvent_);
            hShutdownEvent_ = nullptr;
        }

        if (consecutive_failures_ >= 3) {
            in_backoff_ = true;
            backoff_until_ = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            LOG_WARN("[WebUI 监护] 网页服务连续失败 3 次，进入 30 秒静默退避期，避免重试风暴");
        }
        return false;
    }

    // 绑定至 Job Object 防孤儿
    if (hJob_) {
        AssignProcessToJobObject(hJob_, pi_.hProcess);
    }

    is_running_.store(true, std::memory_order_release);
    consecutive_failures_ = 0; // 重置失败计数
    LOG_INFO("[WebUI 监护] 网页配置服务进程已启动 (PID: " + std::to_string(pi_.dwProcessId) + 
             ", 绑定端口: 127.0.0.1:" + std::to_string(port_) + ")");
    return true;
}

void WebUiSupervisor::StopChildProcess() {
    if (!is_running_.load(std::memory_order_acquire) && !pi_.hProcess) {
        return;
    }

    DWORD pid = pi_.dwProcessId;
    LOG_INFO("[WebUI 监护] 正在通知网页服务平滑退出 (PID: " + std::to_string(pid) + ")...");

    // 第一步：先礼 — 触发命名 Shutdown Event 通知子进程平滑终止
    if (hShutdownEvent_) {
        SetEvent(hShutdownEvent_);
    }

    // 宽限等待最多 500ms
    DWORD wait_res = WAIT_TIMEOUT;
    if (pi_.hProcess) {
        wait_res = WaitForSingleObject(pi_.hProcess, 500);
    }

    if (wait_res == WAIT_OBJECT_0) {
        LOG_INFO("[WebUI 监护] [+] 网页配置服务已平滑关闭 (PID: " + std::to_string(pid) + ")");
    } else {
        // 第二步：后兵 — 500ms 超时未退出，实施 TerminateProcess 兜底强杀
        LOG_WARN("[WebUI 监护] 网页服务平滑退出超时 (500ms)，执行 TerminateProcess 强制终止 (PID: " + std::to_string(pid) + ")");
        if (pi_.hProcess) {
            TerminateProcess(pi_.hProcess, 0);
            WaitForSingleObject(pi_.hProcess, 200);
        }
    }

    // 清理资源句柄
    if (pi_.hProcess) {
        CloseHandle(pi_.hProcess);
        pi_.hProcess = nullptr;
    }
    if (pi_.hThread) {
        CloseHandle(pi_.hThread);
        pi_.hThread = nullptr;
    }
    if (hShutdownEvent_) {
        CloseHandle(hShutdownEvent_);
        hShutdownEvent_ = nullptr;
    }

    is_running_.store(false, std::memory_order_release);
}

void WebUiSupervisor::WorkerLoop() {
    while (!stop_requested_.load(std::memory_order_acquire)) {
        bool should_suppress = target_suppressed_.load(std::memory_order_acquire);
        bool currently_running = is_running_.load(std::memory_order_acquire);

        if (should_suppress && currently_running) {
            StopChildProcess();
        } else if (!should_suppress && !currently_running) {
            StartChildProcess();
        } else if (currently_running && pi_.hProcess) {
            // 探针检测：检查子进程是否意外异常崩溃/退出
            DWORD exit_code = 0;
            if (GetExitCodeProcess(pi_.hProcess, &exit_code) && exit_code != STILL_ACTIVE) {
                LOG_WARN("[WebUI 监护] 网页服务异常退出 (退出码: 0x" + std::to_string(exit_code) + ")");
                StopChildProcess(); // 清理残留句柄
                consecutive_failures_++;
            }
        }

        std::unique_lock<std::mutex> lock(cv_mutex_);
        // 睡眠等待下一次前台状态变更，或 1.5 秒超时用于探针轮检
        cv_.wait_for(lock, std::chrono::milliseconds(1500), [&]() {
            if (stop_requested_.load(std::memory_order_acquire)) return true;
            bool s = target_suppressed_.load(std::memory_order_acquire);
            bool r = is_running_.load(std::memory_order_acquire);
            return (s && r) || (!s && !r);
        });
    }

    // 退出时确保关停
    StopChildProcess();
}

} // namespace aura
