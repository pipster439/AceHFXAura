#include "config/device_profile_automation_coordinator.h"
#include "config/device_profile_binding_engine.h"
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using Json = nlohmann::json;
constexpr const char* A = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr const char* B = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
constexpr const char* R = "11111111-1111-4111-8111-111111111111";
#define CHECK(x) do { if (!(x)) throw std::runtime_error("check line " + std::to_string(__LINE__) + ": " #x); } while (0)
Json Config() {
    auto c = aura::DeviceProfileAutomationConfig::DefaultJson(); c["enabled"] = true;
    c["bindings"] = Json::array({{{"rule_id", R}, {"enabled", true}, {"process_name", "cs2.exe"},
        {"profile_id", A}, {"priority", 100}}});
    c["fallback_profile_id"] = B; return c;
}
struct Fixture {
    int64_t now = 0;
    aura::DeviceProfileBindingEngine engine{[this] { return now; }};
    Json config = Config(); Json extra = Json::object();
    std::vector<Json> activations;
    std::string outcome = "succeeded"; int64_t revision = 10;
    std::function<void()> before;
    aura::DeviceProfileAutomationCoordinator coordinator{
        [this] { return Snapshot(); }, [this](const auto& d, const auto& stopping) {
            if (before) before();
            const auto current = Snapshot();
            if (stopping()) return Json{{"outcome", "cancelled"}};
            for (const char* field : {"decision_sequence", "configuration_document_revision", "manual_action_sequence",
                "foreground_process", "resolved_profile_id", "m605_session_generation"})
                if (current.at(field) != d.at(field)) return Json{{"outcome", "stale"}};
            if (current.at("manual_hold") == true || current.at("debounce_pending") == true)
                return Json{{"outcome", "stale"}};
            activations.push_back(d);
            return Json{{"outcome", outcome}, {"error", outcome == "failed" ? "MockApplyFailure" : ""}};
        }, [this] { return now; }};
    Fixture() { engine.Configure(config, {A, B}, true, revision); }
    Json Snapshot() const {
        auto d = engine.Snapshot();
        d["m605_session_generation"] = 1; d["runtime_health"] = "Clean";
        d["persistent_safety_quarantine"] = false;
        for (auto it = extra.begin(); it != extra.end(); ++it) d[it.key()] = it.value();
        return d;
    }
    void Stable(const std::string& name) { engine.Observe(name); now += 500; engine.Advance(); coordinator.Tick(); }
    void Configure() { engine.Configure(config, {A, B}, true, ++revision); engine.Advance(); }
};
void StartupChurnAndDedup() {
    Fixture f; f.engine.Observe("cs2.exe"); f.coordinator.Tick(); CHECK(f.activations.empty());
    f.now = 499; f.engine.Advance(); f.coordinator.Tick(); CHECK(f.activations.empty());
    f.now = 500; f.engine.Advance(); f.coordinator.Tick(); CHECK(f.activations.size() == 1);
    for (int i = 0; i < 100; ++i) f.coordinator.Tick(); CHECK(f.activations.size() == 1);
    f.Stable("explorer.exe"); f.Stable("cs2.exe"); CHECK(f.activations.size() == 3);
    CHECK(f.activations[0]["resolved_profile_id"] == A); CHECK(f.activations[1]["resolved_profile_id"] == B);
    Fixture churn;
    for (const char* p : {"chrome.exe", "cs2.exe", "discord.exe", "cs2.exe", "chrome.exe"}) {
        churn.engine.Observe(p); churn.now += 100; churn.engine.Advance(); churn.coordinator.Tick();
    }
    CHECK(churn.activations.empty()); churn.now += 500; churn.engine.Advance(); churn.coordinator.Tick();
    CHECK(churn.activations.size() == 1);
}
void NoHiddenFallback() {
    Fixture f; f.config["fallback_profile_id"] = nullptr; f.Configure(); f.Stable("explorer.exe"); CHECK(f.activations.empty());
    f.config["bindings"][0]["profile_id"] = "cccccccc-cccc-4ccc-8ccc-cccccccccccc";
    f.Configure(); f.Stable("cs2.exe"); CHECK(f.activations.empty());
    f.config["enabled"] = false; f.Configure(); f.coordinator.Tick(); CHECK(f.activations.empty());
    f.extra["configuration_available"] = false; f.coordinator.Tick(); CHECK(f.activations.empty());
}
void FreshnessRechecked() {
    for (int mode = 0; mode < 6; ++mode) {
        Fixture f;
        f.before = [&] {
            if (mode == 0) f.Configure();
            if (mode == 1) f.extra["decision_sequence"] = 999;
            if (mode == 2) f.engine.Observe("explorer.exe");
            if (mode == 3) f.engine.NotifyManualProfileAction();
            if (mode == 4) { f.engine.Configure(f.config, {B}, true, ++f.revision); f.engine.Advance(); }
            if (mode == 5) f.extra["m605_session_generation"] = 2;
        };
        f.Stable("cs2.exe"); CHECK(f.activations.empty());
        CHECK(f.coordinator.Snapshot()["activation_outcome"] == "stale");
    }
}
void HoldLifecycle() {
    Fixture f; f.Stable("cs2.exe"); f.engine.NotifyManualProfileAction();
    for (int i = 0; i < 5; ++i) { f.engine.Observe("cs2.exe"); f.engine.Advance(); f.coordinator.Tick(); }
    CHECK(f.activations.size() == 1);
    f.Stable(""); CHECK(f.engine.Snapshot()["manual_hold"] == true);
    f.Stable("explorer.exe"); CHECK(f.engine.Snapshot()["manual_hold"] == false);
    f.Stable("cs2.exe"); CHECK(f.activations.size() == 3);
    Fixture restarted; restarted.Stable("cs2.exe"); CHECK(restarted.engine.Snapshot()["manual_hold"] == false);
}
void RetryOutcomesAndHealthWake() {
    Fixture f; f.outcome = "deferred"; f.Stable("cs2.exe"); CHECK(f.activations.size() == 1);
    for (int i = 0; i < 100; ++i) f.coordinator.Tick(); CHECK(f.activations.size() == 1);
    CHECK(f.coordinator.Snapshot()["retry_at"] == 1500);
    f.now = 1499; f.coordinator.Tick(); CHECK(f.activations.size() == 1);
    f.now = 1500; f.coordinator.Tick(); CHECK(f.activations.size() == 2);
    CHECK(f.coordinator.Snapshot()["retry_at"] == 3500);
    f.extra["m605_session_generation"] = 2; f.outcome = "succeeded"; f.coordinator.Tick(); CHECK(f.activations.size() == 3);
    for (int i = 0; i < 20; ++i) f.coordinator.Tick(); CHECK(f.activations.size() == 3);
    Fixture blocked; blocked.outcome = "blocked"; blocked.Stable("cs2.exe");
    blocked.now += 1000000; blocked.coordinator.Tick(); CHECK(blocked.activations.size() == 1);
    blocked.outcome = "no-op"; blocked.extra["runtime_health"] = "RecoveredClean";
    blocked.coordinator.Tick(); CHECK(blocked.activations.size() == 2);
    CHECK(blocked.coordinator.Snapshot()["activation_outcome"] == "no-op");
    Fixture failed; failed.outcome = "failed"; failed.Stable("cs2.exe");
    failed.now += 1000000; failed.coordinator.Tick(); CHECK(failed.activations.size() == 1);
    CHECK(failed.coordinator.Snapshot()["activation_error"] == "MockApplyFailure");
}
void SelectionRevisionConsumed() {
    int64_t now = 0; Json token{{"configuration_available", true}, {"enabled", true}, {"manual_hold", false},
        {"debounce_pending", false}, {"decision_kind", "Match"}, {"resolved_profile_id", A},
        {"decision_sequence", 1}, {"configuration_document_revision", 10}, {"manual_action_sequence", 0},
        {"foreground_process", "cs2.exe"}, {"decision_foreground_process", "cs2.exe"}};
    int calls = 0;
    aura::DeviceProfileAutomationCoordinator c([&] { return token; }, [&](const auto&, const auto&) {
        ++calls; token["configuration_document_revision"] = 11;
        return Json{{"outcome", "succeeded"}, {"document_revision", 11}};
    }, [&] { return now; });
    c.Tick(); for (int i = 0; i < 100; ++i) c.Tick(); CHECK(calls == 1);
    // A real independent edit is not swallowed as selection's revision.
    token["configuration_document_revision"] = 12; c.Tick(); CHECK(calls == 2);
}
void SingleFlightAndShutdown() {
    Fixture f; f.engine.Observe("cs2.exe"); f.now = 500; f.engine.Advance();
    std::mutex mutex; std::condition_variable cv; bool entered = false, release = false;
    f.before = [&] { std::unique_lock<std::mutex> lock(mutex); entered = true; cv.notify_all(); cv.wait(lock, [&] { return release; }); };
    std::thread first([&] { f.coordinator.Tick(); });
    { std::unique_lock<std::mutex> lock(mutex); CHECK(cv.wait_for(lock, std::chrono::seconds(3), [&] { return entered; })); }
    f.coordinator.Tick(); // cannot overlap the admitted task
    f.coordinator.Stop(); // no worker in deterministic mode; marks pending task cancelled
    { std::lock_guard<std::mutex> lock(mutex); release = true; } cv.notify_all(); first.join();
    f.coordinator.Tick(); CHECK(f.activations.empty());
}
void DiagnosticsPureRead() {
    Fixture f; f.Stable("cs2.exe"); const auto before = f.coordinator.Snapshot();
    for (int i = 0; i < 100; ++i) CHECK(f.coordinator.Snapshot() == before);
    CHECK(f.activations.size() == 1); CHECK(before["hardware_activation_allowed"] == true);
}
}
int main() {
    const std::pair<const char*, void(*)()> tests[] = {
        {"startup_churn_dedup_A_B_A", StartupChurnAndDedup}, {"no_hidden_fallback", NoHiddenFallback},
        {"freshness_races", FreshnessRechecked}, {"manual_hold", HoldLifecycle},
        {"retry_outcomes_health_wake", RetryOutcomesAndHealthWake}, {"selection_revision_consumed", SelectionRevisionConsumed},
        {"single_flight_shutdown", SingleFlightAndShutdown}, {"diagnostic_purity", DiagnosticsPureRead}};
    int failures = 0;
    for (const auto& [name, test] : tests) try { test(); std::cout << "PASS " << name << '\n'; }
    catch (const std::exception& e) { ++failures; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
    return failures ? 1 : 0;
}
