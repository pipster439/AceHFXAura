#include "config/plugin_overview_service.h"
#include "config/config_writer_util.h"
#include "engine/plugin_manager.h"
#include "third_party/json.hpp"

#include <cmath>

namespace aura {
namespace {
using Json = nlohmann::json;

bool ValidPluginPost(const httplib::Request& request, httplib::Response& response) {
    if (!IsAllowedLoopbackHost(request.get_header_value("Host")) ||
        !ValidateLoopbackOriginAndReferer(request.get_header_value("Origin"),
                                         request.get_header_value("Referer"))) {
        response.status = 403;
    } else if (!IsJsonContentType(request.get_header_value("Content-Type"))) {
        response.status = 415;
    } else if (request.body.size() > 4096) {
        response.status = 413;
    } else {
        return true;
    }
    response.set_content(R"({"status":"error","message":"Invalid plugin request"})",
                         "application/json; charset=utf-8");
    return false;
}

} // namespace

PluginOverviewService::PluginOverviewService(
    std::filesystem::path runtime_root,
    std::function<m605::hall::HallTelemetrySnapshot()> hall_snapshot_provider)
    : runtime_root_(std::move(runtime_root)),
      hall_snapshot_provider_(std::move(hall_snapshot_provider)) {}

std::string PluginOverviewService::BuildOverviewJsonString() const {
    std::error_code ec;
    const auto plugins_dir = std::filesystem::weakly_canonical(
        std::filesystem::absolute(runtime_root_ / "plugins"), ec);
    const auto wooting_dir = plugins_dir / "bridges" / "wooting-analog-m605";
    const auto wooting_dll = wooting_dir / "target" / "release" / "wooting_analog_plugin_aura_m605.dll";
    const auto install_script = wooting_dir / "install_plugin.ps1";
    const std::filesystem::path installed_dll =
        R"(C:\Program Files\WootingAnalogPlugins\aura-m605-plugin\wooting_analog_plugin_aura_m605.dll)";

    const auto installed_dll_exists = std::filesystem::is_regular_file(installed_dll, ec);
    const auto mcp_dir = plugins_dir / "mcp";
    const auto mcp_script = mcp_dir / "aura_mcp_server.py";
    const bool mcp_exists = std::filesystem::is_regular_file(mcp_script, ec);

    const bool wooting_built = std::filesystem::is_regular_file(wooting_dll, ec);
    const bool wooting_installed = installed_dll_exists;

    const auto hall = hall_snapshot_provider_ ? hall_snapshot_provider_()
                                              : m605::hall::HallTelemetrySnapshot{};

    Json native_plugins = Json::array();
    for (const auto& s : PluginManager::Instance().GetPluginSummaries()) {
        native_plugins.push_back({
            {"name", s.effect_name},
            {"kind", s.kind},
            {"generation_id", s.generation_id},
            {"path", s.original_path.string()},
            {"has_lifecycle", s.has_lifecycle},
            {"has_logical_keyboard", s.has_logical_keyboard},
            {"has_hall_analog", s.has_hall_analog}
        });
    }

    Json bridges = Json::array({
        {
            {"id", "wooting-analog-m605"},
            {"name", "Wooting Analog SDK 桥接"},
            {"description", "将 ROG Falchion Ace HFX 映射为 Wooting Analog 设备，供 UAC 模拟手柄与支持 Wooting SDK 的游戏/OBS 插件使用。"},
            {"source_dir", wooting_dir.string()},
            {"dll_path", wooting_dll.string()},
            {"install_script_path", install_script.string()},
            {"installed_path", installed_dll.string()},
            {"built", wooting_built},
            {"installed", wooting_installed},
            {"shared_memory_name", m605::hall::kHallShmNameUtf8},
            {"shared_memory_ok", hall.shared_memory_ok},
            {"hall_active", hall.active},
            {"hall_rate_hz", std::round(hall.packet_rate_hz * 10.0) / 10.0}
        },
        {
            {"id", "aura-mcp-server"},
            {"name", "Aura MCP 外部接入服务"},
            {"description", "基于 stdio (JSON-RPC 2.0) 的外部 AI 扩展桥接，替代 Studio 内置 AI 助手，供 Antigravity / Claude / Cursor 直接读写 IR 灯效、原生 C++ 插件与霍尔遥测。"},
            {"source_dir", mcp_dir.string()},
            {"dll_path", mcp_script.string()},
            {"install_script_path", ""},
            {"installed_path", mcp_script.string()},
            {"built", mcp_exists},
            {"installed", mcp_exists},
            {"shared_memory_name", "stdio (JSON-RPC 2.0)"},
            {"shared_memory_ok", mcp_exists},
            {"hall_active", false},
            {"hall_rate_hz", 0.0}
        }
    });

    const Json out = {
        {"status", "ok"},
        {"api_version", 1},
        {"plugins_dir", plugins_dir.string()},
        {"native_plugins", native_plugins},
        {"bridges", bridges}
    };
    return out.dump();
}

void PluginOverviewService::RegisterRoutes(httplib::Server& server) {
    server.Get("/api/plugins/overview", [this](const httplib::Request& request, httplib::Response& response) {
        if (!IsAllowedLoopbackHost(request.get_header_value("Host"))) {
            response.status = 403;
            return;
        }
        response.status = 200;
        response.set_header("Cache-Control", "no-store");
        response.set_content(BuildOverviewJsonString(), "application/json; charset=utf-8");
    });

    server.Post("/api/plugins/rescan", [this](const httplib::Request& request, httplib::Response& response) {
        if (!ValidPluginPost(request, response)) return;
        const Json body = Json::parse(request.body, nullptr, false);
        if (!body.is_object() || !body.empty()) {
            response.status = 422;
            response.set_content(R"({"status":"error","message":"Expected empty JSON object"})",
                                 "application/json; charset=utf-8");
            return;
        }
        PluginManager::Instance().LoadAllFromDirectory(runtime_root_ / "plugins");
        response.status = 200;
        response.set_header("Cache-Control", "no-store");
        response.set_content(BuildOverviewJsonString(), "application/json; charset=utf-8");
    });
}

} // namespace aura
