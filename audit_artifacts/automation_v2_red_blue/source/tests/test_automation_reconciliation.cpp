#include "engine/effect_engine.h"
#include "engine/plugin_publication_queue.h"
#include "gsi/gsi_adapter.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
using namespace aura;
using json=nlohmann::json;
namespace fs=std::filesystem;
namespace {
int checks=0;
void Check(bool ok,const char* message) { ++checks; if(!ok) throw std::runtime_error(message); }
json Field(const char* field,json value,const char* op="==") { return {{"field",field},{"op",op},{"value",value}}; }
json Rule(const char* id,const char* mode="event") {
    return {{"id",id},{"model","automation_v2"},{"when",{{"mode",mode},{"condition",std::string(mode)=="event"?json{{"event","event.kill"}}:Field("player.state.health",15,"<")}}},
        {"action",{{"type","trigger_effect"},{"effect",{{"kind","profile_effect"},{"name","paint"}}},{"lifetime",std::string(mode)=="state"?"while_true":"one_shot"},{"compatibility",{{"duration_ms",10000},{"fade_out_ms",0}}}}}};
}
json Config(json rules) { return {{"default_profile","paint"},{"profiles",{{"paint",{{"type","static"},{"color",{200,0,0}}}}}},{"orchestration",{{"rules",rules}}}}; }
struct Harness {
    RuleEngine rules; GsiState gsi; AutomationEffectRuntime runtime; Keymap keymap;
    std::string process="cs2.exe"; fs::path file; json config;
    Harness(const fs::path& dir,json cfg):file(dir/"config.json"),config(cfg) { Load(); }
    bool Write(const json& value) { std::ofstream(file)<<value.dump(); return rules.LoadConfig(file.string()); }
    void Load() { Check(Write(config),"load valid config"); }
    void Packet(uint64_t time,int health,int kills) { gsi.UpdateFromPayloadAt({{"player",{{"state",{{"health",health},{"round_kills",kills}}}}}},time); }
    AutomationEvaluation Tick(uint64_t now) {
        auto out=rules.EvaluateAutomation(gsi,[&]{return process;},now);
        runtime.Consume(out,[&](const json& reference){return ResolveAutomationEffect(reference,rules);},now,runtime.Revision()); return out;
    }
    int Red(uint64_t time,bool persistent=false) { FrameBuffer frame;frame.Fill(20,20,20);runtime.Apply(persistent,time,frame,keymap,&gsi);return frame.buffer[0]; }
    void Start() { Packet(0,20,0);Tick(0);Packet(10,10,1);Tick(10); }
};
void Reconciliation(const fs::path& dir) {
    Harness h(dir,Config(json::array({Rule("shot")}))); h.Start(); Check(h.runtime.ActiveCount()==1,"event starts");
    h.config["orchestration"]["rules"][0]["name"]="cosmetic";h.config["orchestration"]["rules"][0]["description"]="label";h.Load();h.Tick(20);
    Check(h.runtime.ActiveCount()==1 && h.Red(20)==200,"cosmetic reload retains shot");
    h.process="desktop.exe";h.Tick(30);Check(h.runtime.ActiveCount()==1,"unscoped shot survives process exit");
    h.Packet(40,100,1);h.Tick(40);Check(h.runtime.ActiveCount()==1,"event condition false after admission does not cancel");
    h.Tick(3040);Check(h.runtime.ActiveCount()==1,"3000 boundary retains GSI shot");h.Tick(3041);Check(h.runtime.ActiveCount()==0,"stale cancels GSI shot without packet");
    h.Packet(3050,10,2);h.Tick(3050);Check(h.runtime.ActiveCount()==0,"recovery does not replay occurrence");
    h.Packet(3060,10,3);h.Tick(3060);Check(h.runtime.ActiveCount()==1,"future occurrence after recovery");
    h.config["orchestration"]["rules"][0]["enabled"]=false;h.Load();h.Tick(3061);Check(h.runtime.ActiveCount()==0,"disable cancels");
    h.config["orchestration"]["rules"][0]["enabled"]=true;h.Load();h.Tick(3062);h.Packet(3070,10,4);h.Tick(3070);
    h.config["orchestration"]["rules"]=json::array();h.Load();h.Tick(3071);Check(h.runtime.ActiveCount()==0,"delete cancels");
    auto scoped=Rule("scoped");scoped["scope"]=Field("process","cs2");h.config=Config(json::array({scoped}));h.process="cs2";h.Load();h.Tick(3080);
    h.Packet(3090,10,5);h.Tick(3090);Check(h.runtime.ActiveCount()==1,"scoped starts");h.process="other";h.Tick(3100);Check(h.runtime.ActiveCount()==0,"scope false cancels");
    scoped["scope"]=Field("player.state.armor",0,">");h.config=Config(json::array({scoped}));h.Load();h.Tick(3101);
    auto unknown=h.Tick(3102);Check(unknown.rule_status[0].scope==ConditionTruth::Unknown,"missing continuing scope is Unknown");
    h.gsi.UpdateFromPayloadAt({{"player",{{"state",{{"health",10},{"armor",50},{"round_kills",5}}}}}},3110);h.Tick(3110);
    h.gsi.UpdateFromPayloadAt({{"player",{{"state",{{"health",10},{"armor",50},{"round_kills",6}}}}}},3120);h.Tick(3120);
    Check(h.runtime.ActiveCount()==1,"GSI scoped shot starts");h.Packet(3130,10,6);h.Tick(3130);
    Check(h.runtime.ActiveCount()==0,"Unknown continuing scope cancels active shot");
    auto rise=Rule("rise","rising");h.config=Config(json::array({rise}));h.Load();h.Packet(3200,20,5);h.Tick(3200);
    h.config["orchestration"]["rules"][0]["description"]="unchanged";h.Load();h.Packet(3210,10,5);h.Tick(3210);Check(h.runtime.ActiveCount()==1,"unchanged reload preserves armed rising");
    h.config["orchestration"]["rules"][0]["when"]["condition"]["value"]=14;h.Load();h.Tick(3220);Check(h.runtime.ActiveCount()==0,"semantic edit cancels and seeds True without new rise");
    h.Packet(3230,20,5);h.Tick(3230);h.Packet(3240,10,5);h.Tick(3240);Check(h.runtime.ActiveCount()==1,"edited rising can fire future valid edge");
    auto invalid=h.config;invalid["orchestration"]["rules"][0]["action"]["effect"]["name"]="missing";
    Check(!h.Write(invalid),"invalid V2 candidate rejected");h.Tick(3250);Check(h.runtime.ActiveCount()==1,"invalid schema keeps active last plan");
    std::ofstream(h.file)<<"{";Check(!h.rules.LoadConfig(h.file.string()),"parse failure rejected");h.Tick(3260);Check(h.runtime.ActiveCount()==1,"parse failure preserves runtime");
}
void Recipes(const fs::path& dir) {
    Harness h(dir,Config(json::array({Rule("shot"),Rule("persistent","state")})));h.Start();
    Check(h.Red(20)==200 && h.Red(20,true)==200,"old recipe both classes");
    h.config["profiles"]["paint"]["color"]={100,0,0};h.Load();h.Tick(30);
    Check(h.Red(30)==200 && h.Red(30,true)==100,"old transient kept, persistent recipe replaced");
    h.Packet(40,10,2);h.Tick(40);Check(h.Red(40)==100,"next trigger uses new recipe");
    EffectEngine engine;Check(engine.ReconcileProfile(h.rules.GetProfile("paint")),"first base reconcile");auto base=engine.GetActiveProfile()->base_effect;
    h.config["profiles"]["paint"]["title"]="cosmetic";h.config["unrelated"]=true;h.Load();
    Check(!engine.ReconcileProfile(h.rules.GetProfile("paint")) && base==engine.GetActiveProfile()->base_effect,"unrelated/cosmetic base reload does not restart");
    h.config["profiles"]["paint"]["color"]={50,0,0};h.Load();Check(engine.ReconcileProfile(h.rules.GetProfile("paint")),"changed base recipe swaps");
    auto next=engine.GetActiveProfile()->base_effect;Check(next!=base && !engine.ReconcileProfile(h.rules.GetProfile("paint")),"base swaps once");
}
void PluginReload(const fs::path& generated,const fs::path& dir) {
    auto path=dir/"effect_runtime_reload.dll";fs::copy_file(generated/"automation_lifecycle_old.dll",path,fs::copy_options::overwrite_existing);
    auto& manager=PluginManager::Instance();auto initial=manager.LoadPluginInstance(path.string());Check(bool(initial),"load singleton runtime plugin");
    using Configure=void(*)(int,uint64_t,float);
    auto configure=[](const auto& generation) { return reinterpret_cast<Configure>(GetProcAddress(generation->handle->GetModule(),"FixtureConfigure")); };
    configure(initial.GetGeneration())(0,100000,1.f);
    auto config=Config(json::array({Rule("shot"),Rule("persistent","state")}));config["profiles"]["paint"]={{"type","plugin"},{"plugin_name",path.string()}};
    Harness h(dir,config);h.Start();EffectEngine engine;Check(engine.ReconcileProfile(h.rules.GetProfile("paint")),"plugin base starts");auto old_base=engine.GetActiveProfile()->base_effect;
    fs::copy_file(generated/"automation_lifecycle_new.dll",path,fs::copy_options::overwrite_existing);
    auto prepared=manager.PrepareReload(path.string());Check(bool(prepared) && manager.GetGeneration(path.string())==initial.GetGeneration(),"prepare does not publish");
    Check(manager.PublishReload(prepared),"publish new DLL");configure(prepared.candidate)(11,100000,1.f);h.Tick(20);
    Check(h.Red(20)==200 && h.Red(20,true)==200,"failed persistent replacement keeps old, old transient pinned");
    Check(!engine.ReconcileProfile(h.rules.GetProfile("paint")) && old_base==engine.GetActiveProfile()->base_effect,"failed base replacement retains old");
    // Another successfully published generation re-arms replacement exactly once.
    prepared=manager.PrepareReload(path.string());Check(bool(prepared),"prepare fresh retry generation");Check(manager.PublishReload(prepared),"publish retry generation");configure(prepared.candidate)(0,100000,1.f);h.Tick(30);
    Check(h.Red(30)==200 && h.Red(30,true)==100,"transient old callbacks, persistent new callbacks");
    const auto count=reinterpret_cast<unsigned(*)(unsigned)>(GetProcAddress(prepared.candidate->handle->GetModule(),"FixtureCount"));
    const auto created=count(0);h.Tick(31);h.Tick(32);Check(count(0)==created,"persistent generation swaps exactly once");
    Check(engine.ReconcileProfile(h.rules.GetProfile("paint")) && !engine.ReconcileProfile(h.rules.GetProfile("paint")),"base swaps new DLL once");
    h.Packet(40,10,2);h.Tick(40);Check(h.Red(40)==100,"future admission new DLL");
    fs::copy_file(generated/"abi_two.dll",path,fs::copy_options::overwrite_existing);
    Check(!manager.ReloadPlugin(path.string()),"unknown ABI reload rejects");h.Tick(50);Check(h.Red(50)==100 && h.Red(50,true)==100,"failed reload leaves runtime unchanged");
    auto prior=manager.GetGeneration(path.string());fs::copy_file(generated/"automation_lifecycle_old.dll",path,fs::copy_options::overwrite_existing);
    PluginPublicationQueue queue;auto queued=manager.PrepareReload(path.string());
    Check(!queue.Submit(queued,std::chrono::milliseconds(0)) && !queue.ApplyAtFrameBoundary(manager) && manager.GetGeneration(path.string())==prior,"timed-out command never publishes later");
    std::atomic<bool> done=false; bool success=false;std::thread network([&]{success=queue.Submit(queued);done=true;});
    while(!done) { queue.ApplyAtFrameBoundary(manager);std::this_thread::yield(); }network.join();
    Check(success && manager.GetGeneration(path.string())!=prior,"network prepared command publishes at owner boundary");
    h.Tick(60);h.Red(100061,true);h.Packet(100062,10,2);h.Tick(100062);
    Check(h.Red(100062,true)==20,"completed persistent does not respawn same recipe");
    auto renewed=manager.PrepareReload(path.string());Check(manager.PublishReload(renewed),"publish generation after completion");
    h.Tick(100063);Check(h.Red(100063,true)!=20,"new persistent recipe generation can replace completed interval");
    auto unsupported=manager.PrepareReload((generated/"abi_no_lifecycle.dll").string(),true);Check(!unsupported,"one-shot publication rejects missing lifecycle before registry mutation");
}
void Studio(const fs::path& generated) {
    PluginManager manager;Keymap keymap;FrameBuffer frame;
    for(const auto* name:{"studio_old","studio_continuous"}) {
        auto instance=manager.LoadPluginInstance((generated/(std::string(name)+".dll")).string());Check(bool(instance),"load generated continuous DLL");
        Check(!instance.GetGeneration()->lifecycle.is_finished,"continuous has no implicit lifecycle conversion");
        instance.GetEffect()->Render(0,frame,keymap);Check(frame.buffer[0]==255,"continuous red");instance.GetEffect()->Render(10,frame,keymap);Check(frame.buffer[2]==255,"continuous terminal blue");
        instance.GetEffect()->Render(11,frame,keymap);Check(frame.buffer[0]==255,"continuous loops unchanged");
    }
    auto instance=manager.LoadPluginInstance((generated/"studio_one_shot.dll").string());Check(bool(instance),"load generated one-shot");
    const auto& lifecycle=instance.GetGeneration()->lifecycle;Check(lifecycle.version==1 && lifecycle.is_finished && lifecycle.get_opacity,"negotiate generated lifecycle");
    instance.GetEffect()->Render(0,frame,keymap);Check(frame.buffer[0]==255 && lifecycle.is_finished(instance.GetEffect().get(),0)==0,"one-shot wait yields");
    instance.GetEffect()->Render(10,frame,keymap);Check(frame.buffer[2]==255 && lifecycle.is_finished(instance.GetEffect().get(),10)==0,"terminal visual before finished");
    instance.GetEffect()->Render(15,frame,keymap);Check(lifecycle.get_opacity(instance.GetEffect().get(),15)==0.5f,"defined fade curve");
    instance.GetEffect()->Render(20,frame,keymap);Check(lifecycle.get_opacity(instance.GetEffect().get(),20)==0 && lifecycle.is_finished(instance.GetEffect().get(),20)==0,"zero opacity frame before completion");
    instance.GetEffect()->Render(21,frame,keymap);Check(lifecycle.is_finished(instance.GetEffect().get(),21)==1,"one-shot terminates");
    instance.GetEffect()->Render(0,frame,keymap);Check(lifecycle.is_finished(instance.GetEffect().get(),0)==1,"terminal cannot implicitly restart");
}
}
int main(int argc,char**argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOOPENFILEERRORBOX);
    const auto dir=fs::temp_directory_path()/("aura-stage4-"+std::to_string(GetCurrentProcessId()));
    try { Check(argc==2,"fixture path");fs::create_directories(dir);Reconciliation(dir);Recipes(dir);PluginReload(argv[1],dir);Studio(argv[1]);
        fs::remove_all(dir);std::cout<<"PASS: "<<checks<<" Stage 4 reconciliation/publication assertions\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<"\n";return 1;}
}
