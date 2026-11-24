// HTTP CRUD + real automation coordinator with injected fake M605 only.
namespace {
bool TestAutomationQuickCreate() {
    Phase4BFixture f(false,"cs2.exe",true,true,"AuraDirectRgb");
    httplib::Server server; f.profiles->RegisterRoutes(server);
    const int port=server.bind_to_any_port("127.0.0.1"); if(port<=0)return false;
    std::thread thread([&]{server.listen_after_bind();});
    struct Stop { httplib::Server& server; std::thread& thread; ~Stop(){server.stop();thread.join();} } stop{server,thread};
    httplib::Client client("127.0.0.1",port);
    const auto post=[&](const char* path,const Json& body) {return client.Post(path,body.dump(),"application/json");};
    const auto original=Json::parse(std::ifstream(f.path));
    const auto create=[&](int slot,const char* name,const char* owner,int64_t revision) {
        return post("/api/device-profiles/create",{{"name",name},{"activation_backend","hardware_slot"},
            {"hardware_slot",slot},{"lighting_control_owner",owner},{"expected_revision",revision}});
    };
    #define QUICK_CHECK(x) do {if(!(x)){std::cerr<<"quick-create line "<<__LINE__<<'\n';return false;}} while(false)
    QUICK_CHECK(Json::parse(client.Get("/api/device-profiles/automation")->body).at("hardware_profile_create_supported")==true);
    for(int slot:{0,6}) QUICK_CHECK(create(slot,"invalid","AuraDirectRgb",0)->status==422);
    QUICK_CHECK(create(3,"invalid","invented",0)->status==422);
    QUICK_CHECK(Json::parse(std::ifstream(f.path))==original && f.io->stages==0 && f.io->applies==0);
    const auto r3=create(3,"quick slot 3","AuraDirectRgb",0); QUICK_CHECK(r3 && r3->status==200);
    const auto p3=Json::parse(r3->body).at("profile"); const auto id3=p3.at("id");
    QUICK_CHECK(p3.at("hardware_slot")==3 && p3.at("lighting").at("control_owner")=="AuraDirectRgb");
    QUICK_CHECK(create(5,"stale","FirmwareBank",0)->status==409);
    QUICK_CHECK(create(5,"quick slot 3","FirmwareBank",1)->status==422);
    const auto r5=create(5,"quick slot 5","FirmwareBank",1); QUICK_CHECK(r5 && r5->status==200);
    const auto id5=Json::parse(r5->body).at("profile").at("id");
    const auto doc=Json::parse(std::ifstream(f.path));
    QUICK_CHECK(doc.at("profiles")[0]==original.at("profiles")[0] && doc.at("profiles")[1]==original.at("profiles")[1]);
    QUICK_CHECK(doc.at("selected_profile_id")==A && f.io->stages==0 && f.io->applies==0);
    auto config=doc.at("device_profile_automation"); config["bindings"][0]["profile_id"]=id3;
    config["fallback_profile_id"]=id5;
    auto saved=post("/api/device-profiles/automation",{{"expected_revision",2},{"device_profile_automation",config}});
    QUICK_CHECK(saved && saved->status==200 && f.io->stages==0 && f.io->applies==0);
    // Existing backend consumes the new UUID: HostManaged -> slot 3 -> fallback slot 5.
    f.Stable("cs2.exe"); QUICK_CHECK(f.profiles->State().at("active_profile_id")==id3 && f.io->hardware_slot==3);
    QUICK_CHECK(f.profiles->WithAuraLightingOwnership([]{return true;}));
    f.Stable("other.exe"); QUICK_CHECK(f.profiles->State().at("active_profile_id")==id5 && f.io->hardware_slot==5);
    QUICK_CHECK(!f.profiles->WithAuraLightingOwnership([]{return true;}));
    QUICK_CHECK(f.io->applies==0);
    for(const auto& report:f.io->reports) QUICK_CHECK(report[1]==0x51 && report[2]==0); // selector only
    auto manual=f.profiles->ActivateProfile(id3.get<std::string>(),"Manual",f.profiles->State().at("document_revision"));
    QUICK_CHECK(manual.at("outcome")=="succeeded"); const auto stages=f.io->stages;
    f.Stable("other.exe"); QUICK_CHECK(f.io->stages==stages && f.profiles->Diagnostics().at("automation").at("manual_hold")==true);
    // Creation is an offline document operation even when activation is gated.
    f.io->firmware={}; f.io->current_session=false;
    auto revision=f.profiles->State().at("document_revision");
    const auto unsupported=create(4,"offline unknown firmware","AuraDirectRgb",revision);
    QUICK_CHECK(unsupported && unsupported->status==200);
    const auto before=f.io->stages;
    bool rejected=false; try{f.profiles->ActivateProfile(Json::parse(unsupported->body).at("profile").at("id").get<std::string>(),"Manual",
        f.profiles->State().at("document_revision"));}catch(const std::exception&){rejected=true;}
    QUICK_CHECK(rejected && f.io->stages==before && f.io->applies==0);
    // Legacy clients retain the old create payload and host defaults.
    auto legacy=post("/api/device-profiles/create",{{"name","old client"},{"expected_revision",f.profiles->State().at("document_revision")}});
    QUICK_CHECK(legacy && legacy->status==200);
    const auto old=Json::parse(legacy->body).at("profile");
    QUICK_CHECK(old.at("activation_backend")=="host_managed" && !old.at("lighting").contains("control_owner"));
    std::cout<<"quick-create: atomic document, UUID binding, Host -> slot 3 -> slot 5, ManualHold, firmware gate, selector-only verified with fake transport\n";
    #undef QUICK_CHECK
    return true;
}
}
