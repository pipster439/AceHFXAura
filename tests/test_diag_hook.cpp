#include <windows.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>

std::atomic<int> g_keys{0};
std::atomic<int> g_fg_events{0};

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT* pKb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if (pKb) {
            g_keys++;
            std::cout << "[Hook] KeyDown VK: 0x" << std::hex << pKb->vkCode << std::dec << std::endl;
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    g_fg_events++;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    std::cout << "[WinEvent] Foreground event: 0x" << std::hex << event 
              << " hwnd: " << hwnd << " pid: " << std::dec << pid 
              << " idObj: " << idObject << " idChild: " << idChild << std::endl;
}

int main() {
    std::cout << "Starting hook test for 8 seconds..." << std::endl;

    std::thread t([]() {
        MSG msg;
        PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE);

        HWINEVENTHOOK we = SetWinEventHook(
            EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
            NULL, WinEventProc, 0, 0,
            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
        );
        std::cout << "SetWinEventHook: " << we << " err: " << GetLastError() << std::endl;

        HHOOK kb = SetWindowsHookExW(
            WH_KEYBOARD_LL, LowLevelKeyboardProc,
            GetModuleHandle(NULL), 0
        );
        std::cout << "SetWindowsHookExW: " << kb << " err: " << GetLastError() << std::endl;

        while (GetMessage(&msg, NULL, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (kb) UnhookWindowsHookEx(kb);
        if (we) UnhookWinEvent(we);
    });

    for (int i = 0; i < 8; i++) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "Second " << i + 1 << ": keys=" << g_keys.load() 
                  << " fg_events=" << g_fg_events.load() << std::endl;
    }

    PostThreadMessage(GetThreadId(t.native_handle()), WM_QUIT, 0, 0);
    t.join();

    std::cout << "Done! Total keys: " << g_keys.load() 
              << " fg: " << g_fg_events.load() << std::endl;
    return 0;
}
