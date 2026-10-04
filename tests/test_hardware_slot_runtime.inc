// Production Profile Runtime / coordinator with mock HID. Never opens hardware.
namespace {
bool TestHardwareSlotRuntime() {
    const auto activate = [](Phase4BFixture& f) {
        return f.profiles->ActivateProfile(B, "Manual", f.profiles->State().at("document_revision"));
    };
    {
        Phase4BFixture f(false,"notepad.exe",true);
        f.Stable("notepad.exe");
        const auto selectors=f.io->stages, attempts=f.attempts;
        f.io->hardware_slot=1; f.now+=10000;
        f.profiles->ObserveHardwareSlot();
        for(int i=0;i<100;++i) {
            f.profiles->ObserveAutomationForeground("notepad.exe");
            f.coordinator->Tick();
        }
        // A bounded mismatch observation is a state warning, not a fresh
        // foreground decision. It must not create an automatic reclaim.
        const auto state=f.profiles->Diagnostics().at("runtime");
        if(f.io->stages!=selectors || f.attempts!=attempts ||
           !state.at("active_profile_id").is_null() || state.at("dirty")!=true) return false;
    }
    {
        Phase4BFixture f(false,"notepad.exe",true);
        int frames=0;
        const auto frame=[&] { ++frames; return true; };
        // Saved HardwareSlot backend suppresses the first startup frame and
        // the same gate also wraps shutdown frames; no transport is opened.
        if(f.profiles->WithAuraLightingOwnership(frame) || frames) return false;
        auto r=activate(f);
        if(r.at("outcome")!="succeeded" || f.profiles->WithAuraLightingOwnership(frame) || frames) return false;
        const auto queries=f.io->basic_queries, writes=f.io->stages;
        f.now+=9999; f.profiles->ObserveHardwareSlot();
        if(f.io->basic_queries!=queries) return false;
        f.io->hardware_slot=1; ++f.now; f.profiles->ObserveHardwareSlot();
        auto d=f.profiles->Diagnostics();
        if(f.io->basic_queries!=queries+1 || f.io->stages!=writes ||
           !d.at("runtime").at("active_profile_id").is_null() || d.at("runtime").at("dirty")!=true ||
           d.at("hardware_slot_status").at("hardware_slot_match")!=false ||
           d.at("hardware_slot_status").at("lighting_owner")!="FirmwareBank") return false;
        for(int i=0;i<100;++i) { f.profiles->ObserveHardwareSlot(); f.profiles->Diagnostics(); }
        if(f.io->basic_queries!=queries+1 || f.io->stages!=writes || frames) return false;
        f.io->basic_timeout=true; f.now+=10000; f.profiles->ObserveHardwareSlot();
        const auto failed_queries=f.io->basic_queries;
        for(int i=0;i<100;++i) f.profiles->ObserveHardwareSlot();
        if(f.io->basic_queries!=failed_queries || f.io->stages!=writes) return false;
        WritePhase4BDiagnosticFixture("hardware-slot-drift.diagnostics.mock.json",f.profiles->Diagnostics());
    }
    {
        Phase4BFixture f(false,"notepad.exe",true);
        auto r=activate(f);
        if (r.at("outcome")!="succeeded" || r.at("dirty")!=false || r.at("active_profile_id")!=B ||
            f.io->stages!=1 || f.io->applies || f.latch->arms || f.io->basic_queries!=3) return false;
        if (f.io->reports.front()!=*aura::m605::BuildSelectHardwareProfileSlot(5)) return false;
        if (!f.profiles->Diagnostics().at("last_plan").at("effective_target").empty()) return false;
        auto queries=f.io->basic_queries; auto writes=f.io->stages;
        for (int i=0;i<10;++i) f.profiles->Diagnostics();
        if(f.io->basic_queries!=queries || f.io->stages!=writes) return false;
        // Fresh query, never cached submission, authorizes same-slot no-op.
        r=activate(f);if(r.at("outcome")!="succeeded" || f.io->stages!=writes || f.io->basic_queries!=queries+1) return false;
        f.io->hardware_slot=1;
        r=f.profiles->RefreshHardwareSlot();
        if(r.at("dirty")!=true || !r.at("active_profile_id").is_null() || f.io->stages!=writes) return false;
        f.io->current_session=false; f.io->hardware_slot=5;
        r=f.profiles->State();
        if(r.at("dirty")!=false || r.at("active_profile_id")!=B || f.io->stages!=writes) return false;
        const auto after_reconnect_queries=f.io->basic_queries;
        for(int i=0;i<10;++i) f.profiles->State();
        if(f.io->basic_queries!=after_reconnect_queries) return false;
        f.io->hardware_slot=5; f.io->current_session=false;
        r=f.profiles->RefreshHardwareSlot();
        if(r.at("dirty")!=false || r.at("active_profile_id")!=B || f.io->stages!=writes) return false;
        f.io->current_session=false;f.io->hardware_slot=1;
        r=f.profiles->RefreshHardwareSlot();
        if(r.at("dirty")!=true || !r.at("active_profile_id").is_null() || f.io->stages!=writes) return false;
        WritePhase4BDiagnosticFixture("hardware-slot.diagnostics.mock.json",f.profiles->Diagnostics());
    }
    for(int failure=0;failure<5;++failure) {
        Phase4BFixture f(false,"notepad.exe",true);
        if(failure==0) f.io->selector_changes_slot=false;
        if(failure==1) f.io->fail_at_stage=1;
        if(failure==2) f.io->basic_timeout=true;
        if(failure==3) f.io->basic_malformed=true;
        if(failure==4) { f.latch->armed=true; }
        auto r=activate(f);
        if(r.at("outcome")!="failed" || r.at("dirty")!=true || !r.at("active_profile_id").is_null() || f.io->applies) return false;
        if(failure>=2 && f.io->stages!=0) return false;
    }
    {
        Phase4BFixture f(false,"notepad.exe",true);
        f.Stable("notepad.exe"); const auto old=f.profiles->AutomationAdmissionSnapshot();
        auto stages=f.io->stages;
        for(int i=0;i<30;++i) f.coordinator->Tick();
        if(f.attempts!=1 || f.io->stages!=stages) return false;
        f.Stable("charmap.exe");f.Stable("notepad.exe");
        if(f.attempts!=3 || f.io->stages!=3 || f.io->hardware_slot!=5 || f.io->applies) return false;
        f.Stable("aura.exe");auto r=f.profiles->ActivateProfile(A,"Manual",f.profiles->State().at("document_revision"));
        if(r.at("outcome")!="succeeded") return false;
        stages=f.io->stages; auto attempts=f.attempts;
        f.Stable("notepad.exe");f.coordinator->Tick();
        if(f.attempts!=attempts || f.io->stages!=stages || f.io->hardware_slot!=1) return false;
        r=f.profiles->ActivateAutomationDecision(old,[]{return false;});
        if(r.at("outcome")!="stale" || f.io->stages!=stages) return false;
    }
    {
        Phase4BFixture f(false,"notepad.exe",true);
        // Foreground change during BasicInfo invalidates late admission before selector.
        f.io->after_basic=[&]{ f.profiles->ObserveAutomationForeground("charmap.exe"); };
        f.Stable("notepad.exe");
        if(f.io->stages || f.io->applies) return false;
    }
    {
        Phase4BFixture f(false,"notepad.exe",true);
        httplib::Server server; f.profiles->RegisterRoutes(server);
        const auto port=server.bind_to_any_port("127.0.0.1"); if(port<=0) return false;
        std::thread worker([&]{server.listen_after_bind();}); httplib::Client client("127.0.0.1",port);
        bool okay=true;
        auto doc=Json::parse(std::ifstream(f.path)); const auto unchanged=doc;
        auto profile=doc.at("profiles").at(1);
        for(auto slot : {Json(nullptr),Json(0),Json(6),Json(7)}) {
            profile["hardware_slot"]=slot;
            auto reply=client.Post("/api/device-profiles/update",Json{{"profile_id",B},{"profile",profile},
                {"expected_revision",doc.at("revision")}}.dump(),"application/json");
            okay &= reply && reply->status==422 && Json::parse(std::ifstream(f.path))==unchanged;
        }
        profile["hardware_slot"]=5; profile["activation_backend"]="future";
        auto bad=client.Post("/api/device-profiles/update",Json{{"profile_id",B},{"profile",profile},
            {"expected_revision",doc.at("revision")}}.dump(),"application/json"); okay &= bad && bad->status==422;
        profile["activation_backend"]="hardware_slot";
        auto save=client.Post("/api/device-profiles/update",Json{{"profile_id",B},{"profile",profile},
            {"expected_revision",doc.at("revision")}}.dump(),"application/json"); okay &= save && save->status==200 && f.io->stages==0;
        auto persisted=Json::parse(std::ifstream(f.path));
        okay &= persisted.at("profiles").at(1).at("profile_extension")==profile.at("profile_extension");
        const int queries=f.io->basic_queries;
        client.Get("/api/device-profiles/diagnostics"); okay &= f.io->basic_queries==queries;
        const auto forbidden=client.Post("/api/device-profiles/hardware-slot/refresh",{{"Origin","https://example.invalid"}},"{}","application/json");
        okay &= forbidden && forbidden->status==403 && f.io->basic_queries==queries;
        server.stop();worker.join(); if(!okay) return false;
    }
    {
        Phase4BFixture f(false,"notepad.exe",true);
        int frames=0; const auto frame=[&] { ++frames; return true; };
        if(activate(f).at("outcome")!="succeeded") return false;
        httplib::Server server; f.profiles->RegisterRoutes(server);
        const auto port=server.bind_to_any_port("127.0.0.1"); if(port<=0) return false;
        std::thread worker([&]{server.listen_after_bind();}); httplib::Client client("127.0.0.1",port);
        auto doc=Json::parse(std::ifstream(f.path)); auto p=doc.at("profiles").at(0);
        p["activation_backend"]="host_managed"; p["hardware_slot"]=nullptr;
        auto save=client.Post("/api/device-profiles/update",Json{{"profile_id",A},{"profile",p},
            {"expected_revision",doc.at("revision")}}.dump(),"application/json");
        bool okay=save && save->status==200 && !f.profiles->WithAuraLightingOwnership(frame) && frames==0;
        auto r=f.profiles->ActivateProfile(A,"Manual",f.profiles->State().at("document_revision"));
        okay &= r.at("outcome")=="succeeded" && f.profiles->WithAuraLightingOwnership(frame) && frames==1;
        server.stop(); worker.join(); if(!okay) return false;
    }
    return true;
}
}
