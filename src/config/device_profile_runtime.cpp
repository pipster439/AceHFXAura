#include "config/device_profile_runtime.h"
#include "config/config_writer_util.h"
#include "aura/hardware/m605_key_mapping.h"
#include "aura_version.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <regex>
#include <set>
#include <stdexcept>
#include <windows.h>
#include <objbase.h>

namespace aura {
namespace {
using Json = nlohmann::json;
Json HardwareGateJson(const HardwareRtGateObservation& gate) {
    return {{"state", HardwareRtGateName(gate.state)},
        {"source", "USB status notification; MI_02 Report ID 03 event 76"},
        {"observed_at_utc", gate.observed_at_utc.empty() ? Json(nullptr) : Json(gate.observed_at_utc)},
        {"observation_sequence", gate.observation_sequence},
        {"input_session_generation", gate.input_session_generation},
        {"input_collection_connected", gate.input_collection_connected},
        {"unavailable_reason", gate.unavailable_reason.empty() ? Json(nullptr) : Json(gate.unavailable_reason)}};
}
struct ProfileError : std::runtime_error {
    int code;
    ProfileError(int status, std::string message) : std::runtime_error(std::move(message)), code(status) {}
};
bool Tenth(const Json& value, int low, int high) {
    if (!value.is_number()) return false;
    const double mm = value.get<double>();
    const double raw = std::round(mm * 10);
    return std::isfinite(mm) && raw >= low && raw <= high && std::abs(mm * 10 - raw) < 1e-7;
}
uint8_t Raw(const Json& value) { return static_cast<uint8_t>(std::round(value.get<double>() * 10)); }
Json BaselineWithHost(const Json& defaults, const MagneticHostProfile& host) {
    Json baseline = defaults;
    if (baseline.at("global_actuation_mm").is_null() && host.global_actuation_raw)
        baseline["global_actuation_mm"] = *host.global_actuation_raw / 10.0;
    if (baseline.at("global_deadzone").is_null() &&
        host.global_deadzone_top_raw && host.global_deadzone_bottom_raw)
        baseline["global_deadzone"] = {{"top_mm", *host.global_deadzone_top_raw / 10.0},
            {"bottom_mm", *host.global_deadzone_bottom_raw / 10.0}};
    // HostProfile has no complete global RT enabled/top/bottom baseline.
    return baseline;
}
bool Fields(const Json& object, std::initializer_list<const char*> names) {
    if (!object.is_object()) return false;
    for (const char* name : names) if (!object.contains(name)) return false;
    return true;
}
std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw ProfileError(500, "Cannot open Profile document");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void WriteAtomic(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    const auto temp = std::filesystem::path(path.wstring() + L"." +
        std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64()) + L".tmp");
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw ProfileError(500, "Cannot create temporary Profile document");
    DWORD written = 0;
    const bool okay = content.size() <= MAXDWORD &&
        WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr) &&
        written == content.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!okay || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        throw ProfileError(500, "Atomic Profile document replacement failed");
    }
}
bool ValidName(const Json& value) {
    return value.is_string() && !value.get_ref<const std::string&>().empty() &&
        value.get_ref<const std::string&>().size() <= 100 &&
        value.get_ref<const std::string&>().find_first_not_of(" \t\r\n") != std::string::npos;
}
std::string CanonicalGuid(std::string id) {
    std::transform(id.begin(), id.end(), id.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return id;
}
bool SameGuid(const Json& value, const std::string& id) {
    return value.is_string() && CanonicalGuid(value.get<std::string>()) == CanonicalGuid(id);
}
bool ValidKey(const Json& value) {
    return value.is_number_integer() && value.get<int64_t>() > 0 && value.get<int64_t>() <= 65535;
}
bool ValidTrigger(const Json& value) {
    return value.is_string() && (value == "Inactive" || value == "Tap" || value == "Release" || value == "Hold");
}
m605::DksTriggerState Trigger(const Json& value) {
    if (value == "Tap") return m605::DksTriggerState::Tap;
    if (value == "Release") return m605::DksTriggerState::Release;
    if (value == "Hold") return m605::DksTriggerState::Hold;
    return m605::DksTriggerState::Inactive;
}
bool ValidDeadzone(const Json& value) {
    return Fields(value, {"top_mm", "bottom_mm"}) &&
        Tenth(value.at("top_mm"), 0, 5) && Tenth(value.at("bottom_mm"), 0, 5);
}
bool ValidRt(const Json& value, bool global) {
    if (!Fields(value, {"enabled", "press_mm", "release_mm", "separate_mode"}) ||
        !value.at("enabled").is_boolean() || !value.at("separate_mode").is_boolean() ||
        !Tenth(value.at("press_mm"), 1, 25) || !Tenth(value.at("release_mm"), 1, 25)) return false;
    // Retain older saved continuous intent; capability preflight, not document
    // loading, rejects ON. Unsupported settings must not erase user data.
    if (!global) return !value.contains("continuous") || value.at("continuous").is_boolean();
    return value.at("enabled") == true &&
        (value.at("separate_mode") == true || value.at("press_mm") == value.at("release_mm")) &&
        value.contains("top_mm") && value.contains("bottom_mm") &&
        Tenth(value.at("top_mm"), 0, 5) && Tenth(value.at("bottom_mm"), 0, 5);
}
bool ValidDks(const Json& value) {
    if (!Fields(value, {"start_mm", "end_mm", "slots", "standard"}) ||
        !Tenth(value.at("start_mm"), 1, 40) || !Tenth(value.at("end_mm"), 1, 40) ||
        value.at("start_mm").get<double>() > value.at("end_mm").get<double>() ||
        !value.at("standard").is_boolean() || !value.at("slots").is_array() ||
        value.at("slots").size() != 4) return false;
    for (const auto& slot : value.at("slots")) {
        if (!Fields(slot, {"target", "down_start", "down_end", "up_start", "up_end"}) ||
            !slot.at("target").is_object() || !slot.at("target").contains("kind") ||
            !ValidTrigger(slot.at("down_start")) || !ValidTrigger(slot.at("down_end")) ||
            !ValidTrigger(slot.at("up_start")) || !ValidTrigger(slot.at("up_end"))) return false;
        const auto& target = slot.at("target");
        if (target.at("kind") == "DefaultSentinel") {
            if (target.contains("logical_id") && !target.at("logical_id").is_null()) return false;
        } else if (target.at("kind") == "LogicalKey") {
            if (!target.contains("logical_id") || !ValidKey(target.at("logical_id")) ||
                target.at("logical_id") == 0x0508) return false;
        } else return false;
    }
    return true;
}
bool ValidRequest(const httplib::Request& request, httplib::Response& response) {
    if (!IsAllowedLoopbackHost(request.get_header_value("Host")) ||
        !ValidateLoopbackOriginAndReferer(request.get_header_value("Origin"),
                                          request.get_header_value("Referer"))) response.status = 403;
    else if (!IsJsonContentType(request.get_header_value("Content-Type"))) response.status = 415;
    else if (request.body.size() > 1024 * 1024) response.status = 413;
    else return true;
    response.set_content(R"({"status":"error","message":"Invalid Profile request"})", "application/json");
    return false;
}
void PreserveUnknown(Json& next, const Json& old, std::initializer_list<const char*> known) {
    if (!next.is_object() || !old.is_object()) return;
    std::set<std::string> fields(known.begin(), known.end());
    for (auto it = old.begin(); it != old.end(); ++it)
        if (!fields.count(it.key()) && !next.contains(it.key())) next[it.key()] = it.value();
}
void PreserveMagneticExtensions(Json& magnetic, const Json& prior) {
    PreserveUnknown(magnetic, prior,
        {"global_actuation_mm", "global_deadzone", "global_rapid_trigger", "keys"});
    if (!magnetic.contains("keys") || !magnetic.at("keys").is_array() ||
        !prior.contains("keys") || !prior.at("keys").is_array()) return;
    for (auto& key : magnetic["keys"]) {
        if (!key.is_object() || !key.contains("logical_id")) continue;
        for (const auto& old_key : prior.at("keys")) {
            if (!old_key.is_object() || old_key.value("logical_id", -1) != key.at("logical_id")) continue;
            PreserveUnknown(key, old_key,
                {"logical_id", "actuation_mm", "deadzone", "rapid_trigger", "dks"});
            break;
        }
    }
}
void PreserveProfileExtensions(Json& next, const Json& old) {
    PreserveUnknown(next, old, {"schema_version", "id", "name", "magnetic", "lighting", "automation"});
    if (next.contains("lighting") && old.contains("lighting"))
        PreserveUnknown(next["lighting"], old.at("lighting"), {"legacy_effect_reference", "ownership"});
    if (next.contains("magnetic") && old.contains("magnetic"))
        PreserveMagneticExtensions(next["magnetic"], old.at("magnetic"));
}

Json LegacyRt(const Json& magnetic) {
    return magnetic.is_object() ? magnetic.value("global_rapid_trigger", Json(nullptr)) : Json(nullptr);
}
void RejectNewLegacyRt(const Json& next, const Json& prior) {
    const auto reject = [](const Json& magnetic, const Json& old) {
        const auto value = LegacyRt(magnetic);
        if (!value.is_null() && value != LegacyRt(old))
            throw ProfileError(422, "New legacy global RT settings are prohibited; use explicit per-key RT");
    };
    reject(next.at("global_defaults"), prior.at("global_defaults"));
    for (const auto& profile : next.at("profiles")) {
        Json old = nullptr;
        for (const auto& p : prior.at("profiles"))
            if (SameGuid(p.at("id"), profile.at("id").get<std::string>())) { old = p.at("magnetic"); break; }
        reject(profile.at("magnetic"), old);
    }
}
// Narrow v1 host-intent migration, not firmware inheritance or a 51 53 model.
// Non-neutral obsolete top/bottom and ambiguous disabled/master intent stay opaque.
Json BuildLegacyRtMigration(Json& next) {
    Json entries = Json::array(), archived = Json::array();
    std::set<uint16_t> explicit_root;
    for (const auto& key : next.at("global_defaults").at("keys"))
        if (!key.value("rapid_trigger", Json(nullptr)).is_null())
            explicit_root.insert(key.at("logical_id").get<uint16_t>());
    const auto migrate = [&](Json& magnetic, Json profile_id) {
        const auto value = LegacyRt(magnetic);
        if (value.is_null()) return;
        Json entry = {{"profile_id", profile_id}, {"status", "MigrationRequired"},
            {"generated_key_count", 0}, {"reason", "Ambiguous or unsupported legacy RT retained"}};
        bool empty = value.is_object();
        if (empty) for (const auto& item : value.items()) if (!item.value().is_null()) empty = false;
        const bool convertible = ValidRt(value, true) && value.at("top_mm") == 0 && value.at("bottom_mm") == 0 &&
            (!value.contains("continuous") || value.at("continuous") == false);
        if (!empty && !convertible) { entries.push_back(entry); return; }
        // Do not overwrite an unknown extension owned by another schema/version.
        if (next.contains("legacy_rt_migration_v1") &&
            (!Fields(next.at("legacy_rt_migration_v1"), {"contract", "archived"}) ||
             next.at("legacy_rt_migration_v1").at("contract") != "NeutralV1AllAuditedKeys" ||
             !next.at("legacy_rt_migration_v1").at("archived").is_array())) {
            entry["reason"] = "Migration archive extension is unsupported; retained";
            entries.push_back(entry); return;
        }
        auto keys = magnetic.at("keys");
        size_t generated = 0;
        if (convertible) {
            for (const auto& physical : m605::detail::kVerifiedPhysicalKeys) {
                const auto id = physical.logical_id;
                // Pre-existing explicit root state outranks legacy Profile defaults.
                if (!profile_id.is_null() && explicit_root.count(id)) continue;
                auto key = std::find_if(keys.begin(), keys.end(), [&](const Json& k) { return k.at("logical_id") == id; });
                if (key != keys.end() && !key->value("rapid_trigger", Json(nullptr)).is_null()) continue;
                if (key != keys.end() && key->value("dks", Json(nullptr)).is_object() && key->at("dks").at("standard") == false) {
                    entry["reason"] = "Legacy all-key RT would conflict with existing DKS; retained";
                    entries.push_back(entry); return;
                }
                if (key == keys.end()) { keys.push_back({{"logical_id", id}}); key = keys.end() - 1; }
                (*key)["rapid_trigger"] = {{"enabled", true}, {"press_mm", value.at("press_mm")},
                    {"release_mm", value.at("release_mm")}, {"separate_mode", value.at("separate_mode")}, {"continuous", false}};
                ++generated;
            }
            std::sort(keys.begin(), keys.end(), [](const Json& a, const Json& b) { return a.at("logical_id") < b.at("logical_id"); });
        }
        archived.push_back({{"profile_id", profile_id}, {"original", value},
            {"source_revision", next.at("revision")}, {"generated_key_count", generated}});
        magnetic["keys"] = std::move(keys);
        magnetic.erase("global_rapid_trigger");
        entry["status"] = empty ? "RemovedEmpty" : "ConvertedExplicitPerKey";
        entry["generated_key_count"] = generated;
        entry["reason"] = empty ? "No configured legacy intent" : "Neutral v1 all-audited-key host intent; explicit states preserved";
        entries.push_back(entry);
    };
    migrate(next["global_defaults"], nullptr);
    for (auto& p : next["profiles"]) migrate(p["magnetic"], p.at("id"));
    if (!archived.empty()) {
        if (!next.contains("legacy_rt_migration_v1"))
            next["legacy_rt_migration_v1"] = {{"contract", "NeutralV1AllAuditedKeys"}, {"archived", Json::array()}};
        for (auto& item : archived) next["legacy_rt_migration_v1"]["archived"].push_back(std::move(item));
    }
    const bool pending = std::any_of(entries.begin(), entries.end(), [](const Json& e) { return e.at("status") == "MigrationRequired"; });
    return {{"schema_version", 1}, {"contract", "NeutralV1AllAuditedKeys"},
        {"status", archived.empty() ? (entries.empty() ? "NotRequired" : "MigrationRequired") :
            pending ? "CompletedWithBlockedLegacy" : "Completed"},
        {"changed", !archived.empty()}, {"hardware_operations", 0}, {"entries", entries}};
}
} // namespace

