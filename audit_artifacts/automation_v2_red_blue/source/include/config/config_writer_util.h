#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <filesystem>
#include <functional>
#include <cstdint>

namespace aura {

// ========================================================
// 跨进程 Windows Named Mutex 锁 (RAII)
// 同步 daemon 与 aura_web_ui.exe 对 config.json 的并发写操作
// ========================================================
inline constexpr wchar_t kConfigWriteMutexName[] = L"Local\\AceHFXAuraConfigWriteMutex";

class NamedConfigLock {
public:
    explicit NamedConfigLock(const wchar_t* name = kConfigWriteMutexName, DWORD timeout_ms = 5000);
    ~NamedConfigLock();

    bool IsAcquired() const { return acquired_; }

    NamedConfigLock(const NamedConfigLock&) = delete;
    NamedConfigLock& operator=(const NamedConfigLock&) = delete;

private:
    HANDLE hMutex_{nullptr};
    bool acquired_{false};
};

// ========================================================
// 64-bit FNV-1a 内容哈希与版本计算
// ========================================================
uint64_t ComputeFnv1a64(const void* data, size_t len);
std::string FormatFnv1aHex(uint64_t hash);
std::string ComputeFileRevision(const std::string& content);

// ========================================================
// 原子文件读取与替换工具 (支持测试接缝)
// ========================================================
using FileReplacerFunc = std::function<bool(const std::wstring& tmp_path, const std::wstring& target_path)>;

bool DefaultFileReplacer(const std::wstring& tmp_path, const std::wstring& target_path);

bool ReadRawConfigFile(const std::filesystem::path& config_path,
                       std::string& out_content,
                       std::string& out_revision);

bool AtomicWriteConfigFile(const std::filesystem::path& config_path,
                           const std::string& content,
                           FileReplacerFunc replacer = nullptr);

// ========================================================
// HTTP 安全与格式校验工具 (环回 Host、Origin/Referer 与 Content-Type)
// ========================================================
bool IsAllowedLoopbackHost(const std::string& host_header);
bool IsJsonContentType(const std::string& content_type);
bool IsAllowedOriginOrRefererUrl(const std::string& raw_url);
bool ValidateLoopbackOriginAndReferer(const std::string& origin, const std::string& referer);

} // namespace aura
