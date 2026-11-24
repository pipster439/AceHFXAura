// Real Profile runtime + synthetic transport, never native HID enumeration.
#include "aura/native_hid_backend.h"
#include "engine/builtin_effects.h"
namespace {
#define LIGHT_CHECK(x) do { if (!(x)) { std::cerr << "lighting line " << __LINE__ << ": " << #x << '\n'; return false; } } while (0)
bool LightingFrame(Phase4BFixture& f, std::vector<std::array<uint8_t,65>>& rgb, uint8_t red = 90) {
    return f.profiles->WithAuraLightingOwnership([&] {
        const uint8_t index = 1, colors[] = {red, 20, 30};
        rgb.push_back(aura::NativeHidBackend::BuildReport(&index, colors, 1));
        return true;
    });
}
Json LightingActivate(Phase4BFixture& f, const char* id = B) {
    return f.profiles->ActivateProfile(id,"Manual",f.profiles->State().at("document_revision"));
}
bool TestLightingCombinations() {
    for (bool hardware : {false,true}) for (const char* owner : {static_cast<const char*>(nullptr),"AuraDirectRgb","FirmwareBank"}) {
        Phase4BFixture f(false,"cs2.exe",hardware,false,owner);
        const bool aura = owner ? std::string(owner)=="AuraDirectRgb" : !hardware;
        std::vector<std::array<uint8_t,65>> rgb;
        // Persisted policy controls the very first frame, without claiming activation.
        LIGHT_CHECK(LightingFrame(f,rgb)==aura);
        LIGHT_CHECK(f.profiles->Diagnostics().at("runtime").at("active_profile_id").is_null());
        LIGHT_CHECK(LightingActivate(f).at("outcome")=="succeeded");
        const auto stages=f.io->stages, applies=f.io->applies;
        LIGHT_CHECK(LightingFrame(f,rgb)==aura);
        // Shutdown blackout uses this same admission callback.
        LIGHT_CHECK(LightingFrame(f,rgb,0)==aura);
        LIGHT_CHECK(f.io->stages==stages && f.io->applies==applies);
        if(hardware) {
            LIGHT_CHECK(stages==1 && applies==0 && f.latch->arms==0 && f.io->hardware_slot==5);
            LIGHT_CHECK(f.io->reports==std::vector<aura::m605::Report>{*aura::m605::BuildSelectHardwareProfileSlot(5)});
        }
        if(aura) for(const auto& r:rgb) LIGHT_CHECK(r[0]==0 && r[1]==0xc0 && r[2]==0x81);
        else LIGHT_CHECK(f.profiles->Diagnostics().at("hardware_slot_status").at("lighting_diagnostic")=="FirmwareLightingRestoreUnverified");
        WritePhase4BDiagnosticFixture(hardware && aura ? "lighting-hardware-aura.mock.json" : "lighting-combination.mock.json",f.profiles->Diagnostics());
    }
    // Actual Reactive and Ripple renderers feed the policy, alongside a game-state
    // color fixture. Their source/type is never an admission discriminator.
    Phase4BFixture f(false,"cs2.exe",true,false,"AuraDirectRgb");
    LIGHT_CHECK(LightingActivate(f).at("outcome")=="succeeded");
    aura::Keymap km;
    Write(f.folder/"keymap.json", {{"keys",{{"W",{{"led_id",0},{"physical_row",1},{"physical_col",1}}}}}});
    LIGHT_CHECK(km.LoadFromJson((f.folder/"keymap.json").string()));
    aura::ReactiveEffect reactive({0,0,0},{200,50,80},1000);
    aura::RippleEffect ripple({0,0,0},{200,50,80},1000);
    aura::KeyInputHub::Instance().RecordKeyEvent("W",aura::KeyEventType::Down);
    aura::FrameBuffer frame;
    std::vector<std::array<uint8_t,65>> rgb;
    reactive.Render(0,frame,km); LIGHT_CHECK(frame.buffer[0]>0 && LightingFrame(f,rgb,frame.buffer[0]));
    ripple.Render(0,frame,km); LIGHT_CHECK(ripple.GetActiveRippleCount()==1 && LightingFrame(f,rgb,frame.buffer[0]));
    LIGHT_CHECK(LightingFrame(f,rgb,128)); // synthetic game health color
    aura::KeyInputHub::Instance().RecordKeyEvent("W",aura::KeyEventType::Up);
    LIGHT_CHECK(f.io->stages==1 && f.io->applies==0 && f.io->hardware_slot==5);
    return true;
}
bool TestLightingFailures() {
    for(int failure=0;failure<7;++failure) {
        Phase4BFixture f(false,"cs2.exe",true,false,"AuraDirectRgb");
        if(failure==0) f.available=false;
        if(failure==1) f.io->selector_changes_slot=false;
        if(failure==2) f.io->fail_at_stage=1;
        if(failure==3) f.io->basic_timeout=true;
        if(failure==4) f.io->basic_malformed=true;
        if(failure==5) f.latch->armed=true;
        const auto result=failure==6 ? f.profiles->ActivateProfile(B,"Manual",0,nullptr,[]{return true;}) : LightingActivate(f);
        LIGHT_CHECK(result.at("outcome")==(failure==0?"deferred":failure==6?"stale":"failed"));
        std::vector<std::array<uint8_t,65>> rgb;
        LIGHT_CHECK(!LightingFrame(f,rgb) && rgb.empty() && f.io->applies==0);
        LIGHT_CHECK(result.at("active_profile_id").is_null() && result.at("dirty")==true);
    }
    Phase4BFixture f(false,"cs2.exe",true,false,"AuraDirectRgb");
    f.Stable("cs2.exe");
    LIGHT_CHECK(f.attempts==1 && f.io->hardware_slot==5 && f.io->applies==0);
    std::vector<std::array<uint8_t,65>> rgb;
    LIGHT_CHECK(LightingFrame(f,rgb));
    f.io->hardware_slot=1; f.now+=10000; f.profiles->ObserveHardwareSlot();
    LIGHT_CHECK(f.profiles->Diagnostics().at("runtime").at("active_profile_id").is_null());
    LIGHT_CHECK(LightingFrame(f,rgb) && f.io->hardware_slot==1 && f.io->stages==1);
    f.available=false;
    LIGHT_CHECK(LightingActivate(f).at("outcome")=="deferred" && !LightingFrame(f,rgb));
    f.available=true; f.io->current_session=false; f.io->hardware_slot=5;
    LIGHT_CHECK(f.profiles->State().at("active_profile_id")==B && !LightingFrame(f,rgb));
    // Observation confirms magnetic state only. Explicit activation releases
    // conservative lighting suppression after a failed/deferred selection.
    LIGHT_CHECK(LightingActivate(f).at("outcome")=="succeeded" && LightingFrame(f,rgb));
    LIGHT_CHECK(f.io->stages==1 && f.io->applies==0); // reconnect observes; no replay
    f.io->current_session=false;
    LIGHT_CHECK(f.profiles->State().at("active_profile_id")==B && LightingFrame(f,rgb));
    for(int i=0;i<6;++i) {
        LIGHT_CHECK(LightingActivate(f,i%2?B:A).at("outcome")=="succeeded");
        LIGHT_CHECK(LightingFrame(f,rgb));
    }
    LIGHT_CHECK(f.io->applies==0 && f.latch->arms==0);
    return true;
}
bool TestLightingFirmwareAdmission() {
    for(auto state : {aura::m605::FirmwareState::Unknown,aura::m605::FirmwareState::Unsupported,aura::m605::FirmwareState::ReadFailed}) {
        Phase4BFixture f(false,"cs2.exe",true,true,"AuraDirectRgb");
        f.io->firmware={state,{},"Firmware admission synthetic rejection"};
        const auto before=Json::parse(std::ifstream(f.path));
        bool rejected=false;
        try { LightingActivate(f); } catch(const std::exception&) { rejected=true; }
        LIGHT_CHECK(rejected && f.io->stages==0 && f.io->applies==0 && f.latch->arms==0);
        LIGHT_CHECK(Json::parse(std::ifstream(f.path))==before);
        std::vector<std::array<uint8_t,65>> rgb;
        LIGHT_CHECK(LightingFrame(f,rgb)); // independent Direct RGB policy
        LIGHT_CHECK(f.profiles->Diagnostics().at("runtime").at("active_profile_id").is_null());
        f.Stable("cs2.exe");
        LIGHT_CHECK(f.io->stages==0 && f.io->applies==0);
    }
    return true;
}
bool TestLightingPersistenceApi() {
    Phase4BFixture f(false,"cs2.exe",true,false,"AuraDirectRgb");
    httplib::Server server; f.profiles->RegisterRoutes(server);
    const auto port=server.bind_to_any_port("127.0.0.1"); LIGHT_CHECK(port>0);
    std::thread serving([&]{server.listen_after_bind();});
    // Stop/join on every assertion/exception path.
    struct Stop { httplib::Server& s; std::thread& t; ~Stop(){s.stop();t.join();} } stop{server,serving};
    httplib::Client client("127.0.0.1",port);
    auto doc=Json::parse(std::ifstream(f.path)); auto p=doc.at("profiles")[1];
    const auto update=[&](const Json& profile, int64_t revision) {
        return client.Post("/api/device-profiles/update",Json{{"profile_id",B},{"profile",profile},{"expected_revision",revision}}.dump(),"application/json");
    };
    p["lighting"].erase("control_owner"); // old client that drops new fields
    auto r=update(p,0); LIGHT_CHECK(r && r->status==200);
    doc=Json::parse(std::ifstream(f.path));
    LIGHT_CHECK(doc.at("revision")==1 && doc.at("profiles")[1].at("lighting").at("control_owner")=="AuraDirectRgb");
    LIGHT_CHECK(doc.at("profiles")[1].at("lighting").at("ownership")=="LegacyUnmanaged");
    LIGHT_CHECK(doc.at("profiles")[1].at("lighting").at("lighting_extension")==7);
    LIGHT_CHECK(update(p,0)->status==409);
    for(auto invalid : {Json(nullptr),Json(true),Json("future")}) {
        p["lighting"]["control_owner"]=invalid;
        LIGHT_CHECK(update(p,1)->status==422 && Json::parse(std::ifstream(f.path))==doc);
    }
    p["lighting"]["control_owner"]="FirmwareBank";
    LIGHT_CHECK(update(p,1)->status==200 && LightingActivate(f).at("outcome")=="succeeded");
    std::vector<std::array<uint8_t,65>> rgb;
    LIGHT_CHECK(!LightingFrame(f,rgb));
    // Reading the current slot must not apply a newly saved lighting draft.
    LIGHT_CHECK(f.profiles->Diagnostics().at("hardware_slot_status").at("lighting_diagnostic")=="FirmwareLightingRestoreUnverified");
    p["lighting"]["control_owner"]="AuraDirectRgb";
    LIGHT_CHECK(update(p,f.profiles->Diagnostics().at("runtime").at("document_revision"))->status==200 && !LightingFrame(f,rgb)); // save alone doesn't apply
    f.profiles->RefreshHardwareSlot();
    LIGHT_CHECK(!LightingFrame(f,rgb));
    LIGHT_CHECK(LightingActivate(f).at("outcome")=="succeeded" && LightingFrame(f,rgb));
    const auto stages=f.io->stages;
    // Persisted restore and copied import document use exactly the production loader.
    for(const auto& path : {f.path,f.folder/"imported.json"}) {
        if(path!=f.path) Write(path,Json::parse(std::ifstream(f.path)));
        aura::DeviceProfileRuntime restored(path,{},*f.device,f.gate,[]{return aura::MagneticHostProfile{};},[]{return true;});
        LIGHT_CHECK(restored.WithAuraLightingOwnership([]{return true;}));
    }
    LIGHT_CHECK(f.io->stages==stages && f.io->applies==0);
    // Untrusted request fields never confer firmware admission.
    f.io->current_session=false;
    f.io->firmware={aura::m605::FirmwareState::Unsupported,"0.0","FirmwareUnsupported synthetic"};
    r=client.Post("/api/device-profiles/activate",Json{{"profile_id",B},{"reason","Manual"},{"expected_revision",f.profiles->Diagnostics().at("runtime").at("document_revision")},{"firmware_supported",true}}.dump(),"application/json");
    LIGHT_CHECK(r && r->status==409 && f.io->stages==stages);
    return true;
}
bool TestLightingConcurrency() {
    Phase4BFixture f(false,"cs2.exe",true,false,"AuraDirectRgb",true);
    LIGHT_CHECK(LightingActivate(f).at("outcome")=="succeeded");
    std::mutex mutex; std::condition_variable cv; bool in_frame=false,release=false;
    std::atomic<bool> switching{false},finished{false};
    std::thread frame([&] { f.profiles->WithAuraLightingOwnership([&] {
        std::unique_lock<std::mutex> lock(mutex); in_frame=true; cv.notify_all();
        cv.wait(lock,[&]{return release;}); return true;
    }); });
    bool entered;
    { std::unique_lock<std::mutex> lock(mutex); entered=cv.wait_for(lock,std::chrono::seconds(3),[&]{return in_frame;}); }
    std::thread selector([&] { switching=true; LightingActivate(f,A); finished=true; });
    while(!switching.load()) std::this_thread::yield();
    // A frame holds the mutation gate for the entire callback. Selector cannot
    // reach transport while it is held (check protected by the same cv mutex).
    const bool blocked=!finished.load();
    { std::lock_guard<std::mutex> lock(mutex); release=true; cv.notify_all(); }
    frame.join(); selector.join();
    LIGHT_CHECK(entered && blocked && finished && f.io->stages==2 && f.io->applies==0);
    LIGHT_CHECK(!f.profiles->WithAuraLightingOwnership([]{return true;}));
    // Block within the selector's BasicInfo callback: queued frames must observe
    // the post-selection owner, never the policy from before selector submission.
    f.io->hardware_slot=1;
    std::atomic<bool> queued{false},attempted{false},wrote{false}; std::thread queued_frame;
    f.io->after_basic=[&] {
        if(queued.exchange(true)) return;
        queued_frame=std::thread([&] { attempted=true; wrote=f.profiles->WithAuraLightingOwnership([]{return true;}); });
        while(!attempted.load()) std::this_thread::yield();
    };
    const auto result=LightingActivate(f);
    queued_frame.join(); f.io->after_basic={};
    LIGHT_CHECK(result.at("outcome")=="succeeded" && attempted && wrote && f.io->hardware_slot==5 && f.io->applies==0);
    return true;
}
#undef LIGHT_CHECK
}
