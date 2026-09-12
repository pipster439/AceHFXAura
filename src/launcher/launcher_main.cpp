#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>

#pragma comment(lib, "shell32.lib")

// Resource IDs defined in launcher.rc
#define IDR_DAEMON       101
#define IDR_WEB_UI       102
#define IDR_HAL_DLL      103
#define IDR_KEYMAP       104
#define IDR_CONFIG_EX    105
#define IDR_WEB_HTML     106

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

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    // 0. 单实例检测：若核心守护进程已在运行，直接唤起 Web 控制面板并优雅退出，避免闪退困惑
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

    // 2. 解压核心运行时资产
    std::filesystem::path daemon_exe = runtime_dir / L"aura_daemon.exe";
    std::filesystem::path web_ui_exe = runtime_dir / L"aura_web_ui.exe";
    std::filesystem::path hal_dll    = runtime_dir / L"AacKbHal_x64.dll";
    std::filesystem::path keymap_json= runtime_dir / L"calibrated_keymap.json";
    std::filesystem::path cfg_example= runtime_dir / L"config.example.json";
    std::filesystem::path web_html   = runtime_dir / L"web" / L"index.html";

    bool ok = true;
    ok &= ExtractResource(hSelf, IDR_DAEMON, daemon_exe, force_extract);
    ok &= ExtractResource(hSelf, IDR_WEB_UI, web_ui_exe, force_extract);
    ok &= ExtractResource(hSelf, IDR_HAL_DLL, hal_dll, force_extract);
    ok &= ExtractResource(hSelf, IDR_KEYMAP, keymap_json, force_extract);
    ok &= ExtractResource(hSelf, IDR_CONFIG_EX, cfg_example, force_extract);
    ok &= ExtractResource(hSelf, IDR_WEB_HTML, web_html, force_extract);

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

    // 3. 配置文件智能放置：
    // 若当前工作目录下已有 config.json 则使用之；若无则自动释放 config.json 供用户自由定制
    std::filesystem::path cwd_config = std::filesystem::current_path() / L"config.json";
    if (!std::filesystem::exists(cwd_config, ec)) {
        std::filesystem::copy_file(cfg_example, cwd_config, std::filesystem::copy_options::overwrite_existing, ec);
    }

    // 4. 构建启动命令行 (完全透明透传用户参数)
    std::wstring cmd_line = L"\"" + daemon_exe.wstring() + L"\"";
    bool has_config_arg = false;
    bool has_keymap_arg = false;

    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--config") has_config_arg = true;
        if (arg == L"--keymap") has_keymap_arg = true;
        cmd_line += L" \"" + arg + L"\"";
    }

    // 若用户未显式指定，默认关联 CWD 下的 config.json 与解压出的 calibrated_keymap.json
    if (!has_config_arg && std::filesystem::exists(cwd_config)) {
        cmd_line += L" --config \"" + cwd_config.wstring() + L"\"";
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
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    // 7. 同步等待主进程生命周期
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);

    if (hJob) {
        CloseHandle(hJob);
    }

    return static_cast<int>(exit_code);
}