bool DeviceProfileRuntime::ValidGuid(const std::string& text) {
    static const std::regex guid("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
    return std::regex_match(text, guid);
}
std::string DeviceProfileRuntime::NewGuid() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) throw ProfileError(500, "GUID creation failed");
    char out[37]{};
    std::snprintf(out, sizeof(out), "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2],
        guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return out;
}
void DeviceProfileRuntime::ValidateMagnetic(const Json& magnetic) {
    if (!magnetic.is_object() || !magnetic.contains("keys") || !magnetic.at("keys").is_array())
        throw ProfileError(422, "Missing magnetic settings");
    if (magnetic.contains("global_actuation_mm") && !magnetic.at("global_actuation_mm").is_null() &&
        !Tenth(magnetic.at("global_actuation_mm"), 1, 40)) throw ProfileError(422, "Invalid global actuation");
    if (magnetic.contains("global_deadzone") && !magnetic.at("global_deadzone").is_null() &&
        !ValidDeadzone(magnetic.at("global_deadzone"))) throw ProfileError(422, "Invalid global deadzone");
    // Legacy data is opaque on load. Migration handles recognized intent;
    // malformed/newer legacy content remains preserved and blocked by BuildPlan.
    std::set<uint16_t> keys;
    for (const auto& key : magnetic.at("keys")) {
        if (!key.is_object() || !key.contains("logical_id") || !ValidKey(key.at("logical_id")) ||
            !keys.insert(key.at("logical_id").get<uint16_t>()).second)
            throw ProfileError(422, "Invalid or duplicate logical key");
        if (key.contains("actuation_mm") && !key.at("actuation_mm").is_null() &&
            !Tenth(key.at("actuation_mm"), 1, 40)) throw ProfileError(422, "Invalid key actuation");
        if (key.contains("deadzone") && !key.at("deadzone").is_null() &&
            !ValidDeadzone(key.at("deadzone"))) throw ProfileError(422, "Invalid key deadzone");
        if (key.contains("rapid_trigger") && !key.at("rapid_trigger").is_null() &&
            !ValidRt(key.at("rapid_trigger"), false)) throw ProfileError(422, "Invalid key RT");
        if (key.contains("dks") && !key.at("dks").is_null()) {
            if (!ValidDks(key.at("dks"))) throw ProfileError(422, "Invalid DKS");
            if (!key.at("dks").at("standard").get<bool>() && key.contains("rapid_trigger") &&
                key.at("rapid_trigger").is_object() && key.at("rapid_trigger").at("enabled") == true)
                throw ProfileError(422, "DKS and RT conflict");
        }
    }
}
void DeviceProfileRuntime::Validate(const Json& doc) {
    if (!Fields(doc, {"revision", "selected_profile_id", "profiles", "global_defaults"}) ||
        !doc.at("revision").is_number_integer() || doc.at("revision").get<int64_t>() < 0 ||
        (doc.contains("schema_version") && doc.at("schema_version") != 1) ||
        !doc.at("selected_profile_id").is_string() ||
        !ValidGuid(doc.at("selected_profile_id").get<std::string>()) ||
        !doc.at("profiles").is_array() || doc.at("profiles").empty())
        throw ProfileError(422, "Invalid Profile document schema");
    ValidateMagnetic(doc.at("global_defaults"));
    // Automation is an additive, separately versioned subsystem. Opaque newer
    // or invalid subsections must not make core manual Profiles unusable.
    std::set<std::string> ids;
    for (const auto& profile : doc.at("profiles")) {
        if (!Fields(profile, {"schema_version", "id", "name", "magnetic", "lighting"}) ||
            profile.at("schema_version") != 1 || !profile.at("id").is_string() ||
            !ValidGuid(profile.at("id").get<std::string>()) || !ValidName(profile.at("name")) ||
            !profile.at("lighting").is_object() ||
            !profile.at("lighting").contains("ownership") ||
            !profile.at("lighting").at("ownership").is_string() ||
            profile.at("lighting").value("ownership", std::string{}) != "LegacyUnmanaged" ||
            !ids.insert(CanonicalGuid(profile.at("id").get<std::string>())).second)
            throw ProfileError(422, "Invalid Profile identity or lighting schema");
        ValidateMagnetic(profile.at("magnetic"));
    }
    if (!ids.count(CanonicalGuid(doc.at("selected_profile_id").get<std::string>())))
        throw ProfileError(422, "Selected Profile not found");
}

DeviceProfileRuntime::Json DeviceProfileRuntime::DefaultDocument(const std::filesystem::path& legacy) {
    Json reference = nullptr;
    if (!legacy.empty() && std::filesystem::exists(legacy)) {
        const auto old = Json::parse(Read(legacy));
        if (!old.is_object()) throw ProfileError(422, "Legacy config root is not an object");
        if (old.contains("default_profile") && old.at("default_profile").is_string())
            reference = old.at("default_profile");
        else if (old.contains("profiles") && old.at("profiles").is_object() &&
                 old.at("profiles").contains("desktop")) reference = "desktop";
    }
    const auto id = NewGuid();
    Json profile = {{"schema_version", 1}, {"id", id}, {"name", "Desktop"},
        {"magnetic", {{"global_actuation_mm", nullptr}, {"global_deadzone", nullptr},
            {"global_rapid_trigger", nullptr}, {"keys", Json::array()}}},
        {"lighting", {{"legacy_effect_reference", reference}, {"ownership", "LegacyUnmanaged"}}},
        {"automation", nullptr}};
    return {{"schema_version", 1}, {"revision", 0}, {"selected_profile_id", id},
        {"global_defaults", {{"global_actuation_mm", nullptr}, {"global_deadzone", nullptr},
            {"global_rapid_trigger", nullptr}, {"keys", Json::array()}}},
        {"profiles", Json::array({profile})}, {"device_profile_automation", DeviceProfileAutomationConfig::DefaultJson()}};
}

DeviceProfileRuntime::DeviceProfileRuntime(std::filesystem::path path,
    std::filesystem::path legacy_config, M605Runtime& device, std::mutex& mutation_gate,
    std::function<MagneticHostProfile()> host_profile, std::function<bool()> available,
    DeviceProfileBindingEngine::Clock decision_clock, DaemonProcessIdentity process_identity)
    : path_(std::move(path)), device_(device), mutation_gate_(mutation_gate),
      host_profile_(std::move(host_profile)), available_(std::move(available)), binding_engine_(std::move(decision_clock)),
      process_identity_(std::move(process_identity)) {
    session_generation_ = device_.GetSessionGeneration();
    try {
        NamedConfigLock lock;
        if (!lock.IsAcquired()) throw ProfileError(503, "Profile document lock timed out");
        if (std::filesystem::exists(path_)) {
            document_ = Json::parse(Read(path_));
            Validate(document_);
            if (!document_.contains("schema_version")) document_["schema_version"] = 1;
        } else {
            try { document_ = DefaultDocument(legacy_config); }
            catch (const std::exception& ex) {
                import_warning_ = std::string("Legacy lighting reference was not imported: ") + ex.what();
                document_ = DefaultDocument({});
            }
            Validate(document_);
            WriteAtomic(path_, document_.dump(2) + "\n");
        }
    } catch (const std::exception& ex) {
        load_error_ = std::string("Profile document retained without modification: ") + ex.what();
    }
    if (load_error_.empty()) MigrateLegacyRtLocked();
    if (load_error_.empty()) TrackDeclaredFootprintLocked();
    RefreshAutomationConfigurationLocked();
}
void DeviceProfileRuntime::RefreshAutomationConfigurationLocked() {
    std::set<std::string> ids;
    if (load_error_.empty()) for (const auto& profile : document_.at("profiles")) ids.insert(profile.at("id").get<std::string>());
    auto config = load_error_.empty() ? document_.value("device_profile_automation", DeviceProfileAutomationConfig::DefaultJson()) :
        DeviceProfileAutomationConfig::DefaultJson();
    automation_configuration_available_ = true;
    try { DeviceProfileAutomationConfig::Parse(config); }
    catch (const std::exception&) { automation_configuration_available_ = false; config = DeviceProfileAutomationConfig::DefaultJson(); }
    binding_engine_.Configure(config, ids, load_error_.empty(),
        load_error_.empty() ? document_.at("revision").get<int64_t>() : 0, automation_configuration_available_);
    binding_engine_.Advance(); // decision-only reevaluation; keeps pending foreground stability
}
bool DeviceProfileRuntime::ObserveAutomationForeground(const std::string& name) {
    // Foreground observation never waits for a hardware transaction. A busy
    // coordinator may leave the explicitly timestamped authority context older.
    // These are cached getters, not State()/presence probe/PrepareTransportSession.
    {
        std::unique_lock<std::mutex> gate(mutation_gate_, std::try_to_lock);
        if (gate.owns_lock()) {
            const auto snapshot = device_.GetDiagnosticSnapshot();
            const char* health[] = {"Clean", "TransactionInProgress", "IndeterminateStagedState", "PersistentSafetyQuarantine", "Stopped"};
            binding_engine_.SetContext({load_error_.empty(), std::nullopt, snapshot.persistent_safety_quarantine,
                health[static_cast<size_t>(snapshot.health)], snapshot.session_generation != session_generation_});
            // Actual keyboard presence remains unknown in this observation
            // cache. Activation preflight verifies M605 transport, never this context.
        }
    }
    binding_engine_.Observe(name);
    return binding_engine_.Advance(); // NO activation call / callback / selected mutation
}
DeviceProfileRuntime::Json DeviceProfileRuntime::AutomationAdmissionSnapshot() const {
    auto token = binding_engine_.Snapshot();
    const auto device = device_.GetDiagnosticSnapshot();
    const char* health[] = {"Clean", "TransactionInProgress", "IndeterminateStagedState", "PersistentSafetyQuarantine", "Stopped"};
    token["m605_session_generation"] = device.session_generation;
    token["runtime_health"] = health[static_cast<size_t>(device.health)];
    token["persistent_safety_quarantine"] = device.persistent_safety_quarantine;
    return token;
}
void DeviceProfileRuntime::SetAutomationCoordinatorDiagnostics(std::function<Json()> snapshot) {
    std::lock_guard<std::mutex> gate(mutation_gate_);
    coordinator_snapshot_ = std::move(snapshot);
}
DeviceProfileRuntime::Json DeviceProfileRuntime::AutomationDecisionSnapshotCached() const {
    auto result = binding_engine_.Snapshot();
    if (coordinator_snapshot_) {
        const auto coordinator = coordinator_snapshot_();
        for (auto it = coordinator.begin(); it != coordinator.end(); ++it) result[it.key()] = it.value();
        const auto& reasons = result.at("hardware_block_reasons");
        result["hardware_block_reason"] = reasons.empty() ? Json(nullptr) : reasons.front();
    }
    return result;
}
bool DeviceProfileRuntime::AutomationTokenFreshLocked(const Json& token) const {
    if (!load_error_.empty() || !automation_configuration_available_) return false;
    const auto current = binding_engine_.Snapshot();
    if (!current.value("enabled", false) || current.value("manual_hold", true) ||
        current.value("debounce_pending", true) ||
        (current.at("decision_kind") != "Match" && current.at("decision_kind") != "Fallback")) return false;
    for (const char* field : {"decision_sequence", "configuration_document_revision", "manual_action_sequence", "foreground_observation_sequence",
        "decision_foreground_process", "resolved_profile_id", "matched_rule_id", "decision_kind"})
        if (!token.contains(field) || current.at(field) != token.at(field)) return false;
    if (current.at("foreground_process") != token.at("decision_foreground_process") ||
        current.at("committed_foreground_process") != token.at("decision_foreground_process") ||
        document_.at("revision") != token.at("configuration_document_revision")) return false;
    return std::any_of(document_.at("profiles").begin(), document_.at("profiles").end(),
        [&](const Json& profile) { return SameGuid(profile.at("id"), token.at("resolved_profile_id").get<std::string>()); });
}
DeviceProfileRuntime::Json DeviceProfileRuntime::ActivateAutomationDecision(
    const Json& token, const std::function<bool()>& stopping) {
    // This is the final admission authority, serialized with CRUD, manual
    // magnetic mutation and manual Apply/ManualHold. No loopback HTTP call.
    std::lock_guard<std::mutex> gate(mutation_gate_);
    const auto reject = [&](const char* outcome, const char* reason) {
        return Json{{"outcome", outcome}, {"error", reason}, {"hardware_block_reasons", Json::array({reason})}};
    };
    if (stopping()) return reject("cancelled", "Shutdown");
    if (!AutomationTokenFreshLocked(token)) return reject("stale", "StaleDecision");
    if (device_.GetSessionGeneration() != token.at("m605_session_generation"))
        return reject("stale", "SessionGenerationChanged");
    if (device_.IsPersistentSafetyQuarantined()) return reject("blocked", "SafetyQuarantined");
    if (device_.GetHealth() != M605RuntimeHealth::Clean) return reject("blocked", "M605Unhealthy");
    const Json preflight = {{"document_revision", document_.at("revision")},
        {"session_generation", device_.GetSessionGeneration()}, {"health", "Clean"},
        {"persistent_safety_quarantine", false}, {"checked_under_shared_mutation_gate", true},
        {"transport_validation", "existing ActivateLocked pre-stage PrepareTransportSession; not cached lighting permission"}};
    // Same existing authority/path as explicit Apply. PrepareTransportSession
    // inside ActivateLocked verifies the actual M605 handle before any stage;
    // available_ is a conservative service gate, never sufficient permission.
    auto admitted = token;
    const auto fresh = [&] {
        if (stopping()) return false;
        // Our own durable selected change is the only revision adopted here.
        return AutomationTokenFreshLocked(admitted);
    };
    const auto target = token.at("resolved_profile_id").get<std::string>();
    // ActivateLocked accepts selection then rechecks admission before planning
    // operations. Selected remains the existing durable desired selection.
    const auto admission = [&] {
        if (SameGuid(document_.at("selected_profile_id"), target) &&
            document_.at("revision").get<int64_t>() == token.at("configuration_document_revision").get<int64_t>() + 1)
            admitted["configuration_document_revision"] = document_.at("revision");
        return fresh();
    };
    Json result;
    try { result = ActivateLocked(target, "Automation", nullptr, [] { return false; }, admission); }
    catch (const ProfileError& ex) {
        if (ex.code == 409) return reject("stale", "DocumentRevisionConflict");
        if (ex.code == 404) return reject("stale", "TargetMissing");
        return reject("failed", ex.code == 503 ? "ProfileDocumentUnavailable" : "ProfileValidationRejected");
    }
    if (result.at("outcome") == "succeeded" && result.at("operations").empty()) result["outcome"] = "no-op";
    result["hardware_block_reasons"] = result.at("outcome") == "deferred" ? Json::array({"KeyboardUnavailable"}) : Json::array();
    result["authoritative_preflight"] = preflight;
    result["authoritative_runtime_result"] = {{"document_revision", document_.at("revision")},
        {"session_generation", device_.GetSessionGeneration()}, {"persistent_safety_quarantine", device_.IsPersistentSafetyQuarantined()},
        {"clean", device_.GetHealth() == M605RuntimeHealth::Clean}};
    // Internal errors can mention persistence paths. Operation details remain
    // in the existing Profile diagnostics; coordinator exports bounded reasons.
    if (result.at("outcome") == "failed") result["error"] = "ProfileApplyFailed";
    if (result.at("outcome") == "deferred") result["error"] = "KeyboardUnavailable";
    return result;
}
void DeviceProfileRuntime::TrackDeclaredFootprintLocked() {
    for (const auto& profile : document_.at("profiles")) {
        const auto& magnetic = profile.at("magnetic");
        if (magnetic.contains("global_actuation_mm") &&
            !magnetic.at("global_actuation_mm").is_null()) {
            all_key_actuation_footprint_ = true;
            for (const auto& physical : m605::detail::kVerifiedPhysicalKeys)
                actuation_footprint_.insert(physical.logical_id);
        }
        if (magnetic.contains("global_deadzone") && !magnetic.at("global_deadzone").is_null()) {
            all_key_deadzone_footprint_ = true;
            for (const auto& physical : m605::detail::kVerifiedPhysicalKeys)
                deadzone_footprint_.insert(physical.logical_id);
        }
        for (const auto& key : magnetic.at("keys")) {
            const auto id = key.at("logical_id").get<uint16_t>();
            if (key.contains("actuation_mm") && !key.at("actuation_mm").is_null())
                actuation_footprint_.insert(id);
            if (key.contains("deadzone") && !key.at("deadzone").is_null())
                deadzone_footprint_.insert(id);
        }
    }
}

