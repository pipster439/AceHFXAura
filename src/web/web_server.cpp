#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "web/web_server.h"
#include "third_party/json.hpp"
#include <fstream>
#include <iostream>
#include <chrono>
#include <filesystem>
#include <cctype>
#include <algorithm>
#include <vector>

namespace aura {

namespace {

// -----------------------------------------------------------------------------
// Win32 Code Page & UTF-8 Encoding Helpers
// -----------------------------------------------------------------------------

// 检查并确保字符串为合法 UTF-8 编码，若包含 OEM/ANSI (如 Windows CP936) 非 UTF-8 字符则进行转码，防范 nlohmann::json 抛出 type_error.316
std::string EnsureValidUtf8(const std::string& input) {
    if (input.empty()) {
        return "";
    }

    // 1. 检查是否已经是合法的 UTF-8 编码 (防止重复转码造成乱码)
    int valid_utf8_len = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        input.data(),
        static_cast<int>(input.size()),
        nullptr,
        0
    );
    if (valid_utf8_len > 0) {
        return input;
    }

    // 2. 非合法 UTF-8，优先尝试从系统控制台 OEM 代码页 (如 CP936 GBK) 转码
    UINT src_cp = GetOEMCP();
    if (src_cp == 0) src_cp = CP_OEMCP;

    int wlen = MultiByteToWideChar(src_cp, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (wlen <= 0) {
        // 回退至系统 ANSI 代码页 (CP_ACP)
        src_cp = GetACP();
        if (src_cp == 0) src_cp = CP_ACP;
        wlen = MultiByteToWideChar(src_cp, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
    }

    if (wlen <= 0) {
        // 极端异常二进制数据兜底：替换为 Unicode Replacement Character \uFFFD
        std::string safe;
        safe.reserve(input.size());
        for (unsigned char c : input) {
            if (c < 0x80) {
                safe.push_back(static_cast<char>(c));
            } else {
                safe += "\xEF\xBF\xBD"; // U+FFFD in UTF-8
            }
        }
        return safe;
    }

    std::wstring wstr(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(src_cp, 0, input.data(), static_cast<int>(input.size()), wstr.data(), wlen);

    int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (ulen <= 0) {
        return input;
    }

    std::string utf8_str(static_cast<size_t>(ulen), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), utf8_str.data(), ulen, nullptr, nullptr);
    return utf8_str;
}

inline std::string EnsureUtf8(const std::string& input) {
    return EnsureValidUtf8(input);
}

} // namespace

std::filesystem::path FindVcvars64Bat(const std::filesystem::path& explicit_path) {
    // Testing override: simulate clean machine with no MSVC
    const wchar_t* force_no = _wgetenv(L"AURA_TEST_FORCE_NO_MSVC");
    if (force_no && wcscmp(force_no, L"1") == 0) {
        return {};
    }

    if (!explicit_path.empty()) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(explicit_path, ec)) {
            return explicit_path;
        }
    }

    // Cache lookup for performance (cache both positive AND negative results)
    static std::filesystem::path s_cached_vcvars;
    static bool s_cache_initialized = false;
    static std::chrono::steady_clock::time_point s_cache_time{};
    auto now = std::chrono::steady_clock::now();
    if (explicit_path.empty() && s_cache_initialized && (now - s_cache_time < std::chrono::seconds(5))) {
        return s_cached_vcvars;
    }

    auto update_cache = [&](const std::filesystem::path& val) -> std::filesystem::path {
        if (explicit_path.empty()) {
            s_cached_vcvars = val;
            s_cache_initialized = true;
            s_cache_time = now;
        }
        return val;
    };

    // Environment variables overrides
    const wchar_t* env_vars[] = {
        L"AURA_VCVARS64_PATH",
        L"AURA_VCVARS_BAT"
    };
    for (const wchar_t* ev : env_vars) {
        const wchar_t* val = _wgetenv(ev);
        if (val && val[0] != L'\0') {
            std::error_code ec;
            std::filesystem::path p(val);
            if (std::filesystem::is_regular_file(p, ec)) {
                return update_cache(p);
            }
        }
    }

    // Check if launched from Developer Command Prompt (VSINSTALLDIR / VCINSTALLDIR)
    const wchar_t* vs_install = _wgetenv(L"VSINSTALLDIR");
    if (vs_install && vs_install[0] != L'\0') {
        std::error_code ec;
        std::filesystem::path p = std::filesystem::path(vs_install) / L"VC" / L"Auxiliary" / L"Build" / L"vcvars64.bat";
        if (std::filesystem::is_regular_file(p, ec)) {
            return update_cache(p);
        }
    }
    const wchar_t* vc_install = _wgetenv(L"VCINSTALLDIR");
    if (vc_install && vc_install[0] != L'\0') {
        std::error_code ec;
        std::filesystem::path p = std::filesystem::path(vc_install) / L"Auxiliary" / L"Build" / L"vcvars64.bat";
        if (std::filesystem::is_regular_file(p, ec)) {
            return update_cache(p);
        }
    }

    // Known candidate paths across drive letters C, D, E
    std::vector<std::wstring> drives = { L"C:", L"D:", L"E:" };
    std::vector<std::wstring> subpaths = {
        L"\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat",
        L"\\Program Files\\Microsoft Visual Studio\\18\\Professional\\VC\\Auxiliary\\Build\\vcvars64.bat",
        L"\\Program Files\\Microsoft Visual Studio\\18\\Enterprise\\VC\\Auxiliary\\Build\\vcvars64.bat",
        L"\\Program Files\\Microsoft Visual Studio\\18\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat",
        L"\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat",
        L"\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\VC\\Auxiliary\\Build\\vcvars64.bat",
        L"\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\VC\\Auxiliary\\Build\\vcvars64.bat",
        L"\\Program Files\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat",
        L"\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat",
        L"\\Program Files (x86)\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat",
        L"\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat",
        L"\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Professional\\VC\\Auxiliary\\Build\\vcvars64.bat",
        L"\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Enterprise\\VC\\Auxiliary\\Build\\vcvars64.bat"
    };

    std::error_code ec;
    for (const auto& drive : drives) {
        for (const auto& sp : subpaths) {
            std::filesystem::path cand = drive + sp;
            if (std::filesystem::is_regular_file(cand, ec)) {
                return update_cache(cand);
            }
        }
    }

