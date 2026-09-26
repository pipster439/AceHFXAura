#include "config/magnetic_control_service.h"
#include "aura/runtime_status.h"
#include "third_party/json.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace aura {
struct M605RuntimeTestAccess {
    static std::unique_ptr<M605Runtime> Create(
        std::unique_ptr<m605::detail::Transport> transport,
        std::unique_ptr<m605::detail::SettleWait> wait,
        std::unique_ptr<m605::detail::SafetyLatch> latch) {
        return std::unique_ptr<M605Runtime>(new M605Runtime(
            std::move(transport), std::move(wait), std::move(latch)));
    }
};
}

namespace {
using Json = nlohmann::json;

struct FakeTransport final : aura::m605::detail::Transport {
    bool connected = false;
    int fail_at_stage = 0;
    int stages = 0;
    int applies = 0;
    std::vector<aura::m605::Report> reports;
    bool IsConnected() const override { return connected; }
    bool Connect() override { connected = true; return true; }
    bool WriteStage(const aura::m605::Report& report) override {
        reports.push_back(report);
        return ++stages != fail_at_stage;
    }
    bool WriteApply(const aura::m605::Report& report) override {
        reports.push_back(report);
        ++applies;
        return true;
    }
    void Disconnect() override { connected = false; }
    std::string GetLastError() const override { return "fake write error"; }
};
struct FakeWait final : aura::m605::detail::SettleWait {
    void WaitBetweenDksStages() override {}
    void WaitBeforeApply() override {}
    void WaitAfterApply() override {
        std::unique_lock<std::mutex> lock(mutex);
        if (!block) return;
        entered = true;
        cv.notify_all();
        cv.wait(lock, [this] { return released; });
    }
    void Block() { std::lock_guard<std::mutex> lock(mutex); block = true; entered = false; released = false; }
    bool Await() {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, std::chrono::seconds(3), [this] { return entered; });
    }
    void Release() { std::lock_guard<std::mutex> lock(mutex); released = true; cv.notify_all(); }
    std::mutex mutex;
    std::condition_variable cv;
    bool block = false, entered = false, released = false;
};
struct LatchState { bool armed = false; int arms = 0; };
struct FakeLatch final : aura::m605::detail::SafetyLatch {
    explicit FakeLatch(std::shared_ptr<LatchState> state) : state(std::move(state)) {}
    bool IsQuarantined() const override { return state->armed; }
    bool Arm() override { if (state->armed) return false; state->armed = true; ++state->arms; return true; }
    bool Clear() override { if (!state->armed) return false; state->armed = false; return true; }
    std::shared_ptr<LatchState> state;
};

std::string EncodeXml(const std::string& name, const Json& value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string percent;
    for (const unsigned char byte : value.dump()) {
        percent.push_back('%'); percent.push_back(hex[byte >> 4]); percent.push_back(hex[byte & 15]);
    }
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string base64;
    for (size_t i = 0; i < percent.size(); i += 3) {
        const unsigned a = static_cast<unsigned char>(percent[i]);
        const unsigned b = i + 1 < percent.size() ? static_cast<unsigned char>(percent[i + 1]) : 0;
        const unsigned c = i + 2 < percent.size() ? static_cast<unsigned char>(percent[i + 2]) : 0;
        base64.push_back(alphabet[a >> 2]);
        base64.push_back(alphabet[((a & 3) << 4) | (b >> 4)]);
        base64.push_back(i + 1 < percent.size() ? alphabet[((b & 15) << 2) | (c >> 6)] : '=');
        base64.push_back(i + 2 < percent.size() ? alphabet[c & 63] : '=');
    }
    return "<root><file_name>" + name + "</file_name><file_data>" + base64 + "</file_data></root>";
}