std::string DeviceProfileRuntime::Operation::Identity() const {
    const char* name = nullptr;
    switch (kind) {
    case Kind::DksStandard: case Kind::DksSet: name = "Dks"; break;
    case Kind::RapidTriggerOff: case Kind::RapidTriggerOn:
    case Kind::PerKeyRtUnified: case Kind::PerKeyRtPress: case Kind::PerKeyRtRelease:
        name = "RapidTrigger"; break;
    case Kind::ResetAllActuation: name = "ResetAllPerKeyActuationOverrides"; break;
    case Kind::KeyActuation: name = "KeyActuation"; break;
    case Kind::ResetAllDeadzone: name = "ResetAllDeadzone"; break;
    case Kind::AllKeyDeadzone: name = "AllKeyDeadzone"; break;
    case Kind::KeyDeadzone: name = "KeyDeadzone"; break;
    }
    return std::string(name) + ":" + std::to_string(key);
}
DeviceProfileRuntime::Json DeviceProfileRuntime::Operation::Summary(bool success,
    std::string error, bool recovery) const {
    const char* names[] = {"DksStandard", "PerKeyRtDisabledState", "ResetAllPerKeyActuationOverrides",
        "KeyActuation", "ResetAllDeadzone", "AllKeyDeadzone", "KeyDeadzone",
        "PerKeyRtEnabledState", "DksSet", "PerKeyRtUnified", "PerKeyRtPress", "PerKeyRtRelease"};
    return {{"identity", Identity()}, {"kind", names[static_cast<size_t>(kind)]}, {"logical_id", key},
        {"succeeded", success}, {"error", std::move(error)}, {"prior_intent_resubmission", recovery}, {"restore", restore},
        {"restore_source", restore && value.is_object() ? value.value("restore_source", "KnownPriorIntent") : ""},
        {"disable", value.is_object() && value.contains("enabled") && value.at("enabled") == false}};
}

void DeviceProfileRuntime::InvalidateLocked() {
    active_.reset();
    dirty_ = true;
    applied_.clear();
}
void DeviceProfileRuntime::SynchronizeGenerationLocked() {
    device_.RefreshTransportPresence();
    const auto current = device_.GetSessionGeneration();
    if (current != session_generation_) {
        session_generation_ = current;
        rt_prior_.clear(); rt_prior_source_.clear(); rt_submission_valid_ = false;
        InvalidateLocked();
        ++mutation_revision_;
    }
}
void DeviceProfileRuntime::ExternalMutationLocked() {
    SynchronizeGenerationLocked();
    rt_prior_.clear(); rt_prior_source_.clear(); rt_submission_valid_ = false;
    InvalidateLocked();
    ++mutation_revision_;
}
void DeviceProfileRuntime::RecordManualGlobalBaselineLocked(const std::string& field, const Json& value) {
    if (!load_error_.empty()) throw ProfileError(503, load_error_);
    if (field != "global_actuation_mm" && field != "global_deadzone")
        throw ProfileError(422, "Unknown or legacy magnetic baseline field");
    Json next = document_;
    next["global_defaults"][field] = value;
    CommitLocked(std::move(next), document_.at("revision").get<int64_t>());
}
DeviceProfileRuntime::Json DeviceProfileRuntime::EffectiveBaselineLocked() const {
    return BaselineWithHost(document_.at("global_defaults"), host_profile_());
}
DeviceProfileRuntime::Json DeviceProfileRuntime::StateLocked() {
    SynchronizeGenerationLocked();
    return StateSnapshotLocked();
}
DeviceProfileRuntime::Json DeviceProfileRuntime::StateSnapshotLocked() const {
    if (!load_error_.empty()) return {{"status", "error"}, {"error", load_error_},
        {"schema_version", 1}, {"runtime_revision", mutation_revision_},
        {"m605_session_generation", session_generation_}};
    return {{"status", "ok"}, {"api_version", 1},
        {"selected_profile_id", document_.at("selected_profile_id")},
        {"active_profile_id", active_ ? Json(*active_) : Json(nullptr)},
        {"dirty", dirty_}, {"document_revision", document_.at("revision")},
        {"runtime_revision", mutation_revision_}, {"mutation_revision", mutation_revision_},
        {"m605_session_generation", session_generation_},
        {"hardware_rt_gate", HardwareGateJson(device_.GetHardwareRtGateObservation())},
        {"last_activation_reason", last_reason_.empty() ? Json(nullptr) : Json(last_reason_)},
        {"last_apply_outcome", last_outcome_.is_null() ? Json(nullptr) : last_outcome_},
        {"applied_intent_source", "SessionApplied host submission; not firmware readback"},
        {"global_defaults", document_.at("global_defaults")},
        {"effective_global_defaults", EffectiveBaselineLocked()},
        {"legacy_rt_migration", legacy_rt_migration_},
        {"import_warning", import_warning_.empty() ? Json(nullptr) : Json(import_warning_)}};
}
DeviceProfileRuntime::Json DeviceProfileRuntime::State() {
    std::lock_guard<std::mutex> lock(mutation_gate_);
    return StateLocked();
}

DeviceProfileRuntime::Json DeviceProfileRuntime::AutomationSnapshotLocked() const {
    if (!load_error_.empty()) throw ProfileError(503, "Profile document unavailable");
    return {{"status", "ok"}, {"api_version", 1}, {"document_revision", document_.at("revision")},
        {"runtime_revision", mutation_revision_}, {"mutation_revision", mutation_revision_},
        {"selected_profile_id", document_.at("selected_profile_id")},
        {"active_profile_id", active_ ? Json(*active_) : Json(nullptr)}, {"dirty", dirty_},
        {"profiles", document_.at("profiles")},
        {"device_profile_automation", document_.value("device_profile_automation", DeviceProfileAutomationConfig::DefaultJson())},
        {"automation_decision", AutomationDecisionSnapshotCached()},
        {"automation_configuration_available", automation_configuration_available_},
        {"automation_warning", automation_configuration_available_ ? Json(nullptr) :
            Json("Automation configuration is unsupported or invalid; retained. Manual Profiles remain available.")}};
}

namespace {
// Export only magnetic contract fields, never Profile names, extensions,
// automation metadata, saved HostProfile filenames, or device serial paths.
Json DiagnosticMagnetic(const Json& input) {
    if (input.is_null() || input.is_boolean() || input.is_number()) return input;
    if (input.is_string()) {
        const auto text = input.get<std::string>();
        for (const auto* name : {"Inactive", "Tap", "Release", "Hold", "LogicalKey", "DefaultSentinel"})
            if (text == name) return input;
        return nullptr;
    }
    if (input.is_array()) {
        auto output = Json::array();
        for (const auto& item : input) output.push_back(DiagnosticMagnetic(item));
        return output;
    }
    auto output = Json::object();
    for (const auto* name : {"global_actuation_mm", "global_deadzone", "global_rapid_trigger", "keys",
        "logical_id", "actuation_mm", "deadzone", "rapid_trigger", "dks", "top_mm", "bottom_mm",
        "enabled", "press_mm", "release_mm", "continuous", "separate_mode", "start_mm", "end_mm", "standard",
        "slots", "target", "kind", "down_start", "down_end", "up_start", "up_end"})
        if (input.contains(name)) output[name] = DiagnosticMagnetic(input.at(name));
    return output;
}
Json DiagnosticWin32(const std::string& error) {
    // The backend already records Win32 errors in its error string. Parse
    // that explicit marker only; never GetLastError() on the export thread.
    std::smatch match;
    static const std::regex marker(R"(Win32[^:]*:\s*([0-9]{1,10}))");
    if (!std::regex_search(error, match, marker)) return nullptr;
    return std::stoull(match[1].str());
}
std::string DiagnosticError(const std::string& error) {
    // Errors from I/O may contain paths. Omit the whole path-bearing text,
    // retaining separately the operation identity and explicit Win32 code.
    if (error.find('\\') != std::string::npos || error.find('/') != std::string::npos ||
        error.find(":\\") != std::string::npos)
        return "Path-bearing error text omitted from diagnostic export";
    size_t size = std::min<size_t>(512, error.size());
    while (size < error.size() && size > 0 &&
        (static_cast<unsigned char>(error[size]) & 0xc0) == 0x80) --size;
    return error.substr(0, size);
}
Json DiagnosticOperations(Json operations) {
    if (!operations.is_array()) return operations;
    for (auto& item : operations) {
        if (item.contains("value")) item["value"] = DiagnosticMagnetic(item.at("value"));
        if (item.contains("error")) {
            const auto error = item.at("error").get<std::string>();
            item["win32_error"] = DiagnosticWin32(error);
            item["error"] = DiagnosticError(error);
        }
    }
    return operations;
}
Json DiagnosticTiming(const M605TimingSnapshot& t) {
    return {{"transactions", t.transactions}, {"total_ms", t.total_ms}, {"queue_wait_ms", t.queue_wait_ms},
        {"device_lock_wait_ms", t.device_lock_wait_ms}, {"transport_connect_ms", t.connect_ms},
        {"safety_latch_ms", t.safety_latch_ms}, {"hid_stage_submit_ms", t.stage_submit_ms},
        {"inter_stage_settle_ms", t.inter_stage_settle_ms}, {"pre_apply_settle_ms", t.pre_apply_settle_ms},
        {"hid_apply_submit_ms", t.apply_submit_ms}, {"post_apply_settle_ms", t.post_apply_settle_ms}};
}
Json DiagnosticShadow(const M605AppliedRuntimeState& s) {
    Json act = Json::array(), dz = Json::array(), rt = Json::array(), dks = Json::array(), pairs = Json::array();
    for (const auto& [key, raw] : s.per_key_actuation_raw)
        act.push_back({{"logical_id", key}, {"raw", raw}});
    for (const auto& [key, value] : s.per_key_deadzone)
        dz.push_back({{"logical_id", key}, {"top_raw", value.top_raw}, {"bottom_raw", value.bottom_raw}});
    for (const auto& [key, value] : s.per_key_rapid_trigger)
        rt.push_back({{"logical_id", key}, {"enabled", value.enabled},
            {"press_raw", value.press_known ? Json(value.press_raw) : Json(nullptr)},
            {"release_raw", value.release_known ? Json(value.release_raw) : Json(nullptr)},
            {"press_known", value.press_known}, {"release_known", value.release_known}, {"continuous", value.continuous}});
    for (const auto& [key, value] : s.per_key_dks) {
        Json slots = Json::array();
        for (const auto& slot : value.slots) slots.push_back({
            {"target_kind", slot.target.kind == m605::DksTarget::Kind::LogicalKey ? "LogicalKey" : "DefaultSentinel"},
            {"target_logical_id", slot.target.kind == m605::DksTarget::Kind::LogicalKey ? Json(slot.target.logical_key_id) : Json(nullptr)},
            {"down_start", static_cast<int>(slot.down_start)}, {"down_end", static_cast<int>(slot.down_end)},
            {"up_start", static_cast<int>(slot.up_start)}, {"up_end", static_cast<int>(slot.up_end)}});
        dks.push_back({{"logical_id", key}, {"start_raw", value.start_raw}, {"end_raw", value.end_raw},
            {"standard", value.standard_runtime_configuration}, {"slots", slots}});
    }
    for (const auto& [pair, enabled] : s.speedtap_pair_submissions)
        pairs.push_back({{"first_logical_id", pair.first}, {"second_logical_id", pair.second}, {"enabled", enabled}});
    const auto optional = [](const auto& value) -> Json { return value ? Json(*value) : Json(nullptr); };
    return {{"source", "host-submission only; not firmware readback"}, {"raw_unit_mm", 0.1},
        {"global_actuation_raw", optional(s.global_actuation_raw)},
        {"global_deadzone", s.global_deadzone ? Json{{"top_raw", s.global_deadzone->top_raw},
            {"bottom_raw", s.global_deadzone->bottom_raw}} : Json(nullptr)},
        {"global_rapid_trigger", s.global_rapid_trigger ? Json{
            {"separate_mode", s.global_rapid_trigger->separate_mode}, {"press_raw", s.global_rapid_trigger->press_raw},
            {"release_raw", s.global_rapid_trigger->release_raw}, {"top_raw", s.global_rapid_trigger->top_raw},
            {"bottom_raw", s.global_rapid_trigger->bottom_raw}} : Json(nullptr)},
        {"per_key_actuation", act}, {"per_key_deadzone", dz}, {"per_key_rapid_trigger", rt}, {"per_key_dks", dks},
        {"per_key_actuation_table_known", s.per_key_actuation_table_known},
        {"per_key_deadzone_table_known", s.per_key_deadzone_table_known}, {"static_analog_effect", optional(s.static_analog_effect)},
        {"speedtap_master", optional(s.speedtap_master)}, {"speedtap_pair_submissions", pairs},
        {"speedtap_pair_knowledge", s.speedtap_pair_knowledge == M605AppliedRuntimeState::SpeedTapPairKnowledge::Unknown ?
            "Unknown" : "ProfileBaselineUnknown"}, {"external_override_tables_known", false}};
}
}