    // Try finding via vswhere.exe
    std::vector<std::filesystem::path> vswhere_paths = {
        L"C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe",
        L"C:\\Program Files\\Microsoft Visual Studio\\Installer\\vswhere.exe"
    };
    for (const auto& vswhere : vswhere_paths) {
        if (std::filesystem::is_regular_file(vswhere, ec)) {
            std::wstring cmd = L"\"\"" + vswhere.wstring() + L"\" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath -utf8\"";
            FILE* pipe = _wpopen(cmd.c_str(), L"r");
            if (pipe) {
                char buf[512] = {0};
                if (fgets(buf, sizeof(buf), pipe)) {
                    std::string path_str(buf);
                    while (!path_str.empty() && (path_str.back() == '\r' || path_str.back() == '\n' || path_str.back() == ' ')) {
                        path_str.pop_back();
                    }
                    if (!path_str.empty()) {
                        std::filesystem::path p = std::filesystem::u8path(path_str) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat";
                        if (std::filesystem::is_regular_file(p, ec)) {
                            _pclose(pipe);
                            return update_cache(p);
                        }
                    }
                }
                _pclose(pipe);
            }
        }
    }

    return update_cache({});
}

bool IsValidPluginSdkDir(const std::filesystem::path& dir) {
    if (dir.empty()) return false;
    std::error_code ec;
    return std::filesystem::is_directory(dir, ec) &&
           std::filesystem::is_regular_file(dir / "engine" / "effect.h", ec) &&
           std::filesystem::is_regular_file(dir / "engine" / "plugin_interface.h", ec) &&
           std::filesystem::is_regular_file(dir / "aura" / "aura_types.h", ec) &&
           std::filesystem::is_regular_file(dir / "aura" / "keymap.h", ec);
}

SdkDiscoveryResult DiscoverPluginSdkIncludeDir(const std::filesystem::path& explicit_sdk_path) {
    SdkDiscoveryResult result;
    const wchar_t* force_no_sdk = _wgetenv(L"AURA_TEST_FORCE_NO_SDK");
    if (force_no_sdk && wcscmp(force_no_sdk, L"1") == 0) {
        if (!explicit_sdk_path.empty()) {
            result.probed_paths.push_back(explicit_sdk_path);
        }
        return result;
    }

    static SdkDiscoveryResult s_cached_sdk_result;
    static bool s_sdk_cache_valid = false;
    static std::chrono::steady_clock::time_point s_sdk_cache_time{};
    auto now = std::chrono::steady_clock::now();
    if (explicit_sdk_path.empty() && s_sdk_cache_valid && (now - s_sdk_cache_time < std::chrono::seconds(5))) {
        return s_cached_sdk_result;
    }

    auto update_sdk_cache = [&](const SdkDiscoveryResult& res) -> SdkDiscoveryResult {
        if (explicit_sdk_path.empty()) {
            s_cached_sdk_result = res;
            s_sdk_cache_valid = true;
            s_sdk_cache_time = now;
        }
        return res;
    };

    auto probe = [&](const std::filesystem::path& cand) -> bool {
        if (cand.empty()) return false;
        std::error_code ec;
        std::filesystem::path norm = std::filesystem::weakly_canonical(cand, ec);
        if (ec) norm = cand;
        std::wstring str = norm.wstring();
        while (str.size() > 1 && (str.back() == L'\\' || str.back() == L'/')) {
            str.pop_back();
        }
        norm = str;

        for (const auto& existing : result.probed_paths) {
            if (existing == norm) return false;
        }
        result.probed_paths.push_back(norm);
        if (IsValidPluginSdkDir(norm)) {
            result.found = true;
            result.include_dir = norm;
            return true;
        }
        return false;
    };

    // Priority 1: Explicit Plugin SDK path (CLI / Environment)
    if (!explicit_sdk_path.empty()) {
        if (probe(explicit_sdk_path)) return update_sdk_cache(result);
        if (probe(explicit_sdk_path / "include")) return update_sdk_cache(result);
    }
    const wchar_t* env_sdk = _wgetenv(L"AURA_SDK_INCLUDE_DIR");
    if (env_sdk && env_sdk[0] != L'\0') {
        if (probe(std::filesystem::path(env_sdk))) return update_sdk_cache(result);
        if (probe(std::filesystem::path(env_sdk) / "include")) return update_sdk_cache(result);
    }
    const wchar_t* env_plugin_sdk = _wgetenv(L"AURA_PLUGIN_SDK_DIR");
    if (env_plugin_sdk && env_plugin_sdk[0] != L'\0') {
        if (probe(std::filesystem::path(env_plugin_sdk))) return update_sdk_cache(result);
        if (probe(std::filesystem::path(env_plugin_sdk) / "include")) return update_sdk_cache(result);
    }

    // Priority 2: aura_web_ui.exe / runtime packaged sibling include/
    wchar_t mod_path[MAX_PATH];
    DWORD mod_len = GetModuleFileNameW(nullptr, mod_path, MAX_PATH);
    std::filesystem::path exe_dir;
    if (mod_len > 0 && mod_len < MAX_PATH) {
        exe_dir = std::filesystem::path(mod_path).parent_path();
        if (probe(exe_dir / "include")) return update_sdk_cache(result);
    }

    // Priority 3: Source checkout include/ (development mode fallback)
    // Traverse upwards from executable's directory only. Never consult CWD to avoid foreign checkout pollution.
    if (!exe_dir.empty()) {
        for (auto dir = exe_dir; !dir.empty(); dir = dir.parent_path()) {
            std::error_code ec;
            if (std::filesystem::is_regular_file(dir / "calibrated_keymap.json", ec) &&
                std::filesystem::is_regular_file(dir / "config.example.json", ec)) {
                if (probe(dir / "include")) return update_sdk_cache(result);
                break;
            }
            if (dir == dir.parent_path()) break;
        }
    }

    // Priority 4: Packaged runtime fallback from LOCALAPPDATA
    // (Used when running a standalone aura_web_ui.exe outside the runtime folder and not in a source checkout)
    wchar_t local_app_data[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH)) {
        std::filesystem::path runtime_sdk = std::filesystem::path(local_app_data) / L"Aura" / L"runtime" / L"include";
        if (probe(runtime_sdk)) return update_sdk_cache(result);
    }

    return update_sdk_cache(result);
}

