#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <filesystem>
#include "third_party/httplib.h"

namespace aura {

struct SdkDiscoveryResult {
    bool found = false;
    std::filesystem::path include_dir;
    std::vector<std::filesystem::path> probed_paths;
};

bool IsValidPluginSdkDir(const std::filesystem::path& dir);
SdkDiscoveryResult DiscoverPluginSdkIncludeDir(const std::filesystem::path& explicit_sdk_path = {});
std::filesystem::path FindVcvars64Bat(const std::filesystem::path& explicit_path = {});

bool CompileCppSourceToDll(const std::string& effect_name, 
                           const std::string& source_code, 
                           std::string& out_log, 
                           std::string& out_dll_path, 
                           int& out_exit_code,
                           const std::filesystem::path& explicit_sdk_dir = {},
                           const std::filesystem::path& explicit_vcvars = {});

class WebServer {
public:
    WebServer(const std::filesystem::path& config_path, int port, const std::filesystem::path& sdk_include_dir = {},
        const std::filesystem::path& web_root = {}, const std::string& daemon_instance = "");
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
    std::filesystem::path sdk_include_dir_;
    std::filesystem::path web_root_;
    std::string daemon_instance_;
    std::mutex cfg_mutex_;
    httplib::Server svr_;
    std::atomic<bool> is_running_{false};
    mutable std::mutex file_mutex_;
};

} // namespace aura
