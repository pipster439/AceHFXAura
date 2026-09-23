#include "monitor/foreground_monitor.h"
#include "monitor/key_input_hub.h"
#include "utils/logger.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>

namespace aura {

static ForegroundMonitor* g_monitor_instance = nullptr;

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT* pKb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if (pKb) {
            std::string kName = VkToKeyName(pKb->vkCode, pKb->flags);
            if (!kName.empty()) {
                KeyInputHub::Instance().RecordKeyPress(kName);
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

ForegroundMonitor::ForegroundMonitor(ForegroundProvider foreground_provider, ProcessResolver process_resolver)
    : foreground_provider_(foreground_provider ? std::move(foreground_provider) : ForegroundProvider(GetForegroundWindow)),
      process_resolver_(process_resolver ? std::move(process_resolver) : ProcessResolver(GetProcessNameFromHwnd)),
      running_(false),
      thread_id_(0),
      hook_handle_(nullptr) {
    g_monitor_instance = this;
}

ForegroundMonitor::~ForegroundMonitor() {
    Stop();
    if (g_monitor_instance == this) {
        g_monitor_instance = nullptr;
    }
}

void ForegroundMonitor::SetCallback(ForegroundCallback cb) {
    callback_ = std::move(cb);
}

std::string ForegroundMonitor::GetProcessNameFromHwnd(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return "";

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return "";

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) return "";

    wchar_t exe_path[MAX_PATH] = {0};
    DWORD size = MAX_PATH;
    std::string process_name;

    if (QueryFullProcessImageNameW(hProcess, 0, exe_path, &size)) {
        std::wstring wpath(exe_path);
        size_t last_slash = wpath.find_last_of(L"\\/");
        std::wstring wname = (last_slash != std::wstring::npos) ? wpath.substr(last_slash + 1) : wpath;
        
        // Convert to std::string (ASCII/UTF-8)
        int len = WideCharToMultiByte(CP_UTF8, 0, wname.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            process_name.resize(len);
            WideCharToMultiByte(CP_UTF8, 0, wname.c_str(), -1, &process_name[0], len, nullptr, nullptr);
            process_name.pop_back();
        }
    }
    CloseHandle(hProcess);

    // Windows 11 UWP ApplicationFrameHost unwrapping
    if (process_name == "ApplicationFrameHost.exe") {
        HWND child = FindWindowExW(hwnd, NULL, NULL, NULL);
        while (child) {
            DWORD child_pid = 0;
            GetWindowThreadProcessId(child, &child_pid);
            if (child_pid != 0 && child_pid != pid) {
                HANDLE hChildProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, child_pid);
                if (hChildProc) {
                    wchar_t child_path[MAX_PATH] = {0};
                    DWORD csize = MAX_PATH;
                    if (QueryFullProcessImageNameW(hChildProc, 0, child_path, &csize)) {
                        std::wstring cwpath(child_path);
                        size_t clast_slash = cwpath.find_last_of(L"\\/");
                        std::wstring cwname = (clast_slash != std::wstring::npos) ? cwpath.substr(clast_slash + 1) : cwpath;
                        int clen = WideCharToMultiByte(CP_UTF8, 0, cwname.c_str(), -1, nullptr, 0, nullptr, nullptr);
                        if (clen > 0) {
                            std::string cname(clen, '\0');
                            WideCharToMultiByte(CP_UTF8, 0, cwname.c_str(), -1, &cname[0], clen, nullptr, nullptr);
                            cname.pop_back();
                            CloseHandle(hChildProc);
                            return cname;
                        }
                    }
                    CloseHandle(hChildProc);
                }
            }
            child = FindWindowExW(hwnd, child, NULL, NULL);
        }
    }

    return process_name;
}

std::string ForegroundMonitor::ResolveForeground(HWND hwnd) const {
    return hwnd ? process_resolver_(hwnd) : "";
}

void ForegroundMonitor::PublishForeground(const std::string& process_name, HWND hwnd, ForegroundSource source) {
    auto canonical = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    };

    std::string previous;
    {
        std::lock_guard<std::mutex> lock(name_mutex_);
        if (canonical(current_process_name_) == canonical(process_name)) return;
        previous = current_process_name_;
        current_process_name_ = process_name;
    }
    if (source == ForegroundSource::Event) {
        LOG_INFO("[Foreground] event: " + (process_name.empty() ? "(unknown)" : process_name));
    } else {
        LOG_INFO("[Foreground] reconciled: " + (previous.empty() ? "(unknown)" : previous) +
                 " -> " + (process_name.empty() ? "(unknown)" : process_name));
    }
    if (callback_) callback_(process_name, hwnd);
}

void ForegroundMonitor::ObserveForegroundEvent(HWND hwnd) {
    const std::string process_name = ResolveForeground(hwnd);
    if (process_name.empty()) {
        if (!invalid_event_logged_) {
            LOG_INFO("[Foreground] invalid transient HWND ignored");
            invalid_event_logged_ = true;
        }
        return;
    }
    invalid_event_logged_ = false;
    unresolved_reconciliations_ = 0;
    PublishForeground(process_name, hwnd, ForegroundSource::Event);
}

