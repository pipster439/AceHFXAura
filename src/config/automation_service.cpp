#include "config/automation_service.h"
namespace aura {
AutomationControlService::AutomationControlService(std::filesystem::path config_path)
    : config_path_(std::move(config_path)) {
}

bool AutomationControlService::ValidateWriteRequestSecurity(const httplib::Request& req, httplib::Response& res) {
    // 1. Host 校验：必须为环回地址
    if (!IsAllowedLoopbackHost(req.get_header_value("Host"))) {
        res.status = 403;
        res.set_content(R"json({"status":"error","error":"Forbidden","message":"Host header rejected"})json", "application/json; charset=utf-8");
        return false;
    }

    // 2. 强制 Content-Type: application/json
    if (!IsJsonContentType(req.get_header_value("Content-Type"))) {
        res.status = 415;
        res.set_content(R"json({"status":"error","error":"Unsupported Media Type","message":"Content-Type must be application/json"})json", "application/json; charset=utf-8");
        return false;
    }

    // 3. Origin / Referer 校验：防跨站伪造 (允许无 Origin/Referer 的原生 WinUI 客户端)
    std::string origin = req.get_header_value("Origin");
    std::string referer = req.get_header_value("Referer");
    if (!ValidateLoopbackOriginAndReferer(origin, referer)) {
        res.status = 403;
        res.set_content(R"json({"status":"error","error":"Forbidden","message":"Untrusted Origin or Referer rejected"})json", "application/json; charset=utf-8");
        return false;
    }

    return true;
}

void AutomationControlService::RegisterRoutes(httplib::Server& server) { RegisterAuthoringRoutes(server); }
}