bool TestHostProfileParser() {
    const auto config = EncodeXml("config_123.xml", {{"currProfile", {{"id", 3}}},
        {"profileList", Json::array({{{"id", 1}}, {{"id", 3}}})}});
    const auto profile = EncodeXml("fp_3_config_123.xml", {
        {"button", {{"analogTrigger", {{"actuation", 10}, {"rapidTriggerPress", 4},
            {"rapidTriggerRelease", 2}, {"preKeyRapidTriggerList", Json::array({1026})}}},
            {"deadZone", {{"deadZoneBottom", 1}, {"deadZoneTop", 0}}},
            {"speedTap", Json::array({{{"keys", Json::array({1538, 769})}}})}}},
        {"lighting", {{"keyboard", {{"effectID", "0"}, {"analogEffect", "0"}}}}}
    });
    const auto parsed = aura::MagneticHostProfileProvider::Parse(config, profile, "123");
    if (parsed.active_profile_id != 3 || parsed.global_actuation_raw != 10 ||
        parsed.global_rt_press_raw != 4 || parsed.global_rt_release_raw != 2 ||
        parsed.global_deadzone_bottom_raw != 1 || parsed.global_deadzone_top_raw != 0 ||
        !parsed.per_key_rt_list_known || !parsed.per_key_rt_enabled.at(1026) ||
        parsed.saved_speedtap_pairs.size() != 1 || parsed.static_analog_effect != false)
        return false;
    const auto wrong = aura::MagneticHostProfileProvider::Parse(config, profile, "999");
    if (wrong.active_profile_id || wrong.global_rt_press_raw) return false;
    const auto invalid = EncodeXml("fp_3_config_123.xml", {
        {"button", {{"analogTrigger", {{"rapidTriggerPress", 26}, {"rapidTriggerRelease", "bad"}}},
            {"deadZone", {{"deadZoneTop", -1}, {"deadZoneBottom", 6}}}}}
    });
    const auto unknown = aura::MagneticHostProfileProvider::Parse(config, invalid, "123");
    return unknown.active_profile_id == 3 && !unknown.global_rt_press_raw &&
        !unknown.global_rt_release_raw && !unknown.global_deadzone_top_raw &&
        !unknown.global_deadzone_bottom_raw;
}

