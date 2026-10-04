// Uses the real DeviceProfileRuntime plus the owning test translation unit's
// mock M605 transport/settle/latch. No native HID or production config is opened.
namespace {
void WritePhase4BDiagnosticFixture(const char* name, Json value) {
    char* directory = nullptr; size_t length = 0;
    if (_dupenv_s(&directory, &length, "AURA_P4B_TEST_ARTIFACT_DIR") != 0 || !directory) return;
    const std::filesystem::path folder(directory); std::free(directory);
    std::filesystem::create_directories(folder);
    value["test_fixture"] = "real Runtime with mock transport/settle/latch; NOT physical hardware evidence";
    Write(folder / name, value);
}
struct Phase4BFixture {
    std::filesystem::path folder, path;
    std::shared_ptr<LatchState> latch = std::make_shared<LatchState>();
    std::unique_ptr<aura::M605Runtime> device;
    std::unique_ptr<aura::DeviceProfileRuntime> profiles;
    std::unique_ptr<aura::DeviceProfileAutomationCoordinator> coordinator;
    std::mutex gate;
    Io* io = nullptr; Wait* wait = nullptr;
    int64_t now = 0; std::atomic<bool> available{true};
    int attempts = 0;
    std::function<void()> before_admission;
    Phase4BFixture(bool rt = false, const char* process = "cs2.exe", bool hardware = false) {
        static std::atomic<int> serial{0};
        folder = std::filesystem::temp_directory_path() / ("aura-phase4b-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(++serial));
        std::filesystem::create_directories(folder); path = folder / "device-profiles.json";
        auto doc = Document();
        for (auto& p : doc["profiles"]) p["magnetic"]["keys"] = Json::array();
        doc["profiles"][0]["magnetic"]["global_actuation_mm"] = 1.0;
        doc["profiles"][1]["magnetic"]["global_actuation_mm"] = 4.0;
        auto config = aura::DeviceProfileAutomationConfig::DefaultJson(); config["enabled"] = true; config["fallback_profile_id"] = A;
        config["bindings"] = Json::array({{{"rule_id", C}, {"process_name", process}, {"profile_id", B}, {"enabled", true}, {"priority", 100}}});
        doc["device_profile_automation"] = config;
        if (hardware) {
            doc["profiles"][0]["activation_backend"] = "hardware_slot"; doc["profiles"][0]["hardware_slot"] = 1;
            doc["profiles"][1]["activation_backend"] = "hardware_slot"; doc["profiles"][1]["hardware_slot"] = 5;
        }
        if (rt) doc["profiles"][1]["magnetic"]["keys"] = Json::array({{{"logical_id", 0x0701},
            {"rapid_trigger", {{"enabled", true}, {"press_mm", 0.5}, {"release_mm", 1.5}, {"separate_mode", true}}}}});
        Write(path, doc);
        auto transport = std::make_unique<Io>(); io = transport.get();
        io->basic_supported = hardware;
        auto settle = std::make_unique<Wait>(); wait = settle.get();
        device = aura::M605RuntimeTestAccess::Create(std::move(transport), std::move(settle), std::make_unique<Latch>(latch));
        aura::MagneticHostProfile host; host.global_rt_press_raw = 4; host.global_rt_release_raw = 4; host.per_key_rt_list_known = true;
        profiles = std::make_unique<aura::DeviceProfileRuntime>(path, std::filesystem::path{}, *device, gate,
            [host] { return host; }, [this] { return available.load(); }, [this] { return now; });
        coordinator = std::make_unique<aura::DeviceProfileAutomationCoordinator>(
            [this] { return profiles->AutomationAdmissionSnapshot(); },
            [this](const auto& token, const auto& stopping) { ++attempts; if (before_admission) before_admission(); return profiles->ActivateAutomationDecision(token, stopping); },
            [this] { return now; });
        profiles->SetAutomationCoordinatorDiagnostics([this] { return coordinator->Snapshot(); });
    }
    ~Phase4BFixture() { coordinator->Stop(); device->Stop(); std::error_code e; std::filesystem::remove_all(folder, e); }
    void Stable(const char* name) { profiles->ObserveAutomationForeground(name); now += 500; profiles->ObserveAutomationForeground(name); coordinator->Tick(); }
};
bool TestPhase4BApplyAndNoDuplicate() {
    Phase4BFixture f; f.Stable("cs2.exe");
    auto d = f.profiles->Diagnostics();
    WritePhase4BDiagnosticFixture("diagnostics.succeeded.mock.json", d);
    if (d.at("runtime").at("active_profile_id") != B || d.at("runtime").at("selected_profile_id") != B ||
        d.at("runtime").at("dirty") != false || d.at("automation").at("hardware_activation_allowed") != true ||
        f.coordinator->Snapshot().at("activation_outcome") != "succeeded") {
        std::cerr << "P4B first apply: " << d.at("runtime").dump() << " coordinator=" << f.coordinator->Snapshot().dump() << '\n'; return false;
    }
    const int writes = f.io->stages;
    for (int i = 0; i < 100; ++i) f.coordinator->Tick();
    if (f.attempts != 1 || f.io->stages != writes) { std::cerr << "P4B duplicates=" << f.attempts << '\n'; return false; }
    f.Stable("explorer.exe"); f.Stable("cs2.exe");
    if (f.attempts != 3 || f.profiles->State().at("active_profile_id") != B) { std::cerr << "P4B ABA=" << f.attempts << " " << f.coordinator->Snapshot().dump() << '\n'; return false; }
    auto before = f.profiles->Diagnostics(); before.erase("captured_at_utc");
    const auto probes = f.io->presence_probes; const auto connects = f.io->connects;
    for (int i = 0; i < 10; ++i) {
        auto after = f.profiles->Diagnostics(); after.erase("captured_at_utc");
        if (after != before) { std::cerr << "P4B diagnostic changed: " << Json::diff(before, after).dump() << '\n'; return false; }
    }
    if (f.io->presence_probes != probes || f.io->connects != connects) { std::cerr << "P4B probes changed\n"; return false; }
    const auto selected = Json::parse(std::ifstream(f.path));
    std::cout << "P4B A/B/A activations=3 duplicate=0 diagnostic_side_effects=0\n";
    return selected.at("selected_profile_id") == B && selected.at("global_defaults").at("global_actuation_mm") == 1.0;
}
bool TestPhase4BControlSurfaceHold() {
    Phase4BFixture f(false, "notepad.exe");
    Json timeline = Json::array();
    const auto record = [&](const char* label) {
        auto d = f.profiles->Diagnostics();
        timeline.push_back({{"step", label}, {"timestamp_ms", f.now},
            {"automation", d.at("automation")}, {"runtime", d.at("runtime")},
            {"last_activation", d.at("last_activation")}, {"automation_attempts", f.attempts},
            {"mock_hid_stages", f.io->stages}});
    };
    f.Stable("notepad.exe"); record("automatic-context-profile-succeeded");
    const auto old_token = f.profiles->AutomationAdmissionSnapshot();
    f.Stable("aura.exe"); record("control-surface");
    const auto manual = f.profiles->ActivateProfile(A, "Manual", f.profiles->State().at("document_revision"));
    record("manual-other-profile-succeeded");
    const int attempts = f.attempts, stages = f.io->stages;
    const auto stale = f.profiles->ActivateAutomationDecision(old_token, [] { return false; });
    if (stale.at("outcome") != "stale" || f.io->stages != stages) return false;
    f.Stable("notepad.exe"); record("return-notepad");
    for (const auto* name : {"aura.exe", "notepad.exe", "", "notepad.exe"}) f.Stable(name);
    const bool retained = manual.at("outcome") == "succeeded" && f.attempts == attempts && f.io->stages == stages &&
        f.profiles->State().at("active_profile_id") == A &&
        f.profiles->Diagnostics().at("automation").at("manual_hold") == true;
    f.Stable("aura.exe"); f.Stable("charmap.exe"); record("different-stable-external");
    const bool cleared = !f.profiles->Diagnostics().at("automation").at("manual_hold").get<bool>();
    f.Stable("notepad.exe"); record("automatic-context-resumed");
    WritePhase4BDiagnosticFixture("control-surface-timeline.mock.json", {{"timeline", timeline}});
    return retained && cleared && f.profiles->State().at("active_profile_id") == B;
}
bool TestPhase4BDeferredAndSafety() {
    Phase4BFixture f; f.available = false; f.Stable("cs2.exe");
    WritePhase4BDiagnosticFixture("diagnostics.deferred.mock.json", f.profiles->Diagnostics());
    if (f.coordinator->Snapshot().at("activation_outcome") != "deferred" || f.profiles->State().at("selected_profile_id") != B || f.io->stages != 0) return false;
    for (int i = 0; i < 100; ++i) f.coordinator->Tick();
    if (f.attempts != 1) return false;
    f.now += 1000; f.coordinator->Tick(); if (f.attempts != 2) return false;
    f.available = true; f.now += 2000; f.coordinator->Tick();
    if (f.coordinator->Snapshot().at("activation_outcome") != "succeeded" || f.attempts != 3) return false;
    Phase4BFixture blocked; blocked.latch->armed = true; blocked.Stable("cs2.exe");
    WritePhase4BDiagnosticFixture("diagnostics.quarantined.mock.json", blocked.profiles->Diagnostics());
    if (blocked.coordinator->Snapshot().at("activation_outcome") != "blocked" || blocked.io->stages != 0 || blocked.latch->arms != 0) return false;
    for (int i = 0; i < 100; ++i) { blocked.now += 30000; blocked.coordinator->Tick(); }
    if (blocked.attempts != 1 || !blocked.latch->armed || blocked.profiles->State().at("selected_profile_id") != A) return false;
    Phase4BFixture failed; failed.io->fail_at_stage = 1; failed.Stable("cs2.exe");
    WritePhase4BDiagnosticFixture("diagnostics.failed.mock.json", failed.profiles->Diagnostics());
    const auto state = failed.profiles->State();
    if (failed.coordinator->Snapshot().at("activation_outcome") != "failed" || !state.at("active_profile_id").is_null() ||
        state.at("dirty") != true || state.at("selected_profile_id") != B || !failed.latch->armed) return false;
    for (int i = 0; i < 100; ++i) failed.coordinator->Tick();
    std::cout << "P4B deferred_attempts=3 quarantine_attempts=1 failure_attempts=1\n";
    return failed.attempts == 1 && failed.io->stages == 1;
}
bool TestPhase4BAlreadyActiveNoOp() {
    Phase4BFixture f;
    if (f.profiles->ActivateProfile(B, "Restore", 0).at("outcome") != "succeeded") return false;
    const auto writes = f.io->stages; f.Stable("cs2.exe");
    if (f.coordinator->Snapshot().at("activation_outcome") != "no-op" || f.io->stages != writes || f.attempts != 1) return false;
    for (int i = 0; i < 100; ++i) f.coordinator->Tick();
    return f.attempts == 1 && f.io->stages == writes;
}
bool TestPhase4BStaleChurnReturn() {
    Phase4BFixture f;
    f.profiles->ObserveAutomationForeground("cs2.exe"); f.now = 500; f.profiles->ObserveAutomationForeground("cs2.exe");
    f.before_admission = [&] { f.profiles->ObserveAutomationForeground("explorer.exe"); f.profiles->ObserveAutomationForeground("cs2.exe"); };
    f.coordinator->Tick();
    if (f.io->stages != 0 || f.coordinator->Snapshot().at("activation_outcome") != "stale") return false;
    f.before_admission = {}; f.coordinator->Tick(); if (f.io->stages != 0) return false;
    f.now += 500; f.profiles->ObserveAutomationForeground("cs2.exe"); f.coordinator->Tick();
    return f.profiles->State().at("active_profile_id") == B && f.attempts == 2;
}
bool TestPhase4BManualHoldRace() {
    Phase4BFixture f;
    f.profiles->ObserveAutomationForeground("cs2.exe"); f.now = 500; f.profiles->ObserveAutomationForeground("cs2.exe");
    const auto old = f.profiles->AutomationAdmissionSnapshot();
    const auto manual = f.profiles->ActivateProfile(A, "Manual", 0);
    const auto writes = f.io->stages;
    const auto rejected = f.profiles->ActivateAutomationDecision(old, [] { return false; });
    if (manual.at("outcome") != "succeeded" || rejected.at("outcome") != "stale" || f.io->stages != writes) return false;
    f.coordinator->Tick(); if (f.attempts != 0) return false;
    f.profiles->ObserveAutomationForeground(""); f.now += 500; f.profiles->ObserveAutomationForeground(""); f.coordinator->Tick();
    if (f.profiles->Diagnostics().at("automation").at("manual_hold") != true || f.attempts != 0) return false;
    f.Stable("explorer.exe"); f.Stable("cs2.exe");
    if (f.profiles->State().at("active_profile_id") != B) return false;
    Phase4BFixture deferred; deferred.available = false;
    deferred.profiles->ObserveAutomationForeground("cs2.exe"); deferred.now = 500; deferred.profiles->ObserveAutomationForeground("cs2.exe");
    const auto result = deferred.profiles->ActivateProfile(B, "Manual", 0);
    deferred.coordinator->Tick();
    return result.at("outcome") == "deferred" && deferred.attempts == 0 &&
        deferred.profiles->Diagnostics().at("automation").at("manual_action_sequence") == 1;
}
bool TestPhase4BConcurrentManualWins() {
    Phase4BFixture f;
    f.profiles->ObserveAutomationForeground("cs2.exe"); f.now = 500; f.profiles->ObserveAutomationForeground("cs2.exe");
    std::mutex mutex; std::condition_variable cv; bool captured = false, proceed = false;
    f.before_admission = [&] { std::unique_lock<std::mutex> lock(mutex); captured = true; cv.notify_all(); cv.wait(lock, [&] { return proceed; }); };
    std::thread automatic([&] { f.coordinator->Tick(); });
    { std::unique_lock<std::mutex> lock(mutex); if (!cv.wait_for(lock, std::chrono::seconds(3), [&] { return captured; })) {
        proceed = true; cv.notify_all(); automatic.join(); return false;
    } }
    const auto manual = f.profiles->ActivateProfile(A, "Manual", 0); const auto writes = f.io->stages;
    { std::lock_guard<std::mutex> lock(mutex); proceed = true; } cv.notify_all(); automatic.join();
    const auto state = f.profiles->State();
    std::cout << "P4B concurrent_manual_wins stale=1 extra_hardware=0\n";
    return manual.at("outcome") == "succeeded" && f.coordinator->Snapshot().at("activation_outcome") == "stale" &&
        state.at("selected_profile_id") == A && state.at("active_profile_id") == A && f.io->stages == writes;
}
bool TestPhase4BDocumentRaces() {
    for (const char* action : {"rename", "update", "delete", "automation"}) {
        Phase4BFixture f;
        f.profiles->ObserveAutomationForeground("cs2.exe"); f.now = 500; f.profiles->ObserveAutomationForeground("cs2.exe");
        const auto token = f.profiles->AutomationAdmissionSnapshot();
        httplib::Server server; f.profiles->RegisterRoutes(server); const int port = server.bind_to_any_port("127.0.0.1");
        if (port <= 0) return false;
        std::thread http([&] { server.listen_after_bind(); }); httplib::Client client("127.0.0.1", port); client.set_read_timeout(2);
        Json mutation{{"expected_revision", 0}, {"profile_id", B}};
        if (std::string(action) == "rename") mutation["name"] = "Renamed CS2";
        if (std::string(action) == "update") { auto doc = Json::parse(std::ifstream(f.path));
            mutation["profile"] = doc.at("profiles")[1]; mutation["profile"]["magnetic"]["global_actuation_mm"] = 3.0; }
        if (std::string(action) == "automation") { mutation.erase("profile_id"); auto doc = Json::parse(std::ifstream(f.path));
            mutation["device_profile_automation"] = doc.at("device_profile_automation"); mutation["device_profile_automation"]["enabled"] = false; }
        const auto changed = client.Post(std::string("/api/device-profiles/") + action, mutation.dump(), "application/json");
        const auto result = f.profiles->ActivateAutomationDecision(token, [] { return false; });
        server.stop(); http.join();
        if (!changed || changed->status != 200 || result.at("outcome") != "stale" || f.io->stages != 0 || f.io->connects != 0) return false;
        f.coordinator->Tick();
        if (std::string(action) == "delete" || std::string(action) == "automation") { if (f.attempts != 0) return false; }
        else if (f.profiles->State().at("active_profile_id") != B || f.attempts != 1) return false;
    }
    return true;
}
bool TestPhase4BOnDiskRevisionConflict() {
    Phase4BFixture f;
    f.profiles->ObserveAutomationForeground("cs2.exe"); f.now = 500; f.profiles->ObserveAutomationForeground("cs2.exe");
    auto doc = Json::parse(std::ifstream(f.path)); doc["revision"] = 1;
    doc["profiles"][1]["magnetic"]["global_actuation_mm"] = 3.0; Write(f.path, doc);
    f.coordinator->Tick();
    if (f.coordinator->Snapshot().at("activation_outcome") != "stale" || f.io->stages != 0 ||
        Json::parse(std::ifstream(f.path)).at("revision") != 1) return false;
    f.coordinator->Tick();
    return f.profiles->State().at("active_profile_id") == B &&
        f.device->GetAppliedRuntimeState().global_actuation_raw == 30;
}
bool TestPhase4BStaleAndShutdown() {
    Phase4BFixture f; f.profiles->ObserveAutomationForeground("cs2.exe"); f.now = 500; f.profiles->ObserveAutomationForeground("cs2.exe");
    auto token = f.profiles->AutomationAdmissionSnapshot();
    token["configuration_document_revision"] = 99;
    if (f.profiles->ActivateAutomationDecision(token, [] { return false; }).at("outcome") != "stale" || f.io->stages != 0) return false;
    token = f.profiles->AutomationAdmissionSnapshot(); token["m605_session_generation"] = 99;
    if (f.profiles->ActivateAutomationDecision(token, [] { return false; }).at("outcome") != "stale" || f.io->stages != 0) return false;
    token = f.profiles->AutomationAdmissionSnapshot();
    if (f.profiles->ActivateAutomationDecision(token, [] { return true; }).at("outcome") != "cancelled" || f.io->stages != 0) return false;
    httplib::Server server; f.profiles->RegisterRoutes(server);
    const int port = server.bind_to_any_port("127.0.0.1"); if (port <= 0) return false;
    std::thread http([&] { server.listen_after_bind(); });
    httplib::Client client("127.0.0.1", port); client.set_read_timeout(1);
    f.wait->Block(); f.coordinator->Start(); f.coordinator->Wake();
    if (!f.wait->Await()) { f.wait->Release(); server.stop(); http.join(); return false; }
    const auto probes = f.io->presence_probes;
    const auto status = client.Get("/api/device-profiles/automation/status");
    const bool readable = status && status->status == 200 &&
        Json::parse(status->body).at("automation_decision").at("coordinator_state") == "Activating" && f.io->presence_probes == probes;
    f.profiles->ObserveAutomationForeground("explorer.exe");
    std::thread stopper([&] { f.coordinator->Stop(); });
    f.wait->Release(); stopper.join();
    server.stop(); http.join();
    const auto writes = f.io->stages; f.now += 500; f.profiles->ObserveAutomationForeground("explorer.exe"); f.coordinator->Tick();
    return readable && f.io->stages == writes && f.attempts == 1 && f.coordinator->Snapshot().at("coordinator_state") == "Shutdown";
}
bool TestPhase4BRtHardwareGate() {
    Phase4BFixture f(true);
    if (!f.device->RestorePerKeyDksToStandard(0x0701).get()) return false;
    aura::HardwareRtGateObservation gate; gate.state = aura::HardwareRtGateState::Off;
    f.device->ObserveHardwareRtGate(gate); f.Stable("cs2.exe");
    const auto state = f.profiles->State();
    if (state.at("active_profile_id") != B || state.at("dirty") != false ||
        !f.device->GetAppliedRuntimeState().per_key_rapid_trigger.at(0x0701).enabled) return false;
    const auto sequence = f.profiles->Diagnostics().at("automation").at("decision_sequence");
    const int writes = f.io->stages;
    gate.state = aura::HardwareRtGateState::On; f.device->ObserveHardwareRtGate(gate); f.coordinator->Tick();
    return f.io->stages == writes && f.attempts == 1 && f.profiles->State().at("dirty") == false &&
        f.device->GetAppliedRuntimeState().per_key_rapid_trigger.at(0x0701).enabled &&
        f.profiles->Diagnostics().at("automation").at("decision_sequence") == sequence;
}
}