bool CompileCppSourceToDll(const std::string& effect_name, 
                           const std::string& source_code, 
                           std::string& out_log, 
                           std::string& out_dll_path, 
                           int& out_exit_code,
                           const std::filesystem::path& explicit_sdk_dir,
                           const std::filesystem::path& explicit_vcvars) {
    // 1. Verify Plugin SDK existence and locate include directory
    auto sdk_res = DiscoverPluginSdkIncludeDir(explicit_sdk_dir);
    if (!sdk_res.found) {
        std::string err = "Studio native publish SDK is missing or incomplete; expected engine/effect.h and engine/plugin_interface.h.\nChecked paths:\n";
        for (const auto& p : sdk_res.probed_paths) {
            err += "  - " + EnsureValidUtf8(p.u8string()) + "\n";
        }
        out_log = err;
        out_exit_code = -6;
        return false;
    }

    // 2. Verify MSVC / C++ Build Tools toolchain
    std::filesystem::path vcvars = FindVcvars64Bat(explicit_vcvars);
    if (vcvars.empty() || !std::filesystem::exists(vcvars)) {
        out_log = "Error: Unable to locate MSVC vcvars64.bat on this Windows host.\n\n"
                  "原生发布需要 Microsoft Visual Studio / Build Tools 的 C++ Desktop workload。\n"
                  "你仍然可以编辑、预览和保存草稿；安装 C++ Build Tools 后即可发布。";
        out_exit_code = -1;
        return false;
    }

    std::error_code ec;
    std::filesystem::path plugins_dir = "plugins";
    std::filesystem::path src_dir = plugins_dir / "src";
    std::filesystem::create_directories(plugins_dir, ec);
    std::filesystem::create_directories(src_dir, ec);
    if (ec) {
        out_log = "Cannot create plugins/src directory: " + ec.message();
        out_exit_code = -2;
        return false;
    }

    std::string safe_name = effect_name;
    if (safe_name.rfind("effect_", 0) == 0) {
        safe_name = safe_name.substr(7);
    }
    std::string base_name = "effect_" + safe_name;

    std::filesystem::path src_file = src_dir / (base_name + ".cpp");
    std::filesystem::path dll_file = plugins_dir / (base_name + ".dll");
    std::filesystem::path obj_file = plugins_dir / (base_name + ".obj");

    // 3. 写入 C++ 源码
    {
        std::ofstream ofs(src_file, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) {
            out_log = "Error: Failed to write generated C++ source file to " + src_file.string();
            out_exit_code = -2;
            return false;
        }
        ofs.write(source_code.data(), source_code.size());
        ofs.flush();
        if (!ofs.good()) {
            out_log = "Failed to write generated C++ source file: " + src_file.u8string();
            out_exit_code = -2;
            return false;
        }
    }

    std::filesystem::path inc_dir = sdk_res.include_dir;
    std::wstring inc_dir_str = inc_dir.wstring();
    while (inc_dir_str.size() > 1 && (inc_dir_str.back() == L'\\' || inc_dir_str.back() == L'/')) {
        inc_dir_str.pop_back();
    }

    std::filesystem::path inc_tp = inc_dir / "third_party";
    std::wstring extra_inc = L"";
    if (std::filesystem::exists(inc_tp, ec)) {
        std::wstring inc_tp_str = inc_tp.wstring();
        while (inc_tp_str.size() > 1 && (inc_tp_str.back() == L'\\' || inc_tp_str.back() == L'/')) {
            inc_tp_str.pop_back();
        }
        extra_inc = L"/I \"" + inc_tp_str + L"\" ";
    }

    std::filesystem::path abs_src = std::filesystem::absolute(src_file);
    std::filesystem::path abs_dll = std::filesystem::absolute(dll_file);
    std::filesystem::path abs_obj = std::filesystem::absolute(obj_file);

    // 4. 构造编译器命令行
    std::wstring cmd_str = L"cmd.exe /d /s /c \"call \"" + vcvars.wstring() + L"\" >nul || (echo MSVC vcvars setup failed & exit /b 9008) & where cl.exe >nul || (echo MSVC cl.exe not found & exit /b 9009) & cl.exe /nologo /std:c++17 /O2 /EHsc /utf-8 /MD /LD /DNOMINMAX /DWIN32_LEAN_AND_MEAN "
        + L"/I \"" + inc_dir_str + L"\" "
        + extra_inc
        + L"/Fe:\"" + abs_dll.wstring() + L"\" "
        + L"/Fo:\"" + abs_obj.wstring() + L"\" "
        + L"\"" + abs_src.wstring() + L"\" /link /INCREMENTAL:NO\"";

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE hPipeRead = nullptr;
    HANDLE hPipeWrite = nullptr;
    if (!CreatePipe(&hPipeRead, &hPipeWrite, &sa, 0)) {
        out_log = "Error: Failed to create pipe for compiler process.";
        out_exit_code = -3;
        return false;
    }
    SetHandleInformation(hPipeRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.hStdOutput = hPipeWrite;
    si.hStdError = hPipeWrite;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmd_buf(cmd_str.begin(), cmd_str.end());
    cmd_buf.push_back(L'\0');

    BOOL created = CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hPipeWrite);

    if (!created) {
        CloseHandle(hPipeRead);
        out_log = "Error: Failed to launch compiler process (CreateProcessW error " + std::to_string(GetLastError()) + ")";
        out_exit_code = -4;
        return false;
    }

    // 捕获编译器输出日志，并配合 PeekNamedPipe 轮询，杜绝管道写满造成的死锁
    std::string pipe_output;
    char buf[4096];
    DWORD bytes_read = 0;
    auto start_wait = std::chrono::steady_clock::now();

    while (true) {
        DWORD bytes_avail = 0;
        if (PeekNamedPipe(hPipeRead, nullptr, 0, nullptr, &bytes_avail, nullptr) && bytes_avail > 0) {
            DWORD to_read = (std::min)(bytes_avail, static_cast<DWORD>(sizeof(buf) - 1));
            if (ReadFile(hPipeRead, buf, to_read, &bytes_read, nullptr) && bytes_read > 0) {
                pipe_output.append(buf, bytes_read);
            }
        } else {
            DWORD wait_res = WaitForSingleObject(pi.hProcess, 50);
            if (wait_res == WAIT_OBJECT_0) {
                // 彻底排空管道中残留数据
                while (ReadFile(hPipeRead, buf, sizeof(buf) - 1, &bytes_read, nullptr) && bytes_read > 0) {
                    pipe_output.append(buf, bytes_read);
                }
                break;
            }
        }

        auto elapsed_s = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_wait).count();
        if (elapsed_s >= 25) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 1000);
            while (ReadFile(hPipeRead, buf, sizeof(buf) - 1, &bytes_read, nullptr) && bytes_read > 0) {
                pipe_output.append(buf, bytes_read);
            }
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            CloseHandle(hPipeRead);
            out_log = "Error: Compilation timed out after 25 seconds.\n" + EnsureValidUtf8(pipe_output);
            out_exit_code = -5;
            return false;
        }
    }

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    out_exit_code = static_cast<int>(exit_code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hPipeRead);

    out_log = EnsureValidUtf8(pipe_output);
    out_dll_path = dll_file.string();
    return (exit_code == 0 && std::filesystem::exists(dll_file));
}
 
