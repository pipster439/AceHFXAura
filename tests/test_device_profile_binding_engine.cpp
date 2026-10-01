#include "config/device_profile_binding_engine.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
using Engine = aura::DeviceProfileBindingEngine;
constexpr const char* Desktop = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr const char* Cs2 = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
constexpr const char* Missing = "cccccccc-cccc-4ccc-8ccc-cccccccccccc";
constexpr const char* R1 = "11111111-1111-4111-8111-111111111111";
constexpr const char* R2 = "22222222-2222-4222-8222-222222222222";
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
Json Rule(const char* id = R1, const char* process = "cs2.exe", const char* profile = Cs2,
    int priority = 100, bool enabled = true) {
    return {{"rule_id", id}, {"enabled", enabled}, {"process_name", process}, {"profile_id", profile}, {"priority", priority}};
}
Json Config() {
    auto result = aura::DeviceProfileAutomationConfig::DefaultJson();
    result["enabled"] = true; result["fallback_profile_id"] = Desktop;
    result["bindings"].push_back(Rule()); return result;
}
struct Fixture {
    int64_t now = 0;
    int clock_reads = 0;
    Engine engine{[this] { ++clock_reads; return now; }};
    Fixture() { Configure(Config()); }
    void Configure(const Json& config, std::set<std::string> ids = {Desktop, Cs2}, bool available = true) {
        engine.Configure(config, ids, available, 7);
    }
    Json Commit(const std::string& name) {
        engine.Observe(name); now += 500; engine.Advance(); return engine.Snapshot();
    }
};
void NormalizationAndStartup() {
    for (const auto& name : {"cs2.exe", "CS2.EXE", "C:\\Users\\PRIVATE_SENTINEL\\Games\\cs2.exe", "/games/CS2.EXE"}) {
        Fixture f;
        CHECK(f.engine.Snapshot().at("decision_reason") == "NotInitialized");
        f.engine.Observe(name); f.now = 499; CHECK(!f.engine.Advance());
        CHECK(f.engine.Snapshot().at("debounce_pending") == true);
        CHECK(f.engine.Snapshot().at("decision_sequence") == 0);
        f.now = 500; CHECK(f.engine.Advance());
        const auto snapshot = f.engine.Snapshot();
        CHECK(snapshot.at("foreground_process") == "cs2.exe");
        CHECK(snapshot.at("decision_kind") == "Match");
        CHECK(snapshot.at("decision_reason") == "MatchedRule");
        CHECK(snapshot.at("resolved_profile_id") == Cs2);
        CHECK(snapshot.at("matched_rule_id") == R1);
        CHECK(snapshot.at("hardware_activation_allowed") == false);
        CHECK(snapshot.dump().find("PRIVATE_SENTINEL") == std::string::npos);
    }
    CHECK(Engine::NormalizeForeground("Ä.EXE") == std::optional<std::string>("ä.exe"));
    CHECK(!Engine::NormalizeForeground("C:\\directory\\"));
    CHECK(!Engine::NormalizeForeground("bad?.exe"));
}
void Resolution() {
    Fixture f; auto decision = f.Commit("explorer.exe");
    CHECK(decision.at("decision_kind") == "Fallback"); CHECK(decision.at("resolved_profile_id") == Desktop);
    auto config = Config(); config["fallback_profile_id"] = nullptr;
    f.Configure(config); CHECK(f.engine.Advance());
    CHECK(f.engine.Snapshot().at("decision_reason") == "NoMatchingRule");
    CHECK(f.engine.Snapshot().at("resolved_profile_id").is_null());
    config["enabled"] = false; f.Configure(config); f.Commit("cs2.exe");
    CHECK(f.engine.Snapshot().at("decision_reason") == "AutomationDisabled");
    config["enabled"] = true; config["bindings"][0]["enabled"] = false;
    config["bindings"].push_back(Rule(R2, "cs2.exe", Desktop, 10));
    f.Configure(config); CHECK(f.engine.Advance()); CHECK(f.engine.Snapshot().at("matched_rule_id") == R2);
    config["bindings"][0]["enabled"] = true;
    f.Configure(config); CHECK(f.engine.Advance()); CHECK(f.engine.Snapshot().at("matched_rule_id") == R1);
    config["bindings"][0]["profile_id"] = Missing;
    f.Configure(config); CHECK(f.engine.Advance());
    CHECK(f.engine.Snapshot().at("decision_kind") == "InvalidDecision");
    CHECK(f.engine.Snapshot().at("resolved_profile_id") == Missing); // no lower-rule fall-through
    config["bindings"] = Json::array(); config["fallback_profile_id"] = Missing;
    f.Configure(config); CHECK(f.engine.Advance());
    CHECK(f.engine.Snapshot().at("decision_reason") == "InvalidTargetProfile");
    f.Configure(Config(), {Desktop}); CHECK(f.engine.Advance());
    CHECK(f.engine.Snapshot().at("decision_reason") == "InvalidTargetProfile");
    f.Configure(Config(), {Desktop, Cs2}, false); CHECK(f.engine.Advance());
    CHECK(f.engine.Snapshot().at("decision_reason") == "ProfileDocumentUnavailable");
}
void TieAndSameTarget() {
    auto config = Config(); config["bindings"] = Json::array({Rule(R2, "cs2.exe", Desktop), Rule(R1)});
    for (int i = 0; i < 2; ++i) {
        Fixture f; f.Configure(config); CHECK(f.Commit("cs2.exe").at("matched_rule_id") == R1);
        std::reverse(config["bindings"].begin(), config["bindings"].end());
    }
    Fixture f; config = Config(); config["bindings"].push_back(Rule(R2, "other.exe"));
    f.Configure(config); const auto first = f.Commit("cs2.exe");
    f.now += 10; f.engine.Observe("other.exe"); f.now += 500;
    CHECK(!f.engine.Advance()); // same resolved target, different rule/foreground
    CHECK(f.engine.Snapshot().at("decision_sequence") == first.at("decision_sequence"));
    CHECK(f.engine.Snapshot().at("matched_rule_id") == R2);
    CHECK(f.engine.Snapshot().at("decision_foreground_process") == "other.exe");
    for (int i = 0; i < 10; ++i) { f.now += 50; f.engine.Observe("OTHER.EXE"); CHECK(!f.engine.Advance()); }
    CHECK(f.engine.Snapshot().at("decision_sequence") == first.at("decision_sequence"));
}
void ChurnAndManualHold() {
    Fixture f; f.engine.Observe("cs2.exe"); f.now = 100; f.engine.Observe("explorer.exe");
    f.now = 200; f.engine.Observe("CS2.EXE"); f.now = 699; CHECK(!f.engine.Advance());
    f.now = 700; CHECK(f.engine.Advance()); CHECK(f.engine.Snapshot().at("decision_sequence") == 1);
    f.engine.NotifyManualProfileAction(); const auto hold = f.engine.Snapshot();
    CHECK(hold.at("decision_reason") == "ManualHold"); CHECK(hold.at("resolved_profile_id").is_null());
    f.engine.NotifyManualProfileAction(); CHECK(f.engine.Snapshot().at("decision_sequence") == hold.at("decision_sequence"));
    f.now = 800; f.engine.Observe("explorer.exe"); f.now = 900; f.engine.Observe("cs2.exe");
    f.now = 1400; CHECK(!f.engine.Advance()); CHECK(f.engine.Snapshot().at("manual_hold") == true);
    f.Commit(""); CHECK(f.engine.Snapshot().at("manual_hold") == true); // unknown isn't meaningful change
    CHECK(f.Commit("cs2.exe").at("decision_reason") == "ManualHold");
    const auto changed = f.Commit("explorer.exe");
    CHECK(changed.at("manual_hold") == false); CHECK(changed.at("resolved_profile_id") == Desktop);
    CHECK(f.Commit("cs2.exe").at("resolved_profile_id") == Cs2);
    Fixture restarted; CHECK(restarted.engine.Snapshot().at("manual_hold") == false);
    CHECK(restarted.Commit("cs2.exe").at("decision_reason") == "MatchedRule");
}
void SchemaAndExtensions() {
    auto old = Config(); old["private_extension"] = {{"data", "retained"}};
    old["bindings"][0]["extension"] = 42;
    auto next = Config(); next["bindings"][0]["process_name"] = "CS2.EXE";
    const auto saved = aura::DeviceProfileAutomationConfig::NormalizeAndPreserve(next, old);
    CHECK(saved.at("bindings")[0].at("process_name") == "cs2.exe");
    CHECK(saved.at("private_extension") == old.at("private_extension"));
    CHECK(saved.at("bindings")[0].at("extension") == 42);
    CHECK(aura::DeviceProfileAutomationConfig::NormalizeAndPreserve(saved, saved) == saved);
    for (const auto& name : {"", " ", "bad?.exe", "a/b.exe", "C:\\secret\\cs2.exe", "cs2.exe.", "foo:bar"}) {
        auto invalid = Config(); invalid["bindings"][0]["process_name"] = name;
        bool rejected = false; try { aura::DeviceProfileAutomationConfig::Parse(invalid); } catch (const std::invalid_argument&) { rejected = true; }
        CHECK(rejected);
    }
    for (int i = 0; i < 7; ++i) {
        auto invalid = Config();
        if (i == 0) invalid["schema_version"] = 2;
        if (i == 1) invalid["bindings"].push_back(invalid["bindings"][0]);
        if (i == 2) invalid["bindings"][0]["priority"] = 1.5;
        if (i == 3) invalid["bindings"][0]["priority"] = uint64_t{18446744073709551615ull};
        if (i == 4) invalid["bindings"][0]["priority"] = int64_t{-2147483649ll};
        if (i == 5) invalid["bindings"][0]["rule_id"] = "display-name";
        if (i == 6) invalid["bindings"][0].erase("priority");
        bool rejected = false; try { aura::DeviceProfileAutomationConfig::Parse(invalid); } catch (const std::invalid_argument&) { rejected = true; }
        CHECK(rejected);
    }
}
void ContextAndPureRead() {
    Fixture f; f.Commit("cs2.exe");
    f.engine.SetContext({true, false, true, "IndeterminateStagedState", true});
    const auto before = f.engine.Snapshot(); const int clocks = f.clock_reads;
    for (int i = 0; i < 10; ++i) CHECK(f.engine.Snapshot() == before);
    CHECK(f.clock_reads == clocks); CHECK(before.at("hardware_activation_allowed") == false);
    CHECK(before.at("decision_kind") == "Match");
    CHECK(before.at("hardware_block_reasons") == Json::array({"PhaseHardwareActivationDisabled",
        "KeyboardUnavailable", "SafetyQuarantine", "RuntimeUnhealthyOrUnknown", "SessionReconciliationPending"}));
    f.engine.SetContext({true, true, false, "Clean", false});
    CHECK(f.engine.Snapshot().at("hardware_activation_allowed") == false);
    CHECK(f.engine.Snapshot().at("hardware_block_reason") == "PhaseHardwareActivationDisabled");
    CHECK(f.engine.Snapshot().at("decision_sequence") == before.at("decision_sequence"));
}
}
int main() {
    const std::pair<const char*, void(*)()> tests[] = {{"normalization_startup", NormalizationAndStartup},
        {"resolution", Resolution}, {"tie_dedup", TieAndSameTarget}, {"debounce_hold", ChurnAndManualHold},
        {"schema_extensions", SchemaAndExtensions}, {"context_pure_read", ContextAndPureRead}};
    int failures = 0;
    for (const auto& [name, test] : tests) try { test(); } catch (const std::exception& ex) {
        ++failures; std::cerr << name << ": " << ex.what() << '\n';
    }
    std::cout << "Device Profile Automation decision contracts; hardware activation NOT enabled\n";
    return failures ? 1 : 0;
}
