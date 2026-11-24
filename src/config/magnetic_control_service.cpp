#include "config/magnetic_control_service.h"
#include "aura/hardware/m605_firmware_json.h"
#include "monitor/hardware_rt_gate_monitor.h"
#include "config/config_writer_util.h"
#include "config/device_profile_runtime.h"
#include "config/device_profile_automation_coordinator.h"
#include "utils/logger.h"
#include "aura/hardware/m605_key_mapping.h"
#include <cmath>
#include <future>
#include <set>
#include <string_view>
#include <vector>

namespace aura {
namespace {
using Json = nlohmann::json;

bool ValidRequest(const httplib::Request& request, httplib::Response& response) {
    if (!IsAllowedLoopbackHost(request.get_header_value("Host")) ||
        !ValidateLoopbackOriginAndReferer(request.get_header_value("Origin"),
                                         request.get_header_value("Referer"))) response.status = 403;
    else if (!IsJsonContentType(request.get_header_value("Content-Type"))) response.status = 415;
    else if (request.body.size() > 4096) response.status = 413;
    else return true;
    response.set_content(R"({"status":"error","message":"Invalid magnetic request"})", "application/json");
    return false;
}

bool ExactFields(const Json& body, std::initializer_list<const char*> fields) {
    if (!body.is_object() || body.size() != fields.size()) return false;
    for (const auto* field : fields) if (!body.contains(field)) return false;
    return true;
}

bool LogicalKeyValue(const Json& value, uint16_t& key) {
    if (!value.is_number_integer()) return false;
    if (value.is_number_unsigned() && value.get<uint64_t>() > 65535) return false;
    const auto number = value.get<int64_t>();
    if (number < 0 || number > 65535 ||
        !m605::WireIdForLogicalKey(static_cast<uint16_t>(number))) return false;
    key = static_cast<uint16_t>(number);
    return true;
}

bool LogicalKey(const Json& body, const char* field, uint16_t& key) {
    return body.is_object() && body.contains(field) && LogicalKeyValue(body.at(field), key);
}

bool BatchLogicalKeys(const Json& body, std::vector<uint16_t>& keys) {
    if (!body.is_object() || !body.contains("logical_ids") ||
        !body.at("logical_ids").is_array() || body.at("logical_ids").empty() ||
        body.at("logical_ids").size() > m605::VerifiedM605PhysicalKeyCount()) return false;
    std::set<uint16_t> requested;
    for (const auto& value : body.at("logical_ids")) {
        uint16_t key = 0;
        if (!LogicalKeyValue(value, key) || !requested.insert(key).second) return false;
    }
    // The audited physical layout is the execution order, independent of JSON/click order.
    for (const auto& physical : m605::detail::kVerifiedPhysicalKeys)
        if (requested.find(physical.logical_id) != requested.end()) keys.push_back(physical.logical_id);
    return keys.size() == requested.size();
}

bool Millimeters(const Json& body, const char* field, int min_raw, int max_raw, double& value) {
    if (!body.is_object() || !body.contains(field) || !body.at(field).is_number()) return false;
    value = body.at(field).get<double>();
    if (!std::isfinite(value)) return false;
    const auto raw = std::round(value * 10.0);
    return raw >= min_raw && raw <= max_raw && std::abs(value * 10.0 - raw) < 1e-7;
}

bool Boolean(const Json& body, const char* field, bool& value) {
    if (!body.is_object() || !body.contains(field) || !body.at(field).is_boolean()) return false;
    value = body.at(field).get<bool>();
    return true;
}

std::optional<m605::DksTriggerState> Trigger(const Json& value) {
    if (!value.is_string()) return std::nullopt;
    const auto& name = value.get_ref<const std::string&>();
    if (name == "Inactive") return m605::DksTriggerState::Inactive;
    if (name == "Tap") return m605::DksTriggerState::Tap;
    if (name == "Release") return m605::DksTriggerState::Release;
    if (name == "Hold") return m605::DksTriggerState::Hold;
    return std::nullopt;
}

bool DksConfiguration(const Json& body, m605::DksConfig& config) {
    if (!ExactFields(body, {"logical_id", "start_mm", "end_mm", "slots", "resolve_rt"}) ||
        !LogicalKey(body, "logical_id", config.source_logical_key_id) ||
        !Millimeters(body, "start_mm", 1, 40, config.start_mm) ||
        !Millimeters(body, "end_mm", 1, 40, config.end_mm) ||
        config.start_mm > config.end_mm || !body.at("slots").is_array() ||
        body.at("slots").size() != 4 || !body.at("resolve_rt").is_boolean()) return false;
    for (size_t i = 0; i < 4; ++i) {
        const auto& item = body.at("slots")[i];
        if (!ExactFields(item, {"target", "down_start", "down_end", "up_start", "up_end"}) ||
            !item.at("target").is_object()) return false;
        const auto& target = item.at("target");
        if (ExactFields(target, {"kind"}) && target.at("kind") == "DefaultSentinel") {
            config.slots[i].target = m605::DksTarget::DefaultSentinel();
        } else if (ExactFields(target, {"kind", "logical_id"}) &&
                   target.at("kind") == "LogicalKey") {
            uint16_t id = 0;
            if (!LogicalKey(target, "logical_id", id) || id == 0x0508) return false;
            config.slots[i].target = m605::DksTarget::LogicalKey(id);
        } else return false;
        const auto down_start = Trigger(item.at("down_start"));
        const auto down_end = Trigger(item.at("down_end"));
        const auto up_start = Trigger(item.at("up_start"));
        const auto up_end = Trigger(item.at("up_end"));
        if (!down_start || !down_end || !up_start || !up_end) return false;
        config.slots[i].down_start = *down_start;
        config.slots[i].down_end = *down_end;
        config.slots[i].up_start = *up_start;
        config.slots[i].up_end = *up_end;
    }
    return m605::BuildPerKeyDksStages(config).has_value();
}

const char* HealthName(M605RuntimeHealth health) {
    switch (health) {
    case M605RuntimeHealth::Clean: return "Clean";
    case M605RuntimeHealth::TransactionInProgress: return "TransactionInProgress";
    case M605RuntimeHealth::IndeterminateStagedState: return "IndeterminateStagedState";
    case M605RuntimeHealth::PersistentSafetyQuarantine: return "PersistentSafetyQuarantine";
    case M605RuntimeHealth::Stopped: return "Stopped";
    }
    return "Stopped";
}

Json KnownRaw(std::optional<uint8_t> raw) {
    return raw ? Json{{"known", true}, {"raw", *raw}, {"source", "HostProfile"}} :
                 Json{{"known", false}, {"source", "Unknown"}};
}

Json HallSnapshotJson(const m605::hall::HallTelemetrySnapshot& snap) {
    Json keys = Json::array();
    for (const auto& k : snap.keys) {
        keys.push_back({
            {"logical_id", k.logical_id},
            {"hid_usage", k.hid_usage},
            {"name", k.name},
            {"raw", k.raw},
            {"depth_mm", std::round(k.depth_mm * 100.0) / 100.0},
            {"normalized", std::round(k.normalized * 10000.0) / 10000.0},
            {"digital_down", k.digital_down}
        });
    }
    return {
        {"status", "ok"},
        {"api_version", 1},
        {"active", snap.active},
        {"event_channel_ok", snap.event_channel_ok},
        {"shared_memory_ok", snap.shared_memory_ok},
        {"shared_memory_name", m605::hall::kHallShmNameUtf8},
        {"packet_count", snap.packet_count},
        {"packet_rate_hz", std::round(snap.packet_rate_hz * 10.0) / 10.0},
        {"last_error", snap.last_error},
        {"keys", keys}
    };
}

Json VerifyStatusJson(const m605::hall::VerifyStatusSnapshot& snap) {
    Json results = Json::array();
    for (const auto& r : snap.results) {
        results.push_back({
            {"kind", r.kind},
            {"label", r.label},
            {"target_mm", r.target_mm},
            {"measured_mm", r.measured_mm},
            {"error_mm", r.error_mm},
            {"passed", r.passed},
            {"detail", r.detail}
        });
    }
    return {
        {"status", "ok"},
        {"api_version", 1},
        {"state", snap.state},
        {"logical_id", snap.logical_id},
        {"key_name", snap.key_name},
        {"mode", snap.mode},
        {"instruction", snap.instruction},
        {"step_index", snap.step_index},
        {"step_total", snap.step_total},
        {"live_depth_mm", snap.live_depth_mm},
        {"live_raw", snap.live_raw},
        {"digital_down", snap.digital_down},
        {"bound_bit", snap.bound_bit},
        {"error", snap.error},
        {"results", results}
    };
}

enum class Mutation {
    Actuation, RtOn, RtOff, Deadzone, ResetAllDeadzone, Dks,
    DksStandard, SpeedTapPairOn, SpeedTapPairOff, SpeedTapMaster,
    SpeedTapProfileReset, StaticAnalog,
    GlobalActuation, GlobalDeadzone, GlobalRapidTrigger
};
} // namespace

MagneticControlService::MagneticControlService(std::shared_ptr<const RuntimeStatusStore> status_store,
    std::function<MagneticHostProfile()> host_profile, std::unique_ptr<M605Runtime> runtime,
    std::filesystem::path profile_path, std::filesystem::path legacy_config)
    : status_store_(std::move(status_store)), host_profile_(std::move(host_profile)),
      runtime_(std::move(runtime)),
      hall_telemetry_(std::make_unique<m605::hall::M605HallTelemetry>()) {
    if (!profile_path.empty())
        profiles_ = std::make_unique<DeviceProfileRuntime>(std::move(profile_path),
            std::move(legacy_config), *runtime_, write_mutex_, host_profile_,
            [this] { return HardwareAvailable(); });
}
MagneticControlService::~MagneticControlService() { Stop(); }
void MagneticControlService::StartHardwareRtGateObservation() {
    if (rt_gate_monitor_) return;
    rt_gate_monitor_ = std::make_unique<HardwareRtGateMonitor>([this](const auto& value) {
        runtime_->ObserveHardwareRtGate(value);
    });
    rt_gate_monitor_->Start();
}
void MagneticControlService::Stop() {
    StopDeviceProfileAutomation();
    if (hall_telemetry_) hall_telemetry_->Stop();
    if (rt_gate_monitor_) rt_gate_monitor_->Stop();
    runtime_->Stop();
}
m605::hall::HallTelemetrySnapshot MagneticControlService::GetHallSnapshot() const {
    return hall_telemetry_ ? hall_telemetry_->GetSnapshot() : m605::hall::HallTelemetrySnapshot{};
}
void MagneticControlService::StartDeviceProfileAutomation() {
    if (!profiles_ || automation_coordinator_) return;
    automation_coordinator_ = std::make_unique<DeviceProfileAutomationCoordinator>(
        [this] { return profiles_->AutomationAdmissionSnapshot(); },
        [this](const auto& token, const auto& stopping) { return profiles_->ActivateAutomationDecision(token, stopping); },
        DeviceProfileAutomationCoordinator::Clock{}, [](const auto& snapshot) {
            LOG_INFO("[DeviceProfileAutomation] " + snapshot.dump());
        });
    profiles_->SetAutomationCoordinatorDiagnostics([this] { return automation_coordinator_->Snapshot(); });
    automation_coordinator_->Start();
}
void MagneticControlService::StopDeviceProfileAutomation() {
    if (automation_coordinator_) automation_coordinator_->Stop();
}

bool MagneticControlService::ObserveDeviceProfileForeground(const std::string& name) {
    const bool changed = profiles_ && profiles_->ObserveAutomationForeground(name);
    if (automation_coordinator_) automation_coordinator_->Wake();
    return changed;
}

void MagneticControlService::ObserveHardwareSlot() {
    auto hall_pause = hall_telemetry_ ?
        std::optional<m605::hall::M605HallTelemetry::ScopedPause>(hall_telemetry_->PauseForTransaction()) :
        std::nullopt;
    if (profiles_) profiles_->ObserveHardwareSlot();
}
bool MagneticControlService::WithProfileLightingOwnership(const std::function<bool()>& write_frame) {
    return profiles_ ? profiles_->WithAuraLightingOwnership(write_frame) : write_frame();
}

bool MagneticControlService::HardwareAvailable() const {
    if (!status_store_) return false;
    const auto status = status_store_->GetSnapshot();
    return !status.dry_run && status.hardware_connected && status.active_backend == "native_hid";
}

void MagneticControlService::WriteStatus(httplib::Response& response, bool result, int status_code,
                                         std::string_view detail) const {
    runtime_->RefreshTransportPresence();
    const auto health = runtime_->GetHealth();
    const auto shadow = runtime_->GetAppliedRuntimeState();
    const auto host = host_profile_();
    Json body = {{"status", result ? "ok" : "error"}, {"api_version", 1},
                 {"health", HealthName(health)}, {"available", HardwareAvailable()},
                 {"persistent_safety_quarantine", runtime_->IsPersistentSafetyQuarantined()},
                 {"applied_state_source", "SessionApplied: completed AceHFXAura runtime sequence, not device readback"},
                 {"last_error", detail.empty() ? runtime_->GetLastError() : std::string(detail)}};
    body["m605_session_generation"] = runtime_->GetSessionGeneration();
    body["firmware_compatibility"] = m605::FirmwareCompatibilityJson(runtime_->GetDiagnosticSnapshot().firmware);
    body["host_profile"] = {{"source", host.active_profile_id ? "HostProfile" : "Unknown"},
                             {"active_profile_id", host.active_profile_id ? Json(*host.active_profile_id) : Json(nullptr)},
                             {"global_actuation", KnownRaw(host.global_actuation_raw)},
                             {"global_rt_press", KnownRaw(host.global_rt_press_raw)},
                             {"global_rt_release", KnownRaw(host.global_rt_release_raw)},
                             {"global_deadzone_top", KnownRaw(host.global_deadzone_top_raw)},
                             {"global_deadzone_bottom", KnownRaw(host.global_deadzone_bottom_raw)},
                             {"per_key_rt_list_known", host.per_key_rt_list_known}};
    body["global_actuation"] = shadow.global_actuation_raw ?
        Json{{"known", true}, {"raw", *shadow.global_actuation_raw}, {"source", "SessionApplied"}} :
        host.global_actuation_raw ?
        Json{{"known", true}, {"raw", *host.global_actuation_raw}, {"source", "HostProfile"}} :
        Json{{"known", false}, {"source", "Unknown"}};
    body["global_deadzone"] = shadow.global_deadzone ?
        Json{{"known", true}, {"top_raw", shadow.global_deadzone->top_raw},
             {"bottom_raw", shadow.global_deadzone->bottom_raw}, {"source", "SessionApplied"}} :
        (host.global_deadzone_top_raw && host.global_deadzone_bottom_raw) ?
        Json{{"known", true}, {"top_raw", *host.global_deadzone_top_raw},
             {"bottom_raw", *host.global_deadzone_bottom_raw}, {"source", "HostProfile"}} :
        Json{{"known", false}, {"source", "Unknown"}};
    body["global_rapid_trigger"] = shadow.global_rapid_trigger ?
        Json{{"known", true}, {"separate_mode", shadow.global_rapid_trigger->separate_mode},
             {"press_raw", shadow.global_rapid_trigger->press_raw},
             {"release_raw", shadow.global_rapid_trigger->release_raw},
             {"top_raw", shadow.global_rapid_trigger->top_raw},
             {"bottom_raw", shadow.global_rapid_trigger->bottom_raw},
             {"source", "SessionApplied"}} :
        (host.global_rt_press_raw && host.global_rt_release_raw) ?
        Json{{"known", true},
             {"separate_mode", host.global_rt_separate_mode ? Json(*host.global_rt_separate_mode) :
                               Json(*host.global_rt_press_raw != *host.global_rt_release_raw)},
             {"press_raw", *host.global_rt_press_raw},
             {"release_raw", *host.global_rt_release_raw},
             {"top_raw", host.global_deadzone_top_raw ? Json(*host.global_deadzone_top_raw) : Json(nullptr)},
             {"bottom_raw", host.global_deadzone_bottom_raw ? Json(*host.global_deadzone_bottom_raw) : Json(nullptr)},
             {"source", "HostProfile"}} :
        Json{{"known", false}, {"source", "Unknown"}};
    body["actuation"] = Json::array();
    for (const auto& [key, raw] : shadow.per_key_actuation_raw)
        body["actuation"].push_back({{"logical_id", key}, {"raw", raw}, {"source", "SessionApplied"}});
    body["rapid_trigger"] = Json::array();
    for (const auto& [key, enabled] : host.per_key_rt_enabled)
        body["rapid_trigger"].push_back({{"logical_id", key}, {"enabled", enabled}, {"source", "HostProfile"}});
    for (const auto& [key, rt] : shadow.per_key_rapid_trigger) {
        auto& list = body["rapid_trigger"];
        for (auto it = list.begin(); it != list.end(); ++it) {
            if (it->at("logical_id") == key) { list.erase(it); break; }
        }
        list.push_back({{"logical_id", key}, {"enabled", rt.enabled},
                        {"press_raw", rt.press_raw}, {"release_raw", rt.release_raw},
                        {"press_known", rt.press_known}, {"release_known", rt.release_known}, {"continuous", rt.continuous},
                        {"source", "SessionApplied"}});
    }
    body["deadzone"] = Json::array();
    for (const auto& [key, dz] : shadow.per_key_deadzone)
        body["deadzone"].push_back({{"logical_id", key}, {"top_raw", dz.top_raw},
                                    {"bottom_raw", dz.bottom_raw}, {"source", "SessionApplied"}});
    body["dks"] = Json::array();
    for (const auto& [key, dks] : shadow.per_key_dks) {
        Json slots = Json::array();
        for (const auto& slot : dks.slots) {
            auto state = [](m605::DksTriggerState value) -> const char* {
                switch (value) {
                case m605::DksTriggerState::Inactive: return "Inactive";
                case m605::DksTriggerState::Tap: return "Tap";
                case m605::DksTriggerState::Release: return "Release";
                case m605::DksTriggerState::Hold: return "Hold";
                }
                return "Inactive";
            };
            slots.push_back({{"target", slot.target.kind == m605::DksTarget::Kind::DefaultSentinel ?
                Json{{"kind", "DefaultSentinel"}} :
                Json{{"kind", "LogicalKey"}, {"logical_id", slot.target.logical_key_id}}},
                {"down_start", state(slot.down_start)}, {"down_end", state(slot.down_end)},
                {"up_start", state(slot.up_start)}, {"up_end", state(slot.up_end)}});
        }
        body["dks"].push_back({{"logical_id", key}, {"start_raw", dks.start_raw},
            {"end_raw", dks.end_raw}, {"slots", slots},
            {"standard_runtime_configuration", dks.standard_runtime_configuration},
            {"source", "SessionApplied"}});
    }
    body["speedtap"] = {{"pair_knowledge", shadow.speedtap_pair_knowledge ==
        M605AppliedRuntimeState::SpeedTapPairKnowledge::ProfileBaselineUnknown ?
        "ProfileBaselineUnknown" : "Unknown"}, {"pair_submissions", Json::array()},
        {"saved_profile_pairs", Json::array()},
        {"saved_profile_pairs_known", host.saved_speedtap_pairs_known},
        {"master", shadow.speedtap_master ? Json{{"known", true}, {"value", *shadow.speedtap_master},
            {"source", "SessionApplied"}} : Json{{"known", false}, {"source", "Unknown"}}}};
    for (const auto& [pair, enabled] : shadow.speedtap_pair_submissions)
        body["speedtap"]["pair_submissions"].push_back({{"key1", pair.first}, {"key2", pair.second},
            {"enabled", enabled}, {"source", "SessionApplied"}});
    for (const auto& [pair, enabled] : host.saved_speedtap_pairs)
        body["speedtap"]["saved_profile_pairs"].push_back({{"key1", pair.first}, {"key2", pair.second},
            {"enabled", enabled}, {"source", "HostProfile"}});
    body["static_analog_effect"] = shadow.static_analog_effect ?
        Json{{"known", true}, {"value", *shadow.static_analog_effect}, {"source", "SessionApplied"}} :
        host.static_analog_effect ?
        Json{{"known", true}, {"value", *host.static_analog_effect}, {"source", "HostProfile"}} :
        Json{{"known", false}, {"source", "Unknown"}};
    const auto gate = runtime_->GetHardwareRtGateObservation();
    body["rapid_trigger_master"] = Json{{"known", gate.state != HardwareRtGateState::Unknown},
        {"source", gate.state == HardwareRtGateState::Unknown ? "Unknown" : "USBStatusNotification"}};
    if (gate.state != HardwareRtGateState::Unknown)
        body["rapid_trigger_master"]["value"] = gate.state == HardwareRtGateState::On;
    response.status = status_code;
    response.set_header("Cache-Control", "no-store");
    response.set_content(body.dump(), "application/json; charset=utf-8");
}

void MagneticControlService::RegisterRoutes(httplib::Server& server) {
    if (profiles_) profiles_->RegisterRoutes(server);
    server.Get("/api/magnetic/status", [this](const httplib::Request& request, httplib::Response& response) {
        if (!IsAllowedLoopbackHost(request.get_header_value("Host"))) { response.status = 403; return; }
        WriteStatus(response, true, 200);
    });

    server.Post("/api/magnetic/safety/acknowledge-external-resynchronization",
        [this](const httplib::Request& request, httplib::Response& response) {
            if (!ValidRequest(request, response)) return;
            const Json body = Json::parse(request.body, nullptr, false);
            bool confirm = false;
            if (!ExactFields(body, {"confirm_external_resynchronization"}) ||
                !Boolean(body, "confirm_external_resynchronization", confirm) || !confirm) {
                WriteStatus(response, false, 422, "Explicit confirmation of external resynchronization is required");
                return;
            }
            std::unique_lock<std::mutex> write_lock(write_mutex_, std::try_to_lock);
            if (!write_lock.owns_lock() ||
                runtime_->GetHealth() == M605RuntimeHealth::TransactionInProgress ||
                runtime_->HasQueuedWork()) {
                WriteStatus(response, false, 409, "Cannot acknowledge external resynchronization while transaction or work is pending");
                return;
            }
            if (!runtime_->AcknowledgeExternalResynchronization()) {
                WriteStatus(response, false, 409, "Persistent safety quarantine acknowledgment failed or runtime not quarantined");
                return;
            }
            if (profiles_) profiles_->ExternalMutationLocked();
            WriteStatus(response, true, 200);
        });

    const auto write = [this, &server](const char* route, Mutation kind) {
        server.Post(route, [this, kind](const httplib::Request& request, httplib::Response& response) {
            if (!ValidRequest(request, response)) return;
            if (kind == Mutation::GlobalRapidTrigger) {
                WriteStatus(response, false, 422, "Legacy 51 53 Rapid Trigger writer blocked: not physically validated");
                return; // No mutation invalidation, baseline change, or HID operation.
            }
            const Json body = Json::parse(request.body, nullptr, false);
            uint16_t key = 0, other = 0;
            double first = 0, second = 0;
            bool flag = false, resolve = false;
            m605::DksConfig dks;
            bool valid = false;
            switch (kind) {
            case Mutation::Actuation:
                valid = ExactFields(body, {"logical_id", "mm"}) && LogicalKey(body, "logical_id", key) &&
                    Millimeters(body, "mm", 1, 40, first); break;
            case Mutation::RtOn:
                valid = ExactFields(body, {"logical_id", "press_mm", "release_mm", "resolve_dks"}) &&
                    LogicalKey(body, "logical_id", key) && Millimeters(body, "press_mm", 1, 25, first) &&
                    Millimeters(body, "release_mm", 1, 25, second) && Boolean(body, "resolve_dks", resolve); break;
            case Mutation::RtOff:
                valid = ExactFields(body, {"logical_id"}) && LogicalKey(body, "logical_id", key); break;
            case Mutation::Deadzone:
                valid = ExactFields(body, {"logical_id", "top_mm", "bottom_mm"}) &&
                    LogicalKey(body, "logical_id", key) && Millimeters(body, "top_mm", 0, 5, first) &&
                    Millimeters(body, "bottom_mm", 0, 5, second); break;
            case Mutation::ResetAllDeadzone:
                valid = ExactFields(body, {"confirm_all_keys"}) &&
                    Boolean(body, "confirm_all_keys", flag) && flag; break;
            case Mutation::Dks:
                valid = DksConfiguration(body, dks);
                if (valid) { key = dks.source_logical_key_id; resolve = body.at("resolve_rt").get<bool>(); }
                break;
            case Mutation::DksStandard:
                valid = ExactFields(body, {"logical_id"}) && LogicalKey(body, "logical_id", key); break;
            case Mutation::SpeedTapPairOn:
            case Mutation::SpeedTapPairOff:
                valid = ExactFields(body, {"key1", "key2"}) && LogicalKey(body, "key1", key) &&
                    LogicalKey(body, "key2", other) && key != other; break;
            case Mutation::SpeedTapMaster:
            case Mutation::StaticAnalog:
                valid = ExactFields(body, {"enabled"}) && Boolean(body, "enabled", flag); break;
            case Mutation::SpeedTapProfileReset:
                valid = ExactFields(body, {"confirm_profile_baseline"}) &&
                    Boolean(body, "confirm_profile_baseline", flag) && flag; break;
            case Mutation::GlobalActuation:
                valid = ExactFields(body, {"mm"}) && Millimeters(body, "mm", 1, 40, first); break;
            case Mutation::GlobalDeadzone:
                valid = ExactFields(body, {"top_mm", "bottom_mm"}) &&
                    Millimeters(body, "top_mm", 0, 5, first) &&
                    Millimeters(body, "bottom_mm", 0, 5, second); break;
            case Mutation::GlobalRapidTrigger: break; // Rejected before parsing.

            }
            if (!valid) { WriteStatus(response, false, 422, "Invalid or unsupported magnetic setting input"); return; }
            std::unique_lock<std::mutex> write_lock(write_mutex_, std::try_to_lock);
            if (!write_lock.owns_lock()) { WriteStatus(response, false, 409, "Another magnetic setting is applying"); return; }
            if (!HardwareAvailable()) {
                WriteStatus(response, false, 409, "DeviceDisconnected: connected native_hid device required"); return;
            }
            if (runtime_->GetHealth() != M605RuntimeHealth::Clean ||
                runtime_->IsPersistentSafetyQuarantined()) {
                WriteStatus(response, false, 409, "SafetyQuarantined: magnetic runtime is unhealthy or isolated"); return;
            }
            auto hall_pause = hall_telemetry_ ?
                std::optional<m605::hall::M605HallTelemetry::ScopedPause>(hall_telemetry_->PauseForTransaction()) :
                std::nullopt;
            if (!runtime_->PrepareTransportSession()) {
                const auto firmware = runtime_->GetDiagnosticSnapshot().firmware;
                WriteStatus(response, false, firmware.state == m605::FirmwareState::ReadFailed ? 503 : 409);
                return; // no conflict resolution, baseline persistence, latch or stage
            }
            const auto host = host_profile_();
            const auto shadow = runtime_->GetAppliedRuntimeState();
            const auto rt = shadow.per_key_rapid_trigger.find(key);
            const bool rt_known = rt != shadow.per_key_rapid_trigger.end() || host.per_key_rt_list_known;
            const bool rt_active = rt != shadow.per_key_rapid_trigger.end() ? rt->second.enabled :
                host.per_key_rt_enabled.count(key) != 0;
            const auto dks_known = shadow.per_key_dks.find(key);
            const bool dks_active = dks_known != shadow.per_key_dks.end() &&
                !dks_known->second.standard_runtime_configuration;
            if (kind == Mutation::RtOff && (!host.global_rt_press_raw || !host.global_rt_release_raw)) {
                WriteStatus(response, false, 409, "Trusted global RT Press/Release values are unavailable"); return;
            }
            if (kind == Mutation::ResetAllDeadzone &&
                (!host.global_deadzone_bottom_raw || !host.global_deadzone_top_raw)) {
                WriteStatus(response, false, 409, "Trusted global Deadzone Bottom/Top values are unavailable"); return;
            }
            if (kind == Mutation::RtOn && (dks_active || dks_known == shadow.per_key_dks.end()) && !resolve) {
                WriteStatus(response, false, 409, "DKS state is active or unknown; explicit confirmation is required"); return;
            }
            if (kind == Mutation::Dks && (rt_active || !rt_known) && !resolve) {
                WriteStatus(response, false, 409, "Per-key RT state is active or unknown; explicit confirmation is required"); return;
            }
            if (kind == Mutation::Dks && (rt_active || !rt_known) &&
                (!host.global_rt_press_raw || !host.global_rt_release_raw)) {
                WriteStatus(response, false, 409, "Cannot resolve RT/DKS conflict without trusted inherited RT values"); return;
            }
            if (kind == Mutation::SpeedTapPairOn) {
                if (!host.saved_speedtap_pairs_known) {
                    WriteStatus(response, false, 409,
                        "Saved SpeedTap pair baseline is unknown from HostProfile; enabling a new pair requires a trustworthy pair baseline to prevent key conflicts");
                    return;
                }
                const auto overlaps = [key, other](const std::pair<uint16_t, uint16_t>& pair) {
                    return pair != std::make_pair(key, other) &&
                        (pair.first == key || pair.second == key ||
                         pair.first == other || pair.second == other);
                };
                for (const auto& [pair, enabled] : shadow.speedtap_pair_submissions) {
                    if (enabled && overlaps(pair)) {
                        WriteStatus(response, false, 409,
                            "A key is already in an AceHFXAura-managed SpeedTap pair"); return;
                    }
                }
                for (const auto& [pair, enabled] : host.saved_speedtap_pairs) {
                    const auto submitted = shadow.speedtap_pair_submissions.find(pair);
                    if (enabled && overlaps(pair) &&
                        (submitted == shadow.speedtap_pair_submissions.end() || submitted->second)) {
                        WriteStatus(response, false, 409,
                            "A key is already in a saved HostProfile SpeedTap pair"); return;
                    }
                }
            }
            // These are separate verified runtime transactions, never one
            // fictional device-atomic operation. A failed second step leaves
            // the first success visible in SessionApplied shadow.
            if (profiles_) profiles_->ExternalMutationLocked();
            if (kind == Mutation::RtOn && (dks_active || dks_known == shadow.per_key_dks.end()) &&
                !runtime_->RestorePerKeyDksToStandard(key).get()) {
                WriteStatus(response, false, 503, "DKS standard rewrite failed; Rapid Trigger was not submitted"); return;
            }
            if (kind == Mutation::Dks && (rt_active || !rt_known) &&
                !runtime_->DisablePerKeyRapidTrigger(key, *host.global_rt_press_raw / 10.0,
                                                     *host.global_rt_release_raw / 10.0).get()) {
                WriteStatus(response, false, 503, "RT disable failed; DKS was not submitted"); return;
            }
            std::future<bool> future;
            switch (kind) {
            case Mutation::Actuation: future = runtime_->SetPerKeyActuation(key, first); break;
            case Mutation::RtOn: future = runtime_->SetPerKeyRapidTrigger(key, first, second); break;
            case Mutation::RtOff: future = runtime_->DisablePerKeyRapidTrigger(
                key, *host.global_rt_press_raw / 10.0, *host.global_rt_release_raw / 10.0); break;
            case Mutation::Deadzone: future = runtime_->SetPerKeyDeadzone(key, first, second); break;
            case Mutation::ResetAllDeadzone: future = runtime_->ResetAllPerKeyDeadzoneOverrides(
                *host.global_deadzone_bottom_raw, *host.global_deadzone_top_raw); break;
            case Mutation::Dks: future = runtime_->SetPerKeyDks(dks); break;
            case Mutation::DksStandard: future = runtime_->RestorePerKeyDksToStandard(key); break;
            case Mutation::SpeedTapPairOn: future = runtime_->SetSpeedTapPair(key, other); break;
            case Mutation::SpeedTapPairOff: future = runtime_->DisableSpeedTapPair(key, other); break;
            case Mutation::SpeedTapMaster: future = runtime_->SetSpeedTapMaster(flag); break;
            case Mutation::SpeedTapProfileReset: future = runtime_->ResetSpeedTapRuntimeToProfile(); break;
            case Mutation::StaticAnalog: future = runtime_->SetAnalogEffect(0, flag); break;
            case Mutation::GlobalActuation: future = runtime_->SetGlobalActuation(first); break;
            case Mutation::GlobalDeadzone: future = runtime_->SetGlobalDeadzone(first, second); break;
            case Mutation::GlobalRapidTrigger:
                WriteStatus(response, false, 422, "Legacy RT writer blocked"); return;
            }
            const bool success = future.get(); // HTTP worker; UI awaits network asynchronously.
            const auto health = runtime_->GetHealth();
            if (success && health == M605RuntimeHealth::Clean && profiles_) {
                try {
                    if (kind == Mutation::GlobalActuation)
                        profiles_->RecordManualGlobalBaselineLocked("global_actuation_mm", first);
                    else if (kind == Mutation::GlobalDeadzone)
                        profiles_->RecordManualGlobalBaselineLocked("global_deadzone",
                            {{"top_mm", first}, {"bottom_mm", second}});

                } catch (const std::exception& ex) {
                    WriteStatus(response, false, 503,
                        std::string("Device accepted the global setting, but its desired baseline was not saved: ") + ex.what());
                    return;
                }
            }
            const char* partial = kind == Mutation::RtOn &&
                (dks_active || dks_known == shadow.per_key_dks.end()) ?
                "DKS standard rewrite completed before RT failure; current device state is uncertain" :
                kind == Mutation::Dks && (rt_active || !rt_known) ?
                "RT disable completed before DKS failure; current device state is uncertain" : "";
            const auto firmware = runtime_->GetDiagnosticSnapshot().firmware;
            const bool firmware_rejected = firmware.session_generation == runtime_->GetSessionGeneration() &&
                (firmware.state == m605::FirmwareState::Unknown || firmware.state == m605::FirmwareState::Unsupported);
            WriteStatus(response, success, success ? 200 :
                health == M605RuntimeHealth::IndeterminateStagedState || firmware_rejected ? 409 : 503,
                !success && *partial ? partial : std::string_view{});
        });
    };
    write("/api/magnetic/actuation", Mutation::Actuation);
    write("/api/magnetic/rapid-trigger", Mutation::RtOn);
    write("/api/magnetic/rapid-trigger/disable", Mutation::RtOff);
    write("/api/magnetic/deadzone", Mutation::Deadzone);
    write("/api/magnetic/deadzone/reset-all", Mutation::ResetAllDeadzone);
    write("/api/magnetic/dks", Mutation::Dks);
    write("/api/magnetic/dks/standard", Mutation::DksStandard);
    write("/api/magnetic/speedtap/pair", Mutation::SpeedTapPairOn);
    write("/api/magnetic/speedtap/pair/disable", Mutation::SpeedTapPairOff);
    write("/api/magnetic/speedtap/master", Mutation::SpeedTapMaster);
    write("/api/magnetic/speedtap/profile-reset", Mutation::SpeedTapProfileReset);
    write("/api/magnetic/analog-effect/static", Mutation::StaticAnalog);
    write("/api/magnetic/global/actuation", Mutation::GlobalActuation);
    write("/api/magnetic/global/deadzone", Mutation::GlobalDeadzone);
    write("/api/magnetic/global/rapid-trigger", Mutation::GlobalRapidTrigger);

    const auto batch_write = [this, &server](const char* route, Mutation family) {
        server.Post(route, [this, family](const httplib::Request& request, httplib::Response& response) {
            if (!ValidRequest(request, response)) return;
            const Json body = Json::parse(request.body, nullptr, false);
            std::vector<uint16_t> keys;
            double first = 0, second = 0;
            bool resolve_dks = false;
            bool enable_rt = false;
            bool valid = BatchLogicalKeys(body, keys);
            if (valid && family == Mutation::Actuation)
                valid = ExactFields(body, {"logical_ids", "millimeters"}) &&
                    Millimeters(body, "millimeters", 1, 40, first);
            else if (valid && family == Mutation::Deadzone)
                valid = ExactFields(body, {"logical_ids", "top_mm", "bottom_mm"}) &&
                    Millimeters(body, "top_mm", 0, 5, first) &&
                    Millimeters(body, "bottom_mm", 0, 5, second);
            else if (valid && family == Mutation::RtOn) {
                valid = body.contains("action") && body.at("action").is_string();
                if (valid) {
                    const auto action = body.at("action").get<std::string>();
                    enable_rt = action == "enable";
                    valid = enable_rt ?
                        ExactFields(body, {"logical_ids", "action", "press_mm", "release_mm", "resolve_dks"}) &&
                            Millimeters(body, "press_mm", 1, 25, first) &&
                            Millimeters(body, "release_mm", 1, 25, second) &&
                            Boolean(body, "resolve_dks", resolve_dks) :
                        action == "disable" && ExactFields(body, {"logical_ids", "action"});
                }
            }
            if (!valid) {
                WriteStatus(response, false, 422, "Invalid batch shape, key set, or setting value"); return;
            }

            Json results = Json::array();
            for (uint16_t key : keys)
                results.push_back({{"logical_id", key}, {"status", "NotExecuted"}});
            const auto respond = [&](bool success, int code, std::string_view detail,
                                     const Json& configured, const Json& unknown) {
                WriteStatus(response, success, code, detail);
                Json output = Json::parse(response.body);
                size_t applied = 0, failed = 0;
                for (const auto& item : results) {
                    applied += item.at("status") == "Applied";
                    failed += item.at("status") == "Failed";
                }
                output["batch_result"] = {{"requested_count", keys.size()}, {"applied_count", applied},
                    {"failed_count", failed}, {"not_executed_count", keys.size() - applied - failed},
                    {"completed_fully", success && applied == keys.size()},
                    {"health", HealthName(runtime_->GetHealth())}, {"results", results},
                    {"configured_dks_keys", configured}, {"unknown_dks_keys", unknown}};
                response.set_content(output.dump(), "application/json; charset=utf-8");
            };
            const Json empty = Json::array();
            std::unique_lock<std::mutex> write_lock(write_mutex_, std::try_to_lock);
            if (!write_lock.owns_lock() || runtime_->HasQueuedWork() ||
                runtime_->GetHealth() == M605RuntimeHealth::TransactionInProgress) {
                respond(false, 409, "Another magnetic setting is applying", empty, empty); return;
            }
            if (!HardwareAvailable() || runtime_->GetHealth() != M605RuntimeHealth::Clean ||
                runtime_->IsPersistentSafetyQuarantined()) {
                respond(false, 409, "Magnetic runtime unavailable or safety quarantined", empty, empty); return;
            }
            auto hall_pause = hall_telemetry_ ?
                std::optional<m605::hall::M605HallTelemetry::ScopedPause>(hall_telemetry_->PauseForTransaction()) :
                std::nullopt;
            if (!runtime_->PrepareTransportSession()) {
                const auto firmware = runtime_->GetDiagnosticSnapshot().firmware;
                respond(false, firmware.state == m605::FirmwareState::ReadFailed ? 503 : 409,
                    runtime_->GetLastError(), empty, empty);
                return;
            }

            const auto host = host_profile_();
            const auto shadow = runtime_->GetAppliedRuntimeState();
            Json configured = Json::array(), unknown = Json::array();
            std::set<uint16_t> needs_restore;
            std::optional<std::pair<double, double>> inherited_rt;
            if (host.global_rt_press_raw && host.global_rt_release_raw)
                inherited_rt = {{*host.global_rt_press_raw / 10.0,
                    *host.global_rt_release_raw / 10.0}};
            if (family == Mutation::RtOn && !enable_rt && !inherited_rt) {
                respond(false, 409, "Trusted global RT Press/Release values are unavailable", empty, empty); return;
            }
            // Build the exact existing per-key reports for every key before the first write.
            for (uint16_t key : keys) {
                bool accepted = false;
                if (family == Mutation::Actuation)
                    accepted = m605::BuildPerKeyActuation(key, first).has_value();
                else if (family == Mutation::Deadzone)
                    accepted = m605::BuildPerKeyDeadzone(key, first, second).has_value();
                else if (enable_rt) {
                    accepted = m605::BuildPerKeyRapidTriggerStages(key, first, second, true).has_value();
                    const auto dks = shadow.per_key_dks.find(key);
                    if (dks == shadow.per_key_dks.end()) {
                        unknown.push_back(key); needs_restore.insert(key);
                    } else if (!dks->second.standard_runtime_configuration) {
                        configured.push_back(key); needs_restore.insert(key);
                    }
                    if (accepted && needs_restore.find(key) != needs_restore.end())
                        accepted = m605::BuildPerKeyDksStages(m605::StandardDksConfiguration(key)).has_value();
                } else
                    accepted = m605::BuildPerKeyRapidTriggerStages(key,
                        inherited_rt->first, inherited_rt->second,
                        false).has_value();
                if (!accepted) {
                    respond(false, 422, "Existing per-key builder rejected batch input", configured, unknown); return;
                }
            }
            if (enable_rt && !resolve_dks && !needs_restore.empty()) {
                respond(false, 409, "DKS is configured or unknown for selected keys; explicit confirmation is required",
                    configured, unknown); return;
            }

            if (profiles_) profiles_->ExternalMutationLocked();

            size_t applied = 0;
            for (size_t i = 0; i < keys.size(); ++i) {
                const uint16_t key = keys[i];
                if (runtime_->GetHealth() != M605RuntimeHealth::Clean ||
                    runtime_->IsPersistentSafetyQuarantined()) {
                    results[i]["status"] = "Failed";
                    results[i]["detail"] = "Runtime safety stop before this key was submitted";
                    break;
                }
                bool restored = false;
                std::string detail;
                bool success = false;
                try {
                    if (enable_rt && needs_restore.find(key) != needs_restore.end()) {
                        if (!runtime_->RestorePerKeyDksToStandard(key).get())
                            detail = "DKS standard rewrite failed; RT was not submitted";
                        else restored = true;
                    }
                    if (detail.empty()) {
                        if (family == Mutation::Actuation) success = runtime_->SetPerKeyActuation(key, first).get();
                        else if (family == Mutation::Deadzone) success = runtime_->SetPerKeyDeadzone(key, first, second).get();
                        else if (enable_rt) success = runtime_->SetPerKeyRapidTrigger(key, first, second).get();
                        else success = runtime_->DisablePerKeyRapidTrigger(key,
                            inherited_rt->first, inherited_rt->second).get();
                    }
                } catch (const std::exception& ex) { detail = ex.what(); }
                if (success) {
                    results[i]["status"] = "Applied";
                    ++applied;
                    continue;
                }
                results[i]["status"] = "Failed";
                if (restored && enable_rt)
                    detail = "DKS restored before RT submission failed; this key may now be Standard";
                else if (detail.empty()) detail = runtime_->GetLastError();
                results[i]["detail"] = detail;
                break; // No rollback and no subsequent key mutation.
            }
            const bool complete = applied == keys.size();
            respond(complete, complete ? 200 : applied ? 207 :
                runtime_->GetHealth() == M605RuntimeHealth::Clean ? 503 : 409,
                complete ? "" : "Batch stopped at the first failed or unsafe key", configured, unknown);
        });
    };
    batch_write("/api/magnetic/batch/actuation", Mutation::Actuation);
    batch_write("/api/magnetic/batch/rapid-trigger", Mutation::RtOn);
    batch_write("/api/magnetic/batch/deadzone", Mutation::Deadzone);

    server.Get("/api/magnetic/hall/snapshot", [this](const httplib::Request& request, httplib::Response& response) {
        if (!IsAllowedLoopbackHost(request.get_header_value("Host"))) { response.status = 403; return; }
        const Json body = hall_telemetry_ ? HallSnapshotJson(hall_telemetry_->GetSnapshot()) :
            Json{{"status", "error"}, {"api_version", 1}, {"last_error", "Hall telemetry unavailable"}};
        response.status = 200;
        response.set_header("Cache-Control", "no-store");
        response.set_content(body.dump(), "application/json; charset=utf-8");
    });

    server.Post("/api/magnetic/hall/start", [this](const httplib::Request& request, httplib::Response& response) {
        if (!ValidRequest(request, response)) return;
        const Json body = Json::parse(request.body, nullptr, false);
        bool unlock_fa = false;
        if (!body.is_object() || body.size() > 1 ||
            (body.size() == 1 && !Boolean(body, "unlock_fa", unlock_fa))) {
            response.status = 422;
            response.set_content(R"({"status":"error","api_version":1,"last_error":"Invalid unlock_fa parameter"})",
                                 "application/json; charset=utf-8");
            return;
        }
        if (!HardwareAvailable()) {
            response.status = 409;
            response.set_content(R"({"status":"error","api_version":1,"last_error":"DeviceDisconnected: connected native_hid device required"})",
                                 "application/json; charset=utf-8");
            return;
        }
        const bool ok = hall_telemetry_ && hall_telemetry_->Start(unlock_fa);
        Json out = hall_telemetry_ ? HallSnapshotJson(hall_telemetry_->GetSnapshot()) :
            Json{{"status", "error"}, {"api_version", 1}, {"last_error", "Hall telemetry unavailable"}};
        if (!ok) out["status"] = "error";
        response.status = ok ? 200 : 503;
        response.set_header("Cache-Control", "no-store");
        response.set_content(out.dump(), "application/json; charset=utf-8");
    });

    server.Post("/api/magnetic/hall/stop", [this](const httplib::Request& request, httplib::Response& response) {
        if (!ValidRequest(request, response)) return;
        const Json body = Json::parse(request.body, nullptr, false);
        if (!body.is_object() || !body.empty()) {
            response.status = 422;
            response.set_content(R"({"status":"error","api_version":1,"last_error":"Expected empty JSON object"})",
                                 "application/json; charset=utf-8");
            return;
        }
        if (hall_telemetry_) hall_telemetry_->Stop();
        Json out = hall_telemetry_ ? HallSnapshotJson(hall_telemetry_->GetSnapshot()) :
            Json{{"status", "ok"}, {"api_version", 1}, {"active", false}};
        response.status = 200;
        response.set_header("Cache-Control", "no-store");
        response.set_content(out.dump(), "application/json; charset=utf-8");
    });

    server.Get("/api/magnetic/hall/verify/status", [this](const httplib::Request& request, httplib::Response& response) {
        if (!IsAllowedLoopbackHost(request.get_header_value("Host"))) { response.status = 403; return; }
        Json out = hall_telemetry_ ? VerifyStatusJson(hall_telemetry_->GetVerifyStatus()) :
            Json{{"status", "error"}, {"api_version", 1}, {"state", "Failed"}, {"error", "Hall telemetry unavailable"}};
        response.status = 200;
        response.set_header("Cache-Control", "no-store");
        response.set_content(out.dump(), "application/json; charset=utf-8");
    });

    server.Post("/api/magnetic/hall/verify/stop", [this](const httplib::Request& request, httplib::Response& response) {
        if (!ValidRequest(request, response)) return;
        const Json body = Json::parse(request.body, nullptr, false);
        if (!body.is_object() || !body.empty()) {
            response.status = 422;
            response.set_content(R"({"status":"error","api_version":1,"last_error":"Expected empty JSON object"})",
                                 "application/json; charset=utf-8");
            return;
        }
        if (hall_telemetry_) hall_telemetry_->StopVerify();
        Json out = hall_telemetry_ ? VerifyStatusJson(hall_telemetry_->GetVerifyStatus()) :
            Json{{"status", "ok"}, {"api_version", 1}, {"state", "Idle"}};
        response.status = 200;
        response.set_header("Cache-Control", "no-store");
        response.set_content(out.dump(), "application/json; charset=utf-8");
    });

    server.Post("/api/magnetic/hall/verify/start", [this](const httplib::Request& request, httplib::Response& response) {
        if (!ValidRequest(request, response)) return;
        const Json body = Json::parse(request.body, nullptr, false);
        m605::hall::VerifyRequest req;
        bool valid = body.is_object() &&
            LogicalKey(body, "logical_id", req.logical_id) &&
            body.contains("mode") && body.at("mode").is_string();
        if (valid) {
            req.mode = body.at("mode").get<std::string>();
            valid = (req.mode == "actuation" || req.mode == "rt" || req.mode == "deadzone");
        }
        const std::set<std::string> allowed_fields = {
            "logical_id", "mode", "actuation_points", "rt_points",
            "dz_top_mm", "dz_bottom_mm", "dz_rt_mm", "restore_actuation_mm", "unlock_fa"
        };
        if (valid) {
            for (auto it = body.begin(); it != body.end(); ++it) {
                if (allowed_fields.find(it.key()) == allowed_fields.end()) {
                    valid = false;
                    break;
                }
            }
        }
        if (valid && body.contains("actuation_points")) {
            const auto& arr = body.at("actuation_points");
            valid = arr.is_array() && !arr.empty() && arr.size() <= 8;
            if (valid) {
                req.actuation_points.clear();
                for (const auto& v : arr) {
                    if (!v.is_number()) { valid = false; break; }
                    const double mm = v.get<double>();
                    const double raw = std::round(mm * 10.0);
                    if (!std::isfinite(mm) || raw < 1 || raw > 40 || std::abs(mm * 10.0 - raw) > 1e-4) {
                        valid = false; break;
                    }
                    req.actuation_points.push_back(raw / 10.0);
                }
            }
        }
        if (valid && body.contains("rt_points")) {
            const auto& arr = body.at("rt_points");
            valid = arr.is_array() && !arr.empty() && arr.size() <= 8;
            if (valid) {
                req.rt_points.clear();
                for (const auto& v : arr) {
                    if (!v.is_number()) { valid = false; break; }
                    const double mm = v.get<double>();
                    const double raw = std::round(mm * 10.0);
                    if (!std::isfinite(mm) || raw < 1 || raw > 25 || std::abs(mm * 10.0 - raw) > 1e-4) {
                        valid = false; break;
                    }
                    req.rt_points.push_back(raw / 10.0);
                }
            }
        }
        if (valid && body.contains("dz_top_mm")) valid = Millimeters(body, "dz_top_mm", 0, 5, req.dz_top_mm);
        if (valid && body.contains("dz_bottom_mm")) valid = Millimeters(body, "dz_bottom_mm", 0, 5, req.dz_bottom_mm);
        if (valid && body.contains("dz_rt_mm")) valid = Millimeters(body, "dz_rt_mm", 1, 25, req.dz_rt_mm);
        if (valid && body.contains("restore_actuation_mm")) valid = Millimeters(body, "restore_actuation_mm", 1, 40, req.restore_actuation_mm);
        if (valid && body.contains("unlock_fa")) valid = Boolean(body, "unlock_fa", req.unlock_fa);

        if (!valid) {
            response.status = 422;
            response.set_content(R"({"status":"error","api_version":1,"state":"Failed","error":"Invalid closed-loop verification parameters"})",
                                 "application/json; charset=utf-8");
            return;
        }
        if (!HardwareAvailable() || runtime_->GetHealth() != M605RuntimeHealth::Clean ||
            runtime_->IsPersistentSafetyQuarantined()) {
            response.status = 409;
            response.set_content(R"({"status":"error","api_version":1,"state":"Failed","error":"Magnetic runtime unavailable or safety quarantined"})",
                                 "application/json; charset=utf-8");
            return;
        }

        {
            std::unique_lock<std::mutex> write_lock(write_mutex_, std::try_to_lock);
            if (!write_lock.owns_lock() || runtime_->HasQueuedWork() ||
                runtime_->GetHealth() == M605RuntimeHealth::TransactionInProgress) {
                response.status = 409;
                response.set_content(R"({"status":"error","api_version":1,"state":"Failed","error":"Another magnetic setting is applying"})",
                                     "application/json; charset=utf-8");
                return;
            }
            auto hall_pause = hall_telemetry_ ?
                std::optional<m605::hall::M605HallTelemetry::ScopedPause>(hall_telemetry_->PauseForTransaction()) :
                std::nullopt;
            if (!runtime_->PrepareTransportSession()) {
                const auto firmware = runtime_->GetDiagnosticSnapshot().firmware;
                response.status = firmware.state == m605::FirmwareState::ReadFailed ? 503 : 409;
                Json out = {{"status", "error"}, {"api_version", 1}, {"state", "Failed"},
                            {"error", runtime_->GetLastError()}};
                response.set_content(out.dump(), "application/json; charset=utf-8");
                return;
            }
        }

        const auto host = host_profile_();
        if (host.global_rt_press_raw && host.global_rt_release_raw) {
            req.inherited_rt_press_mm = *host.global_rt_press_raw / 10.0;
            req.inherited_rt_release_mm = *host.global_rt_release_raw / 10.0;
        }

        auto check_writable = [this]() -> bool {
            return HardwareAvailable() && runtime_->GetHealth() == M605RuntimeHealth::Clean &&
                   !runtime_->IsPersistentSafetyQuarantined();
        };

        m605::hall::VerifyHardwareWriter writer;
        writer.set_actuation = [this, check_writable](uint16_t logical_id, double mm) {
            std::lock_guard<std::mutex> lock(write_mutex_);
            if (!check_writable()) return false;
            if (profiles_) profiles_->ExternalMutationLocked();
            return runtime_->SetPerKeyActuation(logical_id, mm).get();
        };
        writer.set_rt = [this, check_writable](uint16_t logical_id, double press_mm, double release_mm) {
            std::lock_guard<std::mutex> lock(write_mutex_);
            if (!check_writable()) return false;
            if (profiles_) profiles_->ExternalMutationLocked();
            const auto shadow = runtime_->GetAppliedRuntimeState();
            const auto dks = shadow.per_key_dks.find(logical_id);
            if (dks == shadow.per_key_dks.end() || !dks->second.standard_runtime_configuration) {
                if (!runtime_->RestorePerKeyDksToStandard(logical_id).get()) return false;
            }
            return runtime_->SetPerKeyRapidTrigger(logical_id, press_mm, release_mm).get();
        };
        writer.disable_rt = [this, check_writable](uint16_t logical_id, double p, double r) {
            std::lock_guard<std::mutex> lock(write_mutex_);
            if (!check_writable()) return false;
            if (profiles_) profiles_->ExternalMutationLocked();
            return runtime_->DisablePerKeyRapidTrigger(logical_id, p, r).get();
        };
        writer.set_deadzone = [this, check_writable](uint16_t logical_id, double top_mm, double bottom_mm) {
            std::lock_guard<std::mutex> lock(write_mutex_);
            if (!check_writable()) return false;
            if (profiles_) profiles_->ExternalMutationLocked();
            return runtime_->SetPerKeyDeadzone(logical_id, top_mm, bottom_mm).get();
        };

        const bool started = hall_telemetry_ && hall_telemetry_->StartVerify(req, std::move(writer));
        Json out = hall_telemetry_ ? VerifyStatusJson(hall_telemetry_->GetVerifyStatus()) :
            Json{{"status", "error"}, {"api_version", 1}, {"state", "Failed"}, {"error", "Hall telemetry unavailable"}};
        if (!started) out["status"] = "error";
        response.status = started ? 200 : 503;
        response.set_header("Cache-Control", "no-store");
        response.set_content(out.dump(), "application/json; charset=utf-8");
    });
}
} // namespace aura