namespace {

const char* EMBEDDED_FALLBACK_HTML = R"rawhtml(<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <title>ROG FALCHION ACE HFX - 配置服务已启动</title>
    <style>
        body { font-family: sans-serif; background: #0f111a; color: #fff; text-align: center; padding-top: 50px; }
        .box { display: inline-block; background: #1a1d2e; border: 1px solid #00e5ff; border-radius: 8px; padding: 30px 50px; }
        h1 { color: #00e5ff; }
        p { color: #8a99ad; }
    </style>
</head>
<body>
    <div class="box">
        <h1>ROG FALCHION ACE HFX</h1>
        <p>网页配置服务正在运行中 (127.0.0.1:19898)</p>
        <p>未找到外部 web/index.html 资源文件，已使用内置应急界面。</p>
    </div>
</body>
</html>)rawhtml";

// 纯词法检查 Web 子路径安全性（零磁盘 I/O）
bool IsSafeWebSubpath(const std::string& subpath) {
    if (subpath.empty()) {
        return false;
    }

    // 3. 含控制字符（含 %00 解码出的内嵌 NUL，以及 ASCII < 0x20 或 0x7F）
    for (char c : subpath) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7F) {
            return false;
        }
    }

    // 2. 含盘符（:），防止 C:/Windows/win.ini 等绝对路径与 NTFS 数据流
    if (subpath.find(':') != std::string::npos) {
        return false;
    }

    // 2. 是绝对路径 / 根路径 / 以 UNC 前缀开头
    if (subpath.front() == '/' || subpath.front() == '\\') {
        return false;
    }

    // 1. 按 '/' 与 '\' 双分隔符切分组件，拒绝包含 ".." 的上跳请求
    size_t start = 0;
    while (start < subpath.size()) {
        size_t end = subpath.find_first_of("/\\", start);
        std::string component;
        if (end == std::string::npos) {
            component = subpath.substr(start);
            start = subpath.size();
        } else {
            component = subpath.substr(start, end - start);
            start = end + 1;
        }
        if (component == "..") {
            return false;
        }
    }

    return true;
}

// 校验 Host 头部是否属于允许的本地白名单 {127.0.0.1, localhost, [::1]}
bool IsAllowedHost(const std::string& host_header) {
    if (host_header.empty()) {
        return false;
    }
    size_t first = host_header.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return false;
    size_t last = host_header.find_last_not_of(" \t\r\n");
    std::string trimmed = host_header.substr(first, last - first + 1);

    std::string host_part;
    if (trimmed.front() == '[') {
        // IPv6 字面量，如 [::1] 或 [::1]:19898
        size_t close_bracket = trimmed.find(']');
        if (close_bracket == std::string::npos) {
            return false;
        }
        host_part = trimmed.substr(0, close_bracket + 1);
        std::string rest = trimmed.substr(close_bracket + 1);
        if (!rest.empty()) {
            if (rest.front() != ':') return false;
            std::string port_part = rest.substr(1);
            if (port_part.empty()) return false;
            for (char c : port_part) {
                if (!std::isdigit(static_cast<unsigned char>(c))) return false;
            }
        }
    } else {
        size_t colon_pos = trimmed.find(':');
        if (colon_pos != std::string::npos) {
            host_part = trimmed.substr(0, colon_pos);
            std::string port_part = trimmed.substr(colon_pos + 1);
            if (port_part.empty()) return false;
            for (char c : port_part) {
                if (!std::isdigit(static_cast<unsigned char>(c))) return false;
            }
        } else {
            host_part = trimmed;
        }
    }

    for (char& c : host_part) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    return (host_part == "127.0.0.1" || host_part == "localhost" || host_part == "[::1]");
}

// 检查 Content-Type 是否为 application/json
bool IsJsonContentType(const std::string& content_type) {
    if (content_type.empty()) {
        return false;
    }
    size_t semicolon = content_type.find(';');
    std::string media_type = (semicolon != std::string::npos) ? content_type.substr(0, semicolon) : content_type;

    size_t first = media_type.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return false;
    size_t last = media_type.find_last_not_of(" \t\r\n");
    media_type = media_type.substr(first, last - first + 1);

    for (char& c : media_type) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return media_type == "application/json";
}

// 检查 URL (Origin 或 Referer) 中的 Host 是否在白名单中
bool IsAllowedOriginOrRefererUrl(const std::string& raw_url) {
    size_t first = raw_url.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return false;
    size_t last = raw_url.find_last_not_of(" \t\r\n");
    std::string url = raw_url.substr(first, last - first + 1);

    if (url.empty() || url == "null") {
        return false;
    }
    size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) {
        return false;
    }

    std::string scheme = url.substr(0, scheme_end);
    for (char& c : scheme) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (scheme != "http" && scheme != "https") {
        return false;
    }

    size_t host_start = scheme_end + 3;
    size_t auth_end = url.find_first_of("/?#", host_start);
    std::string authority = (auth_end == std::string::npos)
        ? url.substr(host_start)
        : url.substr(host_start, auth_end - host_start);

    if (authority.empty() || authority.find('@') != std::string::npos) {
        return false;
    }
    return IsAllowedHost(authority);
}

// 校验 Origin 与 Referer 来源合法性
bool ValidateOriginAndReferer(const httplib::Request& req) {
    bool has_origin = req.has_header("Origin");
    if (has_origin) {
        std::string origin = req.get_header_value("Origin");
        // 显式拒绝字面量 "null"（沙箱 iframe / data: / file: 场景）以及非法 Origin
        if (origin == "null" || origin.empty() || !IsAllowedOriginOrRefererUrl(origin)) {
            return false;
        }
    }

    bool has_referer = req.has_header("Referer");
    if (has_referer) {
        std::string referer = req.get_header_value("Referer");
        if (referer.empty() || !IsAllowedOriginOrRefererUrl(referer)) {
            return false;
        }
    }

    // 两者皆空或均合法才放行（支持本地 curl/脚本）
    return true;
}

// 写接口通用前置校验（第 1/2/3 层纵深防御）
bool ValidateWriteRequest(const httplib::Request& req, httplib::Response& res) {
    // 1. Host 头校验（防御性复核，防 DNS 重绑定）
    if (!IsAllowedHost(req.get_header_value("Host"))) {
        res.status = 403;
        res.set_content(R"json({"status":"error","message":"非法的 Host 请求头"})json", "application/json; charset=utf-8");
        return false;
    }

    // 2. 强制 Content-Type: application/json
    if (!IsJsonContentType(req.get_header_value("Content-Type"))) {
        res.status = 415;
        res.set_content(R"json({"status":"error","message":"请求头 Content-Type 必须为 application/json"})json", "application/json; charset=utf-8");
        return false;
    }

    // 3. Origin/Referer 来源校验
    if (!ValidateOriginAndReferer(req)) {
        res.status = 403;
        res.set_content(R"json({"status":"error","message":"非法的 Origin 或 Referer 来源"})json", "application/json; charset=utf-8");
        return false;
    }

    return true;
}

} // namespace

WebServer::WebServer(const std::filesystem::path& config_path, int port, const std::filesystem::path& sdk_include_dir)
    : config_path_(config_path), port_(port), sdk_include_dir_(sdk_include_dir) {
    SetupRoutes();
}