DeviceProfileRuntime::Json DeviceProfileRuntime::Diagnostics() {
    std::lock_guard<std::mutex> gate(mutation_gate_);
    // Deliberately do not call StateLocked/SynchronizeGenerationLocked or any
    // availability/HostProfile callback: export must have no HID or file side effects.
    const auto device = device_.GetDiagnosticSnapshot();
    const char* health[] = {"Clean", "TransactionInProgress", "IndeterminateStagedState", "PersistentSafetyQuarantine", "Stopped"};
    Json transitions = Json::array();
    for (const auto& transition : device.transitions) transitions.push_back({
        {"from_generation", transition.from_generation}, {"to_generation", transition.to_generation}, {"reason", transition.reason}});
    Json active_effective = nullptr;
    if (active_) {
        active_effective = Json::array();
        for (const auto& [identity, op] : applied_) {
            auto value = op.Summary(true); value["value"] = DiagnosticMagnetic(op.value);
            active_effective.push_back(std::move(value));
        }
    }
    Json activation = last_activation_diagnostics_;
    Json failed = last_failed_profile_operation_.is_null() ? Json(nullptr) :
        DiagnosticOperations(Json::array({last_failed_profile_operation_})).at(0);
    if (activation.is_object()) {
        activation["error"] = DiagnosticError(activation.at("error").get<std::string>());
        activation["operations"] = DiagnosticOperations(activation.at("operations"));
        activation["plan"] = DiagnosticOperations(activation.at("plan"));
        activation["effective_target"] = DiagnosticOperations(activation.at("effective_target"));
        activation["resolved_baseline_at_planning"] = DiagnosticMagnetic(activation.at("resolved_baseline_at_planning"));
        // HTTP/wire serialization is not measured by the runtime. Null is unknown.
        if (!activation["timing"].contains("api_processing_ms"))
            activation["timing"]["api_processing_ms"] = nullptr;
    }
    Json runtime = {{"selected_profile_id", nullptr}, {"active_profile_id", active_ ? Json(*active_) : Json(nullptr)},
        {"dirty", dirty_}, {"deferred", last_outcome_.is_object() && last_outcome_.value("outcome", "") == "deferred"},
        {"document_revision", nullptr}, {"runtime_revision", mutation_revision_}, {"mutation_revision", mutation_revision_},
        {"session_generation", device.session_generation}, {"profile_observed_session_generation", session_generation_},
        {"session_invalidation_pending", device.session_generation != session_generation_},
        {"active_intent_current_session", active_.has_value() && device.session_generation == session_generation_ &&
            device.health == M605RuntimeHealth::Clean && !device.persistent_safety_quarantine},
        {"document_available", load_error_.empty()}, {"document_error", load_error_.empty() ? Json(nullptr) : Json("Profile document unavailable; retained")}};
    Json baseline = nullptr;
    if (load_error_.empty()) {
        runtime["selected_profile_id"] = document_.at("selected_profile_id");
        runtime["document_revision"] = document_.at("revision");
        baseline = DiagnosticMagnetic(document_.at("global_defaults"));
    }
    Json last_plan = last_plan_diagnostics_;
    size_t configured_rt = 0, enabled_rt = 0;
    bool selected_legacy_rt = false;
    if (load_error_.empty()) for (const auto& profile : document_.at("profiles"))
        if (profile.at("id") == document_.at("selected_profile_id")) {
            selected_legacy_rt = !LegacyRt(document_.at("global_defaults")).is_null() || !LegacyRt(profile.at("magnetic")).is_null();
            for (const auto& key : profile.at("magnetic").at("keys"))
                if (!key.value("rapid_trigger", Json(nullptr)).is_null()) {
                    ++configured_rt;
                    if (key.at("rapid_trigger").at("enabled") == true) ++enabled_rt;
                }
        }
    // Effective status covers known submitted keys (including restorations). Unmanaged firmware
    // state and external writers are not observed by this feature.
    std::optional<bool> submitted_enabled;
    if (active_ && !dirty_ && device.session_generation == session_generation_ &&
        device.health == M605RuntimeHealth::Clean && !device.persistent_safety_quarantine) {
        submitted_enabled = false;
        for (const auto& [key, rt] : device.session_applied.per_key_rapid_trigger)
            if (rt.enabled) submitted_enabled = true;
    }
    const auto effective_rt = DeriveEffectiveRt(device.hardware_rt_gate.state, submitted_enabled);
    if (last_plan.is_object()) {
        last_plan["operations"] = DiagnosticOperations(last_plan.at("operations"));
        last_plan["effective_target"] = DiagnosticOperations(last_plan.at("effective_target"));
        for (auto& error : last_plan["preflight_errors"])
            error = DiagnosticError(error.get<std::string>());
    }
    Json m605_failure = device.last_failed_kind.empty() ? Json(nullptr) : Json{
        {"kind", device.last_failed_kind}, {"logical_id", device.last_failed_logical_id},
        {"session_generation", device.last_failed_generation}, {"error", DiagnosticError(device.last_failed_error)},
        {"win32_error", DiagnosticWin32(device.last_failed_error)}};
    SYSTEMTIME captured{}; GetSystemTime(&captured);
    char timestamp[32]{};
    std::snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        captured.wYear, captured.wMonth, captured.wDay, captured.wHour, captured.wMinute, captured.wSecond, captured.wMilliseconds);
    return {{"status", "ok"}, {"api_version", 1}, {"diagnostic_schema_version", 1},
        {"captured_at_utc", timestamp},
        {"daemon", {{"product_version", AURA_PRODUCT_VERSION}, {"profile_api_version", 1},
            {"process_instance_id", process_identity_.process_instance_id}, {"started_at_utc", process_identity_.started_at_utc}}},
        {"evidence", "cached host submissions + passive USB status observations; no HID probe; no magnetic configuration readback"},
        {"runtime", runtime}, {"durable_magnetic_baseline", baseline},
        {"legacy_rt_migration", legacy_rt_migration_},
        {"durable_baseline_source", "Profile document global_defaults; host desired only"},
        {"active_profile_effective_values", active_effective}, {"last_activation", activation},
        {"hardware_rt_gate", HardwareGateJson(device.hardware_rt_gate)},
        {"rapid_trigger", {{"configured_rt_key_count", configured_rt}, {"enabled_rt_key_count", enabled_rt},
            {"legacy_global_rt_present", load_error_.empty() ? Json(selected_legacy_rt) : Json(nullptr)},
            {"count_source", "selected Profile desired per-key objects; not firmware readback"},
            {"effective_rt_active", effective_rt ? Json(*effective_rt) : Json(nullptr)},
            {"effective_source", "derived hardware gate + clean current-session host submission; known submitted keys including restorations only"}}},
        {"last_plan", last_plan}, {"last_failed_operation", failed},
        {"automation", AutomationDecisionSnapshotCached()},
        {"m605", {{"health", health[static_cast<size_t>(device.health)]},
            {"persistent_safety_quarantine", device.persistent_safety_quarantine}, {"session_generation", device.session_generation},
            {"transport", {{"open_at_last_lifecycle_observation", device.transport_open_at_last_observation},
                {"connected_now", nullptr}, {"fresh_session_now", nullptr}, {"last_open_generation", device.last_open_generation},
                {"source", "cached lifecycle; current physical presence not probed"}}},
            {"queued_jobs", device.queued_jobs}, {"session_transitions", transitions}, {"transition_history_limit", 16},
            {"session_applied", DiagnosticShadow(device.session_applied)}, {"cumulative_timing", DiagnosticTiming(device.timing)},
            {"last_error", DiagnosticError(device.last_error)}, {"last_error_win32", DiagnosticWin32(device.last_error)},
            {"last_failed_operation", m605_failure}}}};
}

DeviceProfileRuntime::Json DeviceProfileRuntime::ActivateProfile(const std::string& id,
    const std::string& reason, int64_t expected_revision, const Json& temporary,
    const std::function<bool()>& cancelled) {
    std::lock_guard<std::mutex> gate(mutation_gate_);
    if (!load_error_.empty()) throw ProfileError(503, load_error_);
    if (document_.at("revision") != expected_revision)
        throw ProfileError(409, "Profile document revision conflict; reload required");
    if (reason != "Manual" && reason != "Automation" && reason != "Startup" &&
        reason != "Reconnect" && reason != "Restore")
        throw ProfileError(422, "Unknown Profile activation reason");
    return ActivateLocked(id, reason, temporary, cancelled);
}

void DeviceProfileRuntime::CommitLocked(Json next, int64_t expected_revision) {
    Validate(next);
    RejectNewLegacyRt(next, document_);
    NamedConfigLock lock;
    if (!lock.IsAcquired()) throw ProfileError(503, "Profile document lock timed out");
    if (!std::filesystem::exists(path_)) throw ProfileError(409, "Profile document disappeared; reload required");
    Json disk;
    try { disk = Json::parse(Read(path_)); Validate(disk); }
    catch (const std::exception& ex) {
        throw ProfileError(409, std::string("On-disk Profile document invalid; retained: ") + ex.what());
    }
    if (disk.at("revision") != expected_revision || document_.at("revision") != expected_revision) {
        document_ = std::move(disk);
        if (!document_.contains("schema_version")) document_["schema_version"] = 1;
        RefreshAutomationConfigurationLocked();
        InvalidateLocked();
        ++mutation_revision_;
        throw ProfileError(409, "Profile document revision conflict; reload required");
    }
    next["revision"] = expected_revision + 1;
    WriteAtomic(path_, next.dump(2) + "\n");
    document_ = std::move(next);
    TrackDeclaredFootprintLocked();
    RefreshAutomationConfigurationLocked();
    ++mutation_revision_;
}

