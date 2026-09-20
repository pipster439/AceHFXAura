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
#include <optional>

#include "config/config_writer_util.h"
#include "third_party/json.hpp"
#include "third_party/httplib.h"

namespace aura {

// ========================================================
// 进程名规范化工具函数
// 规则：Trim -> 获取文件名 -> 小写 -> 若无后缀补齐 .exe
// ========================================================
std::string CanonicalizeProcessName(const std::string& input);

// ========================================================
// AutomationControlService
// 运行于 127.0.0.1:19897，负责 Automation API v1 (应用规则 CRUD)
// ========================================================
class AutomationControlService {
public:
    explicit AutomationControlService(std::filesystem::path config_path);

    // 注入可控的文件替换测试接缝 (默认调用 MoveFileExW)
    void SetFileReplacerForTesting(FileReplacerFunc replacer) {
        file_replacer_ = std::move(replacer);
    }

    const std::filesystem::path& GetConfigPath() const { return config_path_; }

    // 注册 HTTP 路由至 GsiAdapter 服务器 (必须在 Start 前调用)
    void RegisterRoutes(httplib::Server& svr);

    // 核心数据模型 (Phase 4 仅限 Application Rule: Foreground Process -> Activate Profile)
    struct AutomationRule {
        std::string process;
        std::string profile;
        int index{-1};
    };

    struct OpResult {
        int http_status{200};
        std::string error_code;
        std::string message;
        std::string current_revision;
    };

    // 业务方法
    OpResult GetRules(std::vector<AutomationRule>& out_rules, std::string& out_revision);

    OpResult AddRule(const std::string& expected_revision,
                     const AutomationRule& new_rule,
                     AutomationRule& out_canonical_rule,
                     int& out_index,
                     std::string& out_new_revision);

    OpResult UpdateRule(int index,
                        const std::string& expected_revision,
                        const std::optional<std::string>& new_process,
                        const std::optional<std::string>& new_profile,
                        AutomationRule& out_canonical_rule,
                        std::string& out_new_revision);

    OpResult DeleteRule(int index,
                        const std::string& expected_revision,
                        std::string& out_new_revision);

private:
    std::filesystem::path config_path_;
    FileReplacerFunc file_replacer_;

    static bool ValidateWriteRequestSecurity(const httplib::Request& req, httplib::Response& res);
};

} // namespace aura
