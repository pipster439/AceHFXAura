#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>

#pragma comment(lib, "shell32.lib")

// Resource IDs defined in launcher.rc
#define IDR_DAEMON             101
#define IDR_WEB_UI             102
#define IDR_KEYMAP             104
#define IDR_CONFIG_EX          105
#define IDR_WEB_HTML           106
#define IDR_SDK_EFFECT         110
#define IDR_SDK_PLUGIN_INTF    111
#define IDR_SDK_AURA_TYPES     112
#define IDR_SDK_KEYMAP         113

namespace {

bool ExtractResource(HMODULE hMod, int resId, const std::filesystem::path& destPath, bool force_extract = false) {
    HRSRC hRes = FindResourceW(hMod, MAKEINTRESOURCEW(resId), MAKEINTRESOURCEW(10));
    if (!hRes) {
        std::cerr << "[Aura] 找不到内嵌资源 ID: " << resId << std::endl;
        return false;
    }

    DWORD size = SizeofResource(hMod, hRes);
    HGLOBAL hData = LoadResource(hMod, hRes);
    if (!hData) {
        return false;
    }

    const void* pData = LockResource(hData);
    if (!pData || size == 0) {
        return false;
    }

    // 检查目标文件是否已存在且大小完全一致（避免每次启动重复写磁盘）
    std::error_code ec;
    if (!force_extract && std::filesystem::exists(destPath, ec) && std::filesystem::file_size(destPath, ec) == size) {
        return true;
    }

    std::filesystem::create_directories(destPath.parent_path(), ec);

    std::ofstream out(destPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "[Aura] 无法写入资源至目标路径: " << destPath.string() << std::endl;
        return false;
    }

    out.write(reinterpret_cast<const char*>(pData), size);
    out.flush();
    return true;
}

std::filesystem::path GetRuntimeDir() {
    wchar_t local_app_data[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH)) {
        return std::filesystem::path(local_app_data) / L"Aura" / L"runtime";
    }
    wchar_t temp_path[MAX_PATH];
    if (GetTempPathW(MAX_PATH, temp_path)) {
        return std::filesystem::path(temp_path) / L"Aura_Runtime";
    }
    return std::filesystem::current_path() / L".aura_runtime";
}

HANDLE g_hChildProcess = nullptr;

