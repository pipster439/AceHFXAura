#include "aura/aura_adapter.h"
#include "aura/hal_compat.h"
#include "aura/keymap.h"
#include "monitor/foreground_monitor.h"
#include "config/rule_engine.h"
#include "engine/effect_engine.h"
#include "engine/plugin_manager.h"
#include "supervisor/web_supervisor.h"
#include "gsi/gsi_adapter.h"
#include "utils/logger.h"
#include "utils/system_info.h"

#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <cmath>
#include <vector>
#include <memory>
#include <chrono>
#include <thread>
#include <atomic>
#include <fstream>
#include <filesystem>
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>

#pragma comment(lib, "shell32.lib")

namespace {

std::atomic<bool> g_running{true};
std::atomic<bool> g_shutdown_done{false};

std::string GetStateFilePath() {
    wchar_t local_app_data[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH)) {
        auto p = std::filesystem::path(local_app_data) / "Aura";
        std::error_code ec;
        std::filesystem::create_directories(p, ec);
        return (p / ".daemon_running").string();
    }
    return ".daemon_running";
}

void RemoveAllStateFiles() {
    std::string p = GetStateFilePath();
    DeleteFileA(p.c_str());
    DeleteFileA(".daemon_running");
}

LONG WINAPI GlobalUnhandledExceptionFilter(EXCEPTION_POINTERS* pEx) {
    DWORD code = pEx && pEx->ExceptionRecord ? pEx->ExceptionRecord->ExceptionCode : 0;
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << code;
    LOG_ERROR("FATAL: 捕获全局未处理异常 (SEH: " + oss.str() + ")，正在紧急清理运行标记以防连续闪退死循环...");
    RemoveAllStateFiles();
    return EXCEPTION_CONTINUE_SEARCH;
}

std::wstring GetRegisteredHalDllPath() {
    wchar_t path[MAX_PATH] = {0};
    DWORD pathSize = sizeof(path);
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\CLSID\\{AE9DB4C8-4F2A-4756-9B11-2F6D78C61F1A}\\InprocServer32", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, nullptr, nullptr, nullptr, reinterpret_cast<LPBYTE>(path), &pathSize);
        RegCloseKey(hKey);
    }
    if (path[0] == L'\0') {
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, L"CLSID\\{AE9DB4C8-4F2A-4756-9B11-2F6D78C61F1A}\\InprocServer32", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            pathSize = sizeof(path);
            RegQueryValueExW(hKey, nullptr, nullptr, nullptr, reinterpret_cast<LPBYTE>(path), &pathSize);
            RegCloseKey(hKey);
        }
    }
    return std::wstring(path);
}

BOOL WINAPI ConsoleCtrlHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT) {
        LOG_INFO("接收到控制台中断信号 (" + std::to_string(signal) + ")，正在请求优雅退出...");
        g_running.store(false, std::memory_order_release);
        return TRUE;
    }
    if (signal == CTRL_CLOSE_EVENT || signal == CTRL_SHUTDOWN_EVENT || signal == CTRL_LOGOFF_EVENT) {
        LOG_INFO("接收到控制台关闭/系统注销信号 (" + std::to_string(signal) + ")，正在等待守护进程优雅停机...");
        g_running.store(false, std::memory_order_release);
        // Windows 给控制台关闭分配有限的超时时间 (通常为 5 秒)。
        // 若直接返回 TRUE，Windows 会瞬间强杀主线程，导致硬件状态未复位、标记文件未清除。
        // 此处等待主线程完成清理，最长等待 4 秒。
        for (int i = 0; i < 80 && !g_shutdown_done.load(std::memory_order_acquire); ++i) {
            Sleep(50);
        }
        return TRUE;
    }
    return FALSE;
}

void PrintUsage() {
    std::cout << "=========================================================\n"
              << " ROG FALCHION ACE HFX 硬件级单键 RGB 核心守护进程\n"
              << "=========================================================\n"
              << "用法: aura_daemon.exe [选项]\n\n"
              << "选项:\n"
              << "  --backend <模式>          指定硬件后端 (auto|native_hid|legacy_hal，默认: auto)\n"
              << "  --dry-run                 启动虚拟推流模式 (不挂载底层驱动，验证监控与规则)\n"
              << "  --test-init <次数>        对底层驱动执行 N 次初始化/释放循环内存安全压力测试\n"
              << "  --test-stability <分钟>   持续以 25 FPS 推流指定时长进行稳定性硬性验收\n"
              << "  --config <文件路径>       指定配置文件路径 (默认: config.json)\n"
              << "  --keymap <文件路径>       指定权威键位表文件路径 (默认: calibrated_keymap.json)\n"
              << "  --log-level <级别>        设置日志级别 (debug|info|warn|error，默认: info)\n"
              << "  --help                    显示本帮助信息\n"
              << "=========================================================\n";
}