void DeviceProfileRuntime::MigrateLegacyRtLocked() {
    Json next = document_;
    auto report = BuildLegacyRtMigration(next);
    if (!report.at("changed").get<bool>()) { legacy_rt_migration_ = std::move(report); return; }
    try {
        Validate(next);
        NamedConfigLock lock;
        if (!lock.IsAcquired()) throw ProfileError(503, "Profile migration lock timed out");
        const auto original = Read(path_);
        auto disk = Json::parse(original);
        if (!disk.contains("schema_version")) disk["schema_version"] = 1;
        if (disk != document_) throw ProfileError(409, "Profile migration revision/content conflict; reload required");
        const auto revision = document_.at("revision").get<int64_t>();
        const auto backup = std::filesystem::path(path_.wstring() + L".legacy-rt-v1.r" + std::to_wstring(revision) + L".bak");
        // Copy never replaces an existing backup; a retry may only reuse the
        // exact same original bytes. No device query, connect, or setter here.
        if (std::filesystem::exists(backup)) {
            if (!std::filesystem::is_regular_file(backup) || Read(backup) != original)
                throw ProfileError(409, "Profile migration backup conflict; original retained");
        } else if (!CopyFileW(path_.c_str(), backup.c_str(), TRUE))
            throw ProfileError(500, "Profile migration backup failed; original retained");
        if (Read(backup) != original) throw ProfileError(500, "Profile migration backup verification failed");
        HANDLE saved = CreateFileW(backup.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        const bool flushed = saved != INVALID_HANDLE_VALUE && FlushFileBuffers(saved);
        if (saved != INVALID_HANDLE_VALUE) CloseHandle(saved);
        if (!flushed) throw ProfileError(500, "Profile migration backup flush failed; original retained");
        // Reuse the sole revisioned atomic document writer (named mutex is
        // recursive on this thread); optimistic revision conflict stays explicit.
        CommitLocked(std::move(next), revision);
        InvalidateLocked();
        report["document_revision"] = document_.at("revision");
        report["backup_created_or_verified"] = true;
        legacy_rt_migration_ = std::move(report);
    } catch (const std::exception&) {
        // Do not make valid core Profiles unavailable or claim a conversion
        // after a failed backup/atomic write. Legacy guard remains authoritative.
        legacy_rt_migration_ = {{"schema_version", 1}, {"status", "Failed"}, {"changed", false},
            {"hardware_operations", 0}, {"error", "Legacy RT migration failed; original document retained; resolve persistence/conflict and retry"}};
    }
}

std::map<std::string, DeviceProfileRuntime::Operation> DeviceProfileRuntime::Resolve(
    const Json& defaults, const Json& profile, const Json& temporary) {
    std::map<std::string, Operation> result;
    for (const auto* layer : {&defaults, &profile, &temporary}) {
        if (!layer->is_object()) continue;
        const auto put = [&result](Kind kind, uint16_t key, const Json& value) {
            Operation op{kind, key, value}; result[op.Identity()] = std::move(op);
        };
        for (const auto& key : layer->at("keys")) {
            const auto id = key.at("logical_id").get<uint16_t>();
            if (key.contains("rapid_trigger") && !key.at("rapid_trigger").is_null()) {
                const auto& rt = key.at("rapid_trigger");
                put(rt.at("enabled") == true ? Kind::RapidTriggerOn : Kind::RapidTriggerOff, id, rt);
            }
            if (key.contains("dks") && !key.at("dks").is_null()) {
                const auto& dks = key.at("dks");
                put(dks.at("standard") == true ? Kind::DksStandard : Kind::DksSet, id, dks);
            }
        }
    }
    return result;
}

DeviceProfileRuntime::Plan DeviceProfileRuntime::BuildPlan(
    const Json& profile, const Json& temporary, const MagneticHostProfile& host) {
    Plan plan;
    const auto& defaults = document_.at("global_defaults");
    const auto& magnetic = profile.at("magnetic");
    plan.target = Resolve(defaults, magnetic, temporary);
    for (const auto* layer : {&defaults, &magnetic, &temporary}) {
        if (!layer->is_object()) continue;
        for (const auto& key : layer->at("keys"))
            if (!m605::WireIdForLogicalKey(key.at("logical_id").get<uint16_t>()))
                plan.blockers.push_back("Unsupported logical key: " +
                    std::to_string(key.at("logical_id").get<uint16_t>()));
    }
    const auto field = [](const Json& layer, const char* name) -> Json {
        return layer.is_object() && layer.contains(name) ? layer.at(name) : Json(nullptr);
    };
    const auto baseline = BaselineWithHost(defaults, host);
    Json baseline_act = field(baseline, "global_actuation_mm");
    Json baseline_dz = field(baseline, "global_deadzone");
    const bool all_act = !field(temporary, "global_actuation_mm").is_null() ||
        !field(magnetic, "global_actuation_mm").is_null();
    const bool all_dz = !field(temporary, "global_deadzone").is_null() ||
        !field(magnetic, "global_deadzone").is_null();
    const bool manage_all_act = !baseline_act.is_null() || all_act || all_key_actuation_footprint_;
    // Per-key deadzone ownership also requires a known-clean table so an
    // inherited key cannot retain an override from a prior Profile or ASUS.
    const bool manage_all_dz = !baseline_dz.is_null() || all_dz || all_key_deadzone_footprint_ ||
        !deadzone_footprint_.empty();
    const Json effective_act_base = !field(temporary, "global_actuation_mm").is_null() ?
        field(temporary, "global_actuation_mm") :
        !field(magnetic, "global_actuation_mm").is_null() ?
            field(magnetic, "global_actuation_mm") : baseline_act;
    const Json effective_dz_base = !field(temporary, "global_deadzone").is_null() ?
        field(temporary, "global_deadzone") :
        !field(magnetic, "global_deadzone").is_null() ?
            field(magnetic, "global_deadzone") : baseline_dz;
    if (manage_all_act && !baseline_act.is_null())
        plan.target[Operation{Kind::ResetAllActuation, 0, effective_act_base}.Identity()] =
            {Kind::ResetAllActuation, 0, effective_act_base};
    if (manage_all_dz && !baseline_dz.is_null())
        plan.target[Operation{Kind::AllKeyDeadzone, 0, effective_dz_base}.Identity()] =
            {Kind::AllKeyDeadzone, 0, effective_dz_base};
    if (manage_all_dz && !baseline_dz.is_null())
        plan.target[Operation{Kind::ResetAllDeadzone, 0, nullptr}.Identity()] =
            {Kind::ResetAllDeadzone, 0, effective_dz_base};
    // The verified per-key RT setter has no top/bottom or separate-mode fields.
    // Never translate a Profile-local RT default into a device-global write.
    if (!field(defaults, "global_rapid_trigger").is_null() ||
        !field(magnetic, "global_rapid_trigger").is_null() ||
        !field(temporary, "global_rapid_trigger").is_null())
        plan.blockers.push_back("Legacy global RT settings cannot be safely applied; migrate or remove legacy settings");
    const auto key_field = [](const Json& layer, uint16_t id, const char* name) -> Json {
        if (!layer.is_object() || !layer.contains("keys")) return nullptr;
        for (const auto& key : layer.at("keys"))
            if (key.at("logical_id") == id && key.contains(name)) return key.at(name);
        return nullptr;
    };
    const auto fill = [&](Kind kind, uint16_t key, const char* name,
                          const char* global_name, bool all, const Json& global_baseline) {
        const auto identity = Operation{kind, key, nullptr}.Identity();
        const Json root_key = key_field(defaults, key, name);
        const Json profile_key = key_field(magnetic, key, name);
        const Json temporary_key = key_field(temporary, key, name);
        const bool footprint = kind == Kind::KeyActuation ?
            actuation_footprint_.count(key) != 0 : deadzone_footprint_.count(key) != 0;
        if (!all && root_key.is_null() && profile_key.is_null() &&
            temporary_key.is_null() && !applied_.count(identity) && !footprint) return;
        const Json key_baseline = root_key.is_null() ? global_baseline : root_key;
        if (key_baseline.is_null()) {
            const std::string error = std::string("Device global baseline unknown for ") +
                (kind == Kind::KeyActuation ? "actuation" : "deadzone");
            if (std::find(plan.blockers.begin(), plan.blockers.end(), error) == plan.blockers.end())
                plan.blockers.push_back(error);
            return;
        }
        Json effective = key_baseline;
        const Json profile_global = field(magnetic, global_name);
        const Json temporary_global = field(temporary, global_name);
        if (!profile_global.is_null()) effective = profile_global;
        if (!profile_key.is_null()) effective = profile_key;
        if (!temporary_global.is_null()) effective = temporary_global;
        if (!temporary_key.is_null()) effective = temporary_key;
        plan.target[identity] = {kind, key, effective};
    };
    // Resolve every managed key for final shadow comparison. Hardware writes
    // are compacted into table reset/common plus exceptions below; inherited
    // keys are not written as explicit per-key common-valued overrides.
    for (const auto& physical : m605::detail::kVerifiedPhysicalKeys) {
        const auto key = physical.logical_id;
        fill(Kind::KeyActuation, key, "actuation_mm", "global_actuation_mm",
            manage_all_act, baseline_act);
        fill(Kind::KeyDeadzone, key, "deadzone", "global_deadzone",
            manage_all_dz, baseline_dz);
    }
    const auto rt_shadow = device_.GetAppliedRuntimeState();
    const auto saved_host_rt = [&](uint16_t key) -> Json {
        // Known saved enabled-list + shared editor values, NOT firmware readback.
        if (!host.per_key_rt_list_known || !host.global_rt_press_raw || !host.global_rt_release_raw)
            return nullptr;
        const auto found = host.per_key_rt_enabled.find(key);
        const bool enabled = found != host.per_key_rt_enabled.end() && found->second;
        return {{"enabled", enabled}, {"press_mm", *host.global_rt_press_raw / 10.0},
            {"release_mm", *host.global_rt_release_raw / 10.0}, {"continuous", false}};
    };
    // Capture prior intent before first ownership, without requiring a prior to
    // submit a new explicit target. An unknown prior will block later removal.
    const auto remember_prior = [&](const Operation& op) {
        if (op.kind != Kind::RapidTriggerOn && op.kind != Kind::RapidTriggerOff) return;
        if (rapid_trigger_footprint_.count(op.key) || rt_prior_.count(op.key)) return;
        Json prior = key_field(defaults, op.key, "rapid_trigger");
        std::string source = "DurablePerKeyBaseline";
        const auto known = rt_shadow.per_key_rapid_trigger.find(op.key);
        if (prior.is_null() && known != rt_shadow.per_key_rapid_trigger.end() &&
            known->second.press_known && known->second.release_known) {
            prior = {{"enabled", known->second.enabled}, {"press_mm", known->second.press_raw / 10.0},
                {"release_mm", known->second.release_raw / 10.0}, {"continuous", false}};
            source = "PreAcquisitionSessionSubmission";
        }
        if (prior.is_null() && host.rt_continuous == false) { prior = saved_host_rt(op.key); source = "TrustedSavedHostIntent"; }
        if (!prior.is_null()) { rt_prior_[op.key] = prior; rt_prior_source_[op.key] = source; }
    };
    for (const auto& [identity, op] : plan.target) remember_prior(op);
    for (uint16_t key : rapid_trigger_footprint_) {
        const auto identity = Operation{Kind::RapidTriggerOff, key, nullptr}.Identity();
        if (plan.target.count(identity)) continue;
        Json prior = key_field(defaults, key, "rapid_trigger");
        std::string source = "DurablePerKeyBaseline";
        if (prior.is_null() && rt_prior_.count(key)) {
            prior = rt_prior_.at(key); source = rt_prior_source_.at(key);
        }
        if (prior.is_null() && host.rt_continuous == false) { prior = saved_host_rt(key); source = "TrustedSavedHostIntent"; }
        if (prior.is_null()) {
            plan.blockers.push_back("RT prior state unknown; cannot remove management for key " + std::to_string(key));
            continue;
        }
        prior["restore_source"] = source;
        plan.target[identity] = {prior.at("enabled") == true ? Kind::RapidTriggerOn : Kind::RapidTriggerOff,
            key, prior, true};
    }
    // DKS safety remains explicit: never silently destroy a DKS assignment to
    // enable RT. Unknown DKS requires user-requested Standard or known shadow.
    std::vector<uint16_t> dks_keys;
    for (const auto& [identity, op] : plan.target) if (op.kind == Kind::DksSet) dks_keys.push_back(op.key);
    for (uint16_t key : dks_keys) {
        const auto identity = Operation{Kind::RapidTriggerOff, key, nullptr}.Identity();
        if (plan.target.count(identity)) continue;
        const auto known = rt_shadow.per_key_rapid_trigger.find(key);
        Json prior = known != rt_shadow.per_key_rapid_trigger.end() &&
            known->second.press_known && known->second.release_known ?
            Json{{"enabled", false}, {"press_mm", known->second.press_raw / 10.0},
                 {"release_mm", known->second.release_raw / 10.0}} : saved_host_rt(key);
        if (prior.is_null()) plan.blockers.push_back("DKS needs trusted per-key RT state");
        else {
            prior["enabled"] = false;
            plan.target[identity] = {Kind::RapidTriggerOff, key, prior};
            remember_prior(plan.target.at(identity));
        }
    }
    for (uint16_t key : dks_footprint_) {
        const auto identity = Operation{Kind::DksStandard, key, nullptr}.Identity();
        if (!plan.target.count(identity))
            plan.target[identity] = {Kind::DksStandard, key, true};
    }
    for (const auto& [identity, op] : plan.target) {
        if (op.key && !m605::WireIdForLogicalKey(op.key))
            plan.blockers.push_back("Unsupported logical key: " + identity);
        if ((op.kind == Kind::RapidTriggerOn || op.kind == Kind::RapidTriggerOff) &&
            op.value.value("continuous", false))
            plan.blockers.push_back("RT continuous ON is not physically validated for key " + std::to_string(op.key));
        if (op.kind == Kind::DksSet) {
            const auto rt = plan.target.find("RapidTrigger:" + std::to_string(op.key));
            if (rt != plan.target.end() && rt->second.kind == Kind::RapidTriggerOn)
                plan.blockers.push_back("DKS and RT conflict on key " + std::to_string(op.key));
            for (const auto& slot : op.value.at("slots")) {
                const auto& target = slot.at("target");
                if (target.at("kind") == "LogicalKey" &&
                    !m605::WireIdForLogicalKey(target.at("logical_id").get<uint16_t>()))
                    plan.blockers.push_back("Unsupported DKS target");
            }
        }
        if (op.kind == Kind::RapidTriggerOn) {
            const auto dks_target = plan.target.find("Dks:" + std::to_string(op.key));
            const auto dks_known = rt_shadow.per_key_dks.find(op.key);
            if (dks_target != plan.target.end() && dks_target->second.kind == Kind::DksSet)
                plan.blockers.push_back("DKS and RT conflict on key " + std::to_string(op.key));
            else if (dks_target == plan.target.end() &&
                     (dks_known == rt_shadow.per_key_dks.end() || !dks_known->second.standard_runtime_configuration))
                plan.blockers.push_back("RT needs known Standard DKS state; explicitly restore Standard for key " + std::to_string(op.key));
        }
    }
    const auto base_changed = [&](Kind kind) {
        const auto identity = Operation{kind, 0, nullptr}.Identity();
        const auto target = plan.target.find(identity);
        if (target == plan.target.end()) return false;
        const auto old = applied_.find(identity);
        return dirty_ || old == applied_.end() || old->second.value != target->second.value;
    };
    const bool dz_base_changed = base_changed(Kind::AllKeyDeadzone);
    const auto shadow = device_.GetAppliedRuntimeState();
    const auto act_identity = Operation{Kind::ResetAllActuation, 0, nullptr}.Identity();
    const auto old_act_base = applied_.find(act_identity);
    bool removed_act_exception = false;
    bool act_shadow_changed = false;
    size_t act_exception_count = 0;
    for (const auto& [identity, op] : plan.target)
        if (op.kind == Kind::KeyActuation && op.value != effective_act_base) ++act_exception_count;
    for (const auto& [identity, old] : applied_) {
        if (old.kind != Kind::ResetAllActuation && old.kind != Kind::KeyActuation) continue;
        if (!Matches(old, shadow)) act_shadow_changed = true;
        if (old.kind != Kind::KeyActuation || old_act_base == applied_.end() ||
            old.value == old_act_base->second.value) continue;
        const auto next = plan.target.find(identity);
        if (next == plan.target.end() || next->second.value == effective_act_base)
            removed_act_exception = true;
    }
    Json act_reset_reasons = Json::array();
    if (manage_all_act && !baseline_act.is_null()) {
        if (dirty_) act_reset_reasons.push_back("UnknownOrInvalidatedIntent");
        if (old_act_base == applied_.end()) act_reset_reasons.push_back("FirstOwnershipAcquisition");
        else if (old_act_base->second.value != effective_act_base) act_reset_reasons.push_back("CommonChange");
        if (!shadow.per_key_actuation_table_known) act_reset_reasons.push_back("UnknownOverrideTable");
        if (removed_act_exception) act_reset_reasons.push_back("ExceptionRemoval");
        if (act_shadow_changed) act_reset_reasons.push_back("SubmissionShadowDrift");
    }
    const bool reset_act = !act_reset_reasons.empty();
    plan.actuation_ownership = {{"common_target_mm", effective_act_base},
        {"exception_count", act_exception_count}, {"reset_required", reset_act},
        {"reset_reasons", act_reset_reasons}, {"layer", 0}};
    bool removed_dz_exception = false;
    if (manage_all_dz) {
        const auto old_base = applied_.find(Operation{Kind::AllKeyDeadzone, 0, nullptr}.Identity());
        for (const auto& [identity, old] : applied_) {
            if (old.kind != Kind::KeyDeadzone || old_base == applied_.end() ||
                old.value == old_base->second.value) continue;
            const auto next = plan.target.find(identity);
            if (next == plan.target.end() ||
                next->second.value == effective_dz_base) { removed_dz_exception = true; break; }
        }
    }
    const bool reset_dz = manage_all_dz &&
        (dirty_ || dz_base_changed || removed_dz_exception ||
            !shadow.per_key_deadzone_table_known);
    for (const auto& [identity, op] : plan.target) {

        if (op.kind == Kind::RapidTriggerOn || op.kind == Kind::RapidTriggerOff) {
            const auto found = shadow.per_key_rapid_trigger.find(op.key);
            const bool known = rt_submission_valid_ && rapid_trigger_footprint_.count(op.key) &&
                found != shadow.per_key_rapid_trigger.end() &&
                found->second.press_known && found->second.release_known;
            const bool flags_changed = !known || found->second.enabled != op.value.at("enabled").get<bool>();
            const bool press_changed = flags_changed || found->second.press_raw != Raw(op.value.at("press_mm"));
            const bool release_changed = flags_changed || found->second.release_raw != Raw(op.value.at("release_mm"));
            if (press_changed && release_changed && op.value.at("press_mm") == op.value.at("release_mm"))
                plan.operations.push_back({Kind::PerKeyRtUnified, op.key, op.value, op.restore});
            else {
                if (press_changed) plan.operations.push_back({Kind::PerKeyRtPress, op.key, op.value, op.restore});
                if (release_changed) plan.operations.push_back({Kind::PerKeyRtRelease, op.key, op.value, op.restore});
            }
            continue;
        }
        // Do not repeat a user-requested Standard rewrite solely because the
        // document changed; its current-session submission is still known.
        if (op.kind == Kind::DksStandard && Matches(op, shadow)) continue;
        const auto old = applied_.find(identity);
        const bool replacing_base = op.kind == Kind::KeyActuation ? reset_act :
            op.kind == Kind::KeyDeadzone && reset_dz;
        if (replacing_base) {
            const auto base_kind = op.kind == Kind::KeyActuation ?
                Kind::ResetAllActuation : Kind::AllKeyDeadzone;
            const auto& base = plan.target.at(Operation{base_kind, 0, nullptr}.Identity());
            // Verified table resets establish inheritance; only exceptions
            // need a per-key write. No fake per-key common-value restoration.
            if (op.value != base.value) plan.operations.push_back(op);
        } else if (op.kind == Kind::ResetAllActuation && reset_act) {
            plan.operations.push_back(op);
        } else if ((op.kind == Kind::ResetAllDeadzone || op.kind == Kind::AllKeyDeadzone) &&
                   reset_dz) {
            plan.operations.push_back(op);
        } else if (dirty_ || old == applied_.end() || old->second.kind != op.kind ||
                   old->second.value != op.value)
            plan.operations.push_back(op);
    }
    size_t managed_rt = 0, enabled_rt = 0, rt_operations = 0;
    for (const auto& [identity, op] : plan.target)
        if ((op.kind == Kind::RapidTriggerOn || op.kind == Kind::RapidTriggerOff) && !op.restore) {
            ++managed_rt; if (op.value.at("enabled") == true) ++enabled_rt;
        }
    for (const auto& op : plan.operations)
        if (op.kind == Kind::PerKeyRtUnified || op.kind == Kind::PerKeyRtPress || op.kind == Kind::PerKeyRtRelease)
            ++rt_operations;
    plan.rapid_trigger_management = {{"managed_key_count", managed_rt}, {"enabled_key_count", enabled_rt},
        {"operation_count", rt_operations}, {"model", "ExplicitPerKeyState"},
        {"legacy_global_rt_present", !field(defaults, "global_rapid_trigger").is_null() ||
            !field(magnetic, "global_rapid_trigger").is_null() || !field(temporary, "global_rapid_trigger").is_null()}};
    std::sort(plan.operations.begin(), plan.operations.end(), [](const Operation& a, const Operation& b) {
        const auto rank = [](const Operation& op) {
            if (op.kind >= Kind::PerKeyRtUnified) return op.value.at("enabled") == false ? 1 : 7;
            return static_cast<int>(op.kind);
        };
        if (rank(a) != rank(b)) return rank(a) < rank(b);
        if (a.key != b.key) return a.key < b.key;
        return a.kind < b.kind;
    });
    for (const auto& op : plan.operations)
        if (op.key && !m605::WireIdForLogicalKey(op.key))
            plan.blockers.push_back("Unsupported operation: " + op.Identity());
    return plan;
}

bool DeviceProfileRuntime::Execute(const Operation& op, const MagneticHostProfile& host) {
    switch (op.kind) {
    case Kind::PerKeyRtUnified:
        return device_.SetPerKeyRapidTriggerUnified(op.key, op.value.at("press_mm"), op.value.at("enabled")).get();
    case Kind::PerKeyRtPress:
        return device_.SetPerKeyRapidTriggerPress(op.key, op.value.at("press_mm"), op.value.at("enabled")).get();
    case Kind::PerKeyRtRelease:
        return device_.SetPerKeyRapidTriggerRelease(op.key, op.value.at("release_mm"), op.value.at("enabled")).get();
    case Kind::DksStandard:
        return device_.RestorePerKeyDksToStandard(op.key).get();
    case Kind::RapidTriggerOff:
        if (op.value.is_object() && op.value.contains("press_mm") &&
            op.value.contains("release_mm"))
            return device_.DisablePerKeyRapidTrigger(op.key,
                op.value.at("press_mm").get<double>(), op.value.at("release_mm").get<double>()).get();
        return host.global_rt_press_raw && host.global_rt_release_raw &&
            device_.DisablePerKeyRapidTrigger(op.key, *host.global_rt_press_raw / 10.0,
                *host.global_rt_release_raw / 10.0).get();
    case Kind::ResetAllActuation:
        return device_.ResetAllPerKeyActuationOverrides(op.value.get<double>()).get();
    case Kind::KeyActuation:
        return device_.SetPerKeyActuation(op.key, op.value.get<double>()).get();
    case Kind::ResetAllDeadzone:
        return device_.ResetAllPerKeyDeadzoneOverrides(
            Raw(op.value.at("bottom_mm")), Raw(op.value.at("top_mm"))).get();
    case Kind::AllKeyDeadzone:
        return device_.SetGlobalDeadzone(op.value.at("top_mm").get<double>(),
            op.value.at("bottom_mm").get<double>()).get();
    case Kind::KeyDeadzone:
        return device_.SetPerKeyDeadzone(op.key, op.value.at("top_mm").get<double>(),
            op.value.at("bottom_mm").get<double>()).get();
    case Kind::RapidTriggerOn:
        return device_.SetPerKeyRapidTrigger(op.key, op.value.at("press_mm").get<double>(),
            op.value.at("release_mm").get<double>()).get();
    case Kind::DksSet: {
        m605::DksConfig dks;
        dks.source_logical_key_id = op.key;
        dks.start_mm = op.value.at("start_mm").get<double>();
        dks.end_mm = op.value.at("end_mm").get<double>();
        for (size_t i = 0; i < 4; ++i) {
            const auto& slot = op.value.at("slots")[i];
            const auto& target = slot.at("target");
            dks.slots[i].target = target.at("kind") == "LogicalKey" ?
                m605::DksTarget::LogicalKey(target.at("logical_id").get<uint16_t>()) :
                m605::DksTarget::DefaultSentinel();
            dks.slots[i].down_start = Trigger(slot.at("down_start"));
            dks.slots[i].down_end = Trigger(slot.at("down_end"));
            dks.slots[i].up_start = Trigger(slot.at("up_start"));
            dks.slots[i].up_end = Trigger(slot.at("up_end"));
        }
        return device_.SetPerKeyDks(dks).get();
    }
    }
    return false;
}

bool DeviceProfileRuntime::Matches(const Operation& op, const M605AppliedRuntimeState& s) const {
    switch (op.kind) {
    case Kind::ResetAllActuation:
        return s.per_key_actuation_table_known && s.global_actuation_raw &&
            *s.global_actuation_raw == Raw(op.value);
    case Kind::KeyActuation: {
        const auto found = s.per_key_actuation_raw.find(op.key);
        // Common-valued target keys must inherit, not retain an explicit
        // override equal to common (which would stop following later changes).
        if (s.global_actuation_raw && *s.global_actuation_raw == Raw(op.value))
            return s.per_key_actuation_table_known && found == s.per_key_actuation_raw.end();
        return found != s.per_key_actuation_raw.end() && found->second == Raw(op.value);
    }
    case Kind::AllKeyDeadzone:
        return s.global_deadzone && s.global_deadzone->top_raw == Raw(op.value.at("top_mm")) &&
            s.global_deadzone->bottom_raw == Raw(op.value.at("bottom_mm"));
    case Kind::ResetAllDeadzone:
        return s.per_key_deadzone_table_known;
    case Kind::KeyDeadzone: {
        const auto found = s.per_key_deadzone.find(op.key);
        return found != s.per_key_deadzone.end() ?
            found->second.top_raw == Raw(op.value.at("top_mm")) &&
                found->second.bottom_raw == Raw(op.value.at("bottom_mm")) :
            s.per_key_deadzone_table_known && s.global_deadzone &&
                s.global_deadzone->top_raw == Raw(op.value.at("top_mm")) &&
                s.global_deadzone->bottom_raw == Raw(op.value.at("bottom_mm"));
    }
    case Kind::PerKeyRtUnified: case Kind::PerKeyRtPress: case Kind::PerKeyRtRelease:
    case Kind::RapidTriggerOn: case Kind::RapidTriggerOff: {
        const auto found = s.per_key_rapid_trigger.find(op.key);
        if (found == s.per_key_rapid_trigger.end()) return false;
        if (found->second.enabled != op.value.at("enabled").get<bool>()) return false;
        const bool press = found->second.press_known && found->second.press_raw == Raw(op.value.at("press_mm"));
        const bool release = found->second.release_known && found->second.release_raw == Raw(op.value.at("release_mm"));
        return op.kind == Kind::PerKeyRtPress ? press : op.kind == Kind::PerKeyRtRelease ? release : press && release;
    }
    case Kind::DksStandard: case Kind::DksSet: {
        const auto found = s.per_key_dks.find(op.key);
        if (found == s.per_key_dks.end()) return false;
        const auto& dks = found->second;
        if (op.kind == Kind::DksStandard) return dks.standard_runtime_configuration;
        if (dks.standard_runtime_configuration || dks.start_raw != Raw(op.value.at("start_mm")) ||
            dks.end_raw != Raw(op.value.at("end_mm"))) return false;
        for (size_t i = 0; i < 4; ++i) {
            const auto& slot = op.value.at("slots")[i];
            const auto& target = slot.at("target");
            const auto& submitted = dks.slots[i];
            if ((target.at("kind") == "LogicalKey") !=
                    (submitted.target.kind == m605::DksTarget::Kind::LogicalKey) ||
                (target.at("kind") == "LogicalKey" &&
                    submitted.target.logical_key_id != target.at("logical_id").get<uint16_t>()) ||
                submitted.down_start != Trigger(slot.at("down_start")) ||
                submitted.down_end != Trigger(slot.at("down_end")) ||
                submitted.up_start != Trigger(slot.at("up_start")) ||
                submitted.up_end != Trigger(slot.at("up_end"))) return false;
        }
        return true;
    }
    }
    return false;
}

DeviceProfileRuntime::Json DeviceProfileRuntime::ActivateLocked(const std::string& id,
    const std::string& reason, const Json& temporary, const std::function<bool()>& cancelled,
    const std::function<bool()>& admission) {
    const auto activation_start = std::chrono::steady_clock::now();
    const auto timing_before = device_.GetTimingSnapshot();
    double document_ms = 0, planning_ms = 0;
    Json planned_types = Json::object();
    Json executed_type_ms = Json::object();
    size_t planned_count = 0;
    size_t executed_count = 0;
    Json diagnostic_plan = nullptr, diagnostic_target = nullptr, diagnostic_baseline = nullptr;
    const auto profile = std::find_if(document_.at("profiles").begin(), document_.at("profiles").end(),
        [&](const Json& item) { return SameGuid(item.at("id"), id); });
    if (profile == document_.at("profiles").end()) throw ProfileError(404, "Profile not found");
    const Json selected_profile = *profile;
    if (!temporary.is_null()) ValidateMagnetic(temporary);
    const auto canonical_id = selected_profile.at("id").get<std::string>();
    if (!SameGuid(document_.at("selected_profile_id"), canonical_id)) {
        const auto document_start = std::chrono::steady_clock::now();
        auto next = document_;
        next["selected_profile_id"] = canonical_id;
        CommitLocked(std::move(next), document_.at("revision").get<int64_t>());
        document_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - document_start).count();
    }
    // Validation/revision/persistence rejection above is NOT accepted intent.
    // Once durable desired selection is accepted, hold also covers deferred or
    // failed hardware submission. The original outcome/error remains visible.
    if (reason == "Manual") binding_engine_.NotifyManualProfileAction();
    last_reason_ = reason;
    SynchronizeGenerationLocked();
    Json outcomes = Json::array();
    bool resubmitted = false;
    const auto finish = [&](const char* outcome, std::string error) {
        last_outcome_ = {{"outcome", outcome}, {"error", error},
            {"prior_intent_resubmitted", resubmitted}};
        ++mutation_revision_;
        auto result = StateLocked();
        result["outcome"] = outcome;
        result["error"] = error;
        result["operations"] = outcomes;
        result["prior_intent_resubmitted"] = resubmitted;
        const auto after = device_.GetTimingSnapshot();
        const double total_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - activation_start).count();
        const double m605_ms = after.total_ms - timing_before.total_ms;
        result["timing"] = {{"total_ms", total_ms}, {"document_ms", document_ms},
            {"planning_ms", planning_ms}, {"profile_overhead_ms",
                std::max(0.0, total_ms - m605_ms - document_ms)},
            {"planned_operation_count", planned_count}, {"planned_operation_types", planned_types},
            {"executed_operation_count", executed_count},
            {"executed_operation_type_ms", executed_type_ms},
            {"m605_transactions", after.transactions - timing_before.transactions},
            {"m605_total_ms", m605_ms},
            {"queue_wait_ms", after.queue_wait_ms - timing_before.queue_wait_ms},
            {"device_lock_wait_ms", after.device_lock_wait_ms - timing_before.device_lock_wait_ms},
            {"transport_connect_ms", after.connect_ms - timing_before.connect_ms},
            {"safety_latch_ms", after.safety_latch_ms - timing_before.safety_latch_ms},
            {"hid_stage_submit_ms", after.stage_submit_ms - timing_before.stage_submit_ms},
            {"inter_stage_settle_ms", after.inter_stage_settle_ms - timing_before.inter_stage_settle_ms},
            {"pre_apply_settle_ms", after.pre_apply_settle_ms - timing_before.pre_apply_settle_ms},
            {"hid_apply_submit_ms", after.apply_submit_ms - timing_before.apply_submit_ms},
            {"post_apply_settle_ms", after.post_apply_settle_ms - timing_before.post_apply_settle_ms}};
        last_activation_diagnostics_ = {{"reason", reason}, {"outcome", outcome},
            {"error", error}, {"operations", outcomes}, {"plan", diagnostic_plan},
            {"effective_target", diagnostic_target}, {"timing", result.at("timing")},
            {"resolved_baseline_at_planning", diagnostic_baseline},
            {"session_generation", session_generation_}, {"mutation_revision", mutation_revision_},
            {"prior_intent_resubmitted", resubmitted}};
        for (const auto& op : outcomes) if (op.at("succeeded") == false) {
            last_failed_profile_operation_ = op;
            last_failed_profile_operation_["session_generation"] = session_generation_;
            last_failed_profile_operation_["mutation_revision"] = mutation_revision_;
            last_failed_profile_operation_["profile_id"] = canonical_id;
        }
        return result;
    };
    if (!available_()) { InvalidateLocked(); return finish("deferred", "Magnetic device unavailable"); }
    if (device_.GetHealth() != M605RuntimeHealth::Clean || device_.IsPersistentSafetyQuarantined()) {
        InvalidateLocked(); return finish("failed", "M605 runtime unhealthy or safety quarantined");
    }
    // Establish the actual M605 transport epoch before diffing against the
    // previous session. A stale handle may still have a non-null HANDLE.
    if (!device_.PrepareTransportSession()) {
        SynchronizeGenerationLocked();
        InvalidateLocked();
        return finish(device_.GetHealth() == M605RuntimeHealth::Clean ? "deferred" : "failed",
            "M605 transport unavailable before profile transaction: " + device_.GetLastError());
    }
    SynchronizeGenerationLocked();
    const auto planning_start = std::chrono::steady_clock::now();
    const auto host = host_profile_();
    // Record the same trusted fallback used by the planner, using its already
    // loaded HostProfile. Exporting later must not load user configuration again.
    diagnostic_baseline = BaselineWithHost(document_.at("global_defaults"), host);
    auto plan = BuildPlan(selected_profile, temporary, host);
    planning_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - planning_start).count();
    planned_count = plan.operations.size();
    diagnostic_plan = Json::array();
    diagnostic_target = Json::array();
    for (const auto& op : plan.operations) {
        auto entry = op.Summary(false); entry.erase("succeeded"); entry.erase("error");
        entry["value"] = op.value; diagnostic_plan.push_back(std::move(entry));
    }
    for (const auto& [identity, op] : plan.target) {
        auto entry = op.Summary(false); entry.erase("succeeded"); entry.erase("error");
        entry["value"] = op.value; diagnostic_target.push_back(std::move(entry));
    }
    for (const auto& op : plan.operations) {
        const auto kind = op.Summary(true).at("kind").get<std::string>();
        planned_types[kind] = planned_types.value(kind, 0) + 1;
    }
    last_plan_diagnostics_ = {{"profile_id", canonical_id}, {"reason", reason},
        {"session_generation", session_generation_}, {"document_revision", document_.at("revision")},
        {"operation_count", planned_count}, {"operation_kinds", planned_types},
        {"operations", diagnostic_plan}, {"effective_target", diagnostic_target},
        {"actuation_ownership", plan.actuation_ownership}, {"rapid_trigger_management", plan.rapid_trigger_management}, {"preflight_errors", plan.blockers}};
    if (!plan.blockers.empty()) {
        InvalidateLocked();
        return finish("failed", plan.blockers.front());
    }
    // Foreground observations do not wait for this gate. Revalidate once more
    // after persistence/transport/planning, before the first staged write.
    // Once admitted, a transaction/plan completes through existing safety;
    // foreground churn/shutdown never cancels an already staged operation.
    if (admission && !admission()) {
        InvalidateLocked(); return finish("stale", "StaleDecisionBeforeSubmission");
    }
    const auto prior = applied_;
    for (const auto& [identity, op] : plan.target) {
        if (op.kind == Kind::ResetAllActuation) all_key_actuation_footprint_ = true;
        if (op.kind == Kind::AllKeyDeadzone) all_key_deadzone_footprint_ = true;
        if (op.kind == Kind::KeyActuation) actuation_footprint_.insert(op.key);
        if (op.kind == Kind::KeyDeadzone) deadzone_footprint_.insert(op.key);
        if (op.kind == Kind::RapidTriggerOn || op.kind == Kind::RapidTriggerOff)
            rapid_trigger_footprint_.insert(op.key);
        if (op.kind == Kind::DksSet || op.kind == Kind::DksStandard)
            dks_footprint_.insert(op.key);
    }
    for (const auto& op : plan.operations) {
        if (op.kind == Kind::RapidTriggerOn || op.kind == Kind::RapidTriggerOff)
            rapid_trigger_footprint_.insert(op.key);
        if (op.kind == Kind::DksSet || op.kind == Kind::DksStandard)
            dks_footprint_.insert(op.key);
    }
    const auto begin_generation = device_.GetSessionGeneration();
    auto observed_generation = begin_generation;
    dirty_ = true;
    std::vector<Operation> successful;
    const auto failure = [&](std::string error) {
        rt_submission_valid_ = false;
        if (device_.GetHealth() == M605RuntimeHealth::Clean &&
            device_.GetSessionGeneration() == observed_generation) {
            resubmitted = !successful.empty();
            const bool act_changed = std::any_of(successful.begin(), successful.end(),
                [](const Operation& op) { return op.kind == Kind::ResetAllActuation ||
                    op.kind == Kind::KeyActuation; });
            const bool dz_broadcast = std::any_of(successful.begin(), successful.end(),
                [](const Operation& op) { return op.kind == Kind::AllKeyDeadzone; });
            const bool dz_reset = std::any_of(successful.begin(), successful.end(),
                [](const Operation& op) { return op.kind == Kind::ResetAllDeadzone; });
            std::vector<Operation> recovery;
            if (act_changed) {
                // A reset or a newly introduced exception cannot be undone by
                // writing common as a per-key override. Re-establish only
                // known prior host intent, in forward reset -> exceptions order.
                const auto old_base = prior.find(Operation{Kind::ResetAllActuation, 0, nullptr}.Identity());
                if (old_base == prior.end()) resubmitted = false;
                else {
                    recovery.push_back(old_base->second);
                    std::vector<Operation> exceptions;
                    for (const auto& [identity, known] : prior)
                        if (known.kind == Kind::KeyActuation &&
                            known.value != old_base->second.value) exceptions.push_back(known);
                    std::sort(exceptions.begin(), exceptions.end(), [](const Operation& a, const Operation& b) {
                        return a.key < b.key;
                    });
                    recovery.insert(recovery.end(), exceptions.begin(), exceptions.end());
                }
            }
            if (dz_reset) {
                // Reset destroys the old per-key table. Rebuild known prior
                // intent in forward order; a reverse delta cannot restore it.
                const auto old_base = prior.find(Operation{Kind::AllKeyDeadzone, 0, nullptr}.Identity());
                if (old_base == prior.end()) resubmitted = false;
                else {
                    recovery.push_back({Kind::ResetAllDeadzone, 0, old_base->second.value});
                    recovery.push_back(old_base->second);
                    for (const auto& [identity, known] : prior)
                        if (known.kind == Kind::KeyDeadzone &&
                            known.value != old_base->second.value) recovery.push_back(known);
                }
            }
            for (auto it = successful.rbegin(); it != successful.rend(); ++it) {
                if (act_changed && (it->kind == Kind::ResetAllActuation ||
                    it->kind == Kind::KeyActuation)) continue;
                if (dz_reset && (it->kind == Kind::ResetAllDeadzone ||
                    it->kind == Kind::AllKeyDeadzone || it->kind == Kind::KeyDeadzone)) continue;
                if (it->kind == Kind::KeyDeadzone && dz_broadcast) continue;
                const auto old = prior.find(it->Identity());
                if (old == prior.end()) {
                    resubmitted = false;
                    // A broadcast cannot reconstruct values that the host did
                    // not know before it. Re-submit known keys best effort.
                    if (it->kind == Kind::AllKeyDeadzone)
                        for (const auto& [identity, known] : prior)
                            if (known.kind == Kind::KeyDeadzone) recovery.push_back(known);
                    continue;
                }
                recovery.push_back(old->second);
                if (it->kind == Kind::AllKeyDeadzone)
                    for (const auto& [identity, known] : prior)
                        if (known.kind == Kind::KeyDeadzone &&
                            known.value != old->second.value) recovery.push_back(known);
            }
            for (const auto& op : recovery) {
                device_.RefreshTransportPresence();
                if (device_.GetSessionGeneration() != observed_generation) {
                    resubmitted = false; break;
                }
                bool okay = false;
                const auto operation_start = std::chrono::steady_clock::now();
                try { okay = Execute(op, host); } catch (...) { okay = false; }
                const auto kind = op.Summary(true).at("kind").get<std::string>();
                executed_type_ms[kind] = executed_type_ms.value(kind, 0.0) +
                    std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - operation_start).count();
                ++executed_count;
                outcomes.push_back(op.Summary(okay, okay ? "" : device_.GetLastError(), true));
                if (!okay || device_.GetHealth() != M605RuntimeHealth::Clean ||
                    device_.GetSessionGeneration() != observed_generation) {
                    resubmitted = false; break;
                }
            }
        }
        InvalidateLocked();
        return finish("failed", std::move(error));
    };
    for (const auto& op : plan.operations) {
        if (cancelled()) return failure("Apply cancelled; device submission uncertain");
        device_.RefreshTransportPresence();
        if (device_.GetSessionGeneration() != observed_generation)
            return failure("M605 session changed during apply; retry full replay");
        if (device_.GetHealth() != M605RuntimeHealth::Clean)
            return failure("M605 runtime became unhealthy");
        bool okay = false;
        const auto operation_start = std::chrono::steady_clock::now();
        std::string thrown_error;
        try { okay = Execute(op, host); }
        catch (const std::exception& ex) { thrown_error = ex.what(); }
        const auto kind = op.Summary(true).at("kind").get<std::string>();
        executed_type_ms[kind] = executed_type_ms.value(kind, 0.0) +
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - operation_start).count();
        ++executed_count;
        if (!thrown_error.empty()) {
            outcomes.push_back(op.Summary(false, thrown_error));
            return failure(thrown_error);
        }
        const auto current = device_.GetSessionGeneration();
        outcomes.push_back(op.Summary(okay && device_.GetHealth() == M605RuntimeHealth::Clean,
            okay ? "" : device_.GetLastError()));
        if (!okay || device_.GetHealth() != M605RuntimeHealth::Clean)
            return failure("Profile operation failed: " + op.Identity());
        successful.push_back(op);
        if (current != observed_generation) {
            // PrepareTransportSession already established this plan's epoch.
            // Even the first operation cannot adopt a replacement session.
            return failure("M605 session changed during apply; retry full replay");
        }
    }
    if (cancelled()) return failure("Apply cancelled after submission; result unknown");
    device_.RefreshTransportPresence(); // also validates a zero-operation apply
    if (!available_() || device_.GetHealth() != M605RuntimeHealth::Clean ||
        device_.GetSessionGeneration() != observed_generation)
        return failure("Final M605 status or session changed");
    const auto shadow = device_.GetAppliedRuntimeState();
    for (const auto& op : plan.operations)
        if (!Matches(op, shadow)) return failure("SessionApplied shadow differs from " + op.Identity());
    for (const auto& [identity, op] : plan.target)
        if (!Matches(op, shadow)) return failure("SessionApplied shadow differs from " + identity);
    for (auto it = plan.target.begin(); it != plan.target.end();) {
        if (it->second.restore) {
            rapid_trigger_footprint_.erase(it->second.key);
            rt_prior_.erase(it->second.key); rt_prior_source_.erase(it->second.key);
            it = plan.target.erase(it);
        } else ++it;
    }
    applied_ = std::move(plan.target);
    active_ = canonical_id;
    dirty_ = false;
    rt_submission_valid_ = true;
    session_generation_ = observed_generation;
    return finish("succeeded", "");
}