WebServer::~WebServer() {
    Stop();
}

void WebServer::SetupRoutes() {
    // 限制请求体上限为 1024KB (1MB)，容纳生成的完整 C++ 源码
    svr_.set_payload_max_length(1024 * 1024);
    svr_.set_error_handler([](const httplib::Request& /*req*/, httplib::Response& res) {
        if (res.status == 413) {
            res.set_content(R"json({"status":"error","error":"Payload Too Large","message":"请求体超过 1MB 上限"})json", "application/json; charset=utf-8");
        }
    });

    // 第 1 层：Host 头全局校验（防 DNS 重绑定），所有路由生效
    svr_.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        if (!IsAllowedHost(req.get_header_value("Host"))) {
            res.status = 403;
            res.set_content(R"json({"status":"error","message":"非法的 Host 请求头"})json", "application/json; charset=utf-8");
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // 根路径提供前端页面
    svr_.Get("/", [this](const httplib::Request&, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store");
        res.set_content(LoadHtmlContent(), "text/html; charset=utf-8");
    });

    // Favicon 静默处理，避免浏览器默认请求产生 404 控制台错误
    svr_.Get("/favicon.ico", [](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    });

    // 静态资源兜底 (支持外部 css/js/ico 等，带 R6 词法路径穿越校验)
    svr_.Get("/web/(.*)", [](const httplib::Request& req, httplib::Response& res) {
        std::string subpath = req.matches[1];
        if (!IsSafeWebSubpath(subpath)) {
            res.status = 404;
            return;
        }
        std::error_code ec;
        std::filesystem::path p = std::filesystem::path("web") / subpath;
        if (std::filesystem::exists(p, ec) && !std::filesystem::is_directory(p, ec)) {
            std::ifstream f(p, std::ios::binary);
            if (f.is_open()) {
                std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                std::string mime = "text/plain";
                if (p.extension() == ".css") mime = "text/css";
                else if (p.extension() == ".js") mime = "application/javascript";
                else if (p.extension() == ".html") mime = "text/html";
                else if (p.extension() == ".svg") mime = "image/svg+xml";
                res.set_content(content, mime);
                return;
            }
        }
        res.status = 404;
    });

    // 心跳检测接口 (前端通过高频轮询感知服务存活，断开时优雅降级)
    svr_.Get("/api/status", [this](const httplib::Request&, httplib::Response& res) {
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        auto sdk_res = DiscoverPluginSdkIncludeDir(sdk_include_dir_);
        std::filesystem::path vcvars = FindVcvars64Bat();
        bool sdk_ok = sdk_res.found;
        bool msvc_ok = !vcvars.empty() && std::filesystem::exists(vcvars);
        nlohmann::json j = {
             {"status", "ok"},
             {"service", "aura_web_ui"},
             {"web_api_version", 2},
             {"timestamp", now_ms},
             {"studio_publish_ready", sdk_ok && msvc_ok},
             {"sdk_headers_found", sdk_ok},
             {"msvc_found", msvc_ok},
             {"sdk_include_dir", sdk_ok ? sdk_res.include_dir.u8string() : ""},
             {"msvc_vcvars_path", msvc_ok ? vcvars.u8string() : ""}
        };
        res.set_content(j.dump(), "application/json; charset=utf-8");
    });

    // 查询所有已编译的光效插件列表
    svr_.Get("/api/plugins", [](const httplib::Request&, httplib::Response& res) {
        nlohmann::json plugins = nlohmann::json::array();
        std::error_code ec;
        std::filesystem::path plugins_dir = "plugins";
        if (std::filesystem::exists(plugins_dir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(plugins_dir, ec)) {
                if (entry.is_regular_file(ec) && entry.path().extension() == ".dll") {
                    std::string stem = entry.path().stem().string();
                    std::string name = stem;
                    if (name.rfind("effect_", 0) == 0) {
                        name = name.substr(7);
                    }
                    plugins.push_back({
                        {"name", name},
                        {"filename", entry.path().filename().string()},
                        {"path", entry.path().string()}
                    });
                }
            }
        }
        nlohmann::json j = {
            {"status", "ok"},
            {"plugins", plugins}
        };
        res.set_content(j.dump(), "application/json; charset=utf-8");
    });

    // 获取完整配置文件内容
    svr_.Get("/api/config", [this](const httplib::Request&, httplib::Response& res) {
        std::string json_str;
        if (ReadConfigFile(json_str)) {
            res.set_content(json_str, "application/json; charset=utf-8");
        } else {
            res.status = 500;
            res.set_content(R"json({"status":"error","message":"无法读取配置文件"})json", "application/json; charset=utf-8");
        }
    });

    // 保存更新配置文件 (受 R7 写接口防御保护)
    svr_.Post("/api/config", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ValidateWriteRequest(req, res)) {
            return;
        }
        try {
            // 校验 JSON 格式合法性
            auto j = nlohmann::json::parse(req.body);
            if (!j.is_object() || !j.contains("profiles") || !j.contains("rules")) {
                res.status = 400;
                res.set_content(R"json({"status":"error","message":"配置数据缺少 profiles 或 rules 核心字段"})json", "application/json; charset=utf-8");
                return;
            }

            // 格式化输出 (保持 2 格缩进)
            std::string formatted = j.dump(2);
            if (WriteConfigFile(formatted)) {
                res.set_content(R"json({"status":"ok","message":"配置已写入，等待 daemon 热重载"})json", "application/json; charset=utf-8");
            } else {
                res.status = 500;
                res.set_content(R"json({"status":"error","message":"写入配置文件失败"})json", "application/json; charset=utf-8");
            }
        } catch (const std::exception& e) {
            res.status = 400;
            nlohmann::json err = {
                {"status", "error"},
                {"message", std::string("JSON 解析失败: ") + e.what()}
            };
            res.set_content(err.dump(), "application/json; charset=utf-8");
        }
    });

    // 代理获取当前 GSI 状态 (转发至 daemon 127.0.0.1:19897)
    svr_.Get("/api/gsi/current", [](const httplib::Request&, httplib::Response& res) {
        httplib::Client cli("127.0.0.1", 19897);
        cli.set_connection_timeout(0, 300000); // 300ms
        cli.set_read_timeout(0, 500000); // 500ms
        auto cli_res = cli.Get("/api/gsi/current");
        if (cli_res && cli_res->status == 200) {
            res.set_content(cli_res->body, "application/json; charset=utf-8");
        } else {
            nlohmann::json j = {
                {"connected", false},
                {"daemon_running", false},
                {"message", "无法连接至 daemon GSI 适配器 (127.0.0.1:19897)，请确认 aura_daemon 是否正在运行"},
                {"data", nlohmann::json::object()}
            };
            res.set_content(j.dump(), "application/json; charset=utf-8");
        }
    });

    // 获取 CS2 GSI 配置文件模板及检测到的安装路径
    svr_.Get("/api/gsi/cfg", [this](const httplib::Request&, httplib::Response& res) {
        std::vector<std::filesystem::path> paths = DetectCs2CfgPaths();
        std::string detected_utf8 = paths.empty() ? "" : paths[0].u8string();
        std::vector<std::string> paths_utf8;
        paths_utf8.reserve(paths.size());
        for (const auto& p : paths) {
            paths_utf8.push_back(p.u8string());
        }

        bool installed = false;
        if (!paths.empty()) {
            std::filesystem::path cfg_file = paths[0] / "gamestate_integration_aura.cfg";
            installed = std::filesystem::exists(cfg_file);
        }

        nlohmann::json j = {
            {"filename", "gamestate_integration_aura.cfg"},
            {"content", GetGsiCfgTemplate()},
            {"detected_path", detected_utf8},
            {"all_paths", paths_utf8},
            {"installed", installed}
        };
        res.set_content(j.dump(2), "application/json; charset=utf-8");
    });

    // 安装 GSI 配置文件到 CS2 目录 (带用户明确确认与路径校验，受 R7 四层纵深防御保护)
    svr_.Post("/api/gsi/install-cfg", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ValidateWriteRequest(req, res)) {
            return;
        }
        try {
            auto j = nlohmann::json::parse(req.body);
            std::string target_dir = j.value("target_dir", "");
            auto detected_paths = DetectCs2CfgPaths();
            std::filesystem::path p;
            if (!target_dir.empty()) {
                p = std::filesystem::u8path(target_dir);
            } else if (!detected_paths.empty()) {
                p = detected_paths[0];
            } else {
                res.status = 400;
                res.set_content(R"json({"status":"error","message":"未指定且未能自动检测到 CS2 cfg 目录"})json", "application/json; charset=utf-8");
                return;
            }

            std::error_code ec;
            if (!std::filesystem::exists(p, ec) || !std::filesystem::is_directory(p, ec)) {
                res.status = 400;
                res.set_content(R"json({"status":"error","message":"目标路径不存在或不是有效目录"})json", "application/json; charset=utf-8");
                return;
            }

            // 第 4 层：install-cfg 的 target_dir 白名单校验
            // 允许值 = DetectCs2CfgPaths() 结果 ∪ 目录内已存在 gamestate_integration_*.cfg 的目录
            bool is_allowed = false;
            for (const auto& dp : detected_paths) {
                std::error_code eq_ec;
                if ((std::filesystem::equivalent(p, dp, eq_ec) && !eq_ec) ||
                    (p.lexically_normal() == dp.lexically_normal())) {
                    is_allowed = true;
                    break;
                }
            }

            if (!is_allowed) {
                // 检查目录内是否已存在 gamestate_integration_*.cfg 文件（兼容自定义 Steam 库）
                for (const auto& entry : std::filesystem::directory_iterator(p, ec)) {
                    if (ec) break;
                    std::error_code file_ec;
                    if (entry.is_regular_file(file_ec)) {
                        std::wstring filename = entry.path().filename().wstring();
                        const std::wstring prefix = L"gamestate_integration_";
                        const std::wstring suffix = L".cfg";
                        if (filename.size() >= prefix.size() + suffix.size() &&
                            filename.compare(0, prefix.size(), prefix) == 0 &&
                            filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0) {
                            is_allowed = true;
                            break;
                        }
                    }
                }
            }

            if (!is_allowed) {
                res.status = 400;
                res.set_content(R"json({"status":"error","message":"目标目录不在允许的 CS2 cfg 白名单中"})json", "application/json; charset=utf-8");
                return;
            }

            std::filesystem::path file_path = p / "gamestate_integration_aura.cfg";
            std::ofstream out(file_path, std::ios::trunc | std::ios::binary);
            if (!out.is_open()) {
                res.status = 500;
                res.set_content(R"json({"status":"error","message":"无法向目标文件写入数据，请检查文件写权限"})json", "application/json; charset=utf-8");
                return;
            }

            out << GetGsiCfgTemplate();
            out.flush();

            nlohmann::json resp = {
                {"status", "ok"},
                {"message", "成功安装 gamestate_integration_aura.cfg 到 CS2 目录！启动 CS2 即可开始接收实时游戏数据。"},
                {"path", file_path.u8string()}
            };
            res.set_content(resp.dump(), "application/json; charset=utf-8");
        } catch (const std::exception& e) {
            res.status = 400;
            nlohmann::json err = {
                {"status", "error"},
                {"message", std::string("请求处理失败: ") + e.what()}
            };
            res.set_content(err.dump(), "application/json; charset=utf-8");
        }
    });

    // 编译自定义 C++ 光效源码为独立插件 DLL
    svr_.Post("/api/compile_effect", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ValidateWriteRequest(req, res)) {
            return;
        }
        auto start_t = std::chrono::steady_clock::now();
        try {
            auto j = nlohmann::json::parse(req.body);
            std::string name = j.value("name", "");
            std::string code = j.value("code", j.value("source", ""));

            if (name.empty()) {
                res.status = 400;
                res.set_content(R"json({"status":"error","success":false,"message":"缺少插件名称 name 字段"})json", "application/json; charset=utf-8");
                return;
            }

            // 校验名称合法性: 1-64 位英文字母、数字与下划线
            if (name.size() > 64) {
                res.status = 400;
                res.set_content(R"json({"status":"error","success":false,"message":"插件名称超过 64 字符上限"})json", "application/json; charset=utf-8");
                return;
            }
            for (char c : name) {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
                    res.status = 400;
                    res.set_content(R"json({"status":"error","success":false,"message":"插件名称非法，仅允许英文字母、数字及下划线"})json", "application/json; charset=utf-8");
                    return;
                }
            }

            if (code.empty()) {
                res.status = 400;
                res.set_content(R"json({"status":"error","success":false,"message":"缺少待编译的 C++ 源码 code 字段"})json", "application/json; charset=utf-8");
                return;
            }

            std::string out_log;
            std::string out_dll_path;
            int exit_code = 0;
            bool ok = CompileCppSourceToDll(name, code, out_log, out_dll_path, exit_code, sdk_include_dir_);

            auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_t).count();

            std::string safe_name = name;
            if (safe_name.rfind("effect_", 0) == 0) {
                safe_name = safe_name.substr(7);
            }
            std::string plugin_file_name = "effect_" + safe_name;

            if (ok) {
                nlohmann::json resp = {
                    {"status", "ok"},
                    {"success", true},
                    {"plugin_name", plugin_file_name},
                    {"dll_path", "plugins/" + plugin_file_name + ".dll"},
                    {"plugin_path", "plugins/" + plugin_file_name + ".dll"},
                    {"compiler_output", out_log},
                    {"log", out_log},
                    {"duration_ms", duration_ms}
                };
                res.status = 200;
                res.set_content(resp.dump(), "application/json; charset=utf-8");
            } else {
                std::string msg;
                std::string stage;
                if (exit_code == -6) {
                    msg = "Studio 原生发布 SDK 缺失或损坏 (Studio native publish SDK is missing or incomplete)";
                    stage = "sdk_missing";
                } else if (exit_code == -1 || exit_code == 9009) {
                    msg = "原生发布需要 Microsoft Visual Studio / Build Tools 的 C++ Desktop workload。你仍然可以编辑、预览和保存草稿；安装 C++ Build Tools 后即可发布。";
                    stage = "msvc_missing";
                } else if (exit_code == 9008) {
                    msg = "MSVC vcvars 环境初始化失败 (MSVC vcvars setup failed)";
                    stage = "vcvars_failed";
                } else if (exit_code == -2) {
                    msg = "无法写入插件源码";
                    stage = "source_write";
                } else if (exit_code == -5) {
                    msg = "MSVC 编译超时";
                    stage = "compiler_timeout";
                } else {
                    msg = "原生插件编译失败 (generated C++ compile failed)";
                    stage = "compile_failed";
                }

                nlohmann::json resp = {
                    {"status", "error"},
                    {"success", false},
                    {"message", msg},
                    {"stage", stage},
                    {"exit_code", exit_code},
                    {"compiler_output", out_log},
                    {"log", out_log},
                    {"duration_ms", duration_ms}
                };
                res.status = 400;
                res.set_content(resp.dump(), "application/json; charset=utf-8");
            }
        } catch (const std::exception& e) {
            res.status = 400;
            nlohmann::json err = {
                {"status", "error"},
                {"success", false},
                {"message", std::string("编译请求处理异常: ") + e.what()}
            };
            res.set_content(err.dump(), "application/json; charset=utf-8");
        }
    });

    // 守护进程插件热重载触发接口
    svr_.Post("/api/reload_plugin", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ValidateWriteRequest(req, res)) {
            return;
        }
        try {
            auto j = nlohmann::json::parse(req.body);
            std::string name = j.value("name", j.value("plugin_name", ""));
            if (name.empty()) {
                res.status = 400;
                res.set_content(R"json({"status":"error","success":false,"message":"缺少 plugin_name 或 name 字段"})json", "application/json; charset=utf-8");
                return;
            }

            // 同步通知 daemon IPC (127.0.0.1:19897)
            httplib::Client cli("127.0.0.1", 19897);
            cli.set_connection_timeout(1, 0);
            cli.set_read_timeout(2, 0);
            nlohmann::json payload = {{"plugin_name", name}, {"name", name}};
            auto cli_res = cli.Post("/api/plugin/reload", payload.dump(), "application/json");

            bool daemon_synced = (cli_res && cli_res->status == 200);
            bool daemon_offline = !cli_res && cli_res.error() != httplib::Error::ConnectionTimeout;
            bool dll_load_failed = cli_res && cli_res->status == 400;

            nlohmann::json resp = {
                {"status", daemon_synced ? "ok" : "error"},
                {"success", daemon_synced},
                {"message", daemon_synced ? "插件已由 daemon 确认加载" : daemon_offline ? "daemon 不在线；旧版本保持运行" : dll_load_failed ? "daemon 未能加载 DLL；旧版本保持运行，请查看 aura_daemon.log" : "daemon 重载未确认或已超时；旧版本保持运行"},
                {"stage", daemon_synced ? "loaded" : daemon_offline ? "daemon_offline" : dll_load_failed ? "dll_load" : "daemon_reload"},
                {"daemon_synced", daemon_synced}
            };
            res.status = daemon_synced ? 200 : 503;
            res.set_content(resp.dump(), "application/json; charset=utf-8");
        } catch (const std::exception& e) {
            res.status = 400;
            nlohmann::json err = {
                {"status", "error"},
                {"success", false},
                {"message", std::string("重载插件异常: ") + e.what()}
            };
            res.set_content(err.dump(), "application/json; charset=utf-8");
        }
    });

    // 编辑态虚拟/硬件推流实时预览接口
    svr_.Post("/api/preview", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ValidateWriteRequest(req, res)) {
            return;
        }
        try {
            // 转发推流预览帧至 daemon (127.0.0.1:19897)
            httplib::Client cli("127.0.0.1", 19897);
            cli.set_connection_timeout(0, 300000);
            cli.set_read_timeout(0, 500000);
            auto cli_res = cli.Post("/api/preview", req.body, "application/json");

            nlohmann::json resp = {
                {"status", "ok"},
                {"success", true},
                {"daemon_active", (cli_res && cli_res->status == 200)}
            };
            res.status = 200;
            res.set_content(resp.dump(), "application/json; charset=utf-8");
        } catch (const std::exception& e) {
            res.status = 400;
            nlohmann::json err = {
                {"status", "error"},
                {"success", false},
                {"message", std::string("预览推流异常: ") + e.what()}
            };
            res.set_content(err.dump(), "application/json; charset=utf-8");
        }
    });

    svr_.Post("/api/preview_frame", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ValidateWriteRequest(req, res)) {
            return;
        }
        try {
            httplib::Client cli("127.0.0.1", 19897);
            cli.set_connection_timeout(0, 300000);
            cli.set_read_timeout(0, 500000);
            auto cli_res = cli.Post("/api/preview", req.body, "application/json");

            nlohmann::json resp = {
                {"status", "ok"},
                {"success", true},
                {"daemon_active", (cli_res && cli_res->status == 200)}
            };
            res.status = 200;
            res.set_content(resp.dump(), "application/json; charset=utf-8");
        } catch (const std::exception& e) {
            res.status = 400;
            nlohmann::json err = {
                {"status", "error"},
                {"success", false},
                {"message", std::string("预览推流异常: ") + e.what()}
            };
            res.set_content(err.dump(), "application/json; charset=utf-8");
        }
    });

    // 查询所有已安装或已编译的动态光效插件
    svr_.Get("/api/plugins", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            std::error_code ec;
            std::filesystem::path plugins_dir = "plugins";
            nlohmann::json plugin_list = nlohmann::json::array();

            if (std::filesystem::exists(plugins_dir, ec) && std::filesystem::is_directory(plugins_dir, ec)) {
                for (const auto& entry : std::filesystem::directory_iterator(plugins_dir, ec)) {
                    if (entry.is_regular_file(ec) && entry.path().extension() == ".dll") {
                        std::string filename = entry.path().filename().string();
                        std::string effect_name = entry.path().stem().string();
                        if (effect_name.rfind("effect_", 0) == 0) {
                            effect_name = effect_name.substr(7);
                        }
                        auto fsize = entry.file_size(ec);
                        auto lwt = entry.last_write_time(ec);
                        auto s_time = std::chrono::duration_cast<std::chrono::seconds>(lwt.time_since_epoch()).count();

                        plugin_list.push_back({
                            {"name", effect_name},
                            {"filename", filename},
                            {"path", entry.path().string()},
                            {"size_bytes", fsize},
                            {"modified_epoch", s_time}
                        });
                    }
                }
            }

            nlohmann::json resp = {
                {"status", "ok"},
                {"plugins", plugin_list},
                {"count", plugin_list.size()}
            };
            res.status = 200;
            res.set_content(resp.dump(), "application/json; charset=utf-8");
        } catch (const std::exception& e) {
            res.status = 500;
            nlohmann::json err = {
                {"status", "error"},
                {"message", std::string("获取插件列表异常: ") + e.what()}
            };
            res.set_content(err.dump(), "application/json; charset=utf-8");
        }
    });
}

