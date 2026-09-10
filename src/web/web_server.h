#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include "third_party/httplib.h"

namespace aura {

class WebServer {
public:
    WebServer(const std::string& config_path, int port);
    ~WebServer();

    // 启动 HTTP 服务（阻塞直到 stop() 被调用）
    bool Start();

    // 请求平滑停止
    void Stop();

private:
    void SetupRoutes();
    std::string LoadHtmlContent() const;
    bool ReadConfigFile(std::string& out_json_str) const;
    bool WriteConfigFile(const std::string& json_str) const;

    std::vector<std::string> DetectCs2CfgPaths() const;
    static std::string GetGsiCfgTemplate();

    std::string config_path_{"config.json"};
    int port_{19898};
    httplib::Server svr_;
    std::atomic<bool> is_running_{false};
    mutable std::mutex file_mutex_;
};

} // namespace aura