DeviceProfileRuntime::Json DeviceProfileRuntime::HandleMutation(const std::string& action,
    const Json& request) {
    std::lock_guard<std::mutex> gate(mutation_gate_);
    // Binding edits are document-only. Leave hardware/session reconciliation to
    // the existing magnetic/Profile paths; do not probe the transport here.
    if (action != "automation" && action != "migrate-legacy-rt") SynchronizeGenerationLocked();
    if (!load_error_.empty()) throw ProfileError(503, load_error_);
    if (!request.is_object() || !request.contains("expected_revision") ||
        !request.at("expected_revision").is_number_integer() ||
        request.at("expected_revision").get<int64_t>() != document_.at("revision").get<int64_t>())
        throw ProfileError(409, "Profile document revision conflict; reload required");
    Json next = document_;
    Json changed = nullptr;
    bool updated_active = false;
    if (action == "migrate-legacy-rt") {
        MigrateLegacyRtLocked();
        auto result = StateSnapshotLocked(); // cached state only, no transport preflight
        result["profiles"] = document_.at("profiles");
        if (legacy_rt_migration_.value("status", "") == "Failed")
            throw ProfileError(409, "Legacy RT migration failed; original retained; reload required");
        return result;
    } else if (action == "automation") {
        if (!automation_configuration_available_)
            throw ProfileError(409, "Automation configuration unavailable; retained without modification");
        if (!request.contains("device_profile_automation")) throw ProfileError(422, "Device Profile automation configuration required");
        try { next["device_profile_automation"] = DeviceProfileAutomationConfig::NormalizeAndPreserve(
            request.at("device_profile_automation"), document_.value("device_profile_automation", DeviceProfileAutomationConfig::DefaultJson())); }
        catch (const std::exception&) { throw ProfileError(422, "Invalid device Profile automation configuration"); }
    } else if (action == "defaults") {
        if (!request.contains("global_defaults") || !request.at("global_defaults").is_object())
            throw ProfileError(422, "Global defaults required");
        next["global_defaults"] = request.at("global_defaults");
        PreserveMagneticExtensions(next["global_defaults"], document_.at("global_defaults"));
        updated_active = active_.has_value();
    } else if (action == "create") {
        if (!request.contains("name") || !ValidName(request.at("name")))
            throw ProfileError(422, "Invalid Profile name");
        changed = DefaultDocument({}).at("profiles")[0];
        changed["name"] = request.at("name");
        next["profiles"].push_back(changed);
    } else if (action == "duplicate") {
        if (!request.contains("profile_id") || !request.at("profile_id").is_string() ||
            !request.contains("name") || !ValidName(request.at("name")))
            throw ProfileError(422, "Invalid duplicate request");
        const auto id = request.at("profile_id").get<std::string>();
        const auto item = std::find_if(next.at("profiles").begin(), next.at("profiles").end(),
            [&](const Json& profile) { return SameGuid(profile.at("id"), id); });
        if (item == next.at("profiles").end()) throw ProfileError(404, "Profile not found");
        changed = *item;
        changed["id"] = NewGuid();
        changed["name"] = request.at("name");
        next["profiles"].push_back(changed);
    } else if (action == "rename" || action == "update" || action == "delete") {
        if (!request.contains("profile_id") || !request.at("profile_id").is_string())
            throw ProfileError(422, "Profile ID required");
        const auto id = request.at("profile_id").get<std::string>();
        auto item = std::find_if(next.at("profiles").begin(), next.at("profiles").end(),
            [&](const Json& profile) { return SameGuid(profile.at("id"), id); });
        if (item == next.at("profiles").end()) throw ProfileError(404, "Profile not found");
        if (action == "rename") {
            if (!request.contains("name") || !ValidName(request.at("name")))
                throw ProfileError(422, "Invalid Profile name");
            (*item)["name"] = request.at("name");
            changed = *item;
        } else if (action == "update") {
            if (!request.contains("profile") || !request.at("profile").is_object() ||
                !request.at("profile").contains("id") ||
                !SameGuid(request.at("profile").at("id"), id))
                throw ProfileError(422, "Updated Profile ID differs");
            Json replacement = request.at("profile");
            replacement["id"] = item->at("id");
            PreserveProfileExtensions(replacement, *item);
            *item = std::move(replacement);
            changed = *item;
            updated_active = active_ && CanonicalGuid(*active_) == CanonicalGuid(id);
        } else {
            if (SameGuid(next.at("selected_profile_id"), id) ||
                (active_ && CanonicalGuid(*active_) == CanonicalGuid(id)) ||
                next.at("profiles").size() == 1)
                throw ProfileError(409, "Cannot delete selected, active, or last Profile");
            next["profiles"].erase(item);
        }
    } else throw ProfileError(404, "Unknown Profile mutation");
    CommitLocked(std::move(next), request.at("expected_revision").get<int64_t>());
    if (updated_active) InvalidateLocked();
    auto result = action == "automation" ? AutomationSnapshotLocked() : StateLocked();
    result["profile"] = changed;
    return result;
}

