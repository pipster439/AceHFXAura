#include "config/magnetic_control_service.h"
#include "config/device_profile_runtime.h"
#include "config/device_profile_automation_coordinator.h"
#include "aura/hardware/m605_key_mapping.h"
#include "aura/runtime_status.h"
#include "third_party/json.hpp"
#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <thread>
#include <atomic>
#include <condition_variable>

namespace aura {
struct M605RuntimeTestAccess {
    static std::unique_ptr<M605Runtime> Create(std::unique_ptr<m605::detail::Transport> io,
        std::unique_ptr<m605::detail::SettleWait> wait,
        std::unique_ptr<m605::detail::SafetyLatch> latch) {
        return std::unique_ptr<M605Runtime>(new M605Runtime(
            std::move(io), std::move(wait), std::move(latch)));
    }
};
}
namespace {
using Json = nlohmann::json;
struct Io : aura::m605::detail::Transport {
    bool connected = false;
    bool current_session = true;
    int stages = 0, connects = 0;
    mutable int presence_probes = 0;
    int fail_at_stage = -1;
    bool IsConnected() const override { return connected; }
    bool IsCurrentSession() const override { ++presence_probes; return current_session; }
    bool Connect() override { connected = true; current_session = true; ++connects; return true; }
    std::vector<aura::m605::Report> reports;
    bool WriteStage(const aura::m605::Report& report) override {
        reports.push_back(report);
        return ++stages != fail_at_stage;
    }
    bool WriteApply(const aura::m605::Report&) override { return true; }
    void Disconnect() override { connected = false; }
    std::string error = "mock transport failure";
    std::string GetLastError() const override { return error; }
};
struct Wait : aura::m605::detail::SettleWait {
    void WaitBetweenDksStages() override {}
    void WaitBeforeApply() override {}
    void WaitAfterApply() override {
        std::unique_lock<std::mutex> lock(mutex);
        if (!block) return;
        entered = true; cv.notify_all();
        cv.wait(lock, [this] { return released; });
    }
    void Block() { std::lock_guard<std::mutex> lock(mutex); block = true; }
    bool Await() {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, std::chrono::seconds(3), [this] { return entered; });
    }
    void Release() { std::lock_guard<std::mutex> lock(mutex); released = true; cv.notify_all(); }
    std::mutex mutex;
    std::condition_variable cv;
    bool block = false, entered = false, released = false;
};
struct TimedWait : aura::m605::detail::SettleWait {
    void WaitBetweenDksStages() override {}
    void WaitBeforeApply() override { std::this_thread::sleep_for(std::chrono::milliseconds(3)); }
    void WaitAfterApply() override { std::this_thread::sleep_for(std::chrono::milliseconds(4)); }
};
struct LatchState { int arms = 0, fail_at = -1; bool armed = false; };
struct Latch : aura::m605::detail::SafetyLatch {
    explicit Latch(std::shared_ptr<LatchState> state) : state(std::move(state)) {}
    bool IsQuarantined() const override { return state->armed; }
    bool Arm() override {
        ++state->arms;
        if (state->arms == state->fail_at) return false;
        state->armed = true; return true;
    }
    bool Clear() override { state->armed = false; return true; }
    std::shared_ptr<LatchState> state;
};
constexpr const char* A = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr const char* B = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
constexpr const char* C = "cccccccc-cccc-4ccc-8ccc-cccccccccccc";
constexpr const char* D = "dddddddd-dddd-4ddd-8ddd-dddddddddddd";
Json Magnetic(double global, double key) {
    return {{"global_actuation_mm", global}, {"global_deadzone", nullptr},
        {"global_rapid_trigger", nullptr}, {"keys", Json::array({
            {{"logical_id", 0x0402}, {"actuation_mm", key}, {"deadzone", nullptr},
             {"rapid_trigger", nullptr}, {"dks", nullptr}, {"key_extension", "retain"}}
        })}};
}
Json Profile(const char* id, const char* name, double global, double key) {
    return {{"schema_version", 1}, {"id", id}, {"name", name},
        {"magnetic", Magnetic(global, key)},
        {"lighting", {{"legacy_effect_reference", "desktop"},
            {"ownership", "LegacyUnmanaged"}, {"lighting_extension", 7}}},
        {"automation", nullptr}, {"profile_extension", Json{{"future", true}}}};
}
Json Document() {
    return {{"schema_version", 1}, {"revision", 0}, {"selected_profile_id", A},
        {"global_defaults", {{"global_actuation_mm", 1.0}, {"global_deadzone", nullptr},
            {"global_rapid_trigger", nullptr}, {"keys", Json::array()}}},
        {"profiles", Json::array({Profile(A, "Desktop", 1.2, 1.0), Profile(B, "CS2", 1.4, 2.0)})},
        {"root_extension", "retain"}};
}
void Write(const std::filesystem::path& path, const Json& value) {
    std::ofstream f(path, std::ios::binary); f << value.dump(2) << '\n';
}
bool Test() {
    auto folder = std::filesystem::temp_directory_path() /
        ("aura-profile-cutover-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    std::filesystem::create_directories(folder);
    const auto path = folder / "device-profiles.json";
    auto profile_doc = Document();
    profile_doc["global_defaults"]["global_actuation_mm"] = nullptr;
    profile_doc["global_defaults"]["global_deadzone"] = {{"top_mm", 0.0}, {"bottom_mm", 0.1}};
    for (size_t i = 0; i < profile_doc["profiles"].size(); ++i) {
        auto& magnetic = profile_doc["profiles"][i]["magnetic"];
        magnetic["global_actuation_mm"] = nullptr;
        magnetic["global_deadzone"] = {{"top_mm", 0.1 * i}, {"bottom_mm", 0.1 * (i + 1)}};
        magnetic["keys"][0]["actuation_mm"] = nullptr;
        magnetic["keys"][0]["deadzone"] = {{"top_mm", 0.2 + 0.1 * i}, {"bottom_mm", 0.3 + 0.1 * i}};
    }
    Write(path, profile_doc);
    auto status = std::make_shared<aura::RuntimeStatusStore>();
    aura::RuntimeStatusSnapshot snapshot;
    snapshot.active_backend = "native_hid";
    snapshot.hardware_connected = false;
    status->Update(snapshot);
    auto io = std::make_unique<Io>(); auto* transport = io.get();
    auto latch_state = std::make_shared<LatchState>();
    auto runtime = aura::M605RuntimeTestAccess::Create(std::move(io), std::make_unique<Wait>(),
        std::make_unique<Latch>(latch_state));
    aura::MagneticHostProfile host;
    host.active_profile_id = 1;
    host.global_rt_press_raw = 4; host.global_rt_release_raw = 4;
    host.global_deadzone_top_raw = 0; host.global_deadzone_bottom_raw = 1;
    aura::MagneticControlService service(status, [&host] { return host; }, std::move(runtime), path, folder / "config.json");
    httplib::Server server; service.RegisterRoutes(server);
    const int port = server.bind_to_any_port("127.0.0.1");
    if (port <= 0) return false;
    std::thread worker([&] { server.listen_after_bind(); });
    const auto done = [&](bool okay) {
        server.stop(); worker.join(); service.Stop();
        std::error_code error; std::filesystem::remove_all(folder, error);
        return okay;
    };
    httplib::Client client("127.0.0.1", port);
    client.set_read_timeout(4);
    const auto get = [&](const char* route) { return client.Get(route); };
    const auto post = [&](const char* route, const Json& body) {
        return client.Post(route, body.dump(), "application/json");
    };
    const auto initial = get("/api/device-profiles");
    if (!initial || initial->status != 200) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    auto state = Json::parse(initial->body);
    if (state.at("document_revision") != 0 || state.at("profiles").size() != 2 ||
        state.at("profiles")[0].at("profile_extension").at("future") != true) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    const auto baseline_response = get("/api/device-profiles/runtime");
    if (!baseline_response || baseline_response->status != 200 ||
        !Json::parse(baseline_response->body).at("global_defaults").at("global_actuation_mm").is_null() ||
        !Json::parse(baseline_response->body).at("effective_global_defaults").at("global_actuation_mm").is_null())
        return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    // An older document has virtual disabled bindings, without migration writes.
    const auto old_document = Json::parse(std::ifstream(path));
    const auto old_bindings = get("/api/device-profiles/automation");
    if (!old_bindings || old_bindings->status != 200 ||
        Json::parse(old_bindings->body).at("device_profile_automation").at("enabled") != false ||
        Json::parse(std::ifstream(path)) != old_document || old_document.contains("device_profile_automation"))
        return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    auto created = post("/api/device-profiles/create", {{"name", "Offline"}, {"expected_revision", 0}});
    if (!created || created->status != 200) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    auto body = Json::parse(created->body);
    const auto id = body.at("profile").at("id").get<std::string>();
    if (body.at("document_revision") != 1) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    auto conflict = post("/api/device-profiles/rename", {{"profile_id", id},
        {"name", "Stale"}, {"expected_revision", 0}});
    if (!conflict || conflict->status != 409) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    auto renamed = post("/api/device-profiles/rename", {{"profile_id", id},
        {"name", "Offline renamed"}, {"expected_revision", 1}});
    if (!renamed || renamed->status != 200) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    auto duplicate = post("/api/device-profiles/duplicate", {{"profile_id", id},
        {"name", "Copy"}, {"expected_revision", 2}});
    if (!duplicate || duplicate->status != 200) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    const auto copy_id = Json::parse(duplicate->body).at("profile").at("id").get<std::string>();
    auto deleted = post("/api/device-profiles/delete", {{"profile_id", copy_id}, {"expected_revision", 3}});
    if (!deleted || deleted->status != 200) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    auto deferred = post("/api/device-profiles/activate", {{"profile_id", id},
        {"reason", "Manual"}, {"expected_revision", 4}});
    if (!deferred || deferred->status != 202) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    body = Json::parse(deferred->body);
    if (body.at("outcome") != "deferred" || body.at("selected_profile_id") != id ||
        !body.at("active_profile_id").is_null() || body.at("dirty") != true ||
        body.at("document_revision") != 5) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    // Update retains the Phase 0/1 document and extension fields.
    auto a = Json::parse(get((std::string("/api/device-profiles/") + A).c_str())->body).at("profile");
    a["name"] = "Desktop updated";
    a.erase("profile_extension");
    a["lighting"].erase("lighting_extension");
    a["magnetic"]["keys"][0].erase("key_extension");
    auto updated = post("/api/device-profiles/update", {{"profile_id", A},
        {"profile", a}, {"expected_revision", 5}});
    if (!updated || updated->status != 200) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    const auto disk = Json::parse(std::ifstream(path));
    if (disk.at("root_extension") != "retain" ||
        disk.at("profiles")[0].at("profile_extension").at("future") != true ||
        disk.at("profiles")[0].at("magnetic").at("keys")[0].at("key_extension") != "retain" ||
        disk.at("profiles")[0].at("lighting").at("lighting_extension") != 7) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    snapshot.hardware_connected = true;
    status->Update(snapshot);
    auto activate = [&](const char* profile, int64_t revision) {
        // Manual global writes now persist the device baseline and advance the
        // document revision; always activate against the latest daemon state.
        revision = Json::parse(get("/api/device-profiles/runtime")->body)
            .at("document_revision").get<int64_t>();
        return post("/api/device-profiles/activate", {{"profile_id", profile},
            {"reason", "Manual"}, {"expected_revision", revision}});
    };
    auto applied = activate(A, 6);
    if (!applied || applied->status != 200) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    body = Json::parse(applied->body);
    if (body.at("outcome") != "succeeded" || body.at("active_profile_id") != A ||
        body.at("dirty") != false || body.at("m605_session_generation") != 1 ||
        body.at("timing").at("planned_operation_count") != 3 ||
        body.at("timing").at("planned_operation_types").at("AllKeyDeadzone") != 1 ||
        body.at("timing").at("planned_operation_types").at("KeyDeadzone") != 1 ||
        body.at("timing").at("m605_transactions") != 3 ||
        !body.at("timing").contains("api_processing_ms")) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    const int first_stages = transport->stages;
    auto diagnostic_response = get("/api/device-profiles/diagnostics");
    if (!diagnostic_response || diagnostic_response->status != 200 ||
        diagnostic_response->get_header_value("Cache-Control") != "no-store") return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    auto diagnostic = Json::parse(diagnostic_response->body).at("diagnostics");
    if (diagnostic.at("diagnostic_schema_version") != 1 ||
        diagnostic.at("runtime").at("active_profile_id") != A ||
        diagnostic.at("last_activation").at("timing").at("api_processing_ms").is_null() ||
        diagnostic.at("last_activation").at("plan").size() != 3 ||
        diagnostic.at("active_profile_effective_values").is_null() ||
        diagnostic.at("m605").at("session_applied").at("global_deadzone").at("bottom_raw") != 1 ||
        transport->stages != first_stages) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    if (first_stages != 3 || !activate(A, 7) || transport->stages != first_stages)
        return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    // Binding edits and decisions preserve an already clean active submission.
    // Baseline setup above uses explicit MOCK activation only.
    auto binding_config = aura::DeviceProfileAutomationConfig::DefaultJson();
    binding_config["enabled"] = true; binding_config["fallback_profile_id"] = B;
    const auto before_bindings = Json::parse(get("/api/device-profiles/diagnostics")->body).at("diagnostics");
    const int probes_before_bindings = transport->presence_probes;
    const auto binding_saved = post("/api/device-profiles/automation", {
        {"expected_revision", before_bindings.at("runtime").at("document_revision")},
        {"device_profile_automation", binding_config}});
    if (!binding_saved || binding_saved->status != 200) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    service.ObserveDeviceProfileForeground("explorer.exe");
    const auto after_bindings = Json::parse(get("/api/device-profiles/diagnostics")->body).at("diagnostics");
    if (after_bindings.at("runtime").at("active_profile_id") != A ||
        after_bindings.at("runtime").at("selected_profile_id") != A ||
        after_bindings.at("runtime").at("dirty") != false ||
        after_bindings.at("runtime").at("mutation_revision").get<uint64_t>() !=
            before_bindings.at("runtime").at("mutation_revision").get<uint64_t>() + 1 ||
        after_bindings.at("m605") != before_bindings.at("m605") ||
        transport->stages != first_stages || transport->presence_probes != probes_before_bindings)
        return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    auto switched = activate(B, 7);
    if (!switched || switched->status != 200 || transport->stages != first_stages + 3)
        return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    auto back = activate(A, 8);
    if (!back || back->status != 200 || transport->stages != first_stages + 6)
        return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    const auto before_manual = Json::parse(get("/api/device-profiles/runtime")->body).at("mutation_revision").get<uint64_t>();
    auto manual = post("/api/magnetic/global/deadzone", {{"top_mm", 0.2}, {"bottom_mm", 0.3}});
    if (!manual || manual->status != 200) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    manual = post("/api/magnetic/global/rapid-trigger", {{"press_mm", 0.5},
        {"release_mm", 0.5}, {"top_mm", 0.0}, {"bottom_mm", 0.1},
        {"separate_mode", false}});
    if (!manual || manual->status != 422) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    const auto manual_baseline = Json::parse(std::ifstream(path)).at("global_defaults");
    diagnostic = Json::parse(get("/api/device-profiles/diagnostics")->body).at("diagnostics");
    if (diagnostic.at("durable_magnetic_baseline").at("global_deadzone").at("top_mm") != 0.2 ||
        diagnostic.at("runtime").at("dirty") != true ||
        !diagnostic.at("runtime").at("active_profile_id").is_null() ||
        diagnostic.at("runtime").at("selected_profile_id") != A) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    if (!manual_baseline.at("global_actuation_mm").is_null() ||
        manual_baseline.at("global_deadzone").at("top_mm") != 0.2 ||
        !manual_baseline.at("global_rapid_trigger").is_null())
        return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    state = Json::parse(get("/api/device-profiles/runtime")->body);
    if (state.at("selected_profile_id") != A || !state.at("active_profile_id").is_null() ||
        state.at("dirty") != true || state.at("mutation_revision").get<uint64_t>() <= before_manual)
        return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    // Partial apply with clean health: submit prior intent best-effort, but active stays unknown.
    if (!activate(A, 8) || Json::parse(get("/api/device-profiles/runtime")->body).at("dirty") != false)
        return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    latch_state->fail_at = latch_state->arms + 2;
    auto partial = activate(B, 8);
    if (!partial || partial->status != 409) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    body = Json::parse(partial->body);
    if (body.at("outcome") != "failed" || body.at("prior_intent_resubmitted") != true ||
        !body.at("active_profile_id").is_null() || body.at("dirty") != true) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    latch_state->fail_at = -1;
    if (!activate(A, 9)) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    const auto previous_generation = state.at("m605_session_generation").get<uint64_t>();
    transport->connected = false; // mock transport replacement, never physical unplug proof
    manual = post("/api/magnetic/global/deadzone", {{"top_mm", 0.2}, {"bottom_mm", 0.3}});
    if (!manual || manual->status != 200) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    state = Json::parse(get("/api/device-profiles/runtime")->body);
    if (state.at("m605_session_generation").get<uint64_t>() <= previous_generation || !state.at("active_profile_id").is_null())
        return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    auto reapplied = activate(A, 10);
    if (!reapplied || reapplied->status != 200) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    manual = post("/api/magnetic/global/actuation", {{"mm", 1.5}});
    if (!manual || manual->status != 200 ||
        Json::parse(std::ifstream(path)).at("global_defaults").at("global_actuation_mm") != 1.5)
        return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    const auto before_batch = Json::parse(get("/api/device-profiles/runtime")->body).at("mutation_revision").get<uint64_t>();
    transport->fail_at_stage = transport->stages + 2;
    const auto partial_batch = post("/api/magnetic/batch/actuation",
        {{"logical_ids", Json::array({0x0402, 0x0602})}, {"millimeters", 1.5}});
    if (!partial_batch || partial_batch->status != 207) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    body = Json::parse(partial_batch->body);
    if (body.at("batch_result").at("applied_count") != 1) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    state = Json::parse(get("/api/device-profiles/runtime")->body);
    if (!state.at("active_profile_id").is_null() || state.at("dirty") != true ||
        state.at("selected_profile_id") != A ||
        state.at("m605_session_generation").get<uint64_t>() <= previous_generation ||
        state.at("mutation_revision").get<uint64_t>() <= before_batch) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    // The document lock and revision check reject one of two racing writers.
    const int64_t revision = state.at("document_revision").get<int64_t>();
    std::atomic<int> accepted{0}, rejected{0};
    const auto race = [&] {
        httplib::Client other("127.0.0.1", port);
        const auto payload = Json{{"name", "Racing"}, {"expected_revision", revision}}.dump();
        const auto result = other.Post("/api/device-profiles/create", payload, "application/json");
        if (result && result->status == 200) ++accepted;
        if (result && result->status == 409) ++rejected;
    };
    std::thread first(race), second(race);
    first.join(); second.join();
    if (accepted != 1 || rejected != 1) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    auto defaults = Document().at("global_defaults");
    defaults["global_actuation_mm"] = 1.6;
    defaults["future_default"] = "preserved";
    const auto changed_defaults = post("/api/device-profiles/defaults",
        {{"global_defaults", defaults}, {"expected_revision", revision + 1}});
    if (!changed_defaults || changed_defaults->status != 200) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    const auto saved = Json::parse(std::ifstream(path));
    if (saved.at("global_defaults").at("future_default") != "preserved" ||
        saved.at("global_defaults").at("global_actuation_mm") != 1.6 ||
        saved.at("revision") != revision + 2) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    return done(true);
}

bool TestRapidTriggerInheritance() {
    const auto folder = std::filesystem::temp_directory_path() /
        ("aura-profile-rt-inherit-" + std::to_string(GetCurrentProcessId()) + "-" +
            std::to_string(GetTickCount64()));
    std::filesystem::create_directories(folder);
    const auto path = folder / "device-profiles.json";
    auto doc = Document();
    doc["global_defaults"]["global_actuation_mm"] = nullptr;
    doc["profiles"][0]["magnetic"] = {{"global_actuation_mm", nullptr},
        {"global_deadzone", nullptr}, {"global_rapid_trigger", nullptr}, {"keys", Json::array()}};
    doc["profiles"][1]["magnetic"] = {{"global_actuation_mm", nullptr},
        {"global_deadzone", nullptr}, {"global_rapid_trigger", nullptr},
        {"keys", Json::array({{{"logical_id", 0x0701},
            {"rapid_trigger", {{"enabled", true}, {"press_mm", 0.6},
                {"release_mm", 0.6}, {"separate_mode", false}}}}})}};
    Json slots = Json::array();
    for (int i = 0; i < 4; ++i) slots.push_back({{"target", {{"kind", "DefaultSentinel"}}},
        {"down_start", i == 0 ? "Hold" : "Inactive"},
        {"down_end", i == 0 ? "Hold" : "Inactive"},
        {"up_start", i == 0 ? "Release" : "Inactive"}, {"up_end", "Inactive"}});
    auto dks_profile = doc["profiles"][1];
    dks_profile["id"] = C;
    dks_profile["name"] = "DKS";
    dks_profile["magnetic"]["keys"] = Json::array({{{"logical_id", 0x0701},
        {"dks", {{"start_mm", 1.0}, {"end_mm", 3.6}, {"standard", false},
            {"slots", slots}}}}});
    doc["profiles"].push_back(dks_profile);
    Write(path, doc);
    auto io = std::make_unique<Io>(); auto* transport = io.get();
    auto runtime = aura::M605RuntimeTestAccess::Create(std::move(io), std::make_unique<Wait>(),
        std::make_unique<Latch>(std::make_shared<LatchState>()));
    aura::MagneticHostProfile host;
    host.global_rt_press_raw = 4; host.global_rt_release_raw = 4;
    host.per_key_rt_list_known = true;
    std::mutex gate;
    aura::DeviceProfileRuntime profiles(path, {}, *runtime, gate, [&host] { return host; },
        [] { return true; });
    const auto activate = [&](const char* id) {
        return profiles.ActivateProfile(id, "Manual",
            profiles.State().at("document_revision").get<int64_t>());
    };
    runtime->PrepareTransportSession(); profiles.State();
    if (!runtime->RestorePerKeyDksToStandard(0x0701).get() ||
        !runtime->DisablePerKeyRapidTrigger(0x0701,0.4,0.4).get()) return false;
    bool okay = activate(B).at("outcome") == "succeeded";
    const auto enabled = runtime->GetAppliedRuntimeState().per_key_rapid_trigger;
    okay = okay && enabled.at(0x0701).enabled && enabled.at(0x0701).press_raw == 6;
    okay = okay && activate(A).at("outcome") == "succeeded";
    const auto disabled = runtime->GetAppliedRuntimeState().per_key_rapid_trigger;
    okay = okay && !disabled.at(0x0701).enabled && disabled.at(0x0701).press_raw == 4 &&
        disabled.at(0x0701).release_raw == 4;
    const int after_restore = transport->stages;
    okay = okay && activate(A).at("outcome") == "succeeded" &&
        transport->stages == after_restore &&
        std::none_of(transport->reports.begin(), transport->reports.end(), [](const auto& report) {
            return report[2] == 0x53;
        });
    okay = okay && activate(C).at("outcome") == "succeeded" &&
        !runtime->GetAppliedRuntimeState().per_key_dks.at(0x0701).standard_runtime_configuration;
    okay = okay && activate(A).at("outcome") == "succeeded" &&
        runtime->GetAppliedRuntimeState().per_key_dks.at(0x0701).standard_runtime_configuration;
    runtime->Stop();
    std::error_code error; std::filesystem::remove_all(folder, error);
    return okay;
}

bool TestFailedManualGlobalWriteRetainsBaseline() {
    const auto folder = std::filesystem::temp_directory_path() /
        ("aura-profile-failed-global-" + std::to_string(GetCurrentProcessId()) + "-" +
            std::to_string(GetTickCount64()));
    std::filesystem::create_directories(folder);
    const auto path = folder / "device-profiles.json";
    Write(path, Document());
    auto status = std::make_shared<aura::RuntimeStatusStore>();
    aura::RuntimeStatusSnapshot snapshot;
    snapshot.active_backend = "native_hid"; snapshot.hardware_connected = true;
    status->Update(snapshot);
    auto io = std::make_unique<Io>(); io->fail_at_stage = 1;
    auto runtime = aura::M605RuntimeTestAccess::Create(std::move(io), std::make_unique<Wait>(),
        std::make_unique<Latch>(std::make_shared<LatchState>()));
    aura::MagneticControlService service(status, [] { return aura::MagneticHostProfile{}; },
        std::move(runtime), path);
    httplib::Server server; service.RegisterRoutes(server);
    const int port = server.bind_to_any_port("127.0.0.1");
    if (port <= 0) return false;
    std::thread worker([&] { server.listen_after_bind(); });
    httplib::Client client("127.0.0.1", port);
    const auto result = client.Post("/api/magnetic/global/actuation",
        R"({"mm":2.0})", "application/json");
    const auto saved = Json::parse(std::ifstream(path));
    const auto state_response = client.Get("/api/device-profiles/runtime");
    const auto state = state_response ? Json::parse(state_response->body) : Json();
    const bool okay = result && result->status != 200 &&
        saved.at("global_defaults").at("global_actuation_mm") == 1.0 &&
        saved.at("revision") == 0 && state.at("selected_profile_id") == A &&
        state.at("active_profile_id").is_null() && state.at("dirty") == true;
    server.stop(); worker.join(); service.Stop();
    std::error_code error; std::filesystem::remove_all(folder, error);
    return okay;
}

bool TestTimingBreakdown() {
    auto io = std::make_unique<Io>(); auto* transport = io.get();
    auto runtime = aura::M605RuntimeTestAccess::Create(std::move(io),
        std::make_unique<TimedWait>(),
        std::make_unique<Latch>(std::make_shared<LatchState>()));
    const bool submitted = runtime->SetPerKeyActuation(0x0602, 1.0).get();
    const auto timing = runtime->GetTimingSnapshot();
    const bool okay = submitted && transport->connects == 1 &&
        timing.transactions == 1 && timing.pre_apply_settle_ms >= 2.0 &&
        timing.post_apply_settle_ms >= 3.0 &&
        timing.total_ms >= timing.pre_apply_settle_ms + timing.post_apply_settle_ms &&
        timing.stage_submit_ms >= 0 && timing.apply_submit_ms >= 0;
    runtime->Stop();
    return okay;
}

bool TestAllKeyFailure(int failed_stage) {
    const auto folder = std::filesystem::temp_directory_path() /
        ("aura-profile-all-key-failure-" + std::to_string(GetCurrentProcessId()) + "-" +
            std::to_string(GetTickCount64()) + "-" + std::to_string(failed_stage));
    std::filesystem::create_directories(folder);
    const auto path = folder / "device-profiles.json";
    auto doc = Document();
    doc["global_defaults"]["global_actuation_mm"] = nullptr;
    doc["global_defaults"]["global_deadzone"] = {{"top_mm", 0.0}, {"bottom_mm", 0.1}};
    for (auto& profile : doc["profiles"])
        profile["magnetic"] = {{"global_actuation_mm", nullptr},
            {"global_deadzone", {{"top_mm", 0.2}, {"bottom_mm", 0.3}}},
            {"global_rapid_trigger", nullptr}, {"keys", Json::array({
                {{"logical_id", 0x0701}, {"deadzone", {{"top_mm", 0.1}, {"bottom_mm", 0.2}}}}
            })}};
    Write(path, doc);
    auto io = std::make_unique<Io>(); auto* transport = io.get();
    io->fail_at_stage = failed_stage;
    auto runtime = aura::M605RuntimeTestAccess::Create(std::move(io), std::make_unique<Wait>(),
        std::make_unique<Latch>(std::make_shared<LatchState>()));
    std::mutex gate;
    aura::DeviceProfileRuntime profiles(path, {}, *runtime, gate,
        [] { return aura::MagneticHostProfile{}; }, [] { return true; });
    const auto result = profiles.ActivateProfile(B, "Manual", 0);
    const bool okay = result.at("outcome") == "failed" && result.at("dirty") == true &&
        result.at("active_profile_id").is_null() &&
        result.at("timing").at("planned_operation_count") == 3 &&
        result.at("timing").at("m605_transactions") == failed_stage &&
        transport->stages == failed_stage && transport->reports[0][2] == 0x52 &&
        (failed_stage == 1 || transport->reports[1][2] == 0x58) &&
        (failed_stage < 3 || transport->reports[2][2] == 0x59) &&
        Json::parse(std::ifstream(path)).at("global_defaults").at("global_actuation_mm").is_null();
    runtime->Stop();
    std::error_code error; std::filesystem::remove_all(folder, error);
    return okay;
}

bool DirectCase(Json doc, const aura::MagneticHostProfile& host,
    const std::string& expected_error, bool quarantine = false, bool cancel = false) {
    const auto folder = std::filesystem::temp_directory_path() /
        ("aura-profile-case-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    std::filesystem::create_directories(folder);
    const auto path = folder / "device-profiles.json";
    Write(path, doc);
    auto io = std::make_unique<Io>(); auto* transport = io.get();
    auto latch = std::make_shared<LatchState>(); latch->armed = quarantine;
    auto runtime = aura::M605RuntimeTestAccess::Create(std::move(io), std::make_unique<Wait>(),
        std::make_unique<Latch>(latch));
    std::mutex gate;
    aura::DeviceProfileRuntime profiles(path, {}, *runtime, gate, [&host] { return host; }, [] { return true; });
    const auto result = profiles.ActivateProfile(A, "Manual", 0, nullptr, [cancel] { return cancel; });
    const bool okay = result.at("outcome") == "failed" &&
        result.at("error").get<std::string>().find(expected_error) != std::string::npos &&
        result.at("active_profile_id").is_null() && result.at("dirty") == true &&
        transport->stages == 0;
    runtime->Stop();
    std::error_code error; std::filesystem::remove_all(folder, error);
    return okay;
}

bool TestHostBaselineFallback() {
    auto doc = Document();
    doc["global_defaults"]["global_actuation_mm"] = nullptr;
    doc["profiles"][0]["magnetic"]["global_actuation_mm"] = nullptr;
    doc["profiles"][0]["magnetic"]["keys"] = Json::array();
    aura::MagneticHostProfile host; host.global_actuation_raw = 10;
    const auto folder = std::filesystem::temp_directory_path() /
        ("aura-act-host-baseline-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    std::filesystem::create_directories(folder);
    const auto path = folder / "device-profiles.json"; Write(path, doc);
    auto io = std::make_unique<Io>(); auto* transport = io.get();
    auto runtime = aura::M605RuntimeTestAccess::Create(std::move(io), std::make_unique<Wait>(),
        std::make_unique<Latch>(std::make_shared<LatchState>()));
    std::mutex gate;
    aura::DeviceProfileRuntime profiles(path, {}, *runtime, gate, [&host] { return host; }, [] { return true; });
    const auto result = profiles.ActivateProfile(A, "Manual", 0);
    const bool okay = result.at("outcome") == "succeeded" && transport->reports.size() == 1 &&
        transport->reports[0][2] == 0x52 && transport->reports[0][3] == 1 && transport->reports[0][5] == 10 &&
        Json::parse(std::ifstream(path)).at("global_defaults").at("global_actuation_mm").is_null();
    runtime->Stop();
    std::error_code error; std::filesystem::remove_all(folder, error);
    return okay;
}

// Software-only regression for enabled -> disabled and stale enabled submission.
// mode 0: edit an applied Profile; 1: first acquisition with an enabled shadow;
// mode 2: manual/external invalidation after a clean disabled Profile submission;
// mode 3: inconsistent enabled shadow even when cached Profile state is still clean.
bool TestRtDisableRegression(int mode, bool unified) {
    const auto folder = std::filesystem::temp_directory_path() /
        ("aura-rt-disable-" + std::to_string(GetCurrentProcessId()) + "-" +
         std::to_string(GetTickCount64()) + "-" + std::to_string(mode));
    std::filesystem::create_directories(folder);
    const auto path = folder / "device-profiles.json";
    auto doc = Document();
    doc["global_defaults"]["global_actuation_mm"] = nullptr;
    for (auto& profile : doc["profiles"])
        profile["magnetic"] = {{"global_actuation_mm", nullptr}, {"global_deadzone", nullptr},
            {"global_rapid_trigger", nullptr}, {"keys", Json::array()}};
    const double press = 0.5, release = unified ? 0.5 : 1.5;
    doc["profiles"][0]["magnetic"]["keys"] = Json::array({
        {{"logical_id", 0x0701}, {"rapid_trigger", {{"enabled", mode == 0},
            {"press_mm", press}, {"release_mm", release}, {"separate_mode", !unified},
            {"future_rt_extension", "retain"}}}}});
    Write(path, doc);
    auto io = std::make_unique<Io>(); auto* transport = io.get();
    auto latch = std::make_shared<LatchState>();
    auto runtime = aura::M605RuntimeTestAccess::Create(std::move(io), std::make_unique<Wait>(),
        std::make_unique<Latch>(latch));
    // Establish only mock known Standard DKS. No physical device is opened.
    if (!runtime->RestorePerKeyDksToStandard(0x0701).get()) return false;
    if (mode == 1 && !runtime->SetPerKeyRapidTrigger(0x0701, press, release).get()) return false;
    std::mutex gate;
    aura::DeviceProfileRuntime profiles(path, {}, *runtime, gate,
        [] { return aura::MagneticHostProfile{}; }, [] { return true; });
    httplib::Server server; profiles.RegisterRoutes(server);
    const int port = server.bind_to_any_port("127.0.0.1"); if (port <= 0) return false;
    std::thread worker([&] { server.listen_after_bind(); });
    httplib::Client client("127.0.0.1", port);
    const auto done = [&](bool okay) {
        server.stop(); worker.join(); runtime->Stop();
        std::error_code error; std::filesystem::remove_all(folder, error);
        return okay;
    };
    const auto activate = [&] {
        return profiles.ActivateProfile(A, "Manual", profiles.State().at("document_revision"));
    };
    if (mode != 1) {
        if (activate().at("outcome") != "succeeded") return done(false);
        if (mode == 0) {
            if (!runtime->GetAppliedRuntimeState().per_key_rapid_trigger.at(0x0701).enabled)
                return done(false);
            doc["profiles"][0]["magnetic"]["keys"][0]["rapid_trigger"]["enabled"] = false;
            const auto response = client.Post("/api/device-profiles/update",
                Json{{"profile_id", A}, {"profile", doc["profiles"][0]},
                    {"expected_revision", profiles.State().at("document_revision")}}.dump(),
                "application/json");
            if (!response || response->status != 200) return done(false);
        } else {
            // Simulate a known daemon manual writer or deliberately stale Profile cache.
            // This does not model/read the physical switch or a hidden firmware state.
            std::lock_guard<std::mutex> lock(gate);
            if (mode == 2) profiles.ExternalMutationLocked();
            if (!runtime->SetPerKeyRapidTrigger(0x0701, press, release).get()) return done(false);
        }
    }
    if (mode == 3 && profiles.State().at("dirty") != false) return done(false);
    // Every scenario reaches the same mismatch: desired disabled, old shadow enabled.
    const auto old_shadow = runtime->GetAppliedRuntimeState().per_key_rapid_trigger.at(0x0701);
    if (!old_shadow.enabled || old_shadow.press_raw != 5 ||
        old_shadow.release_raw != (unified ? 5 : 15)) return done(false);
    const auto stage_start = transport->reports.size();
    const auto result = activate();
    const auto diagnostics = profiles.Diagnostics();
    const auto& plan = diagnostics.at("last_plan");
    const size_t expected_count = unified ? 1 : 2;
    if (result.at("outcome") != "succeeded" || result.at("dirty") != false ||
        result.at("selected_profile_id") != A || result.at("active_profile_id") != A ||
        plan.at("operation_count") != expected_count ||
        transport->reports.size() != stage_start + expected_count) return done(false);
    const auto& management = plan.at("rapid_trigger_management");
    if (management.at("managed_key_count") != 1 || management.at("enabled_key_count") != 0 ||
        management.at("operation_count") != expected_count) return done(false);
    for (size_t i = 0; i < expected_count; ++i) {
        const auto& operation = plan.at("operations").at(i);
        const auto& report = transport->reports.at(stage_start + i);
        const std::string kind = unified ? "PerKeyRtUnified" : i == 0 ? "PerKeyRtPress" : "PerKeyRtRelease";
        if (operation.at("kind") != kind || operation.at("disable") != true ||
            operation.at("restore") != false || operation.at("value").at("enabled") != false ||
            report[1] != 0x51 || report[2] != 0x54 || report[3] != (unified ? 0 : i + 1) ||
            report[7] != (unified || i == 0 ? 5 : 15) ||
            report[9] != 0 || !std::all_of(report.begin() + 10, report.end(),
                [](uint8_t byte) { return byte == 0; })) return done(false);
    }
    const auto shadow = runtime->GetAppliedRuntimeState().per_key_rapid_trigger;
    if (shadow.size() != 1 || shadow.at(0x0701).enabled || !shadow.at(0x0701).press_known ||
        !shadow.at(0x0701).release_known || shadow.at(0x0701).press_raw != 5 ||
        shadow.at(0x0701).release_raw != (unified ? 5 : 15)) return done(false);
    const auto saved = Json::parse(std::ifstream(path));
    const auto& keys = saved.at("profiles").at(0).at("magnetic").at("keys");
    if (keys.size() != 1 || keys.at(0).at("rapid_trigger").at("enabled") != false ||
        keys.at(0).at("rapid_trigger").at("future_rt_extension") != "retain") return done(false);
    // A no-op is valid only AFTER disable has actually been submitted and shadow updated.
    const auto disabled_stage_count = transport->reports.size();
    if (activate().at("outcome") != "succeeded" || transport->reports.size() != disabled_stage_count ||
        profiles.Diagnostics().at("last_plan").at("operation_count") != 0) return done(false);
    return done(!latch->armed);
}

bool TestExplicitPerKeyRtCutover(bool trusted_prior, bool selector_failure = false) {
    const auto folder=std::filesystem::temp_directory_path() / ("aura-rt-cutover-"+
        std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    std::filesystem::create_directories(folder);
    const auto path=folder / "device-profiles.json";
    auto doc=Document();
    doc["global_defaults"]["global_actuation_mm"]=nullptr;
    for (auto& p:doc["profiles"]) p["magnetic"]={{"global_actuation_mm",nullptr},
        {"global_deadzone",nullptr},{"global_rapid_trigger",nullptr},{"keys",Json::array()}};
    const auto rt=[](bool enabled,double press,double release) {return Json{{"enabled",enabled},
        {"press_mm",press},{"release_mm",release},{"separate_mode",press!=release},{"future_rt",7}};};
    doc["profiles"][0]["magnetic"]["keys"]=Json::array({{{"logical_id",0x0701},{"rapid_trigger",rt(true,0.5,1.5)}}});
    Write(path,doc);
    auto io=std::make_unique<Io>(); auto* transport=io.get();
    auto latch=std::make_shared<LatchState>();
    auto runtime=aura::M605RuntimeTestAccess::Create(std::move(io),std::make_unique<Wait>(),std::make_unique<Latch>(latch));
    runtime->PrepareTransportSession();
    if (!runtime->RestorePerKeyDksToStandard(0x0701).get() ||
        !runtime->RestorePerKeyDksToStandard(0x0602).get()) return false;
    if (trusted_prior && !runtime->DisablePerKeyRapidTrigger(0x0701,0.8,0.6).get()) return false;
    std::mutex gate;
    aura::DeviceProfileRuntime profiles(path,{},*runtime,gate,[]{return aura::MagneticHostProfile{};},[]{return true;});
    httplib::Server server; profiles.RegisterRoutes(server);
    const int port=server.bind_to_any_port("127.0.0.1"); if(port<=0)return false;
    std::thread worker([&]{server.listen_after_bind();}); httplib::Client client("127.0.0.1",port);
    const auto done=[&](bool okay){server.stop();worker.join();runtime->Stop();std::error_code e;
        std::filesystem::remove_all(folder,e);return okay;};
    const auto activate=[&](const char* id=A){return profiles.ActivateProfile(id,"Manual",
        profiles.State().at("document_revision"));};
    const auto update=[&](Json magnetic){auto p=doc["profiles"][0];p["magnetic"]=magnetic;
        auto response=client.Post("/api/device-profiles/update",Json{{"profile_id",A},{"profile",p},
            {"expected_revision",profiles.State().at("document_revision")}}.dump(),"application/json");
        return response && response->status==200;};
    int before=transport->stages;
    if(selector_failure) transport->fail_at_stage=before+2;
    auto first=activate();
    if (trusted_prior && !selector_failure)
        if(const auto* output=std::getenv("AURA_RT_DIAGNOSTIC_TEST_OUTPUT"))Write(output,profiles.Diagnostics());
    if(selector_failure) return done(first.at("outcome")=="failed" && first.at("dirty")==true &&
        first.at("active_profile_id").is_null() && transport->stages==before+2 && latch->armed &&
        runtime->GetAppliedRuntimeState().per_key_rapid_trigger.empty());
    if(first.at("outcome")!="succeeded" || transport->stages!=before+2 ||
        transport->reports[before][3]!=1 || transport->reports[before+1][3]!=2 ||
        transport->reports[before][7]!=5 || transport->reports[before+1][7]!=15)return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    before=transport->stages;
    if(activate().at("outcome")!="succeeded" || transport->stages!=before)return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    auto magnetic=doc["profiles"][0]["magnetic"];
    magnetic["keys"][0]["rapid_trigger"]=rt(true,0.7,1.5);
    if(!update(magnetic) || activate().at("outcome")!="succeeded" ||
        transport->stages!=before+1 || transport->reports.back()[3]!=1)return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    before=transport->stages; magnetic["keys"][0]["rapid_trigger"]=rt(true,0.7,1.2);
    if(!update(magnetic) || activate().at("outcome")!="succeeded" || transport->stages!=before+1 ||
        transport->reports.back()[3]!=2)return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    magnetic["keys"][0]["rapid_trigger"]=rt(false,0.7,1.2); before=transport->stages;
    if(!update(magnetic) || activate().at("outcome")!="succeeded" || transport->stages!=before+2 ||
        transport->reports.back()[9]!=0 || Json::parse(std::ifstream(path))["profiles"][0]["magnetic"]["keys"][0]["rapid_trigger"]["enabled"]!=false)return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    before=transport->stages; if(activate().at("outcome")!="succeeded" || transport->stages!=before)return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    magnetic["keys"][0]["rapid_trigger"]=rt(true,0.7,1.2);
    if(!update(magnetic) || activate().at("outcome")!="succeeded" || transport->stages!=before+2 ||
        transport->reports.back()[9]!=1)return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    before=transport->stages;
    magnetic["keys"].push_back({{"logical_id",0x0602},{"rapid_trigger",rt(true,1,1)}});
    if(!update(magnetic) || activate().at("outcome")!="succeeded" || transport->stages!=before+1 ||
        transport->reports.back()[3]!=0)return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    // New key removal has unknown prior: fail preflight, not disable by default.
    before=transport->stages; magnetic["keys"].erase(1);
    if(!update(magnetic))return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    auto blocked=activate();
    if(blocked.at("outcome")!="failed" || transport->stages!=before ||
        blocked.at("error").get<std::string>().find("RT prior state unknown")==std::string::npos)return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    // Supply a trustworthy durable per-key baseline for A, then release A.
    auto baseline=doc["global_defaults"];
    baseline["keys"]=Json::array({{{"logical_id",0x0602},{"rapid_trigger",rt(false,1,1)}}});
    auto defaults=client.Post("/api/device-profiles/defaults",Json{{"global_defaults",baseline},
        {"expected_revision",profiles.State().at("document_revision")}}.dump(),"application/json");
    if(!defaults || defaults->status!=200 || activate().at("outcome")!="succeeded")return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    before=transport->stages; auto removed=activate(B);
    if(trusted_prior) {
        if(removed.at("outcome")!="succeeded" || runtime->GetAppliedRuntimeState().per_key_rapid_trigger.at(0x0701).enabled ||
            runtime->GetAppliedRuntimeState().per_key_rapid_trigger.at(0x0701).press_raw!=8 ||
            runtime->GetAppliedRuntimeState().per_key_rapid_trigger.at(0x0701).release_raw!=6)return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
        before=transport->stages;
        if(activate(B).at("outcome")!="succeeded" || transport->stages!=before)return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    } else if(removed.at("outcome")!="failed" || transport->stages!=before)return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    const auto diagnostics=profiles.Diagnostics(); const int probes=transport->presence_probes;
    profiles.Diagnostics(); if(transport->presence_probes!=probes)return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    return done(std::none_of(transport->reports.begin(),transport->reports.end(),[](const auto& r){return r[2]==0x53;}) &&
        runtime->GetAppliedRuntimeState().per_key_actuation_raw.empty() &&
        runtime->GetAppliedRuntimeState().per_key_deadzone.empty() &&
        runtime->GetAppliedRuntimeState().per_key_dks.at(0x0701).standard_runtime_configuration);
}

bool TestRtSessionAndSafetyEdges() {
    const auto folder=std::filesystem::temp_directory_path() / ("aura-rt-edges-"+std::to_string(GetTickCount64()));
    std::filesystem::create_directories(folder); const auto path=folder/"device-profiles.json";
    auto doc=Document(); doc["global_defaults"]["global_actuation_mm"]=nullptr;
    for(auto& p:doc["profiles"])p["magnetic"]={{"global_actuation_mm",nullptr},{"global_deadzone",nullptr},
        {"global_rapid_trigger",nullptr},{"keys",Json::array()}};
    Json standard={{"standard",true},{"start_mm",1},{"end_mm",3.6},{"slots",Json::array()}};
    for(int i=0;i<4;++i)standard["slots"].push_back({{"target",{{"kind","DefaultSentinel"}}},
        {"down_start","Inactive"},{"down_end","Inactive"},{"up_start","Inactive"},{"up_end","Inactive"}});
    auto rt=Json{{"enabled",true},{"press_mm",0.5},{"release_mm",1.5},{"separate_mode",true}};
    doc["profiles"][0]["magnetic"]["keys"]=Json::array({{{"logical_id",0x0701},{"rapid_trigger",rt},{"dks",standard}}});
    Write(path,doc);
    auto io=std::make_unique<Io>();auto* transport=io.get(); auto latch=std::make_shared<LatchState>();
    auto runtime=aura::M605RuntimeTestAccess::Create(std::move(io),std::make_unique<Wait>(),std::make_unique<Latch>(latch));
    std::mutex gate; aura::DeviceProfileRuntime profiles(path,{},*runtime,gate,[]{return aura::MagneticHostProfile{};},[]{return true;});
    const auto done=[&](bool okay){runtime->Stop();std::error_code e;std::filesystem::remove_all(folder,e);return okay;};
    const auto apply=[&](const Json& temporary=nullptr,const std::function<bool()>& cancelled=[] {return false;}) {
        return profiles.ActivateProfile(A,"Manual",profiles.State().at("document_revision"),temporary,cancelled);};
    auto first=apply();
    if(first.at("outcome")!="succeeded" || first.at("timing").at("planned_operation_count")!=3)return done(false);
    auto generation=runtime->GetSessionGeneration(); transport->current_session=false;
    auto state=profiles.State(); if(!state.at("active_profile_id").is_null() || state.at("dirty")!=true)return done(false);
    auto replay=apply();
    if(replay.at("outcome")!="succeeded" || runtime->GetSessionGeneration()<=generation ||
        replay.at("timing").at("planned_operation_count")!=3)return done(false);
    // Manual mutation callback invalidates active immediately without changing selected.
    {std::lock_guard<std::mutex> lock(gate); profiles.ExternalMutationLocked();}
    state=profiles.State(); if(state.at("selected_profile_id")!=A || !state.at("active_profile_id").is_null() || !state.at("dirty"))return done(false);
    auto manual_replay=apply();
    if(manual_replay.at("outcome")!="succeeded" || manual_replay.at("timing").at("planned_operation_count")!=2)return done(false);
    // Clean pre-stage latch failure permits known-prior forward re-submit only.
    auto temp=doc["profiles"][0]["magnetic"];temp["keys"][0]["rapid_trigger"]["press_mm"]=0.7;
    temp["keys"][0]["rapid_trigger"]["release_mm"]=1.2;
    latch->fail_at=latch->arms+2;
    auto partial=apply(temp);
    if(partial.at("outcome")!="failed" || partial.at("dirty")!=true || !partial.at("active_profile_id").is_null() ||
        partial.at("prior_intent_resubmitted")!=true || latch->armed ||
        runtime->GetAppliedRuntimeState().per_key_rapid_trigger.at(0x0701).press_raw!=5 ||
        runtime->GetAppliedRuntimeState().per_key_rapid_trigger.at(0x0701).release_raw!=15)return done(false);
    latch->fail_at=-1; if(apply().at("outcome")!="succeeded")return done(false);
    int before=transport->stages;
    auto cancelled=apply(temp,[]{return true;});
    if(cancelled.at("outcome")!="failed" || transport->stages!=before)return done(false);
    // A generation replacement DURING an apply cannot commit active/clean.
    auto changed=apply(temp,[&]{if(transport->stages>before)transport->current_session=false;return false;});
    if(changed.at("outcome")!="failed" || !changed.at("active_profile_id").is_null() || changed.at("dirty")!=true)return done(false);
    return done(true);
}

bool TestGuards() {
    aura::MagneticHostProfile host;
    host.global_rt_press_raw = 4; host.global_rt_release_raw = 4;
    auto invalid_key = Document();
    invalid_key["profiles"] = Json::array({invalid_key["profiles"][0]});
    invalid_key["profiles"][0]["magnetic"]["keys"][0]["logical_id"] = 65535;
    if (!DirectCase(invalid_key, host, "Unsupported logical key")) return false;
    auto conflict = Document();
    conflict["profiles"] = Json::array({conflict["profiles"][0]});
    conflict["global_defaults"]["keys"] = Json::array({
        {{"logical_id", 0x0402}, {"rapid_trigger", {{"enabled", true},
            {"press_mm", 0.4}, {"release_mm", 0.4}, {"separate_mode", false}}}}
    });
    Json slots = Json::array();
    for (int i = 0; i < 4; ++i) slots.push_back({{"target", {{"kind", "DefaultSentinel"},
        {"logical_id", nullptr}}}, {"down_start", "Inactive"}, {"down_end", "Inactive"},
        {"up_start", "Inactive"}, {"up_end", "Inactive"}});
    conflict["profiles"][0]["magnetic"]["keys"][0]["dks"] =
        {{"start_mm", 1.0}, {"end_mm", 3.6}, {"slots", slots}, {"standard", false}};
    if (!DirectCase(conflict, host, "DKS and RT conflict")) return false;
    auto baseline = Document();
    baseline["profiles"] = Json::array({baseline["profiles"][0]});
    baseline["profiles"][0]["magnetic"]["global_rapid_trigger"] =
        {{"enabled", true}, {"press_mm", 0.4}, {"release_mm", 0.4},
            {"separate_mode", false}, {"top_mm", 0.0}, {"bottom_mm", 0.1}};
    baseline["profiles"][0]["magnetic"]["keys"][0]["rapid_trigger"] =
        {{"enabled", false}, {"press_mm", 0.4}, {"release_mm", 0.4}, {"separate_mode", false}};
    aura::MagneticHostProfile unknown;
    if (!DirectCase(baseline, unknown, "Legacy global RT")) return false;
    baseline["profiles"][0]["magnetic"]["global_rapid_trigger"] = nullptr;
    baseline["profiles"][0]["magnetic"]["keys"][0]["rapid_trigger"]["continuous"] = true;
    if (!DirectCase(baseline, unknown, "RT continuous ON is not physically validated")) return false;
    baseline["profiles"][0]["magnetic"]["keys"][0]["rapid_trigger"]["continuous"] = false;
    // Explicitly disabled RT requires its own complete values, not a global baseline.
    if (DirectCase(baseline, unknown, "trusted global RT baseline")) return false;
    auto missing = Document();
    missing["global_defaults"]["global_actuation_mm"] = nullptr;
    missing["profiles"] = Json::array({missing["profiles"][0]});
    if (!DirectCase(missing, unknown, "Device global baseline unknown for actuation")) return false;
    auto normal = Document();
    normal["profiles"] = Json::array({normal["profiles"][0]});
    if (!DirectCase(normal, host, "quarantined", true)) return false;
    normal["global_defaults"]["global_actuation_mm"] = nullptr;
    normal["profiles"][0]["magnetic"] = {{"global_actuation_mm", nullptr},
        {"global_deadzone", { {"top_mm", 0.0}, {"bottom_mm", 0.1} }},
        {"global_rapid_trigger", nullptr}, {"keys", Json::array()}};
    normal["global_defaults"]["global_deadzone"] = { {"top_mm", 0.0}, {"bottom_mm", 0.1} };
    if (!DirectCase(normal, host, "cancelled", false, true)) return false;
    return true;
}

bool TestCorruptDocument() {
    const auto folder = std::filesystem::temp_directory_path() /
        ("aura-profile-corrupt-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    std::filesystem::create_directories(folder);
    const auto path = folder / "device-profiles.json";
    { std::ofstream f(path); f << "{broken"; }
    auto io = std::make_unique<Io>();
    auto latch = std::make_shared<LatchState>();
    auto runtime = aura::M605RuntimeTestAccess::Create(std::move(io), std::make_unique<Wait>(),
        std::make_unique<Latch>(latch));
    std::mutex gate;
    aura::DeviceProfileRuntime profiles(path, {}, *runtime, gate, [] { return aura::MagneticHostProfile{}; },
        [] { return false; });
    const auto state = profiles.State();
    std::ifstream input(path); std::string bytes; std::getline(input, bytes);
    runtime->Stop();
    std::error_code error; std::filesystem::remove_all(folder, error);
    return state.at("status") == "error" && bytes == "{broken";
}

bool TestOfflineDefaultDocument() {
    const auto folder = std::filesystem::temp_directory_path() /
        ("aura-profile-default-" + std::to_string(GetCurrentProcessId()) + "-" +
            std::to_string(GetTickCount64()));
    std::filesystem::create_directories(folder);
    const auto path = folder / "device-profiles.json";
    auto runtime = aura::M605RuntimeTestAccess::Create(std::make_unique<Io>(),
        std::make_unique<Wait>(), std::make_unique<Latch>(std::make_shared<LatchState>()));
    std::mutex gate;
    aura::DeviceProfileRuntime profiles(path, {}, *runtime, gate,
        [] { return aura::MagneticHostProfile{}; }, [] { return false; });
    const auto state = profiles.State();
    const auto disk = Json::parse(std::ifstream(path));
    const auto id = disk.at("selected_profile_id").get<std::string>();
    const bool okay = state.at("status") == "ok" && state.at("dirty") == true &&
        state.at("document_revision") == 0 && disk.at("profiles").size() == 1 &&
        disk.at("profiles")[0].at("name") == "Desktop" &&
        disk.at("profiles")[0].at("id") == id &&
        profiles.ActivateProfile(id, "Startup", 0).at("outcome") == "deferred";
    runtime->Stop();
    std::error_code error; std::filesystem::remove_all(folder, error);
    return okay;
}

bool TestApplyCannotInterleaveManualWrite() {
    const auto folder = std::filesystem::temp_directory_path() /
        ("aura-profile-serialized-" + std::to_string(GetCurrentProcessId()) + "-" +
            std::to_string(GetTickCount64()));
    std::filesystem::create_directories(folder);
    const auto path = folder / "device-profiles.json";
    auto doc = Document();
    doc["global_defaults"]["global_actuation_mm"] = nullptr; doc["profiles"] = Json::array({doc["profiles"][0]});
    doc["global_defaults"]["global_deadzone"] = {{"top_mm", 0.0}, {"bottom_mm", 0.1}};
    doc["profiles"][0]["magnetic"] = {{"global_actuation_mm", nullptr},
        {"global_deadzone", {{"top_mm", 0.2}, {"bottom_mm", 0.3}}},
        {"global_rapid_trigger", nullptr}, {"keys", Json::array()}};
    Write(path, doc);
    auto status = std::make_shared<aura::RuntimeStatusStore>();
    aura::RuntimeStatusSnapshot snapshot;
    snapshot.active_backend = "native_hid"; snapshot.hardware_connected = true;
    status->Update(snapshot);
    auto wait = std::make_unique<Wait>(); auto* waiting = wait.get(); waiting->Block();
    auto latch = std::make_shared<LatchState>();
    auto runtime = aura::M605RuntimeTestAccess::Create(std::make_unique<Io>(), std::move(wait),
        std::make_unique<Latch>(latch));
    aura::MagneticControlService service(status, [] { return aura::MagneticHostProfile{}; },
        std::move(runtime), path);
    httplib::Server server; service.RegisterRoutes(server);
    const int port = server.bind_to_any_port("127.0.0.1");
    if (port <= 0) return false;
    std::thread worker([&] { server.listen_after_bind(); });
    std::atomic<int> activation_code{0};
    std::thread apply([&] {
        httplib::Client client("127.0.0.1", port); client.set_read_timeout(4);
        const auto payload = Json{{"profile_id", A}, {"reason", "Manual"},
            {"expected_revision", 0}}.dump();
        const auto response = client.Post("/api/device-profiles/activate", payload, "application/json");
        activation_code = response ? response->status : 0;
    });
    const bool entered = waiting->Await();
    httplib::Client manual("127.0.0.1", port); manual.set_read_timeout(4);
    bool denied_ok = false;
    if (entered) {
        const auto denied = manual.Post("/api/magnetic/global/actuation",
            R"({"mm":1.5})", "application/json");
        denied_ok = denied && denied->status == 409;
    }
    waiting->Release();
    apply.join();
    const auto state = manual.Get("/api/device-profiles/runtime");
    const bool okay = entered && denied_ok && activation_code == 200 &&
        state && Json::parse(state->body).at("active_profile_id") == A &&
        Json::parse(state->body).at("dirty") == false;
    server.stop(); worker.join(); service.Stop();
    std::error_code error; std::filesystem::remove_all(folder, error);
    return okay;
}

bool TestVersionlessEnvelopeMigration() {
    const auto folder = std::filesystem::temp_directory_path() /
        ("aura-profile-v0-" + std::to_string(GetCurrentProcessId()) + "-" +
            std::to_string(GetTickCount64()));
    std::filesystem::create_directories(folder);
    const auto path = folder / "device-profiles.json";
    auto doc = Document(); doc.erase("schema_version");
    Write(path, doc);
    auto status = std::make_shared<aura::RuntimeStatusStore>();
    auto io = std::make_unique<Io>();
    auto latch = std::make_shared<LatchState>();
    auto runtime = aura::M605RuntimeTestAccess::Create(std::move(io), std::make_unique<Wait>(),
        std::make_unique<Latch>(latch));
    aura::MagneticControlService service(status, [] { return aura::MagneticHostProfile{}; },
        std::move(runtime), path);
    httplib::Server server; service.RegisterRoutes(server);
    const int port = server.bind_to_any_port("127.0.0.1");
    if (port <= 0) return false;
    std::thread worker([&] { server.listen_after_bind(); });
    httplib::Client client("127.0.0.1", port);
    const auto response = client.Post("/api/device-profiles/create",
        R"({"name":"Migrated","expected_revision":0})", "application/json");
    const auto saved = Json::parse(std::ifstream(path));
    const bool okay = response && response->status == 200 && saved.at("schema_version") == 1 &&
        saved.at("revision") == 1 && saved.at("root_extension") == "retain";
    server.stop(); worker.join(); service.Stop();
    std::error_code error; std::filesystem::remove_all(folder, error);
    return okay;
}

bool TestVerifiedActuationInheritance() {
    const auto folder = std::filesystem::temp_directory_path() /
        ("aura-profile-act-reset-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    std::filesystem::create_directories(folder);
    const auto path = folder / "device-profiles.json";
    auto doc = Document();
    doc["profiles"][0]["magnetic"] = {{"global_actuation_mm", nullptr},
        {"global_deadzone", nullptr}, {"global_rapid_trigger", nullptr}, {"keys", Json::array()}};
    doc["profiles"][1]["magnetic"] = doc["profiles"][0]["magnetic"];
    doc["profiles"][1]["magnetic"]["global_actuation_mm"] = 4.0;
    doc["profiles"][1]["magnetic"]["keys"] = Json::array({{{"logical_id", 0x0701}, {"actuation_mm", 0.5}}});
    auto common_only = doc["profiles"][1]; common_only["id"] = C;
    common_only["magnetic"]["keys"] = Json::array();
    auto changed_w = doc["profiles"][1]; changed_w["id"] = D;
    changed_w["magnetic"]["keys"][0]["actuation_mm"] = 0.7;
    const char* E = "eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee";
    const char* F = "ffffffff-ffff-4fff-8fff-ffffffffffff";
    auto multiple = doc["profiles"][1]; multiple["id"] = E;
    multiple["magnetic"]["keys"].push_back({{"logical_id", 0x0602}, {"actuation_mm", 0.8}});
    auto remove_w = multiple; remove_w["id"] = F; remove_w["magnetic"]["keys"].erase(0);
    for (const auto& extra : {common_only, changed_w, multiple, remove_w}) doc["profiles"].push_back(extra);
    Write(path, doc);
    // Independent mock firmware model: all-key does NOT clear overrides.
    struct ActuationIo : Io {
        uint8_t common = 10;
        std::map<uint16_t, uint8_t> overrides{{0x0034, 40}}; // external M, unknown to host
        bool WriteApply(const aura::m605::Report&) override {
            const auto& r = reports.back();
            if (r[2] == 0x52 && r[3] == 1) { common = r[5]; overrides.clear(); }
            else if (r[2] == 0x50) common = r[5];
            else if (r[2] == 0x4f) overrides[r[5] | (r[6] << 8)] = r[7];
            return true;
        }
        uint8_t Effective(uint16_t wire) { return overrides.count(wire) ? overrides.at(wire) : common; }
    };
    auto io = std::make_unique<ActuationIo>(); auto* transport = io.get();
    auto runtime = aura::M605RuntimeTestAccess::Create(std::move(io), std::make_unique<Wait>(),
        std::make_unique<Latch>(std::make_shared<LatchState>()));
    std::mutex gate;
    aura::DeviceProfileRuntime profiles(path, {}, *runtime, gate,
        [] { return aura::MagneticHostProfile{}; }, [] { return true; });
    const auto check_apply = [&](const char* id, size_t count) {
        const auto before = transport->reports.size();
        const auto result = profiles.ActivateProfile(id, "Manual", profiles.State().at("document_revision").get<int64_t>());
        const bool success = result.at("outcome") == "succeeded" && result.at("dirty") == false &&
            result.at("active_profile_id") == id && transport->reports.size() == before + count &&
            result.at("timing").at("planned_operation_count") == count;
        if (!success) std::cerr << "Actuation apply " << id << ": " << result.dump() << '\n';
        return success;
    };
    bool okay = check_apply(A, 1) && transport->Effective(0x0034) == 10 && transport->overrides.empty();
    okay = check_apply(B, 2) && okay && transport->common == 40 && transport->Effective(0x0012) == 5 && transport->Effective(0x0034) == 40;
    okay = check_apply(B, 0) && okay;
    okay = check_apply(D, 1) && okay && transport->reports.back()[2] == 0x4f && transport->Effective(0x0012) == 7;
    okay = check_apply(C, 1) && okay && transport->reports.back()[2] == 0x52 && transport->overrides.empty();
    okay = check_apply(A, 1) && okay && transport->common == 10;
    const size_t before_multiple = transport->reports.size();
    okay = check_apply(E, 3) && okay && transport->reports[before_multiple][2] == 0x52 &&
        transport->reports[before_multiple + 1][5] == 0x1f && transport->reports[before_multiple + 2][5] == 0x12;
    const size_t before_remove = transport->reports.size();
    okay = check_apply(F, 2) && okay && transport->reports[before_remove][2] == 0x52 &&
        transport->reports[before_remove + 1][2] == 0x4f && !transport->overrides.count(0x0012) &&
        transport->Effective(0x0012) == 40 && transport->Effective(0x001f) == 8;
    const auto diagnostic = profiles.Diagnostics();
    if (const auto* output = std::getenv("AURA_PROFILE_DIAGNOSTIC_TEST_OUTPUT")) Write(output, diagnostic);
    okay = okay && diagnostic.at("last_plan").at("actuation_ownership").at("exception_count") == 1 &&
        diagnostic.at("last_plan").at("actuation_ownership").at("reset_reasons") == Json::array({"ExceptionRemoval"}) &&
        diagnostic.at("last_plan").at("operation_kinds").at("ResetAllPerKeyActuationOverrides") == 1 &&
        Json::parse(std::ifstream(path)).at("global_defaults").at("global_actuation_mm") == 1.0;
    const auto prior_revision = profiles.State().at("mutation_revision").get<uint64_t>();
    {
        std::lock_guard<std::mutex> lock(gate);
        okay = runtime->SetPerKeyActuation(0x0405, 4.0).get() && okay;
        profiles.ExternalMutationLocked();
    }
    const auto invalidated = profiles.State();
    okay = okay && invalidated.at("selected_profile_id") == F && invalidated.at("active_profile_id").is_null() &&
        invalidated.at("dirty") == true && invalidated.at("mutation_revision").get<uint64_t>() > prior_revision;
    okay = check_apply(F, 2) && okay && !transport->overrides.count(0x0034);
    const auto generation = runtime->GetSessionGeneration();
    transport->current_session = false;
    const auto removed = profiles.State();
    okay = okay && removed.at("active_profile_id").is_null() && removed.at("dirty") == true &&
        runtime->GetSessionGeneration() > generation && !runtime->GetAppliedRuntimeState().per_key_actuation_table_known;
    okay = check_apply(F, 2) && okay && transport->connects == 2;
    {
        std::lock_guard<std::mutex> lock(gate);
        okay = runtime->SetGlobalActuation(1.5).get() && okay;
        profiles.ExternalMutationLocked();
        profiles.RecordManualGlobalBaselineLocked("global_actuation_mm", 1.5);
    }
    okay = check_apply(C, 1) && okay && transport->common == 40;
    okay = check_apply(A, 1) && okay && transport->common == 15 && transport->overrides.empty() &&
        Json::parse(std::ifstream(path)).at("global_defaults").at("global_actuation_mm") == 1.5;
    // Only the explicit manual global action above may use 51 50.
    okay = okay && std::count_if(transport->reports.begin(), transport->reports.end(),
        [](const auto& r) { return r[2] == 0x50; }) == 1;
    runtime->Stop();
    std::error_code error; std::filesystem::remove_all(folder, error);
    return okay;
}

bool TestActuationPartialFailureAndRecovery(int mode) {
    const auto folder = std::filesystem::temp_directory_path() /
        ("aura-act-partial-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    std::filesystem::create_directories(folder);
    const auto path = folder / "device-profiles.json";
    auto doc = Document();
    doc["profiles"][0]["magnetic"]["keys"] = Json::array();
    doc["profiles"][0]["magnetic"]["global_actuation_mm"] = nullptr;
    doc["profiles"][1]["magnetic"]["global_actuation_mm"] = 4.0;
    doc["profiles"][1]["magnetic"]["keys"][0]["actuation_mm"] = 0.5;
    if (mode == 5) doc["profiles"][0]["magnetic"]["global_actuation_mm"] = 4.0;
    if (mode == 6) doc["profiles"][0]["magnetic"]["keys"] =
        Json::array({{{"logical_id", 0x0402}, {"actuation_mm", 0.6}}});
    Write(path, doc);
    auto io = std::make_unique<Io>(); auto* transport = io.get();
    auto latch = std::make_shared<LatchState>();
    auto runtime = aura::M605RuntimeTestAccess::Create(std::move(io), std::make_unique<Wait>(), std::make_unique<Latch>(latch));
    std::mutex gate;
    aura::DeviceProfileRuntime profiles(path, {}, *runtime, gate,
        [] { return aura::MagneticHostProfile{}; }, [] { return true; });
    bool okay = mode == 4 || profiles.ActivateProfile(A, "Manual", 0).at("outcome") == "succeeded";
    const int before = transport->stages;
    if (mode == 0) transport->fail_at_stage = before + 1;
    if (mode == 1) transport->fail_at_stage = before + 2;
    const auto result = profiles.ActivateProfile(B, "Manual", 0, nullptr, [&] {
        if (mode == 3 && transport->stages > before) transport->current_session = false;
        return (mode == 2 || mode >= 4) && transport->stages > before;
    });
    okay = okay && result.at("outcome") == "failed" && result.at("active_profile_id").is_null() &&
        result.at("dirty") == true && result.at("selected_profile_id") == B &&
        Json::parse(std::ifstream(path)).at("global_defaults").at("global_actuation_mm") == 1.0;
    if (mode < 2) okay = okay && runtime->IsPersistentSafetyQuarantined() &&
        result.at("prior_intent_resubmitted") == false && transport->stages == before + mode + 1;
    if (mode == 2) okay = okay && !runtime->IsPersistentSafetyQuarantined() &&
        result.at("prior_intent_resubmitted") == true && transport->stages == before + 2 &&
        transport->reports.back()[2] == 0x52 && transport->reports.back()[5] == 10 &&
        runtime->GetAppliedRuntimeState().per_key_actuation_raw.empty();
    if (mode == 3) okay = okay && !runtime->IsPersistentSafetyQuarantined() &&
        result.at("prior_intent_resubmitted") == false && transport->stages == before + 1;
    if (mode == 4) okay = okay && result.at("prior_intent_resubmitted") == false &&
        transport->stages == before + 1 && !runtime->IsPersistentSafetyQuarantined();
    if (mode == 5) okay = okay && result.at("prior_intent_resubmitted") == true &&
        transport->stages == before + 2 && transport->reports[before][2] == 0x4f &&
        transport->reports.back()[2] == 0x52 && transport->reports.back()[5] == 40 &&
        runtime->GetAppliedRuntimeState().per_key_actuation_raw.empty();
    if (mode == 6) okay = okay && result.at("prior_intent_resubmitted") == true &&
        transport->stages == before + 3 && transport->reports[before + 1][2] == 0x52 &&
        transport->reports[before + 1][5] == 10 && transport->reports.back()[2] == 0x4f &&
        runtime->GetAppliedRuntimeState().per_key_actuation_raw.at(0x0402) == 6;
    if (!okay) std::cerr << "actuation failure mode " << mode << ": " << result.dump() << '\n';
    runtime->Stop();
    std::error_code error; std::filesystem::remove_all(folder, error);
    return okay;
}

bool TestVerifiedDeadzoneResetOrder() {
    const auto folder = std::filesystem::temp_directory_path() /
        ("aura-profile-dz-reset-" + std::to_string(GetCurrentProcessId()) + "-" +
            std::to_string(GetTickCount64()));
    std::filesystem::create_directories(folder);
    const auto path = folder / "device-profiles.json";
    auto doc = Document();
    doc["global_defaults"]["global_actuation_mm"] = nullptr;
    doc["global_defaults"]["global_deadzone"] = {{"top_mm", 0.0}, {"bottom_mm", 0.1}};
    doc["profiles"][0]["magnetic"] = {{"global_actuation_mm", nullptr},
        {"global_deadzone", nullptr}, {"global_rapid_trigger", nullptr},
        {"keys", Json::array()}};
    doc["profiles"][1]["magnetic"] = {{"global_actuation_mm", nullptr},
        {"global_deadzone", {{"top_mm", 0.2}, {"bottom_mm", 0.3}}},
        {"global_rapid_trigger", nullptr}, {"keys", Json::array({
            {{"logical_id", 0x0701}, {"deadzone", {{"top_mm", 0.1}, {"bottom_mm", 0.2}}}}
        })}};
    auto without_w = doc["profiles"][1];
    without_w["id"] = C; without_w["magnetic"]["keys"] = Json::array();
    doc["profiles"].push_back(without_w);
    auto changed_w = doc["profiles"][1];
    changed_w["id"] = D; changed_w["magnetic"]["keys"][0]["deadzone"]["bottom_mm"] = 0.4;
    doc["profiles"].push_back(changed_w);
    Write(path, doc);
    auto io = std::make_unique<Io>(); auto* transport = io.get();
    auto runtime = aura::M605RuntimeTestAccess::Create(std::move(io), std::make_unique<Wait>(),
        std::make_unique<Latch>(std::make_shared<LatchState>()));
    std::mutex gate;
    aura::DeviceProfileRuntime profiles(path, {}, *runtime, gate,
        [] { return aura::MagneticHostProfile{}; }, [] { return true; });
    const auto activate = [&](const char* id) {
        return profiles.ActivateProfile(id, "Manual",
            profiles.State().at("document_revision").get<int64_t>());
    };
    const auto first = activate(B);
    bool okay = first.at("outcome") == "succeeded" &&
        first.at("timing").at("planned_operation_count") == 3 &&
        transport->reports.size() == 3 &&
        transport->reports[0][2] == 0x52 && transport->reports[0][3] == 4 &&
        transport->reports[1][2] == 0x58 && transport->reports[2][2] == 0x59 &&
        runtime->GetAppliedRuntimeState().per_key_deadzone_table_known;
    const auto repeated = activate(B);
    okay = okay && repeated.at("outcome") == "succeeded" &&
        repeated.at("timing").at("planned_operation_count") == 0;
    const auto changed = activate(D);
    okay = okay && changed.at("outcome") == "succeeded" &&
        changed.at("timing").at("planned_operation_count") == 1 &&
        transport->reports.back()[2] == 0x59;
    const size_t before_reset = transport->reports.size();
    const auto removed = activate(C);
    okay = okay && removed.at("outcome") == "succeeded" &&
        transport->reports.size() == before_reset + 2 &&
        transport->reports[before_reset][2] == 0x52 &&
        transport->reports[before_reset + 1][2] == 0x58 &&
        runtime->GetAppliedRuntimeState().per_key_deadzone.empty();
    const auto inherited = activate(A);
    okay = okay && inherited.at("outcome") == "succeeded" &&
        runtime->GetAppliedRuntimeState().global_deadzone->bottom_raw == 1 &&
        Json::parse(std::ifstream(path)).at("global_defaults").at("global_deadzone").at("bottom_mm") == 0.1;
    const auto generation = runtime->GetSessionGeneration();
    transport->current_session = false;
    const auto removed_session = profiles.State();
    okay = okay && removed_session.at("active_profile_id").is_null() &&
        removed_session.at("dirty") == true && removed_session.at("selected_profile_id") == A &&
        removed_session.at("m605_session_generation").get<uint64_t>() > generation &&
        !runtime->GetAppliedRuntimeState().per_key_deadzone_table_known &&
        !runtime->IsPersistentSafetyQuarantined();
    okay = okay && activate(B).at("outcome") == "succeeded" && transport->connects == 2;
    if (!okay) std::cerr << "deadzone first=" << first.dump() << " repeated=" << repeated.dump()
        << " removed=" << removed.dump() << " inherited=" << inherited.dump()
        << " reports=" << transport->reports.size() << '\n';
    runtime->Stop();
    std::error_code error; std::filesystem::remove_all(folder, error);
    return okay;
}

bool TestRemovalBetweenProfileOperations(bool no_op) {
    const auto folder = std::filesystem::temp_directory_path() /
        ("aura-profile-session-boundary-" + std::to_string(GetCurrentProcessId()) + "-" +
            std::to_string(GetTickCount64()));
    std::filesystem::create_directories(folder);
    const auto path = folder / "device-profiles.json";
    auto doc = Document();
    doc["global_defaults"]["global_actuation_mm"] = nullptr;
    doc["global_defaults"]["global_deadzone"] = {{"top_mm", 0.0}, {"bottom_mm", 0.1}};
    doc["profiles"] = Json::array({doc["profiles"][0]});
    doc["profiles"][0]["magnetic"] = {{"global_actuation_mm", nullptr},
        {"global_deadzone", nullptr}, {"global_rapid_trigger", nullptr}, {"keys", Json::array()}};
    Write(path, doc);
    auto io = std::make_unique<Io>(); auto* transport = io.get();
    auto runtime = aura::M605RuntimeTestAccess::Create(std::move(io), std::make_unique<Wait>(),
        std::make_unique<Latch>(std::make_shared<LatchState>()));
    std::mutex gate;
    aura::DeviceProfileRuntime profiles(path, {}, *runtime, gate,
        [] { return aura::MagneticHostProfile{}; }, [] { return true; });
    bool okay = true;
    if (no_op) okay = profiles.ActivateProfile(A, "Manual", 0).at("outcome") == "succeeded";
    const int before = transport->stages;
    const auto result = profiles.ActivateProfile(A, "Manual", 0, nullptr, [&] {
        // Simulate removal between completed transactions, or after planning
        // the zero-operation path. Cancellation itself is not requested.
        if (no_op || transport->stages > before) transport->current_session = false;
        return false;
    });
    okay = okay && result.at("outcome") == "failed" && result.at("dirty") == true &&
        result.at("active_profile_id").is_null() && result.at("selected_profile_id") == A &&
        transport->stages == before + (no_op ? 0 : 1) &&
        !runtime->IsPersistentSafetyQuarantined();
    // The next activation opens a new session and rebuilds the complete plan.
    okay = okay && profiles.ActivateProfile(A, "Reconnect", 0).at("outcome") == "succeeded";
    runtime->Stop();
    std::error_code error; std::filesystem::remove_all(folder, error);
    return okay;
}
bool TestDiagnosticReadOnly(int mode) {
    const auto folder = std::filesystem::temp_directory_path() /
        ("aura-profile-diagnostics-" + std::to_string(GetCurrentProcessId()) + "-" +
            std::to_string(GetTickCount64()));
    std::filesystem::create_directories(folder);
    const auto path = folder / "device-profiles.json";
    auto document = Document();
    document["global_defaults"]["global_actuation_mm"] = nullptr;
    document["global_defaults"]["global_deadzone"] = {{"top_mm", 0.0}, {"bottom_mm", 0.1}};
    document["global_defaults"]["private_path"] = "C:\\Users\\PRIVATE_SENTINEL\\private.json";
    document["profiles"][0]["name"] = "PRIVATE_SENTINEL";
    document["profiles"][0]["magnetic"] = {{"global_actuation_mm", nullptr},
        {"global_deadzone", nullptr}, {"global_rapid_trigger", nullptr}, {"keys", Json::array()}};
    document["profiles"] = Json::array({document["profiles"][0]});
    if (mode == 2) { std::ofstream f(path); f << "{broken PRIVATE_SENTINEL"; }
    else Write(path, document);
    const auto read = [&] { std::ifstream f(path, std::ios::binary); return std::string(
        std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()); };
    const auto bytes = read();
    auto io = std::make_unique<Io>(); auto* transport = io.get();
    auto latch = std::make_shared<LatchState>();
    if (mode == 1) latch->armed = true;
    auto runtime = aura::M605RuntimeTestAccess::Create(std::move(io), std::make_unique<Wait>(),
        std::make_unique<Latch>(latch));
    std::mutex gate;
    bool available = false;
    int host_reads = 0, availability_reads = 0;
    aura::DeviceProfileRuntime profiles(path, {}, *runtime, gate,
        [&] { ++host_reads; return aura::MagneticHostProfile{}; }, [&] { ++availability_reads; return available; });
    aura::HardwareRtGateObservation observed_gate;
    observed_gate.state = aura::HardwareRtGateState::On; observed_gate.input_collection_connected = true;
    observed_gate.input_session_generation = 1; observed_gate.observation_sequence = 1;
    runtime->ObserveHardwareRtGate(observed_gate);
    auto diagnostic = profiles.Diagnostics();
    if (mode == 1 && (!latch->armed || latch->arms != 0 || transport->stages != 0 ||
        diagnostic.at("m605").at("persistent_safety_quarantine") != true)) return false;
    const auto check_read_only = [&] {
        const int probes = transport->presence_probes, connects = transport->connects, stages = transport->stages;
        const int hosts = host_reads, availability = availability_reads, arms = latch->arms;
        auto before = profiles.Diagnostics();
        auto again = profiles.Diagnostics();
        before.erase("captured_at_utc"); again.erase("captured_at_utc");
        return before == again && read() == bytes && transport->presence_probes == probes &&
            transport->connects == connects && transport->stages == stages && host_reads == hosts &&
            availability_reads == availability && latch->arms == arms &&
            again.dump().find("PRIVATE_SENTINEL") == std::string::npos;
    };
    bool okay = check_read_only() && diagnostic.at("diagnostic_schema_version") == 1 &&
        diagnostic.at("daemon").at("profile_api_version") == 1 &&
        !diagnostic.at("daemon").at("product_version").get<std::string>().empty() &&
        diagnostic.at("m605").at("transport").at("connected_now").is_null() &&
        diagnostic.at("m605").at("transport").at("fresh_session_now").is_null() &&
        diagnostic.at("last_activation").is_null();
    if (mode == 2) {
        okay = okay && diagnostic.at("runtime").at("document_available") == false &&
            diagnostic.at("durable_magnetic_baseline").is_null();
    } else if (mode == 1) {
        const auto failed = profiles.ActivateProfile(A, "Manual", 0); // unavailable -> deferred, no write
        okay = okay && failed.at("outcome") == "deferred" && check_read_only() && latch->armed &&
            profiles.Diagnostics().at("m605").at("persistent_safety_quarantine") == true;
    } else {
        okay = okay && profiles.ActivateProfile(A, "Startup", 0).at("outcome") == "deferred";
        diagnostic = profiles.Diagnostics();
        okay = okay && diagnostic.at("runtime").at("deferred") == true && check_read_only();
        available = true;
        const auto applied = profiles.ActivateProfile(A, "Manual", 0);
        diagnostic = profiles.Diagnostics();
        okay = okay && applied.at("outcome") == "succeeded" && check_read_only() &&
            diagnostic.at("runtime").at("dirty") == false &&
            diagnostic.at("runtime").at("active_intent_current_session") == true &&
            diagnostic.at("m605").at("session_generation") == 1 &&
            diagnostic.at("last_activation").at("timing").at("planned_operation_count") == 2 &&
            diagnostic.at("last_plan").at("operation_kinds").at("ResetAllDeadzone") == 1 &&
            diagnostic.at("m605").at("session_transitions")[0].at("reason") == "transport_opened";
        const int stages = transport->stages;
        okay = okay && profiles.ActivateProfile(A, "Manual", 0).at("outcome") == "succeeded" &&
            transport->stages == stages && profiles.Diagnostics().at("last_plan").at("operation_count") == 0;
        available = false;
        okay = okay && profiles.ActivateProfile(A, "Manual", 0).at("outcome") == "deferred" &&
            profiles.Diagnostics().at("last_activation").at("plan").is_null() &&
            profiles.Diagnostics().at("last_plan").at("operation_count") == 0 && check_read_only();
        available = true;
        okay = okay && profiles.ActivateProfile(A, "Manual", 0).at("outcome") == "succeeded";
        // A stale physical session must not be probed/reopened by the exporter.
        transport->current_session = false;
        auto before = profiles.Diagnostics(); before.erase("captured_at_utc");
        auto after = profiles.Diagnostics(); after.erase("captured_at_utc");
        okay = okay && check_read_only() && after == before;
        runtime->RefreshTransportPresence(); // explicit existing lifecycle path, outside export
        diagnostic = profiles.Diagnostics();
        okay = okay && diagnostic.at("runtime").at("session_invalidation_pending") == true &&
            diagnostic.at("runtime").at("active_intent_current_session") == false &&
            diagnostic.at("m605").at("session_generation") == 2 &&
            diagnostic.at("m605").at("session_applied").at("global_deadzone").is_null();
        profiles.State(); // normal owner notification performs the pending invalidation
        diagnostic = profiles.Diagnostics();
        okay = okay && diagnostic.at("runtime").at("dirty") == true &&
            diagnostic.at("runtime").at("active_profile_id").is_null() &&
            diagnostic.at("runtime").at("selected_profile_id") == A && check_read_only();
        okay = okay && profiles.ActivateProfile(A, "Reconnect", 0).at("outcome") == "succeeded";
        // Force a real mock transaction failure, not a diagnostic side effect.
        transport->fail_at_stage = transport->stages + 1;
        transport->error = "WriteFile 失败，Win32 错误码: 1167 C:\\Users\\PRIVATE_SENTINEL\\secret";
        { std::lock_guard<std::mutex> lock(gate); profiles.ExternalMutationLocked(); }
        const auto failed = profiles.ActivateProfile(A, "Manual", 0);
        diagnostic = profiles.Diagnostics();
        okay = okay && failed.at("outcome") == "failed" && check_read_only() && latch->armed &&
            diagnostic.at("runtime").at("dirty") == true &&
            diagnostic.at("m605").at("persistent_safety_quarantine") == true &&
            diagnostic.at("last_failed_operation").at("win32_error") == 1167 &&
            diagnostic.at("m605").at("last_failed_operation").at("win32_error") == 1167 &&
            diagnostic.at("m605").at("session_transitions").back().at("reason") == "transaction_indeterminate";
    }
    runtime->Stop();
    std::error_code error; std::filesystem::remove_all(folder, error);
    return okay;
}
} // namespace

bool TestUnsupportedAutomationKeepsCoreUsable() {
    const auto folder = std::filesystem::temp_directory_path() /
        ("aura-phase4a5-future-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    std::filesystem::create_directories(folder); const auto path = folder / "device-profiles.json";
    auto doc = Document(); doc["device_profile_automation"] = {{"schema_version", 999}, {"opaque_future", {{"enabled", "new semantics"}}}};
    Write(path, doc);
    auto io = std::make_unique<Io>(); auto* transport = io.get();
    auto latch = std::make_shared<LatchState>();
    auto runtime = aura::M605RuntimeTestAccess::Create(std::move(io), std::make_unique<Wait>(), std::make_unique<Latch>(latch));
    std::mutex gate; int64_t now = 0; int host_reads = 0;
    aura::DeviceProfileRuntime profiles(path, {}, *runtime, gate, [&] { ++host_reads; return aura::MagneticHostProfile{}; },
        [] { return false; }, [&] { return now; });
    httplib::Server server; profiles.RegisterRoutes(server);
    const int port = server.bind_to_any_port("127.0.0.1"); if (port <= 0) return false;
    std::thread worker([&] { server.listen_after_bind(); }); httplib::Client client("127.0.0.1", port);
    const auto done = [&](bool okay) { server.stop(); worker.join(); runtime->Stop();
        std::error_code error; std::filesystem::remove_all(folder, error); return okay; };
    const auto post = [&](const char* route, const Json& body) { return client.Post(route, body.dump(), "application/json"); };
    profiles.ObserveAutomationForeground("cs2.exe"); now = 500; profiles.ObserveAutomationForeground("cs2.exe");
    const auto queried = client.Get("/api/device-profiles/automation");
    const auto diagnostic = profiles.Diagnostics();
    if (!queried || queried->status != 200 || Json::parse(queried->body).at("automation_configuration_available") != false ||
        diagnostic.at("runtime").at("document_available") != true ||
        diagnostic.at("automation").at("decision_reason") != "AutomationConfigurationUnavailable" || host_reads != 0)
        return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    const auto blocked = post("/api/device-profiles/automation", {{"expected_revision", 0},
        {"device_profile_automation", aura::DeviceProfileAutomationConfig::DefaultJson()}});
    if (!blocked || blocked->status != 409 || Json::parse(std::ifstream(path)) != doc) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    const auto created = post("/api/device-profiles/create", {{"expected_revision", 0}, {"name", "Manual still works"}});
    if (!created || created->status != 200) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    const auto id = Json::parse(created->body).at("profile").at("id");
    const auto renamed = post("/api/device-profiles/rename", {{"expected_revision", 1}, {"profile_id", id}, {"name", "Manual renamed"}});
    const auto deferred = post("/api/device-profiles/activate", {{"expected_revision", 2}, {"profile_id", id}, {"reason", "Manual"}});
    const auto deleted = post("/api/device-profiles/delete", {{"expected_revision", 3}, {"profile_id", B}});
    return done(renamed && renamed->status == 200 && deferred && deferred->status == 202 && deleted && deleted->status == 200 &&
        Json::parse(std::ifstream(path)).at("device_profile_automation") == doc.at("device_profile_automation") &&
        transport->stages == 0 && transport->connects == 0 && transport->presence_probes == 0 && latch->arms == 0 && !latch->armed);
}

bool TestManualHoldAndProcessIdentity() {
    const auto folder = std::filesystem::temp_directory_path() /
        ("aura-phase4a5-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    std::filesystem::create_directories(folder);
    const auto path = folder / "device-profiles.json";
    auto doc = Document();
    auto config = aura::DeviceProfileAutomationConfig::DefaultJson();
    config["enabled"] = true; config["fallback_profile_id"] = A;
    doc["device_profile_automation"] = config; Write(path, doc);
    auto io = std::make_unique<Io>(); auto* transport = io.get();
    auto latch = std::make_shared<LatchState>();
    auto runtime = aura::M605RuntimeTestAccess::Create(std::move(io), std::make_unique<Wait>(), std::make_unique<Latch>(latch));
    std::mutex gate; int64_t now = 0; bool available = false;
    const auto identity = aura::DaemonProcessIdentity::NewInstance();
    aura::DeviceProfileRuntime profiles(path, {}, *runtime, gate,
        [] { return aura::MagneticHostProfile{}; }, [&] { return available; }, [&] { return now; }, identity);
    httplib::Server server; profiles.RegisterRoutes(server);
    const int port = server.bind_to_any_port("127.0.0.1"); if (port <= 0) return false;
    std::thread worker([&] { server.listen_after_bind(); });
    httplib::Client client("127.0.0.1", port); client.set_read_timeout(4);
    const auto done = [&](bool okay) {
        server.stop(); worker.join(); runtime->Stop(); std::error_code error;
        std::filesystem::remove_all(folder, error); return okay;
    };
    const auto post = [&](const char* route, const Json& body) { return client.Post(route, body.dump(), "application/json"); };
    const auto diagnostic = [&] { return Json::parse(client.Get("/api/device-profiles/diagnostics")->body).at("diagnostics"); };
    profiles.ObserveAutomationForeground("cs2.exe"); now = 500; profiles.ObserveAutomationForeground("cs2.exe");
    auto before = diagnostic();
    // Already foreground: a saved binding reevaluates immediately, without any
    // Profile activation or input change. Config GET carries same-revision targets.
    config["bindings"] = Json::array({{{"rule_id", "11111111-1111-4111-8111-111111111111"},
        {"enabled", true}, {"process_name", "cs2.exe"}, {"profile_id", B}, {"priority", 100}}});
    const auto edited = post("/api/device-profiles/automation", {{"expected_revision", 0}, {"device_profile_automation", config}});
    if (!edited || edited->status != 200 || Json::parse(edited->body).at("automation_decision").at("resolved_profile_id") != B ||
        Json::parse(edited->body).at("selected_profile_id") != A || transport->stages != 0 || transport->connects != 0)
        return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    const auto renamed = post("/api/device-profiles/rename", {{"expected_revision", 1}, {"profile_id", B}, {"name", "Renamed CS2"}});
    if (!renamed || renamed->status != 200 || diagnostic().at("automation").at("resolved_profile_id") != B) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    const auto saved_profile = post("/api/device-profiles/update", {{"expected_revision", 2}, {"profile_id", A}, {"profile", doc.at("profiles")[0]}});
    if (!saved_profile || saved_profile->status != 200 || diagnostic().at("automation").at("manual_action_sequence") != 0) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    const auto rejected = post("/api/device-profiles/activate", {{"expected_revision", 0}, {"profile_id", A}, {"reason", "Manual"}});
    const auto missing = post("/api/device-profiles/activate", {{"expected_revision", 3}, {"profile_id", C}, {"reason", "Manual"}});
    if (!rejected || rejected->status != 409 || !missing || missing->status != 404 ||
        diagnostic().at("automation").at("manual_action_sequence") != 0) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    for (const auto* reason : {"Startup", "Reconnect", "Restore", "Automation"}) {
        // Explicit mock API calls test reason discrimination, not an evaluator callback.
        auto result = post("/api/device-profiles/activate", {{"expected_revision", 3}, {"profile_id", A}, {"reason", reason}});
        if (!result || result->status != 202 || diagnostic().at("automation").at("manual_action_sequence") != 0) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    }
    const auto deferred = post("/api/device-profiles/activate", {{"expected_revision", 3}, {"profile_id", A}, {"reason", "Manual"}});
    if (!deferred || deferred->status != 202 || Json::parse(deferred->body).at("outcome") != "deferred" ||
        diagnostic().at("automation").at("manual_action_sequence") != 1 ||
        diagnostic().at("automation").at("decision_reason") != "ManualHold") return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    profiles.ObserveAutomationForeground("CS2.EXE");
    if (diagnostic().at("automation").at("decision_reason") != "ManualHold") return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    profiles.ObserveAutomationForeground("explorer.exe"); now += 500; profiles.ObserveAutomationForeground("explorer.exe");
    if (diagnostic().at("automation").at("manual_hold") != false) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    profiles.ObserveAutomationForeground("cs2.exe"); now += 500; profiles.ObserveAutomationForeground("cs2.exe");
    if (diagnostic().at("automation").at("resolved_profile_id") != B) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    // Selection accepted but unsafe submission refused: retain hold AND failure.
    available = true; latch->armed = true;
    auto failed = post("/api/device-profiles/activate", {{"expected_revision", 3}, {"profile_id", B}, {"reason", "Manual"}});
    if (!failed || failed->status != 409 || Json::parse(failed->body).at("outcome") != "failed" ||
        Json::parse(failed->body).at("selected_profile_id") != B || diagnostic().at("automation").at("manual_action_sequence") != 2 ||
        diagnostic().at("runtime").at("dirty") != true || diagnostic().at("automation").at("decision_reason") != "ManualHold")
        return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    const auto one = diagnostic(); const auto two = diagnostic();
    aura::DeviceProfileRuntime restarted(path, {}, *runtime, gate,
        [] { return aura::MagneticHostProfile{}; }, [] { return false; }, [&] { return now; }, aura::DaemonProcessIdentity::NewInstance());
    auto fresh = restarted.Diagnostics();
    return done(one.at("daemon") == two.at("daemon") && one.at("daemon").at("process_instance_id") == identity.process_instance_id &&
        one.at("daemon").at("process_instance_id") != fresh.at("daemon").at("process_instance_id") &&
        !one.at("daemon").at("started_at_utc").get<std::string>().empty() &&
        fresh.at("automation").at("manual_hold") == false &&
        aura::DaemonProcessIdentity::Current().process_instance_id == aura::DaemonProcessIdentity::Current().process_instance_id &&
        transport->stages == 0 && transport->connects == 0 && transport->presence_probes == 0 && latch->armed && latch->arms == 0);
}

bool TestDeviceAutomationDocumentVersions() {
    for (int mode = 0; mode < 3; ++mode) {
        const auto folder = std::filesystem::temp_directory_path() /
            ("aura-phase4a-version-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(mode));
        std::filesystem::create_directories(folder);
        const auto path = folder / "device-profiles.json";
        auto doc = Document();
        if (mode != 0) {
            doc["device_profile_automation"] = aura::DeviceProfileAutomationConfig::DefaultJson();
            doc["device_profile_automation"]["future_extension"] = {{"retain", true}};
            if (mode == 2) doc["device_profile_automation"]["schema_version"] = 2;
        }
        Write(path, doc);
        const auto bytes = [&] { std::ifstream input(path, std::ios::binary); return std::string(
            std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()); };
        const auto original = bytes();
        auto io = std::make_unique<Io>(); auto* transport = io.get();
        auto latch = std::make_shared<LatchState>();
        auto runtime = aura::M605RuntimeTestAccess::Create(std::move(io), std::make_unique<Wait>(), std::make_unique<Latch>(latch));
        std::mutex gate; int64_t now = 0;
        aura::DeviceProfileRuntime profiles(path, {}, *runtime, gate,
            [] { return aura::MagneticHostProfile{}; }, [] { return false; }, [&] { return now; });
        profiles.ObserveAutomationForeground("cs2.exe"); now = 500;
        profiles.ObserveAutomationForeground("cs2.exe");
        const auto diagnostic = profiles.Diagnostics();
        // A newer Automation subsection remains opaque while manual core CRUD
        // and accepted deferred selection still work and preserve the section.
        bool core_available = diagnostic.at("runtime").at("document_available") == true;
        if (mode == 2) {
            const auto deferred = profiles.ActivateProfile(B, "Manual", 0);
            const auto disk = Json::parse(std::ifstream(path));
            core_available = core_available && deferred.at("outcome") == "deferred" &&
                disk.at("device_profile_automation") == doc.at("device_profile_automation");
            Write(path, doc); // fixture restoration, not production migration
        }
        const bool okay = core_available && bytes() == original && transport->stages == 0 && transport->connects == 0 &&
            transport->presence_probes == 0 && latch->arms == 0 &&
            diagnostic.at("automation").at("decision_reason") == (mode == 2 ? "AutomationConfigurationUnavailable" : "AutomationDisabled") &&
            diagnostic.at("automation").at("hardware_activation_allowed") == false;
        runtime->Stop(); std::error_code error; std::filesystem::remove_all(folder, error);
        if (!okay) return false;
    }
    return true;
}

bool TestDeviceAutomationDecisionIsolation() {
    const auto folder = std::filesystem::temp_directory_path() /
        ("aura-phase4a-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    std::filesystem::create_directories(folder);
    const auto path = folder / "device-profiles.json";
    const auto legacy = folder / "config.json";
    { std::ofstream file(legacy); file << R"({"profiles":{"cs2":{"lighting":"retain"}},"automation":{"activate_profile":"lighting-only"}})"; }
    auto document = Document();
    auto config = aura::DeviceProfileAutomationConfig::DefaultJson();
    config["enabled"] = true; config["fallback_profile_id"] = A;
    const char* rule_id = "11111111-1111-4111-8111-111111111111";
    config["bindings"] = Json::array({{{"rule_id", rule_id}, {"enabled", true},
        {"process_name", "CS2.EXE"}, {"profile_id", B}, {"priority", 100},
        {"private_extension", "PRIVATE_SENTINEL"}}});
    config["private_root"] = "PRIVATE_SENTINEL";
    document["device_profile_automation"] = config;
    Write(path, document);
    const auto bytes = [](const auto& file) { std::ifstream input(file, std::ios::binary); return std::string(
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()); };
    auto original = bytes(path); const auto legacy_original = bytes(legacy);
    auto io = std::make_unique<Io>(); auto* transport = io.get();
    auto latch = std::make_shared<LatchState>(); latch->armed = true; // cannot be cleared by evaluator/export
    auto runtime = aura::M605RuntimeTestAccess::Create(std::move(io), std::make_unique<Wait>(), std::make_unique<Latch>(latch));
    std::mutex gate; int64_t now = 0; int host_reads = 0, available_reads = 0;
    aura::DeviceProfileRuntime profiles(path, {}, *runtime, gate,
        [&] { ++host_reads; return aura::MagneticHostProfile{}; }, [&] { ++available_reads; return false; }, [&] { return now; });
    httplib::Server server; profiles.RegisterRoutes(server);
    const int port = server.bind_to_any_port("127.0.0.1"); if (port <= 0) return false;
    std::thread worker([&] { server.listen_after_bind(); });
    httplib::Client client("127.0.0.1", port); client.set_read_timeout(4);
    const auto done = [&](bool okay) {
        server.stop(); worker.join(); runtime->Stop();
        std::error_code error; std::filesystem::remove_all(folder, error); return okay;
    };
    const auto get = [&] {
        const auto response = client.Get("/api/device-profiles/diagnostics");
        if (!response || response->status != 200) return Json();
        return Json::parse(response->body).at("diagnostics");
    };
    const auto before = get();
    if (before.is_null()) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    profiles.ObserveAutomationForeground("C:\\Users\\PRIVATE_SENTINEL\\Games\\CS2.EXE");
    now = 499;
    const auto pending = get(); // GET must not advance/evaluate despite clock/time elapsed
    if (pending.at("automation").at("decision_sequence") != 0) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    now = 800; const auto still_pending = get();
    if (still_pending.at("automation").at("decision_sequence") != 0 ||
        still_pending.at("automation").at("debounce_pending") != true) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    if (!profiles.ObserveAutomationForeground("CS2.EXE")) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    const auto decision = get();
    if (decision.at("automation").at("resolved_profile_id") != B ||
        decision.at("automation").at("decision_kind") != "Match" ||
        decision.at("automation").at("hardware_activation_allowed") != false ||
        decision.at("runtime") != before.at("runtime") ||
        decision.at("m605") != before.at("m605") || decision.at("last_activation") != before.at("last_activation") ||
        bytes(path) != original || bytes(legacy) != legacy_original ||
        decision.dump().find("PRIVATE_SENTINEL") != std::string::npos ||
        host_reads != 0 || available_reads != 0 || !latch->armed || latch->arms != 0 ||
        transport->stages != 0 || transport->connects != 0 || transport->presence_probes != 0)
        return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    const auto sequence = decision.at("automation").at("decision_sequence");
    profiles.ObserveAutomationForeground("cs2.exe");
    if (get().at("automation").at("decision_sequence") != sequence) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    // Explicit binding mutation is the ONLY durable write here. It leaves
    // selection, active/dirty, quarantine, and the legacy lighting file alone.
    auto replacement = config;
    replacement.erase("private_root"); replacement["bindings"][0].erase("private_extension");
    replacement["bindings"][0]["priority"] = 101;
    const auto payload = Json{{"expected_revision", 0}, {"device_profile_automation", replacement}}.dump();
    const auto saved = client.Post("/api/device-profiles/automation", payload, "application/json");
    if (!saved || saved->status != 200) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    const auto state = Json::parse(saved->body);
    const auto stored = Json::parse(std::ifstream(path));
    if (state.at("document_revision") != 1 || state.at("selected_profile_id") != A ||
        !state.at("active_profile_id").is_null() || state.at("dirty") != true ||
        stored.at("device_profile_automation").at("private_root") != "PRIVATE_SENTINEL" ||
        stored.at("device_profile_automation").at("bindings")[0].at("private_extension") != "PRIVATE_SENTINEL" ||
        stored.at("device_profile_automation").at("bindings")[0].at("process_name") != "cs2.exe") return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    const auto conflict = client.Post("/api/device-profiles/automation", payload, "application/json");
    if (!conflict || conflict->status != 409) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    replacement["bindings"][0]["process_name"] = "C:\\secret\\cs2.exe";
    const auto invalid = client.Post("/api/device-profiles/automation",
        Json{{"expected_revision", 1}, {"device_profile_automation", replacement}}.dump(), "application/json");
    if (!invalid || invalid->status != 422 || Json::parse(std::ifstream(path)) != stored) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    // Deleting a bound but unselected/unapplied Profile produces InvalidDecision;
    // the missing highest-priority target is NOT silently remapped to fallback.
    const auto removed = client.Post("/api/device-profiles/delete",
        Json{{"expected_revision", 1}, {"profile_id", B}}.dump(), "application/json");
    if (!removed || removed->status != 200) return done((std::cerr << "Failure line " << __LINE__ << '\n', false));
    profiles.ObserveAutomationForeground("cs2.exe");
    const auto invalid_target = get();
    return done(invalid_target.at("automation").at("decision_kind") == "InvalidDecision" &&
        invalid_target.at("automation").at("decision_reason") == "InvalidTargetProfile" &&
        invalid_target.at("runtime").at("selected_profile_id") == A &&
        transport->stages == 0 && transport->connects == 0 && transport->presence_probes == 0 &&
        latch->arms == 0 && latch->armed && available_reads == 0 && bytes(legacy) == legacy_original);
}

bool TestHardwareRtGateIsolation(bool enabled) {
    const auto folder = std::filesystem::temp_directory_path() / ("aura-rt-gate-" +
        std::to_string(GetCurrentProcessId()) + (enabled ? "-enabled" : "-disabled"));
    std::filesystem::create_directories(folder);
    const auto path = folder / "device-profiles.json";
    auto doc = Document(); doc["global_defaults"]["global_actuation_mm"] = nullptr;
    for (auto& p : doc["profiles"]) p["magnetic"] = {{"global_actuation_mm", nullptr},
        {"global_deadzone", nullptr}, {"global_rapid_trigger", nullptr}, {"keys", Json::array()}};
    doc["profiles"][0]["magnetic"]["keys"] = Json::array({{{"logical_id", 0x0701},
        {"rapid_trigger", {{"enabled", enabled}, {"press_mm", 0.5}, {"release_mm", 1.5}, {"separate_mode", true}}}}});
    Write(path, doc);
    auto io = std::make_unique<Io>(); auto* transport = io.get();
    auto latch = std::make_shared<LatchState>();
    auto runtime = aura::M605RuntimeTestAccess::Create(std::move(io), std::make_unique<Wait>(), std::make_unique<Latch>(latch));
    runtime->RestorePerKeyDksToStandard(0x0701).get();
    std::mutex gate; int host_calls = 0, availability_calls = 0;
    aura::DeviceProfileRuntime profiles(path, {}, *runtime, gate,
        [&] { ++host_calls; return aura::MagneticHostProfile{}; }, [&] { ++availability_calls; return true; });
    // Hardware OFF does not prevent legal explicit per-key configuration.
    aura::HardwareRtGateObservation observed; observed.state = aura::HardwareRtGateState::Off;
    observed.input_collection_connected = true; observed.input_session_generation = 1;
    observed.observation_sequence = 1; observed.unavailable_reason = ""; observed.observed_at_utc = "2026-09-30T21:52:30Z";
    runtime->ObserveHardwareRtGate(observed);
    bool okay = profiles.ActivateProfile(A, "Manual", 0).at("outcome") == "succeeded";
    const auto before = profiles.Diagnostics();
    const auto original = Json::parse(std::ifstream(path));
    const auto shadow = before.at("m605").at("session_applied");
    const auto host_count = host_calls, available_count = availability_calls;
    const auto writes = transport->stages, connects = transport->connects, probes = transport->presence_probes;
    const auto latch_arms = latch->arms;
    httplib::Server server; profiles.RegisterRoutes(server);
    const int port = server.bind_to_any_port("127.0.0.1"); if (port <= 0) return false;
    std::thread http_thread([&] { server.listen_after_bind(); });
    httplib::Client client("127.0.0.1", port);
    for (auto state : {aura::HardwareRtGateState::On, aura::HardwareRtGateState::Off, aura::HardwareRtGateState::Unknown}) {
        observed.state = state; ++observed.observation_sequence;
        runtime->ObserveHardwareRtGate(observed);
        const auto reply = client.Get("/api/device-profiles/hardware-rt-gate");
        okay &= reply && reply->status == 200 && Json::parse(reply->body).at("hardware_rt_gate").at("state") == aura::HardwareRtGateName(state);
        const auto export_reply = client.Get("/api/device-profiles/diagnostics");
        okay &= export_reply && export_reply->status == 200;
        const auto after = profiles.Diagnostics();
        const auto effective = after.at("rapid_trigger").at("effective_rt_active");
        okay &= after.at("runtime") == before.at("runtime") &&
            after.at("m605").at("session_applied") == shadow && after.at("last_activation") == before.at("last_activation") &&
            after.at("hardware_rt_gate").at("state") == aura::HardwareRtGateName(state) &&
            after.at("rapid_trigger").at("configured_rt_key_count") == 1 &&
            after.at("rapid_trigger").at("enabled_rt_key_count") == (enabled ? 1 : 0);
        okay &= enabled && state == aura::HardwareRtGateState::Unknown ? effective.is_null() :
            effective == (enabled && state == aura::HardwareRtGateState::On);
    }
    server.stop(); http_thread.join();
    okay &= host_calls == host_count && availability_calls == available_count && transport->stages == writes &&
        transport->connects == connects && transport->presence_probes == probes && latch->arms == latch_arms &&
        !latch->armed && Json::parse(std::ifstream(path)) == original;
    okay &= profiles.State().at("dirty") == false && profiles.State().at("active_profile_id") == A &&
        runtime->GetAppliedRuntimeState().per_key_rapid_trigger.at(0x0701).enabled == enabled;
    const auto repeat = profiles.ActivateProfile(A, "Manual", profiles.State().at("document_revision"));
    okay &= repeat.at("outcome") == "succeeded" && transport->stages == writes;
    runtime->Stop(); std::error_code error; std::filesystem::remove_all(folder, error);
    return okay;
}

#include "test_phase4b_runtime.inc"
#include "test_legacy_rt_migration.inc"
int main() {
    const std::pair<const char*, bool(*)()> cases[] = {{"cutover", Test},
        {"legacy_rt_conversion", TestLegacyMigrationConversion},
        {"legacy_rt_idempotence_absent", TestLegacyMigrationIdempotence},
        {"legacy_rt_explicit_disabled_unmanaged", TestLegacyMigrationExplicitPrecedence},
        {"legacy_rt_root_precedence", TestLegacyMigrationRootPrecedence},
        {"legacy_rt_empty_no_rt_activation", TestLegacyMigrationEmpty},
        {"legacy_rt_backup_failure", []{return TestLegacyMigrationFailure(1);}},
        {"legacy_rt_write_failure", []{return TestLegacyMigrationFailure(2);}},
        {"legacy_rt_unsupported", TestLegacyMigrationUnsupported},
        {"legacy_rt_bounds_dks_archive", TestLegacyMigrationBoundaryAndCollision},
        {"legacy_rt_new_writer_gate", TestLegacyMigrationApiGate},
        {"legacy_rt_explicit_activation", TestLegacyMigrationExplicitActivation},
        {"legacy_rt_p4b_integration", TestLegacyMigrationP4BIntegration},
        {"phase4b_apply_no_duplicate", TestPhase4BApplyAndNoDuplicate},
        {"phase4b_deferred_safety", TestPhase4BDeferredAndSafety},
        {"phase4b_already_active_noop", TestPhase4BAlreadyActiveNoOp},
        {"phase4b_stale_churn_return", TestPhase4BStaleChurnReturn},
        {"phase4b_manual_hold_race", TestPhase4BManualHoldRace},
        {"phase4b_concurrent_manual_wins", TestPhase4BConcurrentManualWins},
        {"phase4b_document_races", TestPhase4BDocumentRaces},
        {"phase4b_disk_revision_conflict", TestPhase4BOnDiskRevisionConflict},
        {"phase4b_stale_shutdown", TestPhase4BStaleAndShutdown},
        {"phase4b_rt_hardware_gate", TestPhase4BRtHardwareGate},
        {"hardware_gate_enabled_isolation", [] { return TestHardwareRtGateIsolation(true); }},
        {"hardware_gate_disabled_isolation", [] { return TestHardwareRtGateIsolation(false); }},
        {"device_automation_decision_isolation", TestDeviceAutomationDecisionIsolation},
        {"device_automation_document_versions", TestDeviceAutomationDocumentVersions},
        {"manual_hold_process_identity", TestManualHoldAndProcessIdentity},
        {"unsupported_automation_core_available", TestUnsupportedAutomationKeepsCoreUsable},
        {"diagnostic_read_only", [] { return TestDiagnosticReadOnly(0); }},
        {"diagnostic_persistent_quarantine", [] { return TestDiagnosticReadOnly(1); }},
        {"diagnostic_corrupt_document", [] { return TestDiagnosticReadOnly(2); }},
        {"actuation_inheritance", TestVerifiedActuationInheritance},
        {"actuation_reset_failure", [] { return TestActuationPartialFailureAndRecovery(0); }},
        {"actuation_exception_failure", [] { return TestActuationPartialFailureAndRecovery(1); }},
        {"actuation_cancel_recovery", [] { return TestActuationPartialFailureAndRecovery(2); }},
        {"actuation_session_between_operations", [] { return TestActuationPartialFailureAndRecovery(3); }},
        {"actuation_no_unknown_recovery", [] { return TestActuationPartialFailureAndRecovery(4); }},
        {"actuation_added_exception_recovery", [] { return TestActuationPartialFailureAndRecovery(5); }},
        {"actuation_prior_exceptions_forward_recovery", [] { return TestActuationPartialFailureAndRecovery(6); }},
        {"deadzone_reset_order", TestVerifiedDeadzoneResetOrder},
        {"session_change_between_operations", [] { return TestRemovalBetweenProfileOperations(false); }},
        {"session_change_no_op", [] { return TestRemovalBetweenProfileOperations(true); }},
        {"host_baseline", TestHostBaselineFallback},
        {"reset_failure", [] { return TestAllKeyFailure(1); }},
        {"all_key_failure", [] { return TestAllKeyFailure(2); }},
        {"exception_failure", [] { return TestAllKeyFailure(3); }},
        {"rt_inheritance", TestRapidTriggerInheritance},
        {"failed_manual_baseline", TestFailedManualGlobalWriteRetainsBaseline},
        {"timing_breakdown", TestTimingBreakdown},
        {"rt_session_safety_edges", TestRtSessionAndSafetyEdges},
        {"rt_disable_enabled_profile_independent", [] { return TestRtDisableRegression(0, false); }},
        {"rt_disable_enabled_profile_unified", [] { return TestRtDisableRegression(0, true); }},
        {"rt_disable_initial_enabled_shadow", [] { return TestRtDisableRegression(1, false); }},
        {"rt_disable_external_enabled_shadow", [] { return TestRtDisableRegression(2, false); }},
        {"rt_disable_stale_clean_cache", [] { return TestRtDisableRegression(3, false); }},
        {"rt_cutover_known_prior", [] {return TestExplicitPerKeyRtCutover(true);}},
        {"rt_cutover_unknown_prior", [] {return TestExplicitPerKeyRtCutover(false);}},
        {"rt_cutover_partial_failure", [] {return TestExplicitPerKeyRtCutover(true,true);}},
        {"guards", TestGuards},
        {"corrupt", TestCorruptDocument}, {"offline", TestOfflineDefaultDocument},
        {"serialization", TestApplyCannotInterleaveManualWrite},
        {"migration", TestVersionlessEnvelopeMigration}};
    bool okay = true;
    for (const auto& [name, run] : cases)
        if (!run()) { std::cerr << "device profile runtime contract failed: " << name << '\n'; okay = false; }
    return okay ? 0 : 1;
}