bool WebServer::Start() {
    is_running_.store(true, std::memory_order_release);
    // 严格绑定 127.0.0.1，杜绝 0.0.0.0 局域网暴露与防火墙弹窗
    return svr_.listen("127.0.0.1", port_);
}

void WebServer::Stop() {
    if (is_running_.exchange(false, std::memory_order_acq_rel)) {
        svr_.stop();
    }
}

std::string WebServer::LoadHtmlContent() const {
    // 优先从本地文件系统加载最新的 index.html，方便前端热调试与非预期 CWD 启动
    std::vector<std::filesystem::path> candidates = {
        "web/index.html",
        "../web/index.html",
        "../../web/index.html"
    };

    wchar_t mod_path[MAX_PATH];
    if (GetModuleFileNameW(nullptr, mod_path, MAX_PATH)) {
        std::filesystem::path exe_dir = std::filesystem::path(mod_path).parent_path();
        candidates.push_back(exe_dir / "web" / "index.html");
        candidates.push_back(exe_dir / ".." / "web" / "index.html");
        candidates.push_back(exe_dir / ".." / ".." / "web" / "index.html");
    }

    std::error_code ec;
    for (const auto& p : candidates) {
        if (std::filesystem::exists(p, ec) && !std::filesystem::is_directory(p, ec)) {
            std::ifstream f(p, std::ios::binary);
            if (f.is_open()) {
                return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            }
        }
    }

    return EMBEDDED_FALLBACK_HTML;
}

