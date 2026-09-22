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

// Revision-bound Automation v2 authoring on the daemon loopback server.
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

    struct AuthoringResult { int http_status; nlohmann::json body; };
    AuthoringResult Author(const std::string& operation, const nlohmann::json& request = nlohmann::json::object());
    static nlohmann::json AuthoringCapabilities();

private:
    std::filesystem::path config_path_;
    FileReplacerFunc file_replacer_;

    static bool ValidateWriteRequestSecurity(const httplib::Request& req, httplib::Response& res);
    void RegisterAuthoringRoutes(httplib::Server& svr);
};

} // namespace aura
