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
    std::filesystem::path sdk_include_dir;
    std::wstring shutdown_event_name;
    bool has_explicit_config = false;

    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--port") {
            if (i + 1 >= argc) {
                std::cerr << "[WebUI] 错误: --port 缺少端口参数\n";
                return 1;
            }
            std::wstring val = argv[++i];
            try {
                size_t pos = 0;
                int p = std::stoi(val, &pos);
                if (pos != val.size() || p <= 0 || p > 65535) {
                    std::cerr << "[WebUI] 错误: --port 参数非法 (必须为 1~65535 整数)\n";
                    return 1;
                }
                port = p;
            } catch (const std::exception&) {
                std::cerr << "[WebUI] 错误: --port 参数非法 (必须为 1~65535 整数)\n";
                return 1;
            }
        } else if (arg == L"--config") {
            if (i + 1 >= argc) {
                std::cerr << "[WebUI] 错误: --config 缺少配置文件路径\n";
                return 1;
            }
            config_path = argv[++i];
            has_explicit_config = true;
        } else if (arg == L"--sdk-include") {
            if (i + 1 >= argc) {
                std::cerr << "[WebUI] 错误: --sdk-include 缺少路径参数\n";
                return 1;
            }
            sdk_include_dir = argv[++i];
        } else if (arg == L"--shutdown-event") {
            if (i + 1 >= argc) {
                std::cerr << "[WebUI] 错误: --shutdown-event 缺少事件名称\n";
                return 1;
            }
            shutdown_event_name = argv[++i];
        } else if (arg == L"--help" || arg == L"-h") {
            std::cout << "用法: aura_web_ui [选项]\n"
                      << "选项:\n"
                      << "  --port <1-65535>        指定 HTTP 监听端口 (默认: 19898)\n"
                      << "  --config <path>         指定配置文件路径 (默认: config.json)\n"
                      << "  --sdk-include <path>    指定 C++ Plugin SDK include 头文件目录\n"
                      << "  --shutdown-event <name> 指定父进程同步平滑退出命名事件\n"
                      << "  --help, -h              显示帮助信息\n";
            return 0;
        } else {
            std::cerr << "[WebUI] 错误: 未知的命令行参数: " << std::filesystem::path(arg).u8string() << "\n";
            return 1;
        }
    }

    std::error_code ec;
    if (has_explicit_config) {
        if (!std::filesystem::exists(config_path, ec)) {
            std::cerr << "[WebUI] 错误: 显式指定的配置文件不存在: " << config_path.u8string() << std::endl;
            return 1;
        }
    } else {
        if (!std::filesystem::exists(config_path, ec)) {
            std::vector<std::filesystem::path> example_candidates = {
                "config.example.json",
                "../config.example.json"
            };
            wchar_t mod_path[MAX_PATH];
            if (GetModuleFileNameW(nullptr, mod_path, MAX_PATH)) {
                for (auto dir = std::filesystem::path(mod_path).parent_path(); !dir.empty(); dir = dir.parent_path()) {
                    example_candidates.push_back(dir / "config.example.json");
                    if (dir == dir.parent_path()) break;
                }
            }
            wchar_t local_app_data[MAX_PATH];
            if (GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH)) {
                example_candidates.push_back(std::filesystem::path(local_app_data) / L"Aura" / L"runtime" / L"config.example.json");
            }
            std::filesystem::path found_example;
            for (const auto& cand : example_candidates) {
                if (std::filesystem::exists(cand, ec)) {
                    found_example = cand;
                    break;
                }
            }
            if (found_example.empty()) {
                std::cerr << "[WebUI] 错误: 默认配置文件不存在且未找到模板 config.example.json" << std::endl;
                return 1;
            }
            std::filesystem::copy_file(found_example, config_path, std::filesystem::copy_options::skip_existing, ec);
            if (ec) {
                std::cerr << "[WebUI] 错误: 无法从模板初始化 config.json: " << ec.message() << std::endl;
                return 1;
            }
            std::cout << "[WebUI] 已从模板成功初始化默认配置文件: " << config_path.u8string() << std::endl;
        }
    }

    aura::WebServer server(config_path, port, sdk_include_dir);
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
