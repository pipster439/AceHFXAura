#include "web/web_server.h"
#include "third_party/json.hpp"
#include <fstream>
#include <iostream>
#include <chrono>
#include <filesystem>

namespace aura {

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

} // namespace

WebServer::WebServer(const std::string& config_path, int port)
    : config_path_(config_path), port_(port) {
    SetupRoutes();
}

WebServer::~WebServer() {
    Stop();
}

void WebServer::SetupRoutes() {
    // 根路径提供前端页面
    svr_.Get("/", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(LoadHtmlContent(), "text/html; charset=utf-8");
    });

    // 静态资源兜底 (支持外部 css/js/ico 等)
    svr_.Get("/web/(.*)", [](const httplib::Request& req, httplib::Response& res) {
        std::string subpath = req.matches[1];
        std::filesystem::path p = std::filesystem::path("web") / subpath;
        if (std::filesystem::exists(p) && !std::filesystem::is_directory(p)) {
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
    svr_.Get("/api/status", [](const httplib::Request&, httplib::Response& res) {
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        nlohmann::json j = {
            {"status", "ok"},
            {"service", "aura_web_ui"},
            {"timestamp", now_ms}
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
            res.set_content("{\"status\":\"error\",\"message\":\"无法读取配置文件\"}", "application/json; charset=utf-8");
        }
    });

    // 保存更新配置文件
    svr_.Post("/api/config", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            // 校验 JSON 格式合法性
            auto j = nlohmann::json::parse(req.body);
            if (!j.is_object() || !j.contains("profiles") || !j.contains("rules")) {
                res.status = 400;
                res.set_content("{\"status\":\"error\",\"message\":\"配置数据缺少 profiles 或 rules 核心字段\"}", "application/json; charset=utf-8");
                return;
            }

            // 格式化输出 (保持 2 格缩进)
            std::string formatted = j.dump(2);
            if (WriteConfigFile(formatted)) {
                res.set_content("{\"status\":\"ok\",\"message\":\"配置已保存，daemon 已通过热重载自动生效\"}", "application/json; charset=utf-8");
            } else {
                res.status = 500;
                res.set_content("{\"status\":\"error\",\"message\":\"写入配置文件失败\"}", "application/json; charset=utf-8");
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
    // 优先从本地文件系统加载最新的 index.html，方便前端热调试
    std::vector<std::filesystem::path> candidates = {
        "web/index.html",
        "../web/index.html",
        "../../web/index.html"
    };

    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) {
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
    std::string tmp_path = config_path_ + ".tmp";
    {
        std::ofstream f(tmp_path, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) {
            return false;
        }
        f.write(json_str.data(), json_str.size());
        f.flush();
    }

    // Windows MoveFileEx 原子替换
    if (!MoveFileExA(tmp_path.c_str(), config_path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        // 若 MoveFileEx 失败则尝试常规写入
        std::ofstream f(config_path_, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) return false;
        f.write(json_str.data(), json_str.size());
    }

    return true;
}

} // namespace aura
