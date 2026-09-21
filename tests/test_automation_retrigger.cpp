#include "engine/automation_effect_runtime.h"
#include "gsi/gsi_adapter.h"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace aura;
using Json=nlohmann::json;
namespace fs=std::filesystem;
int checks=0;
void Check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
AutomationDecision Decision(std::string id="shot",std::string policy="queue") {
    AutomationDecision d;d.rule_id=id;d.admitted=true;d.eligibility=ConditionTruth::True;
    d.action={{"type","trigger_effect"},{"lifetime","one_shot"},{"effect",{{"kind","profile_effect"},{"name","paint"}}},{"retrigger",policy},
        {"compatibility",{{"duration_ms",100},{"fade_out_ms",0}}}};return d;
}
PreparedEffectSource Recipe(int red){return PreparedEffectSource::Recipe(Json{{"type","static"},{"color",{red,0,0}}}.dump());}
struct Harness {
    AutomationEffectRuntime runtime;AutomationEvaluation evaluation;PreparedEffectSource source=Recipe(100);Keymap keymap;
    AutomationEffectRuntime::Resolver resolver=[&](const Json&){return source.Create();};
    AutomationEffectRuntime::Preparer prepare=[&](const Json&){return source;};
    void Consume(std::vector<AutomationDecision> ds,uint64_t time){evaluation.decisions=std::move(ds);runtime.Consume(evaluation,resolver,time,runtime.Revision(),prepare);}
    unsigned Render(uint64_t time){FrameBuffer frame;frame.Fill(5,0,0);runtime.Apply(false,time,frame,keymap);return frame.buffer[0];}
    void Status(std::string id="shot",std::string semantic="same",ConditionTruth continuation=ConditionTruth::True){
        evaluation.reconciliation_complete=true;AutomationEvaluation::RuleStatus s;s.id=id;s.enabled=true;s.semantic_identity=semantic;s.continuation=continuation;evaluation.rule_status={s};
    }
};
void StackAndQueue() {
    Harness h;auto s=Decision("shot","stack");h.Consume({s},0);Check(h.runtime.ActiveCount()==1,"one stack admission fresh");
    h.source=Recipe(150);h.Consume({s,s,s,s},10);Check(h.runtime.ActiveCount()==4 && h.runtime.GetCounters().capacity_drops==1,"stack accepts four drops fifth newest");
    Check(h.Render(10)==150,"newer instance_sequence renders later");h.Render(100);Check(h.runtime.ActiveCount()==3,"retiring first does not retire siblings");h.Render(110);Check(h.runtime.ActiveCount()==0,"independent starts retire later siblings");
    auto q=Decision();h.Consume({q},200);h.source=Recipe(70);h.Consume({q},210);h.source=Recipe(80);h.Consume({q,q,q,q},220);
    Check(h.runtime.ActiveCount()==1 && h.runtime.PendingCount()==4,"queue one active four pending");
    Check(h.runtime.GetCounters().capacity_drops==2,"queue drops newest without evicting pending");
    h.Render(300);h.Consume({},301);Check(h.Render(301)==70,"oldest FIFO recipe starts");h.Render(401);h.Consume({},402);Check(h.Render(402)==80,"later recipe remains FIFO");
    h.runtime.Clear();h.source=Recipe(100);h.Consume({q},0);h.source=Recipe(200);h.Consume({q},1);h.source=Recipe(50);h.Consume({q},1000);
    h.Render(2001);h.Consume({},2001);Check(h.Render(2001)==200,"age 2000 starts at elapsed zero despite long wait");
    h.runtime.Clear();h.source=Recipe(100);h.Consume({q},0);h.source=Recipe(200);h.Consume({q},1);h.source=Recipe(50);h.Consume({q},1000);
    h.Render(2002);h.Consume({},2002);Check(h.Render(2002)==50 && h.runtime.GetCounters().expired==1,"age 2001 expires head before later valid token");
    h.runtime.Clear();h.source={};h.Consume({q},0);Check(h.runtime.PendingCount()==0 && h.runtime.GetCounters().unavailable==1,"unresolvable admission consumed");h.source=Recipe(80);h.Consume({},1);Check(h.runtime.ActiveCount()==0,"missing source never replayed");
}
void Bounds() {
    Harness h;
    for(unsigned i=0;i<8;++i){auto d=Decision("stack"+std::to_string(i),"stack");d.action["compatibility"]["duration_ms"]=10000;h.Consume({d,d,d,d},0);}
    Check(h.runtime.ActiveCount()==32,"global active limit");h.Consume({Decision("extra","stack")},0);Check(h.runtime.ActiveCount()==32 && h.runtime.GetCounters().capacity_drops==1,"global stack drop newest");
    for(unsigned i=0;i<16;++i){auto d=Decision("queue"+std::to_string(i));h.Consume({d,d,d,d},0);}
    Check(h.runtime.PendingCount()==64 && h.runtime.ActiveCount()==32,"global pending 64 while active full");
    h.Consume({Decision("overflow")},1);Check(h.runtime.PendingCount()==64 && h.runtime.GetCounters().capacity_drops==2,"global pending drop newest");
    h.Consume({},2000);Check(h.runtime.PendingCount()==64,"capacity wait retains exact TTL boundary");h.Consume({},2001);Check(h.runtime.PendingCount()==0 && h.runtime.GetCounters().expired==64,"all over-age tokens expire under pressure");
    h.runtime.Clear();for(unsigned i=0;i<8;++i){auto d=Decision("s"+std::to_string(i),"stack");h.Consume({d,d,d,d},0);}
    h.source=Recipe(70);auto d=Decision();h.Consume({d},1);h.source=Recipe(90);h.Consume({d},2);h.Render(100);h.Consume({},101);
    Check(h.Render(101)==70 && h.runtime.PendingCount()==1,"global capacity release starts FIFO head not newest");
}
void Reconciliation() {
    for(const auto* policy:{"stack","queue"}) {
        Harness h;h.Status();auto d=Decision("shot",policy);h.Consume({d,d,d},0);
        const auto active=h.runtime.ActiveCount(),pending=h.runtime.PendingCount();++h.evaluation.config_generation;h.Consume({},10);
        Check(h.runtime.ActiveCount()==active && h.runtime.PendingCount()==pending,"cosmetic/unchanged reload preserves all work");
        h.evaluation.rule_status[0].continuation=ConditionTruth::False;h.Consume({},11);
        Check(h.runtime.ActiveCount()==0 && h.runtime.PendingCount()==0,"scope false cancels entire rule");
        h.Status();h.Consume({d,d,d},20);h.evaluation.rule_status[0].continuation=ConditionTruth::Unknown;h.Consume({},21);
        Check(h.runtime.ActiveCount()==0 && h.runtime.PendingCount()==0,"scope unknown/stale cancels entire rule");
        for(int change=0;change<3;++change){h.Status();h.Consume({d,d,d},30);if(change==0)h.evaluation.rule_status.clear();if(change==1)h.evaluation.rule_status[0].enabled=false;if(change==2)h.evaluation.rule_status[0].semantic_identity="edited";h.Consume({},31);Check(h.runtime.ActiveCount()==0 && h.runtime.PendingCount()==0,"delete disable semantic edit clears all work");}
    }
    Harness h;h.Status();auto d=Decision();h.Consume({d,d},0);++h.evaluation.config_generation;h.Consume({},1999);h.Render(2001);h.Consume({},2001);
    Check(h.runtime.PendingCount()==0 && h.runtime.ActiveCount()==0,"cosmetic reload does not reset pending admission time");
}
struct Api {
    using Configure=void(*)(unsigned,uint64_t);using Count=unsigned(*)(unsigned);using Elapsed=uint64_t(*)(unsigned);
    Configure configure;Count count;Elapsed elapsed;
    Api(const std::shared_ptr<const PluginEntry>& g):configure(reinterpret_cast<Configure>(GetProcAddress(g->handle->GetModule(),"FixtureConfigure"))),count(reinterpret_cast<Count>(GetProcAddress(g->handle->GetModule(),"FixtureCount"))),elapsed(reinterpret_cast<Elapsed>(GetProcAddress(g->handle->GetModule(),"FixtureElapsed"))){}
};
void Dlls(const fs::path& binaries,const fs::path& dir) {
    PluginManager manager;auto path=dir/"live.dll";fs::copy_file(binaries/"retrigger_old.dll",path,fs::copy_options::overwrite_existing);
    auto old=manager.PrepareEffectGeneration(path.string());Api old_api(old);Check(old_api.count(0)==0,"prepare cold DLL never invokes factory");old_api.configure(0,100);
    auto shadow=old->handle->GetShadowPath();std::weak_ptr<const PluginEntry> weak=old;
    Harness h;h.source=PreparedEffectSource::Plugin(old);auto q=Decision();h.Consume({q,q,q},0);Check(old_api.count(0)==1,"pending generation pins without Effect creation");
    old_api.configure(1,100);h.Render(100);h.Consume({},101);Check(old_api.count(0)==2 && h.runtime.PendingCount()==1 && h.runtime.ActiveCount()==0,"failed queued factory consumed once per rule frame");
    h.Consume({},102);Check(old_api.count(0)==3 && h.runtime.ActiveCount()==1,"later token progresses after failure");h.Render(102);Check(old_api.elapsed(2)==0,"queued effect first render elapsed zero");
    h.Consume({q},103);fs::copy_file(binaries/"retrigger_new.dll",path,fs::copy_options::overwrite_existing);Check(manager.ReloadPlugin(path.string()),"publish new generation");
    auto fresh=manager.GetGeneration(path.string());h.source=PreparedEffectSource::Plugin(fresh);h.Consume({q},104);old.reset();
    h.Render(202);h.Consume({},203);Check(h.Render(203)==17,"old token starts with old generation callbacks after reload");
    h.Render(303);Check(weak.expired() && !fs::exists(shadow) && !GetModuleHandleW(shadow.c_str()),"old DLL unloads after last old token/active owner");
    h.Consume({},304);Check(h.Render(304)==93,"post-reload token uses new generation");
    h.runtime.Clear();h.source=PreparedEffectSource::Plugin(fresh);Api api(fresh);api.configure(0,100);
    auto s=Decision("shot","stack");unsigned before=api.count(1);h.Consume({s},0);api.configure(0,200);h.Consume({s},20);h.Render(50);
    Check(api.elapsed(before+1)==50 && api.elapsed(before+2)==30,"stack independent lifecycle elapsed");h.Render(100);Check(h.runtime.ActiveCount()==1,"finished callback retires only one sibling");
    s.action["watchdog_ms"]=50;h.Consume({s},110);h.Render(160);Check(h.runtime.ActiveCount()==1,"watchdog independent of older sibling");
    // A token alone owns an unpublished generation under active-cap pressure.
    h.runtime.Clear();h.source=Recipe(50);for(int i=0;i<8;++i){auto d=Decision(std::to_string(i),"stack");h.Consume({d,d,d,d},0);}
    auto cold=manager.PrepareEffectGeneration((binaries/"retrigger_old.dll").string());auto cold_shadow=cold->handle->GetShadowPath();std::weak_ptr<const PluginEntry> cold_weak=cold;
    h.source=PreparedEffectSource::Plugin(cold);h.Consume({q},0);cold.reset();h.source=Recipe(50);Check(!cold_weak.expired(),"pending alone owns generation");h.Consume({},2001);Check(cold_weak.expired() && !fs::exists(cold_shadow) && !GetModuleHandleW(cold_shadow.c_str()),"expiry releases pending DLL pin");
    cold=manager.PrepareEffectGeneration((binaries/"retrigger_old.dll").string());cold_weak=cold;h.source=PreparedEffectSource::Plugin(cold);h.Consume({q},2002);cold.reset();h.source={};h.runtime.Clear();Check(cold_weak.expired(),"shutdown/reset releases token pins");
    h.source=Recipe(50);for(int i=0;i<8;++i){auto d=Decision(std::to_string(i),"stack");h.Consume({d,d,d,d},0);}
    cold=manager.PrepareEffectGeneration((binaries/"retrigger_old.dll").string());cold_weak=cold;h.source=PreparedEffectSource::Plugin(cold);h.Consume({q},1);cold.reset();h.source={};h.Status("shot","",ConditionTruth::False);h.Consume({},2);
    Check(cold_weak.expired() && h.runtime.PendingCount()==0,"authoritative scope cancellation releases pending generation pin");h.evaluation={};
    h.source=Recipe(50);for(int i=0;i<8;++i){auto d=Decision(std::to_string(i),"stack");h.Consume({d,d,d,d},0);}
    h.source=PreparedEffectSource::Plugin(fresh);for(int i=0;i<8;++i){auto d=Decision("q"+std::to_string(i));h.Consume({d,d},0);}
    h.Render(100);api.configure(20,100);unsigned attempted=api.count(0);h.Consume({},101);
    Check(api.count(0)==attempted+4 && h.runtime.PendingCount()==12,"pending failures capped at four per frame globally");
    h.Consume({},102);Check(api.count(0)==attempted+8,"failed tokens are dropped; later FIFO work remains bounded");
}