void DeviceProfileRuntime::Reply(httplib::Response& response, const Json& body, int code) const {
    response.status = code;
    response.set_header("Cache-Control", "no-store");
    response.set_content(body.dump(), "application/json; charset=utf-8");
}

void DeviceProfileRuntime::RegisterRoutes(httplib::Server& server) {
    const auto query = [this](const httplib::Request& request, httplib::Response& response,
        const std::function<Json()>& run) {
        if (!IsAllowedLoopbackHost(request.get_header_value("Host"))) {
            Reply(response, {{"status", "error"}, {"error", "Loopback host required"}}, 403); return;
        }
        try {
            auto result = run();
            Reply(response, result, result.value("status", std::string{}) == "error" ? 503 : 200);
        }
        catch (const ProfileError& ex) { Reply(response, {{"status", "error"}, {"error", ex.what()}}, ex.code); }
        catch (const std::exception& ex) { Reply(response, {{"status", "error"}, {"error", ex.what()}}, 500); }
    };
    server.Get("/api/device-profiles", [this, query](const httplib::Request& req, httplib::Response& res) {
        query(req, res, [this] {
            std::lock_guard<std::mutex> gate(mutation_gate_);
            if (!load_error_.empty()) throw ProfileError(503, load_error_);
            auto state = StateLocked();
            state["profiles"] = document_.at("profiles");
            state["global_defaults"] = document_.at("global_defaults");
            return state;
        });
    });
    server.Get("/api/device-profiles/runtime", [this, query](const httplib::Request& req, httplib::Response& res) {
        query(req, res, [this] { return State(); });
    });
    server.Get("/api/device-profiles/hardware-rt-gate", [this, query](const httplib::Request& req, httplib::Response& res) {
        // Cached passive input only: no Profile synchronization, probe or connect.
        query(req, res, [this] { return Json{{"status", "ok"}, {"api_version", 1},
            {"hardware_rt_gate", HardwareGateJson(device_.GetHardwareRtGateObservation())}}; });
    });
    server.Get("/api/device-profiles/diagnostics", [this, query](const httplib::Request& req, httplib::Response& res) {
        query(req, res, [this] { return Json{{"status", "ok"}, {"api_version", 1}, {"diagnostics", Diagnostics()}}; });
    });
    server.Get("/api/device-profiles/automation", [this, query](const httplib::Request& req, httplib::Response& res) {
        query(req, res, [this] {
            std::lock_guard<std::mutex> gate(mutation_gate_);
            return AutomationSnapshotLocked();
        });
    });
    server.Get("/api/device-profiles/automation/status", [this, query](const httplib::Request& req, httplib::Response& res) {
        // Pure caches outside the mutation gate: long RT plans must not prevent
        // reading Activating. GET never ticks, probes, connects or writes.
        query(req, res, [this] { return Json{{"status", "ok"}, {"api_version", 1},
            {"automation_decision", AutomationDecisionSnapshotCached()}}; });
    });
    server.Get(R"(/api/device-profiles/([0-9a-fA-F-]+))",
        [this, query](const httplib::Request& req, httplib::Response& res) {
            query(req, res, [this, &req] {
                std::lock_guard<std::mutex> gate(mutation_gate_);
                if (!load_error_.empty()) throw ProfileError(503, load_error_);
                const auto id = req.matches[1].str();
                const auto item = std::find_if(document_.at("profiles").begin(), document_.at("profiles").end(),
                    [&](const Json& profile) { return SameGuid(profile.at("id"), id); });
                if (item == document_.at("profiles").end()) throw ProfileError(404, "Profile not found");
                auto state = StateLocked();
                state["profile"] = *item;
                return state;
            });
        });
    for (const auto* action : {"create", "duplicate", "rename", "update", "delete", "defaults", "activate", "automation", "migrate-legacy-rt"}) {
        server.Post(std::string("/api/device-profiles/") + action,
            [this, action](const httplib::Request& req, httplib::Response& res) {
                const auto api_start = std::chrono::steady_clock::now();
                if (!ValidRequest(req, res)) return;
                try {
                    const auto input = Json::parse(req.body, nullptr, false);
                    if (input.is_discarded()) throw ProfileError(422, "Malformed Profile request");
                    Json result;
                    if (std::string(action) == "activate") {
                        if (!input.is_object() || !input.contains("profile_id") ||
                            !input.at("profile_id").is_string() || !input.contains("reason") ||
                            !input.at("reason").is_string() || !input.contains("expected_revision") ||
                            !input.at("expected_revision").is_number_integer())
                            throw ProfileError(422, "Profile activation requires ID, reason and revision");
                        result = ActivateProfile(input.at("profile_id").get<std::string>(),
                            input.at("reason").get<std::string>(),
                            input.at("expected_revision").get<int64_t>(),
                            input.value("temporary_override", Json(nullptr)),
                            [&req] { return req.is_connection_closed(); });
                    } else result = HandleMutation(action, input);
                    if (std::string(action) == "activate" && result.contains("timing")) {
                        const auto elapsed = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - api_start).count();
                        result["timing"]["api_processing_ms"] = elapsed;
                        std::lock_guard<std::mutex> gate(mutation_gate_);
                        // Another activation may already have completed. Never
                        // attach this request's duration to that newer result.
                        if (last_activation_diagnostics_.is_object() &&
                            last_activation_diagnostics_.at("mutation_revision") == result.at("mutation_revision"))
                            last_activation_diagnostics_["timing"]["api_processing_ms"] = elapsed;
                    }
                    Reply(res, result, result.value("outcome", std::string{}) == "deferred" ? 202 :
                        result.value("outcome", std::string{}) == "failed" ? 409 : 200);
                } catch (const ProfileError& ex) {
                    Reply(res, {{"status", "error"}, {"error", ex.what()}}, ex.code);
                } catch (const std::exception& ex) {
                    Reply(res, {{"status", "error"}, {"error", ex.what()}}, 500);
                }
            });
    }
}

} // namespace aura