// RAII 守卫管理 COM 生命周期：确保与 CoInitializeEx 成对释放，并在提前返回时自动清理
struct ComScope {
    HRESULT hr;
    explicit ComScope() : hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComScope() {
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    }
    bool ok() const { return SUCCEEDED(hr); }

    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
    ComScope(ComScope&&) = delete;
    ComScope& operator=(ComScope&&) = delete;
};

} // namespace

int main(int argc, char* argv[]) {
    // 设置全局 SEH 未处理异常过滤器：遇到致命崩溃时确保清除运行标记，打破连续闪退死循环
    SetUnhandledExceptionFilter(GlobalUnhandledExceptionFilter);

    // 0. 初始化 COM 单线程套间 (RAII 守卫置于 main 顶部，先于所有局部对象声明以保证 LIFO 逆序析构)
    ComScope com;

    try {
        // 检查是否有 --probe-hardware 参数 (硬件隔离探测模式)
        bool probe_mode = false;
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--probe-hardware") {
                probe_mode = true;
                break;
            }
        }

        // 初始化日志系统 (探测模式仅向控制台输出，避免与父进程日志文件句柄冲突)
        aura::Logger::Instance().Init(probe_mode ? "" : "aura_daemon.log");

        // 检查 COM 初始化结果 (含 RPC_E_CHANGED_MODE 模式冲突处理)
        if (!com.ok()) {
            std::ostringstream hr_oss;
            hr_oss << "0x" << std::hex << std::uppercase << static_cast<unsigned long>(com.hr);
            if (com.hr == RPC_E_CHANGED_MODE) {
                LOG_ERROR("FATAL: COM 初始化失败: 当前线程已被初始化为与 STA 不兼容的并发模式 (RPC_E_CHANGED_MODE, " 
                          + hr_oss.str() + ")，守护进程无法继续运行。");
            } else {
                LOG_ERROR("FATAL: COM 初始化失败 (错误码: " + hr_oss.str() + ")，守护进程无法继续运行。");
            }
            return 1;
        }

        // 1. 获取主执行文件与模块目录
        wchar_t current_mod[MAX_PATH];
        std::filesystem::path current_exe_dir;
        if (GetModuleFileNameW(nullptr, current_mod, MAX_PATH)) {
            current_exe_dir = std::filesystem::path(current_mod).parent_path();
        }

        // 2. 解析命令行参数
        bool dry_run = false;
        int test_init_count = 0;
        double test_stability_minutes = 0.0;
        std::string config_path = "config.json";
        std::string keymap_path = "calibrated_keymap.json";
        bool has_explicit_config = false;
        aura::HardwareBackend cli_backend = aura::HardwareBackend::Auto;
        bool has_cli_backend = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--probe-hardware") {
            probe_mode = true;
        } else if (arg == "--dry-run") {
            dry_run = true;
        } else if (arg == "--test-init") {
            if (i + 1 >= argc) {
                std::cerr << "错误: --test-init 缺少正整数参数\n\n";
                PrintUsage();
                return 1;
            }
            std::string val = argv[++i];
            try {
                size_t pos = 0;
                int count = std::stoi(val, &pos);
                if (pos != val.size() || count <= 0) {
                    std::cerr << "错误: --test-init 参数必须为大于 0 的正整数: " << val << "\n\n";
                    PrintUsage();
                    return 1;
                }
                test_init_count = count;
            } catch (const std::exception&) {
                std::cerr << "错误: --test-init 参数非法 (必须为有效正整数): " << val << "\n\n";
                PrintUsage();
                return 1;
            }
        } else if (arg == "--test-stability") {
            if (i + 1 >= argc) {
                std::cerr << "错误: --test-stability 缺少运行分钟数参数\n\n";
                PrintUsage();
                return 1;
            }
            std::string val = argv[++i];
            try {
                size_t pos = 0;
                double minutes = std::stod(val, &pos);
                if (pos != val.size() || minutes <= 0.0 || !std::isfinite(minutes)) {
                    std::cerr << "错误: --test-stability 参数必须为大于 0 的有限正数: " << val << "\n\n";
                    PrintUsage();
                    return 1;
                }
                test_stability_minutes = minutes;
            } catch (const std::exception&) {
                std::cerr << "错误: --test-stability 参数非法 (必须为有效正数): " << val << "\n\n";
                PrintUsage();
                return 1;
            }
        } else if (arg == "--config") {
            if (i + 1 >= argc) {
                std::cerr << "错误: --config 缺少配置文件路径\n\n";
                PrintUsage();
                return 1;
            }
            config_path = argv[++i];
            has_explicit_config = true;
        } else if (arg == "--keymap") {
            if (i + 1 >= argc) {
                std::cerr << "错误: --keymap 缺少键位映射表路径\n\n";
                PrintUsage();
                return 1;
            }
            keymap_path = argv[++i];
        } else if (arg == "--log-level") {
            if (i + 1 >= argc) {
                std::cerr << "错误: --log-level 缺少级别参数\n\n";
                PrintUsage();
                return 1;
            }
            std::string level_str = argv[++i];
            for (auto& c : level_str) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (level_str == "debug") {
                aura::Logger::Instance().SetLogLevel(aura::LogLevel::Debug);
            } else if (level_str == "info") {
                aura::Logger::Instance().SetLogLevel(aura::LogLevel::Info);
            } else if (level_str == "warn" || level_str == "warning") {
                aura::Logger::Instance().SetLogLevel(aura::LogLevel::Warn);
            } else if (level_str == "error") {
                aura::Logger::Instance().SetLogLevel(aura::LogLevel::Error);
            } else {
                std::cerr << "未知的日志级别: " << level_str << " (支持: debug, info, warn, error)\n\n";
                PrintUsage();
                return 1;
            }
        } else if (arg == "--backend") {
            if (i + 1 >= argc) {
                std::cerr << "错误: --backend 缺少参数 (auto|native_hid|legacy_hal)\n\n";
                PrintUsage();
                return 1;
            }
            std::string b_str = argv[++i];
            cli_backend = aura::StringToHardwareBackend(b_str);
            has_cli_backend = true;
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage();
            return 0;
        } else {
            std::cerr << "未知的命令行参数: " << arg << "\n\n";
            PrintUsage();
            return 1;
        }
    }

    // A build/Release executable must use the same config, web assets and plugins
    // as a repository-root launch, even when started from another directory.
    if (config_path == "config.json" && keymap_path == "calibrated_keymap.json" &&
        !std::filesystem::exists("config.json") && !std::filesystem::exists("calibrated_keymap.json")) {
        for (auto root = current_exe_dir; !root.empty(); root = root.parent_path()) {
            if (std::filesystem::exists(root / "calibrated_keymap.json") &&
                std::filesystem::exists(root / "include" / "engine" / "effect.h")) {
                std::error_code ec;
                std::filesystem::current_path(root, ec);
                if (!ec) LOG_INFO("已将工作目录定位到项目根目录: " + root.u8string());
                break;
            }
            if (root == root.parent_path()) break;
        }
    }

    // 2.5 硬件探测隔离模式分支 (在创建全局互斥量与维护状态文件之前执行)
    if (probe_mode) {
        // 屏蔽 Windows 错误报告 (WER) 崩溃弹窗，确保底层异常时静默终止以触发通道复位
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

        LOG_INFO("[Probe] 正在执行硬件隔离探测与通道复位...");
        if (!std::filesystem::exists(keymap_path) && !current_exe_dir.empty()) {
            auto cand = current_exe_dir / keymap_path;
            if (std::filesystem::exists(cand)) {
                keymap_path = cand.string();
            }
        }
        aura::Keymap probe_keymap;
        if (!probe_keymap.LoadFromJson(keymap_path)) {
            LOG_ERROR("[Probe] 无法加载键位表: " + keymap_path);
            return 1;
        }
        aura::AuraAdapter probe_adapter(false, cli_backend);
        if (!probe_adapter.Initialize(&probe_keymap)) {
            LOG_WARN("[Probe] 底层硬件初次挂载未就绪");
            return 2;
        }
        probe_adapter.Shutdown();
        LOG_INFO("[Probe] 硬件探测成功完成");
        return 0;
    }

    // 2.5 校验显式指定的配置文件合法性 (快速失败，避免在参数错误时误触单实例分支或状态写盘)
    if (has_explicit_config) {
        if (!std::filesystem::exists(config_path)) {
            auto same_exe_dir_config = current_exe_dir / config_path;
            if (!current_exe_dir.empty() && std::filesystem::exists(same_exe_dir_config)) {
                config_path = same_exe_dir_config.string();
            } else {
                LOG_ERROR("FATAL: 显式指定的配置文件不存在: " + config_path);
                std::cerr << "错误: 显式指定的配置文件不存在: " << config_path << "\n";
                return 1;
            }
        }
    }

    // 3. 单实例保护 (Named Mutex)
    HANDLE hMutex = CreateMutexW(NULL, FALSE, L"Local\\RogFalchionAceHfxDaemonMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        LOG_WARN("检测到已有另一个 ROG Falchion Ace HFX 守护进程正在运行，自动唤起 Web 配置面板...");
        std::cout << "[Aura] 检测到守护进程已在后台运行。\n";
        std::cout << "[Aura] 正在自动打开 Web 控制面板: http://127.0.0.1:19898/ ...\n";
        ShellExecuteW(nullptr, L"open", L"http://127.0.0.1:19898/", nullptr, nullptr, SW_SHOWNORMAL);
        if (hMutex) CloseHandle(hMutex);
        Sleep(1500);
        return 0;
    }

    // 4. 注册控制台中断处理器 (Ctrl+C)
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    // 5. 压力测试独立分支
    if (test_init_count > 0) {
        aura::AuraAdapter test_adapter(false, cli_backend);
        bool test_ok = test_adapter.RunInitStressTest(static_cast<size_t>(test_init_count));
        RemoveAllStateFiles();
        if (hMutex) CloseHandle(hMutex);
        g_shutdown_done.store(true, std::memory_order_release);
        return test_ok ? 0 : 1;
    }

    // 6. 检测异常退出标记 (在底层驱动初始化并就绪前严禁写入状态文件，防止启动崩溃形成死循环)
    std::string state_file = GetStateFilePath();
    bool had_abnormal_exit = false;
    {
        std::ifstream check_file(state_file);
        if (!check_file.is_open()) {
            check_file.open(".daemon_running");
        }
        if (check_file.is_open()) {
            had_abnormal_exit = true;
            LOG_WARN("【异常退出兜底】检测到上一次进程未正常退出（存在残留标记文件），将在连接前启动硬件探测隔离子进程自愈！");
        }
    }

    // 7. 加载权威键位表与配置文件
    if (!std::filesystem::exists(keymap_path) && !current_exe_dir.empty()) {
        auto cand_km = current_exe_dir / keymap_path;
        if (std::filesystem::exists(cand_km)) {
            keymap_path = cand_km.string();
        }
    }

    aura::Keymap keymap;
    if (!keymap.LoadFromJson(keymap_path)) {
        LOG_ERROR("FATAL: 无法加载键位表: " + keymap_path);
        RemoveAllStateFiles();
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    if (has_explicit_config) {
        if (!std::filesystem::exists(config_path)) {
            auto same_exe_dir_config = current_exe_dir / config_path;
            if (!current_exe_dir.empty() && std::filesystem::exists(same_exe_dir_config)) {
                config_path = same_exe_dir_config.string();
            } else {
                LOG_ERROR("FATAL: 显式指定的配置文件不存在: " + config_path);
                RemoveAllStateFiles();
                if (hMutex) CloseHandle(hMutex);
                return 1;
            }
        }
    } else {
        if (!std::filesystem::exists(config_path)) {
            auto same_exe_dir_config = current_exe_dir / config_path;
            if (!current_exe_dir.empty() && std::filesystem::exists(same_exe_dir_config)) {
                config_path = same_exe_dir_config.string();
            } else {
                std::filesystem::path example = "config.example.json";
                if (!std::filesystem::exists(example) && !current_exe_dir.empty()) {
                    example = current_exe_dir / "config.example.json";
                }
                if (!std::filesystem::exists(example) && !current_exe_dir.empty()) {
                    example = current_exe_dir.parent_path() / "config.example.json";
                }
                if (std::filesystem::exists(example)) {
                    std::error_code ec;
                    std::filesystem::copy_file(example, config_path, std::filesystem::copy_options::skip_existing, ec);
                    if (ec) {
                        LOG_ERROR("FATAL: 无法从模板初始化 config.json: " + ec.message());
                        RemoveAllStateFiles();
                        if (hMutex) CloseHandle(hMutex);
                        return 1;
                    }
                    LOG_INFO("已从模板成功初始化默认配置文件: " + config_path);
                } else {
                    LOG_ERROR("FATAL: 默认配置文件不存在且未找到模板 config.example.json");
                    RemoveAllStateFiles();
                    if (hMutex) CloseHandle(hMutex);
                    return 1;
                }
            }
        }
    }

    aura::RuleEngine rule_engine;
    if (!rule_engine.LoadConfig(config_path)) {
        LOG_WARN("加载配置文件失败: " + config_path + "，将使用内部预设规则");
    }

    // 8. 挂载华硕底层驱动适配器 (在主线程完全拥有，杜绝多线程 COM 激活冲突)
    bool probe_failed = false;
    if (had_abnormal_exit && !dry_run) {
        LOG_INFO("【异常退出自愈】检测到上次非正常退出，正在启动硬件探测隔离子进程以安全复位驱动通道...");

        wchar_t exe_path[MAX_PATH];
        DWORD len = GetModuleFileNameW(NULL, exe_path, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            std::wstring cmd = L"\"" + std::wstring(exe_path) + L"\" --probe-hardware";
            if (keymap_path != "calibrated_keymap.json") {
                int wlen = MultiByteToWideChar(CP_UTF8, 0, keymap_path.c_str(), -1, NULL, 0);
                if (wlen > 0) {
                    std::vector<wchar_t> wbuf(wlen);
                    MultiByteToWideChar(CP_UTF8, 0, keymap_path.c_str(), -1, wbuf.data(), wlen);
                    cmd += L" --keymap \"" + std::wstring(wbuf.data()) + L"\"";
                }
            }
            if (has_cli_backend) {
                cmd += L" --backend ";
                std::string bname = aura::HardwareBackendToString(cli_backend);
                cmd += std::wstring(bname.begin(), bname.end());
            }

            STARTUPINFOW si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};

            std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
            cmd_buf.push_back(L'\0');

            if (CreateProcessW(NULL, cmd_buf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                DWORD wait_res = WaitForSingleObject(pi.hProcess, 10000);
                if (wait_res == WAIT_TIMEOUT) {
                    LOG_WARN("【异常退出自愈】探测子进程执行超时，已强制终止并放弃等待");
                    TerminateProcess(pi.hProcess, 1);
                    WaitForSingleObject(pi.hProcess, 1000);
                    probe_failed = true;
                } else {
                    DWORD exit_code = 0;
                    GetExitCodeProcess(pi.hProcess, &exit_code);
                    if (exit_code == 0) {
                        LOG_INFO("【异常退出自愈】探测子进程执行成功 (硬件通道正常，未发生驱动崩溃)");
                        std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    } else {
                        std::ostringstream oss;
                        oss << "0x" << std::hex << std::uppercase << exit_code;
                        LOG_WARN("【异常退出自愈】探测子进程异常终止 (退出码: " + oss.str() + 
                                 ")，底层硬件驱动异常！");
                        probe_failed = true;
                    }
                }
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            } else {
                LOG_WARN("【异常退出自愈】创建探测子进程失败 (Win32错误码: " + std::to_string(GetLastError()) + ")");
                probe_failed = true;
            }
        }
    }

    if (probe_failed) {
        LOG_WARN("【安全降级】硬件探测异常，为防止主守护进程崩溃闪退，自动降级为虚拟推流模式 (Dry-Run)！WebUI 与规则系统将保持正常运行。");
        dry_run = true;
        had_abnormal_exit = false;
        RemoveAllStateFiles();
    }

    aura::HardwareBackend hw_backend = has_cli_backend ? cli_backend : rule_engine.GetHardwareBackend();
    aura::AuraAdapter adapter(dry_run, hw_backend);
    if (!adapter.Initialize(&keymap)) {
        LOG_WARN("底层硬件初次挂载未就绪，将在推流循环中自动重连");
    } else {
        LOG_INFO("AuraAdapter 硬件初始化成功: backend=" + adapter.GetActiveBackendName() + 
                 (adapter.GetDevicePath().empty() ? "" : (", path=" + adapter.GetDevicePath())));
    }

    // 异常退出强制复位兜底
    if (had_abnormal_exit) {
        if (!dry_run) {
            adapter.ForceReset();
        }
        RemoveAllStateFiles();
    }

    // 9. 启动 CS2 GSI 适配器 (严格监听 127.0.0.1:19897)
    // 核心线程隔离纪律：网络 I/O 线程仅在内存中维护 GsiState 键值字典，严禁直接触碰 COM/HAL
    aura::GsiAdapter gsi_adapter;
    if (!gsi_adapter.Start(19897)) {
        LOG_WARN("CS2 GSI 适配器监听 127.0.0.1:19897 失败 (可能端口已被占用)");
    } else {
        LOG_INFO("[+] CS2 GSI 接收服务已启动，监听 http://127.0.0.1:19897/");
    }

    // 9.5 自动加载已安装的动态光效插件 (plugins/)
    aura::PluginManager::Instance().LoadAllFromDirectory("plugins");

    // 10. 构建效果引擎
    aura::EffectEngine effect_engine;
    std::shared_ptr<const aura::Profile> initial_profile = rule_engine.MatchProfile("", &gsi_adapter.GetState());
    std::string current_active_profile_name = initial_profile ? initial_profile->name : "(None)";
    effect_engine.SetActiveProfile(initial_profile);

    // 同步编排事件覆盖规则至 OverlayManager
    auto sync_event_overlays = [&rule_engine, &effect_engine]() {
        auto& om = effect_engine.GetOverlayManager();
        om.ClearBindings();
        for (const auto& r : rule_engine.GetEventOverlayRules()) {
            aura::OverlayBinding b;
            b.id = r.id;
            b.trigger = r.trigger;
            b.priority = r.priority;
            b.condition = [condition = r.condition](const aura::GsiState* gsi, const std::string& proc) {
                return condition.Evaluate(gsi, proc);
            };
            b.event_name = r.event;
            b.effect_name = r.effect;
            b.duration_ms = r.duration_ms;
            b.fade_out_ms = r.fade_out_ms;
            b.attack_ms = r.attack_ms;
            b.blend_mode = r.blend_mode;

            auto prof = rule_engine.GetProfile(r.effect);
            if (prof && prof->base_effect) {
                b.effect = prof->base_effect;
            } else {
                b.effect = aura::PluginManager::Instance().CreateEffect(r.effect);
            }
            b.make_effect = [name = (prof && !prof->plugin_name.empty()) ? prof->plugin_name : r.effect, fallback = b.effect]() {
                auto instance = aura::PluginManager::Instance().CreateEffect(name);
                return instance ? instance : fallback;
            };
            if (b.effect) {
                om.RegisterBinding(b);
            }
        }
    };
    sync_event_overlays();

    // 注册守护进程插件热重载 IPC 处理回调
    gsi_adapter.SetPluginReloadHandler([&rule_engine, &effect_engine, &sync_event_overlays](const std::string& name) {
        LOG_INFO("[Daemon] 收到插件热重载 IPC 请求: " << name);
        bool reloaded = aura::PluginManager::Instance().ReloadPlugin(name);
        if (reloaded) {
            rule_engine.CheckAndReload();
            sync_event_overlays();
        }
        return reloaded;
    });

    // 注册守护进程编辑态推流硬件预览 IPC 回调
    gsi_adapter.SetPreviewHandler([&effect_engine, &keymap](const std::string& body) {
        try {
            auto j = nlohmann::json::parse(body);
            if (j.contains("colors") && j["colors"].is_array()) {
                aura::FrameBuffer fb;
                fb.Clear();
                const auto& arr = j["colors"];
                const auto& layout_keys = aura::Keymap::GetStandardLayoutKeys();
                if (arr.size() == layout_keys.size()) {
                    // 68 键标准物理键盘配列推流帧映射：将前端 68 键色彩几何序列精准映射至硬件矩阵 led_id
                    for (size_t i = 0; i < layout_keys.size(); ++i) {
                        if (arr[i].is_array() && arr[i].size() >= 3) {
                            int r = std::clamp(arr[i][0].get<int>(), 0, 255);
                            int g = std::clamp(arr[i][1].get<int>(), 0, 255);
                            int b = std::clamp(arr[i][2].get<int>(), 0, 255);
                            int led_id = -1;
                            if (keymap.FindLedId(layout_keys[i], led_id) && led_id >= 0 && led_id < static_cast<int>(aura::TOTAL_LEDS)) {
                                fb.SetKey(static_cast<size_t>(led_id), static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
                            }
                        }
                    }
                } else {
                    // 原始底层帧缓冲直接赋值兜底 (支持原始 128 通道及单元测试)
                    for (size_t i = 0; i < arr.size() && (i * 3 + 2) < aura::FRAME_BUFFER_SIZE; ++i) {
                        if (arr[i].is_array() && arr[i].size() >= 3) {
                            fb.buffer[i * 3 + 0] = static_cast<uint8_t>(std::clamp(arr[i][0].get<int>(), 0, 255));
                            fb.buffer[i * 3 + 1] = static_cast<uint8_t>(std::clamp(arr[i][1].get<int>(), 0, 255));
                            fb.buffer[i * 3 + 2] = static_cast<uint8_t>(std::clamp(arr[i][2].get<int>(), 0, 255));
                        }
                    }
                }
                uint64_t dur = j.value("duration_ms", 300ULL);
                effect_engine.SetPreviewFrame(fb, dur);
                return true;
            } else if (j.contains("colors") && j["colors"].is_object()) {
                aura::FrameBuffer fb;
                fb.Clear();
                for (auto& [kname, col_arr] : j["colors"].items()) {
                    if (col_arr.is_array() && col_arr.size() >= 3) {
                        int led_id = -1;
                        if (keymap.FindLedId(kname, led_id) && led_id >= 0 && led_id < static_cast<int>(aura::TOTAL_LEDS)) {
                            fb.SetKey(static_cast<size_t>(led_id),
                                      static_cast<uint8_t>(std::clamp(col_arr[0].get<int>(), 0, 255)),
                                      static_cast<uint8_t>(std::clamp(col_arr[1].get<int>(), 0, 255)),
                                      static_cast<uint8_t>(std::clamp(col_arr[2].get<int>(), 0, 255)));
                        }
                    }
                }
                uint64_t dur = j.value("duration_ms", 300ULL);
                effect_engine.SetPreviewFrame(fb, dur);
                return true;
            }
        } catch (...) {}
        return false;
    });

    // 11. 启动网页配置服务后台监护器 (默认桌面状态自动拉起，由独立工线程异步监护)
    aura::WebUiSupervisor web_supervisor;
    web_supervisor.StartSupervisor(std::filesystem::path(config_path), 19898);

    // 12. 启动前台窗口监控线程 (WinEventHook 专属消息线程)
    // 线程纪律：WinEventHook 回调运行在监控线程，只做【最小化通知】——把前台进程名写入
    // GsiState（该写入已由 GsiState::mutex_ 保护）。回调内【不】做规则匹配、不切换方案、
    // 不触碰效果引擎与网页监护器，也【不】读写任何主循环的局部变量，从而消除跨线程数据竞争。
    // 所有决策统一收敛到主循环单点执行（见下方 25FPS 循环）。
    aura::ForegroundMonitor monitor;

    monitor.SetCallback([&gsi_adapter](const std::string& proc_name, HWND /*hwnd*/) {
        gsi_adapter.GetState().SetForegroundProcess(proc_name);
    });

    if (!monitor.Start()) {
        LOG_ERROR("启动前台窗口监控失败");
    }

    // 写入当前运行标记 (PID + 时间戳) - 必须在底层驱动及全部子系统初始化成功后写入，避免启动崩溃留下残留标记
    {
        std::ofstream state_out(state_file, std::ios::trunc);
        if (state_out.is_open()) {
            state_out << "PID=" << GetCurrentProcessId() << "\n"
                      << "StartTime=" << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) << "\n";
        }
    }

    // 13. 主循环：25 FPS (40ms) 定时时钟推流与配置热重载
    LOG_INFO("[+] ROG FALCHION ACE HFX 守护进程正在运行中 (按 Ctrl+C 优雅退出)...");

    aura::FrameBuffer frame_buf;
    int target_fps = rule_engine.GetFps();
    if (target_fps < 10 || target_fps > 100) target_fps = 25;
    std::chrono::milliseconds frame_time(1000 / target_fps);

    auto loop_start = std::chrono::steady_clock::now();
    auto next_tick = loop_start;
    auto last_stat_time = loop_start;
    auto last_reload_check = loop_start;

    uint64_t total_frames = 0;
    uint64_t frames_since_stat = 0;
    uint64_t current_minute = 0;
    // 主循环独占：上次已按【前台进程名】处理过的值，用于驱动网页服务抑制状态
    // （仅主循环访问，故无需加锁；这正是 R2 把决策单点化后获得的简化）
    std::string last_proc_seen = "__UNSET__";

    while (g_running.load(std::memory_order_acquire)) {
        // 主线程统一评估当前前台进程与 GSI 状态驱动的灯效方案
        // (严格遵循主线程独占 COM/HAL 纪律，绝不在网络线程执行硬件调用)
        std::string cur_proc = monitor.GetCurrentProcessName();
        gsi_adapter.GetState().SetForegroundProcess(cur_proc);
        std::shared_ptr<const aura::Profile> matched = rule_engine.MatchProfile(cur_proc, &gsi_adapter.GetState());
        std::string prof_name = matched ? matched->name : "(None)";
        if (prof_name != current_active_profile_name) {
            current_active_profile_name = prof_name;
            effect_engine.SetActiveProfile(matched);
            int new_fps = (matched && matched->fps >= 10 && matched->fps <= 100) ? matched->fps : rule_engine.GetFps();
            if (new_fps < 10 || new_fps > 100) new_fps = 25;
            if (new_fps != target_fps) {
                target_fps = new_fps;
                frame_time = std::chrono::milliseconds(1000 / target_fps);
                next_tick = std::chrono::steady_clock::now();
            }
            LOG_INFO("灯效方案动态切换 -> [" + prof_name + "] (前台: " + 
                     (cur_proc.empty() ? "桌面/未知" : cur_proc) + 
                     (gsi_adapter.GetState().IsActive() ? ", GSI在线" : "") + 
                     ", 帧率: " + std::to_string(target_fps) + " FPS)");
        }

        // 网页服务抑制状态：必须按【前台进程名】变化来更新，不能挂在【方案名】变化上。
        // 原因：不同进程可能映射到同一个方案（例如 code.exe 与 devenv.exe 都 → coding），
        // 此时方案名不变；若只在方案切换时更新，就会漏掉 suppress_web_ui 标记的变化，
        // 导致该开的服务没开、该停的没停。
        // SetSuppressed 内部为原子交换，且仅在值真正变化时才 notify，重复调用无副作用。
        if (cur_proc != last_proc_seen) {
            last_proc_seen = cur_proc;
            bool suppress = rule_engine.ShouldSuppressWebUi(cur_proc, &gsi_adapter.GetState());
            web_supervisor.SetSuppressed(suppress);
            LOG_INFO("前台进程变更 -> [" + (cur_proc.empty() ? "桌面/未知" : cur_proc) + "]" +
                     (suppress ? " (网页服务已抑制)" : ""));
        }

        web_supervisor.SetSuppressed(rule_engine.ShouldSuppressWebUi(cur_proc, &gsi_adapter.GetState()));

        // 零分配计算当前帧 (包含 GSI 原子读取与瞬态事件叠加)
        effect_engine.GetOverlayManager().UpdateBindingsFromGsi(&gsi_adapter.GetState(), effect_engine.GetElapsedMs(), aura::RuleEngine::ToLower(cur_proc));
        effect_engine.Tick(frame_buf, keymap, &gsi_adapter.GetState());

        // 推流至硬件
        if (adapter.IsConnected()) {
            adapter.PushFrame(frame_buf);
        } else {
            adapter.CheckReconnect();
        }

        total_frames++;
        frames_since_stat++;

        // 实时热重载检测 (每 50ms 轮询一次文件修改时间，保障网页调参实时生效)
        auto now_reload = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now_reload - last_reload_check).count() >= 50) {
            last_reload_check = now_reload;
            if (rule_engine.CheckAndReload()) {
                sync_event_overlays();
                std::string cur_proc_now = monitor.GetCurrentProcessName();
                std::shared_ptr<const aura::Profile> reload_matched = rule_engine.MatchProfile(cur_proc_now, &gsi_adapter.GetState());
                current_active_profile_name = reload_matched ? reload_matched->name : "(None)";
                effect_engine.SetActiveProfile(reload_matched);

                int new_fps = (reload_matched && reload_matched->fps >= 10 && reload_matched->fps <= 100) ? reload_matched->fps : rule_engine.GetFps();
                if (new_fps < 10 || new_fps > 100) new_fps = 25;
                if (new_fps != target_fps) {
                    target_fps = new_fps;
                    frame_time = std::chrono::milliseconds(1000 / target_fps);
                    next_tick = std::chrono::steady_clock::now();
                }

                bool suppress = rule_engine.ShouldSuppressWebUi(cur_proc_now, &gsi_adapter.GetState());
                web_supervisor.SetSuppressed(suppress);

                LOG_INFO("配置实时重载生效，当前活跃方案更新为: [" + current_active_profile_name + "]" + 
                         (suppress ? " (网页服务已抑制)" : "") +
                         ", 帧率: " + std::to_string(target_fps) + " FPS");
            }
        }

        // 统计与长效稳定性监控 (每 60 秒打印一次)
        auto now = std::chrono::steady_clock::now();
        auto stat_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_stat_time).count();
        if (stat_elapsed_ms >= 60000) {
            current_minute++;
            double instant_fps = static_cast<double>(frames_since_stat) / (static_cast<double>(stat_elapsed_ms) / 1000.0);
            auto total_elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(now - loop_start).count();
            double avg_fps = total_elapsed_sec > 0 ? (static_cast<double>(total_frames) / static_cast<double>(total_elapsed_sec)) : 0.0;

            aura::ProcessMemoryStats mem{};
            aura::GetCurrentProcessMemory(mem);

            std::string mode_str = dry_run ? "[Dry-Run]" : "[Hardware]";
            LOG_INFO(mode_str + " [推流稳定性报告 第 " + std::to_string(current_minute) + " 分钟] " +
                     "累计帧数: " + std::to_string(total_frames) + 
                     ", 瞬时FPS: " + std::to_string(instant_fps).substr(0, 5) + 
                     ", 平均FPS: " + std::to_string(avg_fps).substr(0, 5) + 
                     ", 物理内存WorkingSet: " + aura::FormatBytes(mem.working_set_bytes) + 
                     ", 提交内存PrivateBytes: " + aura::FormatBytes(mem.private_bytes));

            last_stat_time = now;
            frames_since_stat = 0;

            // 若处于稳定性验收测试模式且达到指定分钟数
            if (test_stability_minutes > 0.0 && current_minute >= static_cast<uint64_t>(test_stability_minutes)) {
                LOG_INFO("【稳定性验收达成】已完成预设的 " + std::to_string(test_stability_minutes) + 
                         " 分钟持续推流测试！正在退出测试循环...");
                g_running.store(false, std::memory_order_release);
                break;
            }
        }

        // 对齐帧节拍
        next_tick += frame_time;
        auto sleep_dur = next_tick - std::chrono::steady_clock::now();
        if (sleep_dur > std::chrono::milliseconds(0)) {
            std::this_thread::sleep_for(sleep_dur);
        } else {
            next_tick = std::chrono::steady_clock::now();
        }
    }

    // 14. 优雅停机与资源回收
    LOG_INFO("正在停止网页配置服务监护器...");
    web_supervisor.Shutdown();

    LOG_INFO("正在停止 CS2 GSI 接收服务...");
    gsi_adapter.Stop();

    LOG_INFO("正在停止前台监控线程...");
    monitor.Stop();

    LOG_INFO("正在安全关闭 Aura 硬件连接...");
    adapter.Shutdown();

    // 正常优雅退出：清除运行状态标记文件
    RemoveAllStateFiles();
    LOG_INFO("[+] 运行状态标记已清除 (.daemon_running 已删除)");

    if (hMutex) {
        CloseHandle(hMutex);
    }

    g_shutdown_done.store(true, std::memory_order_release);
    LOG_INFO("[+] 守护进程已优雅退出，所有资源已安全释放。");
    return 0;
    } catch (const std::exception& e) {
        LOG_ERROR("FATAL: 守护进程捕获未处理的 C++ 异常: " + std::string(e.what()));
        std::cerr << "\n[Aura 异常退出] " << e.what() << "\n";
        RemoveAllStateFiles();
        return 1;
    } catch (...) {
        LOG_ERROR("FATAL: 守护进程捕获未知底层异常！");
        std::cerr << "\n[Aura 异常退出] 捕获未知底层异常！\n";
        RemoveAllStateFiles();
        return 1;
    }
}