bool WebServer::ReadConfigFile(std::string& out_json_str) const {
    std::lock_guard<std::mutex> lock(file_mutex_);
    std::ifstream f(config_path_, std::ios::binary);
    if (!f.is_open()) {
        return false;
    }
    out_json_str.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return true;
}

bool WebServer::WriteConfigFile(const std::string& json_str) const {
    std::lock_guard<std::mutex> lock(file_mutex_);
    // 原子写入：先写入临时文件，再原子替换覆盖
    std::filesystem::path tmp_path = config_path_.wstring() + L".tmp";
    {
        std::ofstream f(tmp_path, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) {
            return false;
        }
        f.write(json_str.data(), json_str.size());
        f.flush();
        if (!f.good()) {
            f.close();
            std::error_code ec;
            std::filesystem::remove(tmp_path, ec);
            return false;
        }
    }

    // Windows MoveFileExW 原子替换（支持非 ASCII 路径）。失败时保留旧文件。
    if (!MoveFileExW(tmp_path.c_str(), config_path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ec;
        std::filesystem::remove(tmp_path, ec);
        return false;
    }

    return true;
}

std::string WebServer::GetGsiCfgTemplate() {
    return R"rawcfg("Aura CS2 GSI Integration"
{
    "uri" "http://127.0.0.1:19897/"
    "timeout" "5.0"
    "buffer"  "0.1"
    "throttle" "0.1"
    "heartbeat" "1.0"
    "data"
    {
        "provider"              "1"
        "map"                   "1"
        "round"                 "1"
        "player_id"             "1"
        "player_state"          "1"
        "player_weapons"        "1"
        "player_match_stats"    "1"
        "map_round_wins"        "1"
        "bomb"                  "1"
        "phase_countdowns"      "1"
        "allplayers_id"         "1"
        "allplayers_state"      "1"
        "allplayers_match_stats" "1"
        "allplayers_weapons"    "1"
        "allplayers_position"   "1"
        "allgrenades"           "1"
        "player_position"       "1"
    }
}
)rawcfg";
}

