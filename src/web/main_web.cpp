#include "web/web_server.h"
#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <filesystem>

namespace {

aura::WebServer* g_server_ptr = nullptr;

BOOL WINAPI ConsoleCtrlHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT) {
        std::cout << "\n[WebUI] 收到控制台关闭信号，正在平滑退出..." << std::endl;
        if (g_server_ptr) {
            g_server_ptr->Stop();
        }
        return TRUE;
    }
    return FALSE;
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    // 强制 UTF-8 控制台编码
    SetConsoleOutputCP(CP_UTF8);

    int port = 19898;
    std::filesystem::path config_path = L"config.json";
    std::wstring shutdown_event_name;

    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == L"--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == L"--shutdown-event" && i + 1 < argc) {
            shutdown_event_name = argv[++i];
        }
    }

    aura::WebServer server(config_path, port);
    g_server_ptr = &server;

    // 安装控制台信号处理器（支持手动命令行调试时按 Ctrl+C 平滑退出）
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    // 安装 Windows 命名同步事件监听器（支持 daemon 监护器发起"先礼后兵"平滑退出）
    std::thread shutdown_listener;
    std::atomic<bool> listener_running{true};

    if (!shutdown_event_name.empty()) {
        shutdown_listener = std::thread([&server, shutdown_event_name, &listener_running]() {
            HANDLE hEvent = OpenEventW(SYNCHRONIZE, FALSE, shutdown_event_name.c_str());
            if (hEvent) {
                while (listener_running.load(std::memory_order_relaxed)) {
                    DWORD res = WaitForSingleObject(hEvent, 200);
                    if (res == WAIT_OBJECT_0) {
                        std::cout << "[WebUI] 收到 daemon 命名 Shutdown Event 信号，正在平滑退出..." << std::endl;
                        server.Stop();
                        break;
                    }
                }
                CloseHandle(hEvent);
            }
        });
    }

    std::cout << "=========================================================\n"
              << " ROG FALCHION ACE HFX - 轻量网页配置服务\n"
              << " 监听地址: http://127.0.0.1:" << port << "\n"
              << " 配置文件: " << config_path.u8string() << "\n"
              << "=========================================================" << std::endl;

    bool ok = server.Start();

    // 清理监听器线程
    listener_running.store(false, std::memory_order_relaxed);
    if (shutdown_listener.joinable()) {
        shutdown_listener.join();
    }

    std::cout << "[WebUI] 网页配置服务已安全退出。" << std::endl;
    return ok ? 0 : 1;
}