bool TestService() {
    auto status = std::make_shared<aura::RuntimeStatusStore>();
    aura::RuntimeStatusSnapshot snapshot;
    snapshot.dry_run = true;
    snapshot.hardware_connected = false;
    snapshot.active_backend = "unknown";
    status->Update(snapshot);
    auto io = std::make_unique<FakeTransport>();
    auto* io_ptr = io.get();
    auto latch = std::make_shared<LatchState>();
    aura::MagneticHostProfile host;
    host.active_profile_id = 3;
    host.global_rt_press_raw = 4;
    host.global_rt_release_raw = 2;
    host.global_deadzone_bottom_raw = 1;
    host.global_deadzone_top_raw = 0;
    host.per_key_rt_list_known = true;
    host.per_key_rt_enabled[1026] = true;
    auto wait = std::make_unique<FakeWait>();
    auto* wait_ptr = wait.get();
    auto runtime = aura::M605RuntimeTestAccess::Create(std::move(io),
        std::move(wait), std::make_unique<FakeLatch>(latch));
    aura::MagneticControlService service(status, [&host] { return host; }, std::move(runtime));
    httplib::Server server;
    service.RegisterRoutes(server);
    const int port = server.bind_to_any_port("127.0.0.1");
    if (port <= 0) return false;
    std::thread worker([&] { server.listen_after_bind(); });
    const auto finish = [&](bool result) {
        server.stop(); worker.join(); service.Stop(); return result;
    };
    httplib::Client client("127.0.0.1", port);
    client.set_read_timeout(4);
    const auto post = [&](const char* path, const char* json) {
        return client.Post(path, json, "application/json");
    };
    const auto initial = client.Get("/api/magnetic/status");
    if (!initial || initial->status != 200) return finish(false);
    auto body = Json::parse(initial->body);
    if (body.at("health") != "Clean" || body.at("available") != false ||
        body.at("host_profile").at("global_deadzone_top").at("raw") != 0 ||
        body.at("host_profile").at("global_deadzone_top").at("source") != "HostProfile")
        return finish(false);
    const auto invalid_key = post("/api/magnetic/actuation", R"({"logical_id":65535,"mm":1.0})");
    const auto invalid_dks = post("/api/magnetic/dks", R"({"logical_id":1026,"start_mm":3.6,"end_mm":1.0,"slots":[],"resolve_rt":false})");
    const auto invalid_wire = post("/api/magnetic/deadzone", R"({"logical_id":1026,"top_mm":0.2,"bottom_mm":0.3,"wire_id":49})");
    const auto fn_target_dks = post("/api/magnetic/dks",
        R"({"logical_id":1026,"start_mm":1.0,"end_mm":3.6,"resolve_rt":false,"slots":[{"target":{"kind":"LogicalKey","logical_id":1288},"down_start":"Tap","down_end":"Hold","up_start":"Inactive","up_end":"Release"},{"target":{"kind":"DefaultSentinel"},"down_start":"Inactive","down_end":"Inactive","up_start":"Inactive","up_end":"Inactive"},{"target":{"kind":"DefaultSentinel"},"down_start":"Inactive","down_end":"Inactive","up_start":"Inactive","up_end":"Inactive"},{"target":{"kind":"DefaultSentinel"},"down_start":"Inactive","down_end":"Inactive","up_start":"Inactive","up_end":"Inactive"}]})");
    if (!invalid_key || invalid_key->status != 422 || !invalid_dks || invalid_dks->status != 422 ||
        !invalid_wire || invalid_wire->status != 422 ||
        !fn_target_dks || fn_target_dks->status != 422 ||
        latch->arms != 0 || !io_ptr->reports.empty())
        return finish(false);
    const auto offline = post("/api/magnetic/actuation", R"({"logical_id":1026,"mm":1.0})");
    if (!offline || offline->status != 409 || latch->arms != 0) return finish(false);

    // Only the deterministic injected transport is reachable below.
    snapshot.dry_run = false;
    snapshot.hardware_connected = true;
    snapshot.active_backend = "native_hid";
    status->Update(snapshot);
    const auto actuation = post("/api/magnetic/actuation", R"({"logical_id":1026,"mm":1.0})");
    if (!actuation || actuation->status != 200 || io_ptr->reports.size() != 2 || latch->armed)
        return finish(false);
    body = Json::parse(actuation->body);
    if (body.at("actuation")[0].at("source") != "SessionApplied") return finish(false);
    const auto rt_conflict = post("/api/magnetic/rapid-trigger", R"({"logical_id":1026,"press_mm":0.8,"release_mm":0.6,"resolve_dks":false})");
    if (!rt_conflict || rt_conflict->status != 409 || io_ptr->reports.size() != 2)
        return finish(false);
    const auto rt = post("/api/magnetic/rapid-trigger", R"({"logical_id":1026,"press_mm":0.8,"release_mm":0.6,"resolve_dks":true})");
    if (!rt || rt->status != 200 || io_ptr->reports.size() != 10 || // four DKS + apply, two RT + apply
        io_ptr->reports[2][2] != 0x23 || io_ptr->reports[7][2] != 0x54)
        return finish(false);
    body = Json::parse(rt->body);
    if (body.at("rapid_trigger")[0].at("source") != "SessionApplied") return finish(false);
    const auto reset = post("/api/magnetic/deadzone/reset-all", R"({"confirm_all_keys":true})");
    if (!reset || reset->status != 200 || io_ptr->reports[10][2] != 0x52 ||
        io_ptr->reports[10][5] != 1 || io_ptr->reports[10][7] != 0)
        return finish(false);
    const auto analog = post("/api/magnetic/analog-effect/static", R"({"enabled":true})");
    if (!analog || analog->status != 200 || io_ptr->reports[12][2] != 0x2d)
        return finish(false);
    const auto speedtap = post("/api/magnetic/speedtap/profile-reset", R"({"confirm_profile_baseline":true})");
    if (!speedtap || speedtap->status != 200) return finish(false);
    body = Json::parse(speedtap->body);
    if (body.at("speedtap").at("pair_knowledge") != "ProfileBaselineUnknown" ||
        !body.at("speedtap").at("pair_submissions").empty()) return finish(false);

    // SpeedTap enable fails closed when baseline is unknown
    const auto speedtap_pair_on_unknown = post("/api/magnetic/speedtap/pair", R"({"key1":1538,"key2":769})");
    if (!speedtap_pair_on_unknown || speedtap_pair_on_unknown->status != 409) return finish(false);
    body = Json::parse(speedtap_pair_on_unknown->body);
    if (body.at("last_error").get<std::string>().find("trustworthy pair baseline") == std::string::npos)
        return finish(false);

    // Targeted disable of this pair remains available
    const auto speedtap_pair_off = post("/api/magnetic/speedtap/pair/disable", R"({"key1":1538,"key2":769})");
    if (!speedtap_pair_off || speedtap_pair_off->status != 200 ||
        io_ptr->reports[io_ptr->reports.size() - 2][2] != 0x55 ||
        io_ptr->reports[io_ptr->reports.size() - 2][9] != 0) return finish(false);

    // When saved pair baseline becomes known, pair-on succeeds
    host.saved_speedtap_pairs_known = true;
    const auto speedtap_pair_on_known = post("/api/magnetic/speedtap/pair", R"({"key1":1538,"key2":769})");
    if (!speedtap_pair_on_known || speedtap_pair_on_known->status != 200 ||
        io_ptr->reports[io_ptr->reports.size() - 2][2] != 0x55 ||
        io_ptr->reports[io_ptr->reports.size() - 2][9] != 1) return finish(false);

    // Global Actuation route
    const auto bad_global_act = post("/api/magnetic/global/actuation", R"({"mm":4.5})");
    if (!bad_global_act || bad_global_act->status != 422) return finish(false);
    const auto global_act = post("/api/magnetic/global/actuation", R"({"mm":1.5})");
    if (!global_act || global_act->status != 200 ||
        io_ptr->reports[io_ptr->reports.size() - 2][2] != 0x50 ||
        io_ptr->reports[io_ptr->reports.size() - 2][5] != 15) return finish(false);
    body = Json::parse(global_act->body);
    if (body.at("global_actuation").at("source") != "SessionApplied" ||
        body.at("global_actuation").at("raw") != 15) return finish(false);

    // Global Deadzone route
    const auto bad_global_dz = post("/api/magnetic/global/deadzone", R"({"top_mm":0.6,"bottom_mm":0.1})");
    if (!bad_global_dz || bad_global_dz->status != 422) return finish(false);
    const auto global_dz = post("/api/magnetic/global/deadzone", R"({"top_mm":0.2,"bottom_mm":0.3})");
    if (!global_dz || global_dz->status != 200 ||
        io_ptr->reports[io_ptr->reports.size() - 2][2] != 0x58 ||
        io_ptr->reports[io_ptr->reports.size() - 2][5] != 2 ||
        io_ptr->reports[io_ptr->reports.size() - 2][6] != 3) return finish(false);
    body = Json::parse(global_dz->body);
    if (body.at("global_deadzone").at("source") != "SessionApplied" ||
        body.at("global_deadzone").at("top_raw") != 2 ||
        body.at("global_deadzone").at("bottom_raw") != 3) return finish(false);

    // Global Rapid Trigger route (5 fields required)
    const auto bad_global_rt = post("/api/magnetic/global/rapid-trigger",
        R"({"separate_mode":true,"press_mm":3.0,"release_mm":0.2,"top_mm":0.0,"bottom_mm":0.1})");
    if (!bad_global_rt || bad_global_rt->status != 422) return finish(false);

    // Missing deadzones (3 fields): rejected with 422 because exact 5 fields are required
    const auto missing_dz = post("/api/magnetic/global/rapid-trigger",
        R"({"separate_mode":true,"press_mm":0.5,"release_mm":0.3})");
    if (!missing_dz || missing_dz->status != 422) return finish(false);

    // Requirement 4.C: separate_mode=false, press=0.4, release=0.2 -> 422 and zero HID reports
    const auto count_before_c = io_ptr->reports.size();
    const auto contradictory_rt = post("/api/magnetic/global/rapid-trigger",
        R"({"separate_mode":false,"press_mm":0.4,"release_mm":0.2,"top_mm":0.0,"bottom_mm":0.1})");
    if (!contradictory_rt || contradictory_rt->status != 422 ||
        io_ptr->reports.size() != count_before_c) return finish(false);

    // Requirement 4.D: separate_mode=false, press=release=0.4 -> accepted (200)
    const auto global_rt_linked = post("/api/magnetic/global/rapid-trigger",
        R"({"separate_mode":false,"press_mm":0.4,"release_mm":0.4,"top_mm":0.0,"bottom_mm":0.1})");
    if (!global_rt_linked || global_rt_linked->status != 200 ||
        io_ptr->reports[io_ptr->reports.size() - 2][2] != 0x53 ||
        io_ptr->reports[io_ptr->reports.size() - 2][3] != 0 ||
        io_ptr->reports[io_ptr->reports.size() - 2][5] != 4 ||
        io_ptr->reports[io_ptr->reports.size() - 2][6] != 4 ||
        io_ptr->reports[io_ptr->reports.size() - 2][7] != 0 ||
        io_ptr->reports[io_ptr->reports.size() - 2][8] != 1) return finish(false);
    body = Json::parse(global_rt_linked->body);
    if (body.at("global_rapid_trigger").at("source") != "SessionApplied" ||
        body.at("global_rapid_trigger").at("separate_mode") != false ||
        body.at("global_rapid_trigger").at("press_raw") != 4 ||
        body.at("global_rapid_trigger").at("release_raw") != 4) return finish(false);

    // Requirement 4.E: separate_mode=true, press=0.4, release=0.2 -> accepted (200)
    const auto global_rt_sep = post("/api/magnetic/global/rapid-trigger",
        R"({"separate_mode":true,"press_mm":0.4,"release_mm":0.2,"top_mm":0.0,"bottom_mm":0.1})");
    if (!global_rt_sep || global_rt_sep->status != 200 ||
        io_ptr->reports[io_ptr->reports.size() - 2][2] != 0x53 ||
        io_ptr->reports[io_ptr->reports.size() - 2][3] != 1 ||
        io_ptr->reports[io_ptr->reports.size() - 2][5] != 4 ||
        io_ptr->reports[io_ptr->reports.size() - 2][6] != 2 ||
        io_ptr->reports[io_ptr->reports.size() - 2][7] != 0 ||
        io_ptr->reports[io_ptr->reports.size() - 2][8] != 1) return finish(false);
    body = Json::parse(global_rt_sep->body);
    if (body.at("global_rapid_trigger").at("source") != "SessionApplied" ||
        body.at("global_rapid_trigger").at("separate_mode") != true ||
        body.at("global_rapid_trigger").at("press_raw") != 4 ||
        body.at("global_rapid_trigger").at("release_raw") != 2) return finish(false);

    wait_ptr->Block();
    std::atomic<bool> first_ok = false;
    std::thread in_flight([&] {
        httplib::Client other_client("127.0.0.1", port);
        other_client.set_read_timeout(4);
        const auto first = other_client.Post("/api/magnetic/actuation",
            R"({"logical_id":1026,"mm":2.0})", "application/json");
        first_ok = first && first->status == 200;
    });
    if (!wait_ptr->Await()) { wait_ptr->Release(); in_flight.join(); return finish(false); }
    // Inspect status while transaction is deliberately blocked in post-Apply settle:
    const auto settling_status = client.Get("/api/magnetic/status");
    if (!settling_status || settling_status->status != 200) {
        wait_ptr->Release(); in_flight.join(); return finish(false);
    }
    const auto settling_body = Json::parse(settling_status->body);
    if (settling_body.at("health") != "TransactionInProgress" ||
        settling_body.at("persistent_safety_quarantine") != false) {
        wait_ptr->Release(); in_flight.join(); return finish(false);
    }
    const auto count_during = io_ptr->reports.size();
    const auto busy = post("/api/magnetic/actuation", R"({"logical_id":1026,"mm":3.0})");
    const bool blocked_busy = busy && busy->status == 409 && io_ptr->reports.size() == count_during;
    wait_ptr->Release();
    in_flight.join();
    if (!blocked_busy || !first_ok) return finish(false);

    // After completion, status is Clean and quarantine is false:
    const auto clean_status = client.Get("/api/magnetic/status");
    if (!clean_status || clean_status->status != 200) return finish(false);
    const auto clean_body = Json::parse(clean_status->body);
    if (clean_body.at("health") != "Clean" || clean_body.at("persistent_safety_quarantine") != false)
        return finish(false);

    // First cross-feature operation succeeds; the next stage fails. No
    // rollback is attempted and the response reports the partial sequence.
    io_ptr->fail_at_stage = io_ptr->stages + 5;
    const auto failed = post("/api/magnetic/rapid-trigger",
        R"({"logical_id":1281,"press_mm":0.8,"release_mm":0.6,"resolve_dks":true})");
    if (!failed || failed->status != 409 || !latch->armed) return finish(false);
    body = Json::parse(failed->body);
    if (body.at("health") != "IndeterminateStagedState" ||
        body.at("persistent_safety_quarantine") != true ||
        body.at("last_error").get<std::string>().find("DKS standard rewrite completed") == std::string::npos ||
        !body.at("dks").empty()) return finish(false);
    const auto count = io_ptr->reports.size();
    const auto blocked = post("/api/magnetic/actuation", R"({"logical_id":1026,"mm":1.0})");
    if (!blocked || blocked->status != 409 || io_ptr->reports.size() != count)
        return finish(false);
    return finish(true);
}
}

int main() {
    return TestHostProfileParser() && TestService() ? 0 : 1;
}