BOOL WINAPI LauncherCtrlHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT) {
        // 由子进程捕获并执行优雅停机，父进程在主循环 WaitForSingleObject 处等待，不在此提前退出
        return TRUE;
    }
    if (signal == CTRL_CLOSE_EVENT) {
        if (g_hChildProcess) {
            WaitForSingleObject(g_hChildProcess, 3500);
        }
        return TRUE;
    }
    return FALSE;
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    // 注册控制台信号处理器：拦截 Ctrl+C，让子进程执行优雅停机，防止 JobObject 瞬间强杀
    SetConsoleCtrlHandler(LauncherCtrlHandler, TRUE);

    // 0. 单实例检测：仅在无参数启动（如双击图标）时检查；若用户传入了命令行参数则透传给守护进程处理
    if (argc == 1) {
        HANDLE hExistingMutex = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\RogFalchionAceHfxDaemonMutex");
        if (hExistingMutex) {
            CloseHandle(hExistingMutex);
            SetConsoleOutputCP(CP_UTF8);
            SetConsoleCP(CP_UTF8);
            std::cout << "[Aura] 检测到 ROG Falchion Ace HFX 核心守护进程已在后台运行中。\n";
            std::cout << "[Aura] 正在自动打开 Web 控制面板: http://127.0.0.1:19898/ ...\n";
            ShellExecuteW(nullptr, L"open", L"http://127.0.0.1:19898/", nullptr, nullptr, SW_SHOWNORMAL);
            Sleep(1500);
            return 0;
        }
    }

    // 1. 设置控制台 UTF-8 输出
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    HMODULE hSelf = GetModuleHandleW(nullptr);
    std::filesystem::path runtime_dir = GetRuntimeDir();
    std::error_code ec;
    std::filesystem::create_directories(runtime_dir, ec);

    // 检查启动器是否更新：若启动器二进制时间戳更新，或存在显式重解压参数，则强制刷新全套运行时资产
    wchar_t self_path_buf[MAX_PATH];
    GetModuleFileNameW(nullptr, self_path_buf, MAX_PATH);
    std::filesystem::path self_path(self_path_buf);

    bool force_extract = false;
    std::filesystem::path stamp_file = runtime_dir / L".launcher_build";
    auto self_time = std::filesystem::last_write_time(self_path, ec);
    if (!ec) {
        if (!std::filesystem::exists(stamp_file, ec)) {
            force_extract = true;
        } else {
            auto stamp_time = std::filesystem::last_write_time(stamp_file, ec);
            if (ec || self_time > stamp_time) {
                force_extract = true;
            }
        }
    } else {
        force_extract = true;
    }

    // 2. 解压核心运行时资产与 Plugin SDK
    std::filesystem::path daemon_exe = runtime_dir / L"aura_daemon.exe";
    std::filesystem::path web_ui_exe = runtime_dir / L"aura_web_ui.exe";
    std::filesystem::path keymap_json= runtime_dir / L"calibrated_keymap.json";
    std::filesystem::path cfg_example= runtime_dir / L"config.example.json";
    std::filesystem::path web_html   = runtime_dir / L"web" / L"index.html";

    std::filesystem::path sdk_effect_h = runtime_dir / L"include" / L"engine" / L"effect.h";
    std::filesystem::path sdk_plugin_h = runtime_dir / L"include" / L"engine" / L"plugin_interface.h";
    std::filesystem::path sdk_types_h  = runtime_dir / L"include" / L"aura" / L"aura_types.h";
    std::filesystem::path sdk_keymap_h = runtime_dir / L"include" / L"aura" / L"keymap.h";

    bool ok = true;
    ok &= ExtractResource(hSelf, IDR_DAEMON, daemon_exe, force_extract);
    ok &= ExtractResource(hSelf, IDR_WEB_UI, web_ui_exe, force_extract);
    ok &= ExtractResource(hSelf, IDR_KEYMAP, keymap_json, force_extract);
    ok &= ExtractResource(hSelf, IDR_CONFIG_EX, cfg_example, force_extract);
    ok &= ExtractResource(hSelf, IDR_WEB_HTML, web_html, force_extract);
    ok &= ExtractResource(hSelf, IDR_SDK_EFFECT, sdk_effect_h, force_extract);
    ok &= ExtractResource(hSelf, IDR_SDK_PLUGIN_INTF, sdk_plugin_h, force_extract);
    ok &= ExtractResource(hSelf, IDR_SDK_AURA_TYPES, sdk_types_h, force_extract);
    ok &= ExtractResource(hSelf, IDR_SDK_KEYMAP, sdk_keymap_h, force_extract);

    if (!ok) {
        std::cerr << "[Aura] 错误: 内嵌运行时解压失败！" << std::endl;
        std::cout << "[Aura] 窗口将在 3 秒后退出...\n";
        Sleep(3000);
        return 1;
    }

    if (force_extract) {
        std::ofstream stamp_out(stamp_file, std::ios::trunc);
        if (stamp_out.is_open()) {
            stamp_out << "Aura Runtime Synchronized\n";
        }
        std::filesystem::last_write_time(stamp_file, self_time, ec);
    }

    // 3. 构建启动命令行 (完全透明透传用户参数)
    std::wstring cmd_line = L"\"" + daemon_exe.wstring() + L"\"";
    bool has_config_arg = false;
    bool has_keymap_arg = false;
    bool is_help = false;

    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--help" || arg == L"-h") is_help = true;
        if (arg == L"--config") has_config_arg = true;
        if (arg == L"--keymap") has_keymap_arg = true;
        cmd_line += L" \"" + arg + L"\"";
    }

    // 4. 配置文件智能放置：
    // 仅在用户未显式指定 --config 且非单纯帮助模式时才在当前工作目录释放默认 config.json
    std::filesystem::path cwd_config = std::filesystem::current_path() / L"config.json";
    if (!has_config_arg && !is_help) {
        if (!std::filesystem::exists(cwd_config, ec)) {
            std::filesystem::copy_file(cfg_example, cwd_config, std::filesystem::copy_options::skip_existing, ec);
            if (ec || !std::filesystem::exists(cwd_config, ec)) {
                std::cerr << "[Aura] 错误: 无法在当前目录创建默认 config.json: " << (ec ? ec.message() : "未知错误") << std::endl;
                return 1;
            }
        }
        if (std::filesystem::exists(cwd_config)) {
            cmd_line += L" --config \"" + cwd_config.wstring() + L"\"";
        }
    }
    if (!has_keymap_arg && std::filesystem::exists(keymap_json)) {
        cmd_line += L" --keymap \"" + keymap_json.wstring() + L"\"";
    }

    // 5. 创建 Windows Job Object 并启用 KILL_ON_JOB_CLOSE (终极防孤儿保护)
    HANDLE hJob = CreateJobObjectW(nullptr, nullptr);
    if (hJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    }

    // 6. 启动主守护进程
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> cmd_buf(cmd_line.begin(), cmd_line.end());
    cmd_buf.push_back(L'\0');

    DWORD creation_flags = CREATE_SUSPENDED;

    BOOL proc_ok = CreateProcessW(
        nullptr,
        cmd_buf.data(),
        nullptr,
        nullptr,
        TRUE,
        creation_flags,
        nullptr,
        nullptr, // 继承 CWD
        &si,
        &pi
    );

    if (!proc_ok) {
        std::cerr << "[Aura] 无法启动核心守护进程 (错误代码: " << GetLastError() << ")" << std::endl;
        if (hJob) CloseHandle(hJob);
        std::cout << "[Aura] 启动异常，窗口将在 3 秒后退出...\n";
        Sleep(3000);
        return 1;
    }

    if (hJob) {
        AssignProcessToJobObject(hJob, pi.hProcess);
    }
    g_hChildProcess = pi.hProcess;
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    // 7. 同步等待主进程生命周期
    WaitForSingleObject(pi.hProcess, INFINITE);
    g_hChildProcess = nullptr;

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);

    if (hJob) {
        CloseHandle(hJob);
    }

    return static_cast<int>(exit_code);
}
