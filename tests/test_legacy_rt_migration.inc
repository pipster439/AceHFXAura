// Real document owner with mock transport only. Migration never calls M605.
namespace {
Json LegacyRtValue() {
    return {{"enabled", true}, {"press_mm", 0.5}, {"release_mm", 1.5},
        {"separate_mode", true}, {"top_mm", 0.0}, {"bottom_mm", 0.0}, {"future_legacy", {{"keep", 7}}}};
}
struct LegacyMigrationFixture {
    std::filesystem::path folder, path;
    std::mutex gate;
    Io* io = nullptr;
    std::shared_ptr<LatchState> latch = std::make_shared<LatchState>();
    std::unique_ptr<aura::M605Runtime> device;
    std::unique_ptr<aura::DeviceProfileRuntime> profiles;
    int64_t now = 0;
    explicit LegacyMigrationFixture(Json doc, int failure = 0) {
        static std::atomic<int> serial{0};
        folder = std::filesystem::temp_directory_path() / ("aura-legacy-rt-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(++serial));
        std::filesystem::create_directories(folder); path = folder / "device-profiles.json";
        Write(path, doc);
        if (failure == 1) std::filesystem::create_directory(Backup());
        if (failure == 2) SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY);
        auto transport = std::make_unique<Io>(); io = transport.get();
        device = aura::M605RuntimeTestAccess::Create(std::move(transport), std::make_unique<Wait>(), std::make_unique<Latch>(latch));
        Load();
    }
    void Load() {
        profiles = std::make_unique<aura::DeviceProfileRuntime>(path, std::filesystem::path{}, *device, gate,
            [] { return aura::MagneticHostProfile{}; }, [] { return true; }, [this] { return now; });
    }
    std::filesystem::path Backup() const { return std::filesystem::path(path.wstring() + L".legacy-rt-v1.r0.bak"); }
    std::string Bytes() const { std::ifstream f(path, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
    Json Disk() const { return Json::parse(Bytes()); }
    bool NoHardware() const { return io->stages == 0 && io->connects == 0 && io->presence_probes == 0 && latch->arms == 0; }
    ~LegacyMigrationFixture() {
        profiles.reset(); device->Stop(); SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        SetFileAttributesW(Backup().c_str(), FILE_ATTRIBUTE_NORMAL);
        std::error_code e; std::filesystem::remove_all(folder, e);
    }
};
Json LegacyCleanDoc() {
    auto doc = Document();
    doc["profiles"] = Json::array({doc["profiles"][0]});
    doc["profiles"][0]["magnetic"]["keys"] = Json::array();
    return doc;
}
Json* FindRtKey(Json& layer, uint16_t key) {
    for (auto& k : layer["keys"]) if (k.at("logical_id") == key) return &k;
    return nullptr;
}
Json StandardDks() {
    Json slots = Json::array();
    for (int i=0;i<4;++i) slots.push_back({{"target", {{"kind", "DefaultSentinel"}, {"logical_id", nullptr}}},
        {"down_start", "Inactive"}, {"down_end", "Inactive"}, {"up_start", "Inactive"}, {"up_end", "Inactive"}});
    return {{"standard", true}, {"start_mm", 1.0}, {"end_mm", 3.6}, {"slots", slots}};
}
bool TestLegacyMigrationConversion() {
    auto doc = LegacyCleanDoc(); doc["profiles"][0]["magnetic"]["global_rapid_trigger"] = LegacyRtValue();
    LegacyMigrationFixture f(doc);
    if (!f.NoHardware()) return false;
    auto disk=f.Disk();
    if (disk.at("revision") != 1 || disk.at("selected_profile_id") != A ||
        disk.at("profiles")[0].at("magnetic").contains("global_rapid_trigger") ||
        disk.at("profiles")[0].at("magnetic").at("keys").size() != 68 ||
        disk.at("root_extension") != doc.at("root_extension") ||
        disk.at("legacy_rt_migration_v1").at("archived")[0].at("original") != LegacyRtValue()) return false;
    std::ifstream backup(f.Backup()); if (Json::parse(backup) != doc) return false;
    for (const auto& k : disk["profiles"][0]["magnetic"]["keys"])
        if (k.at("rapid_trigger").at("press_mm") != 0.5 || k.at("rapid_trigger").at("release_mm") != 1.5 ||
            k.at("rapid_trigger").at("enabled") != true || k.at("rapid_trigger").contains("top_mm")) return false;
    auto diagnostic=f.profiles->Diagnostics();
    return f.NoHardware() && diagnostic.at("legacy_rt_migration").at("status") == "Completed" &&
        diagnostic.at("rapid_trigger").at("legacy_global_rt_present") == false && diagnostic.at("runtime").at("dirty") == true;
}
bool TestLegacyMigrationIdempotence() {
    auto doc=LegacyCleanDoc(); doc["profiles"][0]["magnetic"]["global_rapid_trigger"]=LegacyRtValue();
    LegacyMigrationFixture f(doc); const auto bytes=f.Bytes();
    f.Load(); if (f.Bytes()!=bytes || f.Disk().at("revision")!=1 || !f.NoHardware()) return false;
    auto nolegacy=LegacyCleanDoc(); nolegacy["profiles"][0]["magnetic"].erase("global_rapid_trigger");
    LegacyMigrationFixture n(nolegacy);
    return n.Disk()==nolegacy && !std::filesystem::exists(n.Backup()) && n.NoHardware() &&
        n.profiles->Diagnostics().at("legacy_rt_migration").at("status")=="NotRequired";
}
bool TestLegacyMigrationExplicitPrecedence() {
    auto doc=LegacyCleanDoc(); auto& m=doc["profiles"][0]["magnetic"];
    m["global_rapid_trigger"]=LegacyRtValue();
    const Json explicit_rt={{"enabled",false},{"press_mm",0.7},{"release_mm",1.2},{"separate_mode",true},{"future_rt",9}};
    m["keys"]=Json::array({{{"logical_id",0x0701},{"rapid_trigger",explicit_rt},{"actuation_mm",0.3},{"key_extension","keep"}},
        {{"logical_id",0x0602},{"rapid_trigger",nullptr}}});
    LegacyMigrationFixture f(doc); auto after=f.Disk()["profiles"][0]["magnetic"];
    auto* w=FindRtKey(after,0x0701); auto* a=FindRtKey(after,0x0602);
    return f.NoHardware() && after.at("keys").size()==68 && w && a && w->at("rapid_trigger")==explicit_rt &&
        w->at("actuation_mm")==0.3 && w->at("key_extension")=="keep" && a->at("rapid_trigger").at("enabled")==true &&
        f.profiles->Diagnostics().at("legacy_rt_migration").at("entries")[0].at("generated_key_count")==67;
}
bool TestLegacyMigrationRootPrecedence() {
    auto doc=LegacyCleanDoc(); doc["global_defaults"]["global_rapid_trigger"]=LegacyRtValue();
    Json rt={{"enabled",false},{"press_mm",0.9},{"release_mm",0.9},{"separate_mode",false}};
    doc["global_defaults"]["keys"]=Json::array({{{"logical_id",0x0701},{"rapid_trigger",rt}}});
    doc["profiles"][0]["magnetic"]["global_rapid_trigger"]=LegacyRtValue();
    LegacyMigrationFixture f(doc); auto disk=f.Disk(); auto root=disk["global_defaults"], profile=disk["profiles"][0]["magnetic"];
    return f.NoHardware() && disk.at("revision")==1 && FindRtKey(root,0x0701)->at("rapid_trigger")==rt &&
        FindRtKey(profile,0x0701)==nullptr && root.at("keys").size()==68 && profile.at("keys").size()==67;
}
bool TestLegacyMigrationEmpty() {
    auto doc=LegacyCleanDoc(); doc["profiles"][0]["magnetic"]["global_rapid_trigger"]=Json::object();
    LegacyMigrationFixture f(doc);
    if (!f.NoHardware() || f.Disk().at("revision")!=1 || !f.Disk()["profiles"][0]["magnetic"]["keys"].empty()) return false;
    auto result=f.profiles->ActivateProfile(A,"Manual",1);
    return result.at("outcome")=="succeeded" && f.profiles->Diagnostics().at("last_plan").at("rapid_trigger_management").at("legacy_global_rt_present")==false;
}
bool TestLegacyMigrationFailure(int failure) {
    auto doc=LegacyCleanDoc(); doc["profiles"][0]["magnetic"]["global_rapid_trigger"]=LegacyRtValue();
    LegacyMigrationFixture f(doc,failure);
    return f.NoHardware() && f.Disk()==doc && f.profiles->Diagnostics().at("legacy_rt_migration").at("status")=="Failed" &&
        f.profiles->Diagnostics().at("rapid_trigger").at("legacy_global_rt_present")==true;
}
bool TestLegacyMigrationUnsupported() {
    for (int kind=0;kind<6;++kind) {
        auto doc=LegacyCleanDoc(); auto value=LegacyRtValue();
        if(kind==0) value["bottom_mm"]=0.1;
        if(kind==1) value["enabled"]=false;
        if(kind==2) value["press_mm"]=0.0;
        if(kind==3) value["continuous"]=true;
        if(kind==4) value=Json::array({1,2});
        if(kind==5) value={{"enabled",true}};
        doc["profiles"][0]["magnetic"]["global_rapid_trigger"]=value;
        LegacyMigrationFixture f(doc); if (!f.NoHardware() || f.Disk()!=doc || std::filesystem::exists(f.Backup())) return false;
        auto result=f.profiles->ActivateProfile(A,"Manual",0);
        if(result.at("outcome")!="failed" || result.at("error").get<std::string>().find("Legacy global RT")==std::string::npos || f.io->stages!=0) return false;
    }
    return true;
}
bool TestLegacyMigrationBoundaryAndCollision() {
    for (double value : {0.1, 2.5}) {
        auto doc=LegacyCleanDoc(); auto rt=LegacyRtValue(); rt["press_mm"]=value; rt["release_mm"]=value;
        doc["profiles"][0]["magnetic"]["global_rapid_trigger"]=rt;
        LegacyMigrationFixture f(doc);
        if(!f.NoHardware() || f.Disk().at("revision")!=1 ||
            f.Disk()["profiles"][0]["magnetic"]["keys"][0]["rapid_trigger"]["press_mm"]!=value) return false;
    }
    auto conflict=LegacyCleanDoc(); conflict["profiles"][0]["magnetic"]["global_rapid_trigger"]=LegacyRtValue();
    auto dks=StandardDks(); dks["standard"]=false;
    conflict["profiles"][0]["magnetic"]["keys"].push_back({{"logical_id",0x0405},{"dks",dks}});
    LegacyMigrationFixture f(conflict);
    if(!f.NoHardware() || f.Disk()!=conflict || std::filesystem::exists(f.Backup())) return false;
    auto collision=LegacyCleanDoc(); collision["profiles"][0]["magnetic"]["global_rapid_trigger"]=LegacyRtValue();
    collision["legacy_rt_migration_v1"]={{"future",true}};
    LegacyMigrationFixture g(collision);
    return g.NoHardware() && g.Disk()==collision && !std::filesystem::exists(g.Backup());
}
bool TestLegacyMigrationApiGate() {
    LegacyMigrationFixture f(LegacyCleanDoc()); httplib::Server server; f.profiles->RegisterRoutes(server);
    auto port=server.bind_to_any_port("127.0.0.1"); std::thread worker([&]{server.listen_after_bind();});
    httplib::Client client("127.0.0.1",port);
    auto profile=f.Disk()["profiles"][0]; profile["magnetic"]["global_rapid_trigger"]=LegacyRtValue();
    auto update=client.Post("/api/device-profiles/update",Json{{"expected_revision",0},{"profile_id",A},{"profile",profile}}.dump(),"application/json");
    auto defaults=f.Disk()["global_defaults"]; defaults["global_rapid_trigger"]=LegacyRtValue();
    auto baseline=client.Post("/api/device-profiles/defaults",Json{{"expected_revision",0},{"global_defaults",defaults}}.dump(),"application/json");
    auto stale=client.Post("/api/device-profiles/migrate-legacy-rt",R"({"expected_revision":999})","application/json");
    bool manual_rejected=false;
    { std::lock_guard<std::mutex> lock(f.gate); try { f.profiles->RecordManualGlobalBaselineLocked("global_rapid_trigger",LegacyRtValue()); } catch(...) { manual_rejected=true; } }
    server.stop(); worker.join();
    return update && update->status==422 && baseline && baseline->status==422 && stale && stale->status==409 &&
        manual_rejected && f.Disk().at("revision")==0 && f.io->stages==0;
}
bool TestLegacyMigrationExplicitActivation() {
    auto doc=LegacyCleanDoc(); auto& m=doc["profiles"][0]["magnetic"]; m["global_rapid_trigger"]=LegacyRtValue();
    for(const auto& p:aura::m605::detail::kVerifiedPhysicalKeys)
        m["keys"].push_back({{"logical_id",p.logical_id},{"dks",StandardDks()}});
    LegacyMigrationFixture f(doc); if(!f.NoHardware()) return false;
    auto result=f.profiles->ActivateProfile(A,"Manual",1);
    auto diag=f.profiles->Diagnostics();
    return result.at("outcome")=="succeeded" && diag.at("last_plan").at("rapid_trigger_management").at("legacy_global_rt_present")==false &&
        diag.at("last_plan").at("rapid_trigger_management").at("managed_key_count")==68;
}
bool TestLegacyMigrationP4BIntegration() {
    auto doc=LegacyCleanDoc(); doc["profiles"][0]["magnetic"]["global_rapid_trigger"]=Json::object();
    auto config=aura::DeviceProfileAutomationConfig::DefaultJson(); config["enabled"]=true;
    config["bindings"]=Json::array({{{"rule_id",C},{"process_name","cs2.exe"},{"profile_id",A},{"priority",100},{"enabled",true}}});
    doc["device_profile_automation"]=config;
    LegacyMigrationFixture f(doc,1); int attempts=0;
    aura::DeviceProfileAutomationCoordinator coordinator([&]{return f.profiles->AutomationAdmissionSnapshot();},
        [&](const auto& token,const auto& stopping){++attempts;return f.profiles->ActivateAutomationDecision(token,stopping);},[&]{return f.now;});
    f.profiles->SetAutomationCoordinatorDiagnostics([&]{return coordinator.Snapshot();});
    f.profiles->ObserveAutomationForeground("cs2.exe"); f.now+=500; f.profiles->ObserveAutomationForeground("cs2.exe"); coordinator.Tick();
    if(coordinator.Snapshot().at("activation_outcome")!="failed" || f.io->stages!=0 || attempts!=1) return false;
    std::filesystem::remove(f.Backup());
    httplib::Server server; f.profiles->RegisterRoutes(server);
    auto port=server.bind_to_any_port("127.0.0.1"); std::thread worker([&]{server.listen_after_bind();});
    httplib::Client client("127.0.0.1",port);
    const auto probes=f.io->presence_probes, connects=f.io->connects, stages=f.io->stages;
    auto migration=client.Post("/api/device-profiles/migrate-legacy-rt",R"({"expected_revision":0})","application/json");
    auto repeat=client.Post("/api/device-profiles/migrate-legacy-rt",R"({"expected_revision":1})","application/json");
    server.stop(); worker.join();
    if(!migration || migration->status!=200 || !repeat || repeat->status!=200 || f.Disk().at("revision")!=1 ||
        f.io->presence_probes!=probes || f.io->connects!=connects || f.io->stages!=stages) return false;
    coordinator.Tick(); auto diag=f.profiles->Diagnostics();
    char* directory=nullptr; size_t size=0;
    if(_dupenv_s(&directory,&size,"AURA_LEGACY_RT_TEST_ARTIFACT_DIR")==0 && directory) {
        std::filesystem::create_directories(directory); diag["test_fixture"]="real Runtime; mock transport; NOT physical validation";
        Write(std::filesystem::path(directory)/"diagnostics.after_migration.mock.json",diag); std::free(directory);
    }
    std::cout << "Legacy migration P4B: before failed/0 hardware; migration 0 probes/connects/stages; after succeeded; attempts=" << attempts << '\n';
    return attempts==2 && coordinator.Snapshot().at("activation_outcome")=="succeeded" &&
        diag.at("rapid_trigger").at("legacy_global_rt_present")==false && diag.at("runtime").at("active_profile_id")==A;
}
}