// Test-only observer: re-enter public mutex-taking accessors synchronously from
// both Effect destruction and final generation deletion. A locked retirement
// deadlocks this test (CTest timeout), rather than reporting a false try_lock win.
struct RetirementObserver {
    AutomationEffectRuntime* runtime{};
    DWORD caller=GetCurrentThreadId();
    PfnAuraDestroyEffect destroy{};
    unsigned effects=0,generations=0;
    bool same_thread=true;
    void Observe(bool generation) {
        same_thread &= GetCurrentThreadId()==caller;
        (void)runtime->ActiveCount(); (void)runtime->PendingCount();
        if(generation) ++generations; else ++effects;
    }
};
RetirementObserver* observer=nullptr;
void ObservedDestroy(Effect* effect) { observer->Observe(false);observer->destroy(effect); }
std::shared_ptr<const PluginEntry> ObservedGeneration(PluginManager& manager,const fs::path& binary,RetirementObserver& probe) {
    auto original=manager.PrepareEffectGeneration(binary.string());
    if(!original) throw std::runtime_error("retirement fixture load failed");
    Api(original).configure(0,100);
    probe.destroy=original->destroy_fn;
    auto* copy=new PluginEntry(*original);copy->destroy_fn=ObservedDestroy;
    return std::shared_ptr<const PluginEntry>(copy,[&probe](const PluginEntry* generation){
        probe.Observe(true);delete generation;
    });
}
void Retirement(const fs::path& binaries) {
    for(int mode=0;mode<7;++mode) {
        PluginManager manager;auto h=std::make_unique<Harness>();
        RetirementObserver probe;probe.runtime=&h->runtime;observer=&probe;
        auto generation=ObservedGeneration(manager,binaries/"retrigger_old.dll",probe);
        auto shadow=generation->handle->GetShadowPath();std::weak_ptr<const PluginEntry> weak=generation;
        h->source=PreparedEffectSource::Plugin(generation);generation.reset();
        auto d=Decision("shot","stack");h->Consume({d},0);h->Consume({d},20);h->source={};
        if(mode==0) {
            h->Render(100);Check(probe.effects==1 && probe.generations==0 && !weak.expired(),"Apply retires one stack sibling outside lock without unloading other owner");
            h->Render(120);
        } else if(mode==1) { h->Status("shot","",ConditionTruth::False);h->Consume({},30); }
        else if(mode==2) h->runtime.Clear();
        else if(mode==3) h.reset();
        else if(mode==4) { ++h->evaluation.config_generation;h->Consume({},30); }
        else if(mode==5) { // Restart replaces the first existing layer as before.
            h->source=Recipe(50);h->Consume({Decision("shot","restart")},30);
            Check(probe.effects==1 && probe.generations==0,"restart replacement retires only replaced stack owner outside lock");
            h->runtime.Clear();
        } else { h->Status("shot","semantic edit");h->Consume({},30); }
        Check(probe.effects==2 && probe.generations==1 && probe.same_thread,"Effect and final generation destructors re-enter runtime unlocked on caller thread");
        Check(weak.expired() && !fs::exists(shadow) && !GetModuleHandleW(shadow.c_str()),"retirement synchronously unloads DLL and deletes shadow after final owner");
        observer=nullptr;
    }
    for(int mode=0;mode<4;++mode) {
        PluginManager manager;Harness h;RetirementObserver probe;probe.runtime=&h.runtime;observer=&probe;
        for(int i=0;i<8;++i){auto d=Decision(std::to_string(i),"stack");h.Consume({d,d,d,d},0);}
        auto generation=ObservedGeneration(manager,binaries/"retrigger_old.dll",probe);
        auto shadow=generation->handle->GetShadowPath();std::weak_ptr<const PluginEntry> weak=generation;
        h.source=PreparedEffectSource::Plugin(generation);h.Consume({Decision()},0);h.source={};generation.reset();
        Check(probe.effects==0 && !weak.expired(),"pending-only pin without constructed Effect");
        if(mode==0)h.Consume({},2001);
        else if(mode==1){h.Status("shot","",ConditionTruth::Unknown);h.Consume({},1);}
        else if(mode==2)h.runtime.Clear();
        else { h.Render(100);observer->destroy=nullptr; // failed head factory has no Effect to destroy
            auto pin=weak.lock();Api(pin).configure(1,100);pin.reset();h.Consume({},101); }
        Check(probe.generations==1 && probe.effects==0 && probe.same_thread,"pending expiry/cancellation/clear/failed start releases final generation outside lock on caller thread");
        Check(weak.expired() && !fs::exists(shadow) && !GetModuleHandleW(shadow.c_str()),"pending final owner unloads immediately");
        observer=nullptr;
    }
    std::cout<<"Retirement instrumentation: synchronous caller thread "<<GetCurrentThreadId()<<"; no background destruction\n";
}
void StackGeneration(const fs::path& binaries,const fs::path& dir) {
    PluginManager manager;auto path=dir/"stack.dll";fs::copy_file(binaries/"retrigger_old.dll",path,fs::copy_options::overwrite_existing);
    Check(manager.ReloadPlugin(path.string()),"initial stack plugin publish");auto old=manager.GetGeneration(path.string());Api api(old);api.configure(0,100);
    auto shadow=old->handle->GetShadowPath();std::weak_ptr<const PluginEntry> weak=old;Harness h;h.source=PreparedEffectSource::Plugin(old);
    auto d=Decision("shot","stack");h.Consume({d},0);h.Consume({d},20);
    fs::copy_file(binaries/"retrigger_new.dll",path,fs::copy_options::overwrite_existing);Check(manager.ReloadPlugin(path.string()),"stack generation reload");
    h.source=PreparedEffectSource::Plugin(manager.GetGeneration(path.string()));old.reset();h.Consume({d},30);
    Check(h.Render(100)==93 && !weak.expired() && h.runtime.ActiveCount()==2,"new stack renders with new generation while old sibling pins DLL");
    h.Render(120);Check(weak.expired() && !fs::exists(shadow) && !GetModuleHandleW(shadow.c_str()) && h.runtime.ActiveCount()==1,"last old stack retirement unloads old DLL only");
}
void Detector(const fs::path& dir) {
    for(const auto* policy:{"stack","queue"}) {
        Json cfg={{"default_profile","base"},{"profiles",{{"base",{{"type","static"}}},{"paint",{{"type","static"},{"color",{17,0,0}}}}}},
            {"orchestration",{{"rules",Json::array({{{"id","shot"},{"model","automation_v2"},{"when",{{"mode","event"},{"condition",{{"event","event.kill"}}}}},{"action",Decision("shot",policy).action}}})}}}};
        auto path=dir/"config.json";std::ofstream(path)<<cfg.dump();RuleEngine rules;Check(rules.LoadConfig(path.string()),"load advanced rule");GsiState gsi;AutomationEffectRuntime runtime;
        auto packet=[&](uint64_t t,int k){gsi.UpdateFromPayloadAt({{"player",{{"state",{{"health",100},{"round_kills",k}}}}}},t);};
        std::string foreground="cs2.exe";
        auto tick=[&](uint64_t t){auto out=rules.EvaluateAutomation(gsi,[&]{return foreground;},t);runtime.Consume(out,[&](const Json& r){return ResolveAutomationEffect(r,rules);},t,runtime.Revision(),[&](const Json&r){return PrepareAutomationEffect(r,rules);});};
        packet(0,0);tick(0);packet(1,1);packet(2,2);packet(3,3);tick(4);
        Check(runtime.ActiveCount()==(std::string(policy)=="stack"?3:1) && runtime.PendingCount()==(std::string(policy)=="queue"?2:0),"separate detector packets preserve burst admissions before render");
        foreground="desktop.exe";tick(4);
        Check(runtime.ActiveCount()==(std::string(policy)=="stack"?3:1) && runtime.PendingCount()==(std::string(policy)=="queue"?2:0),"unscoped foreground change and absent current occurrence do not cancel accepted work");
        if(std::string(policy)=="queue") {
            cfg["profiles"]["paint"]["color"]={93,0,0};std::ofstream(path)<<cfg.dump();Check(rules.LoadConfig(path.string()),"profile recipe publication while queued");packet(5,4);tick(5);
            FrameBuffer frame;Keymap keys;auto render=[&](uint64_t t){frame.Fill(5,0,0);runtime.Apply(false,t,frame,keys);return frame.buffer[0];};
            render(104);tick(105);Check(render(105)==17,"queued captured profile recipe survives real config reload");
            render(205);tick(206);Check(render(206)==17,"second old recipe token FIFO");render(306);tick(307);Check(render(307)==93,"future token captures new profile recipe");
        }
        tick(3006);Check(runtime.ActiveCount()==0 && runtime.PendingCount()==0,"real RuleEngine staleness reconciles stack/queue without packet");
    }
}
int main(int argc,char**argv){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOOPENFILEERRORBOX);auto dir=fs::temp_directory_path()/("aura-retrigger-"+std::to_string(GetCurrentProcessId()));try{Check(argc==2,"fixtures path");fs::create_directories(dir);StackAndQueue();Bounds();Reconciliation();Dlls(argv[1],dir);StackGeneration(argv[1],dir);Retirement(argv[1]);Detector(dir);fs::remove_all(dir);std::cout<<"PASS: "<<checks<<" Stage 6 retrigger assertions\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<"\n";return 1;}}
