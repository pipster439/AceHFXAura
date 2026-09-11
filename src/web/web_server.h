#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <filesystem>
#include "third_party/httplib.h"

namespace aura {

class WebServer {
public:
    WebServer(const std::filesystem::path& config_path, int port);
    ~WebServer();

    // 启动 HTTP 服务（阻塞直到 stop() 被调用）
    bool Start();

    // 请求平滑停止
    void Stop();

    // 加载 HTML 资源（供服务分发及自测验证）
    std::string LoadHtmlContent() const;

private:
    void SetupRoutes();
    bool ReadConfigFile(std::string& out_json_str) const;
    bool WriteConfigFile(const std::string& json_str) const;

    std::vector<std::filesystem::path> DetectCs2CfgPaths() const;
    static std::string GetGsiCfgTemplate();

    std::filesystem::path config_path_{"config.json"};
    int port_{19898};
    httplib::Server svr_;
    std::atomic<bool> is_running_{false};
    mutable std::mutex file_mutex_;
};

} // namespace aura