void ForegroundMonitor::ReconcileForeground() {
    const HWND hwnd = foreground_provider_();
    const std::string process_name = ResolveForeground(hwnd);
    if (process_name.empty()) {
        // A single missing/vanished HWND is common during fullscreen transitions.
        // Two consecutive authoritative samples allow a real desktop/unknown state to converge.
        if (unresolved_reconciliations_ < 2) ++unresolved_reconciliations_;
        if (unresolved_reconciliations_ < 2) return;
    } else {
        unresolved_reconciliations_ = 0;
    }
    PublishForeground(process_name, hwnd, ForegroundSource::Reconciled);
}

void CALLBACK ForegroundMonitor::WinEventProc(
    HWINEVENTHOOK /*hWinEventHook*/,
    DWORD event,
    HWND hwnd,
    LONG idObject,
    LONG idChild,
    DWORD /*dwEventThread*/,
    DWORD /*dwmsEventTime*/
) {
    if (event != EVENT_SYSTEM_FOREGROUND || idObject != OBJID_WINDOW || idChild != CHILDID_SELF) {
        return;
    }

    if (!g_monitor_instance) return;

    if (g_monitor_instance->running_.load(std::memory_order_acquire)) {
        g_monitor_instance->ObserveForegroundEvent(hwnd);
    }
}

void ForegroundMonitor::MonitorThreadProc() {
    // Ensure thread has a complete input message queue
    MSG msg;
    PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE);
    thread_id_.store(GetCurrentThreadId(), std::memory_order_release);
    if (!running_.load(std::memory_order_acquire)) return;

    hook_handle_ = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND,
        EVENT_SYSTEM_FOREGROUND,
        NULL,
        WinEventProc,
        0,
        0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
    );

    if (!hook_handle_) {
        LOG_ERROR("SetWinEventHook 注册失败: 0x" + std::to_string(GetLastError()));
        running_.store(false, std::memory_order_release);
        return;
    }

    LOG_INFO("前台窗口监控线程启动成功 (WinEvent + 400ms reconciliation)");

    // Initial check for currently active foreground window
    ReconcileForeground();

    // 注册全局低开销键盘钩子，捕捉真实物理敲击以驱动按键响应与涟漪光效
    HHOOK kb_hook = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        LowLevelKeyboardProc,
        GetModuleHandle(NULL),
        0
    );
    if (!kb_hook) {
        kb_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    }
    if (kb_hook) {
        LOG_INFO("全局物理按键监听钩子 (WH_KEYBOARD_LL) 注册成功");
    } else {
        LOG_WARN("WH_KEYBOARD_LL 注册失败: 0x" + std::to_string(GetLastError()));
    }

    // Keep pumping hook messages, with a monotonic 400ms deadline even if messages arrive often.
    // The timeout runs on this thread; the render thread only reads the cached name.
    constexpr auto kReconcileInterval = std::chrono::milliseconds(400);
    auto next_reconcile = std::chrono::steady_clock::now() + kReconcileInterval;
    while (running_.load(std::memory_order_acquire)) {
        unsigned int dispatched = 0;
        while (dispatched++ < 64 && PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running_.store(false, std::memory_order_release);
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!running_.load(std::memory_order_acquire)) break;
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_reconcile) {
            ReconcileForeground();
            next_reconcile = now + kReconcileInterval;
            continue;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(next_reconcile - now);
        const DWORD wait = MsgWaitForMultipleObjectsEx(0, nullptr,
            static_cast<DWORD>(std::max<int64_t>(1, remaining.count())), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait == WAIT_FAILED) {
            LOG_ERROR("[Foreground] message wait failed: " + std::to_string(GetLastError()));
            break;
        }
    }

    if (kb_hook) {
        UnhookWindowsHookEx(kb_hook);
    }

    if (hook_handle_) {
        UnhookWinEvent(hook_handle_);
        hook_handle_ = nullptr;
    }

    running_.store(false, std::memory_order_release);

    LOG_INFO("前台窗口监控线程安全退出");
}

bool ForegroundMonitor::Start() {
    if (running_.load(std::memory_order_acquire)) return true;

    // A failed hook/wait may have ended the previous thread without a Stop call.
    if (thread_.joinable()) thread_.join();
    thread_id_.store(0, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&ForegroundMonitor::MonitorThreadProc, this);
    return true;
}

void ForegroundMonitor::Stop() {
    bool was_running = running_.exchange(false, std::memory_order_acq_rel);
    DWORD tid = thread_id_.load(std::memory_order_acquire);
    if (was_running && tid != 0) {
        PostThreadMessage(tid, WM_QUIT, 0, 0);
    }

    if (thread_.joinable()) {
        thread_.join();
    }
    thread_id_.store(0, std::memory_order_release);
}

std::string ForegroundMonitor::GetCurrentProcessName() const {
    std::lock_guard<std::mutex> lock(name_mutex_);
    return current_process_name_;
}

} // namespace aura
