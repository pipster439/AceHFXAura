#include "monitor/foreground_monitor.h"
#include "monitor/key_input_hub.h"
#include "utils/logger.h"
#include <algorithm>

namespace aura {

static ForegroundMonitor* g_monitor_instance = nullptr;

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT* pKb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if (pKb) {
            std::string kName = VkToKeyName(pKb->vkCode);
            if (!kName.empty()) {
                KeyInputHub::Instance().RecordKeyPress(kName);
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

ForegroundMonitor::ForegroundMonitor()
    : running_(false),
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
            process_name.resize(len - 1);
            WideCharToMultiByte(CP_UTF8, 0, wname.c_str(), -1, &process_name[0], len, nullptr, nullptr);
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
                            std::string cname(clen - 1, '\0');
                            WideCharToMultiByte(CP_UTF8, 0, cwname.c_str(), -1, &cname[0], clen, nullptr, nullptr);
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

    std::string proc_name = GetProcessNameFromHwnd(hwnd);
    {
        std::lock_guard<std::mutex> lock(g_monitor_instance->name_mutex_);
        g_monitor_instance->current_process_name_ = proc_name;
    }

    if (g_monitor_instance->callback_) {
        g_monitor_instance->callback_(proc_name, hwnd);
    }
}

void ForegroundMonitor::MonitorThreadProc() {
    thread_id_ = GetCurrentThreadId();

    // Ensure thread has a complete input message queue
    MSG msg;
    PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE);

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
        running_ = false;
        return;
    }

    LOG_INFO("前台窗口监控线程启动成功 (事件驱动模式，无轮询)");

    // Initial check for currently active foreground window
    HWND initial_fg = GetForegroundWindow();
    std::string proc = initial_fg ? GetProcessNameFromHwnd(initial_fg) : "";
    {
        std::lock_guard<std::mutex> lock(name_mutex_);
        current_process_name_ = proc;
    }
    if (callback_) {
        callback_(proc, initial_fg);
    }

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

    // Windows Message Pump (blocks until message arrives, 0% CPU)
    while (running_ && GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (kb_hook) {
        UnhookWindowsHookEx(kb_hook);
    }

    if (hook_handle_) {
        UnhookWinEvent(hook_handle_);
        hook_handle_ = nullptr;
    }

    LOG_INFO("前台窗口监控线程安全退出");
}

bool ForegroundMonitor::Start() {
    if (running_) return true;

    running_ = true;
    thread_ = std::thread(&ForegroundMonitor::MonitorThreadProc, this);
    return true;
}

void ForegroundMonitor::Stop() {
    if (!running_) return;

    running_ = false;
    if (thread_id_ != 0) {
        PostThreadMessage(thread_id_, WM_QUIT, 0, 0);
    }

    if (thread_.joinable()) {
        thread_.join();
    }
    thread_id_ = 0;
}

std::string ForegroundMonitor::GetCurrentProcessName() const {
    std::lock_guard<std::mutex> lock(name_mutex_);
    return current_process_name_;
}

} // namespace aura
