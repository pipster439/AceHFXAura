#include "engine/input_capture_service.h"
#include "config/config_writer_util.h"
#include "gsi/automation_input.h"

namespace aura {
namespace {
using Json = nlohmann::json;
const char* Availability(InputAvailability value) {
    switch (value) {
        case InputAvailability::Disabled: return "disabled";
        case InputAvailability::Fresh: return "fresh";
        case InputAvailability::Stale: return "stale";
        default: return "unknown";
    }
}
Json Reading(const InputHealth& health, const InputSourceMetrics& source,
             uint64_t captured, uint64_t errors) {
    return {{"availability", Availability(health.availability)}, {"observed_at_ms", health.observed_at_ms},
        {"age_ms", health.age_ms}, {"sequence", health.sequence}, {"accepted_samples", source.accepted_samples},
        {"rejected_samples", source.rejected_samples}, {"reads", source.reads},
        {"frames_captured", captured}, {"capture_errors", errors}};
}
}

InputCaptureService::InputCaptureService()
    : audio_adapter_(audio_), screen_adapter_(screen_) {}
InputCaptureService::~InputCaptureService() { Shutdown(); }
void InputCaptureService::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    screen_adapter_.Disable(); audio_adapter_.Disable();
    screen_.Disable(); audio_.Disable();
}

Json InputCaptureService::StatusLocked(uint64_t now_ms) const {
    const auto audio_reading = audio_.Read(now_ms);
    const auto screen_reading = screen_.Read(now_ms);
    return {{"schema_version", 1}, {"consent_scope", "daemon_session"},
        {"audio", {{"enabled", audio_adapter_.Enabled()}, {"capture", Reading(audio_reading.health, audio_.Metrics(),
            audio_adapter_.GetMetrics().frames_captured, audio_adapter_.GetMetrics().capture_errors)}}},
        {"screen", {{"enabled", screen_adapter_.Enabled()}, {"capture", Reading(screen_reading.health, screen_.Metrics(),
            screen_adapter_.GetMetrics().frames_captured, screen_adapter_.GetMetrics().capture_errors)}}}};
}
Json InputCaptureService::Status(uint64_t now_ms) const {
    std::lock_guard<std::mutex> lock(mutex_); return StatusLocked(now_ms);
}
Json InputCaptureService::SetEnabled(const Json& request, uint64_t now_ms) {
    if (!request.is_object()) return {{"status", "error"}, {"error", "invalid_request"}};
    for (const auto& field : request.items()) {
        if ((field.key() != "audio" && field.key() != "screen") || !field.value().is_boolean())
            return {{"status", "error"}, {"error", "invalid_request"}};
    }
    std::lock_guard<std::mutex> lock(mutex_);
    bool ok = true;
    if (request.contains("audio")) ok = request["audio"].get<bool>() ? audio_adapter_.Enable() : (audio_adapter_.Disable(), true);
    if (request.contains("screen")) ok = request["screen"].get<bool>() ? screen_adapter_.Enable() : (screen_adapter_.Disable(), true);
    auto result = StatusLocked(now_ms); result["status"] = ok ? "ok" : "error";
    if (!ok) result["error"] = "capture_start_failed";
    return result;
}
void InputCaptureService::RegisterRoutes(httplib::Server& server) {
    server.Get("/api/inputs", [this](const httplib::Request& req, httplib::Response& res) {
        if (!IsAllowedLoopbackHost(req.get_header_value("Host"))) { res.status = 403; return; }
        if (!req.params.empty()) { res.status = 422; return; }
        res.set_header("Cache-Control", "no-store");
        res.set_content(Status(AutomationMonotonicMs()).dump(), "application/json");
    });
    server.Post("/api/inputs", [this](const httplib::Request& req, httplib::Response& res) {
        if (!IsAllowedLoopbackHost(req.get_header_value("Host")) ||
            !ValidateLoopbackOriginAndReferer(req.get_header_value("Origin"), req.get_header_value("Referer"))) { res.status = 403; return; }
        if (!IsJsonContentType(req.get_header_value("Content-Type"))) { res.status = 415; return; }
        if (req.body.size() > 1024) { res.status = 413; return; }
        try {
            auto result = SetEnabled(Json::parse(req.body), AutomationMonotonicMs());
            res.status = result.value("status", "error") == "ok" ? 200 : 422;
            res.set_content(result.dump(), "application/json");
        } catch (...) { res.status = 422; res.set_content(R"({"status":"error","error":"invalid_request"})", "application/json"); }
    });
}

} // namespace aura