std::vector<std::filesystem::path> WebServer::DetectCs2CfgPaths() const {
    std::vector<std::filesystem::path> result;

    // 常见可能盘符路径优先扫描
    const std::vector<std::filesystem::path> prefixes = {
        L"D:\\SteamLibrary",
        L"C:\\Program Files (x86)\\Steam",
        L"C:\\SteamLibrary",
        L"E:\\SteamLibrary",
        L"F:\\SteamLibrary",
        L"G:\\SteamLibrary",
        L"C:\\Steam",
        L"D:\\Steam",
        L"E:\\Steam"
    };

    for (const auto& pre : prefixes) {
        std::filesystem::path p = pre / "steamapps" / "common" / "Counter-Strike Global Offensive" / "game" / "csgo" / "cfg";
        if (std::filesystem::exists(p) && std::filesystem::is_directory(p)) {
            result.push_back(p);
        }
    }

    // 解析 Steam libraryfolders.vdf
    std::filesystem::path vdf_path = L"C:\\Program Files (x86)\\Steam\\steamapps\\libraryfolders.vdf";
    if (std::filesystem::exists(vdf_path)) {
        std::ifstream f(vdf_path);
        std::string line;
        while (std::getline(f, line)) {
            size_t p_pos = line.find("\"path\"");
            if (p_pos != std::string::npos) {
                size_t first_quote = line.find('\"', p_pos + 6);
                if (first_quote != std::string::npos) {
                    size_t second_quote = line.find('\"', first_quote + 1);
                    if (second_quote != std::string::npos) {
                        std::string base_path = line.substr(first_quote + 1, second_quote - first_quote - 1);
                        std::string clean_path;
                        for (size_t i = 0; i < base_path.size(); ++i) {
                            if (base_path[i] == '\\' && i + 1 < base_path.size() && base_path[i + 1] == '\\') {
                                clean_path += '\\';
                                ++i;
                            } else {
                                clean_path += base_path[i];
                            }
                        }
                        std::filesystem::path cs2_cfg = std::filesystem::u8path(clean_path) / "steamapps" / "common" / "Counter-Strike Global Offensive" / "game" / "csgo" / "cfg";
                        if (std::filesystem::exists(cs2_cfg) && std::filesystem::is_directory(cs2_cfg)) {
                            if (std::find(result.begin(), result.end(), cs2_cfg) == result.end()) {
                                result.push_back(cs2_cfg);
                            }
                        }
                    }
                }
            }
        }
    }

    return result;
}

} // namespace aura
