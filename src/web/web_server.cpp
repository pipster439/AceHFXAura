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
        std::vector<std::string> paths = DetectCs2CfgPaths();
        std::string detected = paths.empty() ? "" : paths[0];

        bool installed = false;
        if (!detected.empty()) {
            std::filesystem::path cfg_file = std::filesystem::path(detected) / "gamestate_integration_aura.cfg";
            installed = std::filesystem::exists(cfg_file);
        }

        nlohmann::json j = {
            {"filename", "gamestate_integration_aura.cfg"},
            {"content", GetGsiCfgTemplate()},
            {"detected_path", detected},
            {"all_paths", paths},
            {"installed", installed}
        };
        res.set_content(j.dump(2), "application/json; charset=utf-8");
    });

    // 安装 GSI 配置文件到 CS2 目录 (带用户明确确认与路径校验，绝不静默写入)
    svr_.Post("/api/gsi/install-cfg", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = nlohmann::json::parse(req.body);
            std::string target_dir = j.value("target_dir", "");
            if (target_dir.empty()) {
                auto paths = DetectCs2CfgPaths();
                if (!paths.empty()) target_dir = paths[0];
            }

            if (target_dir.empty()) {
                res.status = 400;
                res.set_content("{\"status\":\"error\",\"message\":\"未指定且未能自动检测到 CS2 cfg 目录\"}", "application/json; charset=utf-8");
                return;
            }

            std::filesystem::path p(target_dir);
            if (!std::filesystem::exists(p) || !std::filesystem::is_directory(p)) {
                res.status = 400;
                res.set_content("{\"status\":\"error\",\"message\":\"目标路径不存在或不是有效目录: " + target_dir + "\"}", "application/json; charset=utf-8");
                return;
            }

            std::filesystem::path file_path = p / "gamestate_integration_aura.cfg";
            std::ofstream out(file_path, std::ios::trunc | std::ios::binary);
            if (!out.is_open()) {
                res.status = 500;
                res.set_content("{\"status\":\"error\",\"message\":\"无法向目标文件写入数据，请检查文件写权限\"}", "application/json; charset=utf-8");
                return;
            }

            out << GetGsiCfgTemplate();
            out.flush();

            nlohmann::json resp = {
                {"status", "ok"},
                {"message", "成功安装 gamestate_integration_aura.cfg 到 CS2 目录！启动 CS2 即可开始接收实时游戏数据。"},
                {"path", file_path.string()}
            };
            res.set_content(resp.dump(), "application/json; charset=utf-8");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(std::string("{\"status\":\"error\",\"message\":") + nlohmann::json(e.what()).dump() + "}", "application/json; charset=utf-8");
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

std::vector<std::string> WebServer::DetectCs2CfgPaths() const {
    std::vector<std::string> result;

    // 常见可能盘符路径优先扫描
    const std::vector<std::string> prefixes = {
        "D:\\SteamLibrary",
        "C:\\Program Files (x86)\\Steam",
        "C:\\SteamLibrary",
        "E:\\SteamLibrary",
        "F:\\SteamLibrary",
        "G:\\SteamLibrary",
        "C:\\Steam",
        "D:\\Steam",
        "E:\\Steam"
    };

    for (const auto& pre : prefixes) {
        std::filesystem::path p = std::filesystem::path(pre) / "steamapps" / "common" / "Counter-Strike Global Offensive" / "game" / "csgo" / "cfg";
        if (std::filesystem::exists(p) && std::filesystem::is_directory(p)) {
            result.push_back(p.string());
        }
    }

    // 解析 Steam libraryfolders.vdf
    std::filesystem::path vdf_path = "C:\\Program Files (x86)\\Steam\\steamapps\\libraryfolders.vdf";
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
                        std::filesystem::path cs2_cfg = std::filesystem::path(clean_path) / "steamapps" / "common" / "Counter-Strike Global Offensive" / "game" / "csgo" / "cfg";
                        if (std::filesystem::exists(cs2_cfg) && std::filesystem::is_directory(cs2_cfg)) {
                            std::string s = cs2_cfg.string();
                            if (std::find(result.begin(), result.end(), s) == result.end()) {
                                result.push_back(s);
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
