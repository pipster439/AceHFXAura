#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <functional>
#include <cstdint>
#include <mutex>
#include <unordered_set>

#include "third_party/json.hpp"
#include "third_party/httplib.h"

namespace aura {

// ========================================================
// 跨进程 Windows Named Mutex 锁 (RAII)
// 同步 daemon 与 aura_web_ui.exe 对 config.json 的并发写操作
// ========================================================
class NamedConfigLock {
public:
    explicit NamedConfigLock(const wchar_t* name = L"Local\\AceHFXAuraConfigWriteMutex", DWORD timeout_ms = 5000) {
        hMutex_ = CreateMutexW(nullptr, FALSE, name);
        if (hMutex_) {
            DWORD wait_res = WaitForSingleObject(hMutex_, timeout_ms);
            acquired_ = (wait_res == WAIT_OBJECT_0 || wait_res == WAIT_ABANDONED);
        }
    }

    ~NamedConfigLock() {
        if (acquired_ && hMutex_) {
            ReleaseMutex(hMutex_);
        }
        if (hMutex_) {
            CloseHandle(hMutex_);
        }
    }

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
// 动画周期 (period_ms) 共享元数据与解析
// ========================================================
bool IsPeriodSupported(const std::string& effect_type);
uint64_t GetEffectDefaultPeriod(const std::string& effect_type);
uint64_t ResolveEffectivePeriodMs(const std::string& effect_type, const nlohmann::json& pval, const std::string& pname = "");

// ========================================================
// 亮度 (brightness) 规范化
// ========================================================
double NormalizeBrightnessToRatio(const nlohmann::json& pval);

// ========================================================
// LightingControlService
// 运行于 127.0.0.1:19897，负责 Lighting API v1
// ========================================================
class LightingControlService {
public:
    using FileReplacerFunc = std::function<bool(const std::wstring& tmp_path, const std::wstring& target_path)>;

    explicit LightingControlService(std::filesystem::path config_path);

    // 注入可控的文件替换测试接缝 (默认调用 MoveFileExW)
    void SetFileReplacerForTesting(FileReplacerFunc replacer) {
        file_replacer_ = std::move(replacer);
    }

    const std::filesystem::path& GetConfigPath() const { return config_path_; }

    // 注册 HTTP 路由至 GsiAdapter 服务器 (必须在 Start 前调用)
    void RegisterRoutes(httplib::Server& svr);

    // 核心业务方法 (供 HTTP 路由与直接单元测试调用)
    struct ProfileSummary {
        std::string name;
        std::string type;
    };

    struct ProfileDetail {
        std::string name;
        std::string type;
        double brightness{1.0};
        int fps{25};
        bool fps_inherited{true};
        bool supports_period{false};
        bool has_period{false};
        uint64_t period_ms{0};
        std::string revision;
    };

    struct PatchInput {
        std::string expected_revision;
        bool has_brightness{false};
        double brightness{1.0};
        bool has_period_ms{false};
        uint64_t period_ms{0};
        bool has_fps{false};
        int fps{25};
    };

    struct OpResult {
        int http_status{200};
        std::string error_code;
        std::string message;
        std::string current_revision;
    };

    OpResult GetProfileList(std::vector<ProfileSummary>& out_profiles, std::string& out_revision);
    OpResult GetProfileDetail(const std::string& name, ProfileDetail& out_detail);
    OpResult UpdateProfile(const std::string& name, const PatchInput& patch, std::string& out_new_revision);

private:
    std::filesystem::path config_path_;
    FileReplacerFunc file_replacer_;

    bool ReadRawConfigFile(std::string& out_content, std::string& out_revision) const;
    static bool ValidatePatchRequestSecurity(const httplib::Request& req, httplib::Response& res);
};

} // namespace aura
