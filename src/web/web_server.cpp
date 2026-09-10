#include "web/web_server.h"
#include "third_party/json.hpp"
#include <fstream>
#include <iostream>
#include <chrono>
#include <filesystem>
#include <cctype>
#include <algorithm>

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

WebServer::WebServer(const std::filesystem::path& config_path, int port)
    : config_path_(config_path), port_(port) {
    SetupRoutes();
}

WebServer::~WebServer() {
    Stop();
}

void WebServer::SetupRoutes() {
    // 限制请求体上限为 256KB，防止超大请求引发内存拒绝服务 (DoS)
    svr_.set_payload_max_length(256 * 1024);
    svr_.set_error_handler([](const httplib::Request& /*req*/, httplib::Response& res) {
        if (res.status == 413) {
            res.set_content(R"json({"status":"error","error":"Payload Too Large","message":"请求体超过 256KB 上限"})json", "application/json; charset=utf-8");
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
        res.set_content(LoadHtmlContent(), "text/html; charset=utf-8");
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
                res.set_content(R"json({"status":"ok","message":"配置已保存，daemon 已通过热重载自动生效"})json", "application/json; charset=utf-8");
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
    std::filesystem::path tmp_path = config_path_.wstring() + L".tmp";
    {
        std::ofstream f(tmp_path, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) {
            return false;
        }
        f.write(json_str.data(), json_str.size());
        f.flush();
    }

    // Windows MoveFileExW 原子替换（支持非 ASCII 路径）
    if (!MoveFileExW(tmp_path.c_str(), config_path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        // 若 MoveFileExW 失败则尝试常规写入并清理临时文件
        std::ofstream f(config_path_, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) {
            std::error_code ec;
            std::filesystem::remove(tmp_path, ec);
            return false;
        }
        f.write(json_str.data(), json_str.size());
        std::error_code ec;
        std::filesystem::remove(tmp_path, ec);
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
