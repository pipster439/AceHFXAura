#include "engine/effect_engine.h"
#include "gsi/gsi_adapter.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <atomic>
#include <thread>
#include <sstream>
using namespace aura;
using json = nlohmann::json;
namespace fs = std::filesystem;
namespace {
int checks = 0;
void Check(bool ok, const char* msg) { ++checks; if (!ok) throw std::runtime_error(msg); }
struct Paint : Effect {
    ColorRGB color; unsigned renders = 0; uint64_t elapsed = UINT64_MAX;
    explicit Paint(ColorRGB c) : color(c) {}
    void Render(uint64_t ms, FrameBuffer& frame, const Keymap&) override { ++renders; elapsed=ms; frame.SetKey(0,color); }
};
AutomationDecision Decision(std::string id="rule", bool persistent=false) {
    AutomationDecision d; d.rule_id=id; d.eligibility=ConditionTruth::True; d.admitted=!persistent;
    d.action={{"type","trigger_effect"},{"effect",{{"kind","plugin"},{"name","fixture"}}},
        {"lifetime",persistent?"while_true":"one_shot"},{"composition","overlay"},{"blend","alpha"}};
    return d;
}
void Consume(AutomationEffectRuntime& runtime, std::vector<AutomationDecision> decisions,
             const AutomationEffectRuntime::Resolver& factory, uint64_t now=0, uint64_t generation=1) {
    AutomationEvaluation evaluation; evaluation.config_generation=generation; evaluation.decisions=std::move(decisions);
    runtime.Consume(evaluation,factory,now,runtime.Revision());
}
FrameBuffer Frame(AutomationEffectRuntime& runtime, uint64_t time, bool persistent=false, uint8_t base=20) {
    FrameBuffer frame; frame.Clear(); frame.SetKey(0,base,base,base); Keymap keymap;
    runtime.Apply(persistent,time,frame,keymap); return frame;
}
void HostLifecycle() {
    AutomationEffectRuntime runtime;
    std::vector<std::shared_ptr<Paint>> instances;
    bool fail=false;
    auto factory=[&](const json&) { if(fail) return TriggeredEffectInstance{};
        auto paint=std::make_shared<Paint>(ColorRGB(200,100,50)); instances.push_back(paint);
        return TriggeredEffectInstance::FromHostEffect(paint); };
    auto persistent=Decision("persistent",true);
    Consume(runtime,{persistent},factory); Frame(runtime,100,true); Consume(runtime,{persistent},factory,200); Frame(runtime,200,true);
    Check(instances.size()==1 && instances[0]->elapsed==200,"while_true constructs once and elapsed advances");
    persistent.eligibility=ConditionTruth::Unknown; Consume(runtime,{persistent},factory,300);
    Check(runtime.ActiveCount()==0,"Unknown removes persistent");
    persistent.eligibility=ConditionTruth::True; Consume(runtime,{persistent},factory,400);
    persistent.eligibility=ConditionTruth::False; Consume(runtime,{persistent},factory,500);
    Check(runtime.ActiveCount()==0 && instances.size()==2,"False removes rearmed persistent");
    runtime.Clear(); auto shot=Decision(); shot.admitted=false; Consume(runtime,{shot},factory);
    Check(runtime.ActiveCount()==0,"non-admission cannot start one-shot");
    shot.admitted=true; Consume(runtime,{shot},factory,100); auto first=instances.back(); Frame(runtime,200);
    Consume(runtime,{shot},factory,300); auto second=instances.back(); Frame(runtime,300);
    Check(first!=second && second->elapsed==0,"restart constructs fresh instance at zero");
    fail=true; Consume(runtime,{shot},factory,400); Frame(runtime,500);
    Check(runtime.ActiveCount()==1 && second->elapsed==200,"failed restart preserves previous running instance");
    fail=false; shot.action["retrigger"]="ignore_while_active";
    const auto count=instances.size(); Consume(runtime,{shot},factory,600); Frame(runtime,700);
    Check(instances.size()==count && second->elapsed==400,"ignore consumes without reset or creation");
    Frame(runtime,1500); Check(runtime.ActiveCount()==0,"default envelope expires at 1200");
    Consume(runtime,{},factory,1600); Check(runtime.ActiveCount()==0,"ignored event is not replayed");
    runtime.Clear(); shot=Decision(); shot.action["compatibility"]={{"duration_ms",10000},{"fade_out_ms",2000},{"attack_ms",1000}};
    Consume(runtime,{shot},factory);
    Check(Frame(runtime,0).buffer[0]==20 && instances.back()->renders==1,"zero host attack opacity still renders");
    Check(Frame(runtime,500).buffer[0]==110,"explicit attack envelope");
    Check(Frame(runtime,6000).buffer[0]==200 && runtime.ActiveCount()==1,"legacy envelope not shortened to default watchdog");
    Check(Frame(runtime,9000).buffer[0]==110,"explicit duration/fade envelope");
    Frame(runtime,10000); Check(runtime.ActiveCount()==0,"explicit envelope expiry");
    runtime.Clear(); persistent=Decision("persistent",true); persistent.action["compatibility"]={{"duration_ms",1},{"fade_out_ms",1}};
    Consume(runtime,{persistent},factory); Check(Frame(runtime,100000,true).buffer[0]==200,"persistent ignores duration/fade");
    runtime.Clear(); shot=Decision(); shot.action["composition"]="replace";
    for(int i=0;i<100;++i) Consume(runtime,{shot},[](const json&) { return TriggeredEffectInstance{}; },i);
    Check(Frame(runtime,100).buffer[0]==20 && runtime.DiagnosticCount()==1,"missing Replace preserves lower frame and bounds diagnostics");
    runtime.Clear(); std::vector<AutomationDecision> many;
    for(int i=0;i<40;++i) many.push_back(Decision(std::to_string(i)));
    Consume(runtime,many,factory); Check(runtime.ActiveCount()==32,"bounded active instance budget");
    auto revision=runtime.Revision(); runtime.Clear(); AutomationEvaluation old; old.config_generation=1; old.decisions={Decision()};
    runtime.Consume(old,factory,0,revision); Check(runtime.ActiveCount()==0,"rebuild rejects in-flight old admission");
    Consume(runtime,{Decision()},factory,0,2); Consume(runtime,{},factory,0,3);
    Check(runtime.ActiveCount()==0,"config generation rebuild conservatively cancels");
}
struct Dll {
    PluginManager manager; TriggeredEffectInstance sample;
    using Configure=void(*)(int,uint64_t,float); using Count=unsigned(*)(unsigned);
    Configure configure; Count count;
    explicit Dll(const fs::path& file) {
        sample=manager.LoadPluginInstance(file.string()); Check(bool(sample),"load real lifecycle DLL");
        auto module=sample.GetGeneration()->handle->GetModule();
        configure=reinterpret_cast<Configure>(GetProcAddress(module,"FixtureConfigure"));
        count=reinterpret_cast<Count>(GetProcAddress(module,"FixtureCount"));
        Check(configure && count,"fixture control exports");
    }
    auto Factory() { return [this](const json&) { return manager.CreateEffectInstance("automation_lifecycle"); }; }
};
void DllLifecycle(const fs::path& generated) {
    Dll dll(generated/"automation_lifecycle_old.dll"); AutomationEffectRuntime runtime;
    auto factory=dll.Factory(); auto shot=Decision();
    dll.configure(0,100,0.5f); Consume(runtime,{shot},factory);
    const auto opacities=dll.count(4);
    Check(Frame(runtime,50).buffer[0]==110,"DLL alpha and same elapsed Render/finished/opacity order");
    Check(Frame(runtime,100).buffer[0]==20 && runtime.ActiveCount()==0,"finished retires without composing");
    Check(dll.count(4)==opacities+1,"finished frame does not query opacity");
    runtime.Clear(); auto capable_envelope=shot;
    capable_envelope.action["compatibility"]={{"duration_ms",1},{"attack_ms",1},{"fade_out_ms",1}};
    Consume(runtime,{capable_envelope},factory);
    Check(Frame(runtime,0).buffer[0]==110 && Frame(runtime,50).buffer[0]==110,"capable opacity/completion never multiplied by host envelope");
    runtime.Clear();
    auto persistent=Decision("persistent",true); Consume(runtime,{persistent},factory);
    Frame(runtime,100,true); const auto created=dll.count(0); Consume(runtime,{persistent},factory,200);
    Check(runtime.ActiveCount()==0 && dll.count(0)==created,"early-finished persistent does not respawn in True interval");
    persistent.eligibility=ConditionTruth::Unknown; Consume(runtime,{persistent},factory,201);
    persistent.eligibility=ConditionTruth::True; Consume(runtime,{persistent},factory,202);
    Check(runtime.ActiveCount()==1 && dll.count(0)==created+1,"Unknown then True rearms completed persistent");
    runtime.Clear(); dll.configure(7,100,0); Consume(runtime,{shot},factory);
    Check(Frame(runtime,0).buffer[0]==20,"zero plugin opacity preserves lower");
    Check(Frame(runtime,1).buffer[0]==200,"zero-opacity render advanced mutable DLL state");
    for(int behavior:{1,2,3,4,5,6,10}) {
        runtime.Clear(); dll.configure(behavior,10,0.5f); Consume(runtime,{shot},factory);
        Check(Frame(runtime,behavior==10?10:0).buffer[0]==20 && runtime.ActiveCount()==0,"throw/invalid callback retires preserving lower frame");
    }
    runtime.Clear(); dll.configure(0,100000,2.f); Consume(runtime,{shot},factory);
    Check(Frame(runtime,0).buffer[0]==200,"finite opacity clamps high");
    Frame(runtime,5000); Check(runtime.ActiveCount()==0,"default capable watchdog expires");
    runtime.Clear(); dll.configure(0,100000,-2.f); shot.action["watchdog_ms"]=20; Consume(runtime,{shot},factory);
    Check(Frame(runtime,0).buffer[0]==20,"finite opacity clamps low");
    Check(Frame(runtime,20).buffer[0]==20 && runtime.ActiveCount()==0,"explicit watchdog expiration");
    runtime.Clear(); dll.configure(0,1000,0.5f); shot=Decision(); Consume(runtime,{shot},factory);
    auto before=dll.count(0); dll.configure(11,1000,0.5f); Consume(runtime,{shot},factory,10);
    Check(Frame(runtime,20).buffer[0]==110 && dll.count(0)==before,"null DLL factory restart retains old generation instance");
    dll.configure(12,1000,0.5f); Consume(runtime,{shot},factory,30);
    Check(Frame(runtime,40).buffer[0]==110,"throwing DLL factory restart retains old instance");
}
void Capabilities(const fs::path& generated) {
    for(const auto* name:{"abi_finished_only","abi_opacity_only","abi_no_lifecycle","abi_lifecycle_unknown","abi_lifecycle_no_revision"}) {
        PluginManager manager; auto instance=manager.LoadPluginInstance((generated/(std::string(name)+".dll")).string());
        Check(bool(instance),"load capability fixture"); AutomationEffectRuntime runtime;
        Consume(runtime,{Decision()},[&](const json&) { return manager.CreateEffectInstance("abi_fixture"); });
        Check(Frame(runtime,0).buffer[0]==17,"finished-only opacity one or legacy fallback without double fading");
        const bool capable=std::string(name)=="abi_finished_only";
        auto frame=Frame(runtime,17);
        Check((runtime.ActiveCount()==0)==capable,"only negotiated finished capability expires at DLL completion");
        if(!capable) {
            Check(Frame(runtime,1000).buffer[0]==18,"fallback default fade 400 of 1200");
            Frame(runtime,1200); Check(runtime.ActiveCount()==0,"fallback default duration");
        }
    }
}
void Generation(const fs::path& generated, const fs::path& dir) {
    AutomationEffectRuntime orphan;
    std::weak_ptr<const PluginEntry> orphan_generation;
    {
        PluginManager short_lived;
        auto instance=short_lived.LoadPluginInstance((generated/"automation_lifecycle_old.dll").string());
        Check(bool(instance),"load generation for manager teardown"); orphan_generation=instance.GetGeneration();
        Consume(orphan,{Decision()},[&](const json&) { return short_lived.CreateEffectInstance("automation_lifecycle"); });
    }
    Check(!orphan_generation.expired() && Frame(orphan,10).buffer[0]==110,"callbacks remain valid after PluginManager destruction");
    orphan.Clear(); Check(orphan_generation.expired(),"last runtime owner releases generation after destruction");
    const auto path=dir/"reload.dll"; fs::copy_file(generated/"automation_lifecycle_old.dll",path,fs::copy_options::overwrite_existing);
    PluginManager manager; auto old=manager.LoadPluginInstance(path.string()); Check(bool(old),"load generation old");
    AutomationEffectRuntime runtime;
    auto factory=[&](const json&) { return manager.CreateEffectInstance("automation_lifecycle"); };
    Consume(runtime,{Decision("old")},factory);
    std::weak_ptr<const PluginEntry> old_generation=old.GetGeneration();
    fs::copy_file(generated/"automation_lifecycle_new.dll",path,fs::copy_options::overwrite_existing);
    Check(manager.ReloadPlugin("automation_lifecycle"),"reload new generation"); old={};
    Check(!old_generation.expired() && Frame(runtime,10).buffer[0]==110,"old generation remains pinned and callbacks paired after replacement");
    runtime.Clear(); Check(old_generation.expired(),"old DLL released only after runtime effect destruction");
    Consume(runtime,{Decision("new")},factory);
    Check(Frame(runtime,10).buffer[0]==60,"new trigger renders new generation with its own callbacks");
}
class HealthGradient final : public Effect {
public:
    void Render(uint64_t,FrameBuffer&,const Keymap&) override { throw std::runtime_error("context required"); }
    void RenderWithContext(const EffectContext& context,FrameBuffer& frame) override {
        frame.Clear(); const auto health=static_cast<uint8_t>(context.gsi->GetNumber("player.state.health",0));
        frame.SetKey(0,100-health,health,0);
    }
};
void Composition() {
    auto solid=[](ColorRGB color) { return [color](const json&) { return TriggeredEffectInstance::FromHostEffect(std::make_shared<Paint>(color)); }; };
    AutomationEffectRuntime runtime; auto shot=Decision();
    Consume(runtime,{shot},solid(ColorRGB(0,0,0))); Check(Frame(runtime,0).buffer[0]==20,"Overlay black uncovered");
    runtime.Clear(); shot.action["composition"]="replace"; Consume(runtime,{shot},solid(ColorRGB(0,0,0)));
    Check(Frame(runtime,0).buffer[0]==0,"Replace black covered");
    runtime.Clear(); shot.action["blend"]="additive"; Consume(runtime,{shot},solid(ColorRGB(0,0,0)));
    Check(Frame(runtime,0).buffer[0]==20,"Replace Additive black adds zero");
    runtime.Clear(); Consume(runtime,{shot},solid(ColorRGB(250,0,0)));
    auto f=Frame(runtime,0); Check(f.buffer[0]==255 && f.buffer[1]==20,"Additive saturates independently");
    const auto replace_add=Frame(runtime,1000); runtime.Clear(); shot.action["composition"]="overlay";
    Consume(runtime,{shot},solid(ColorRGB(250,0,0))); const auto overlay_add=Frame(runtime,1000);
    Check(std::equal(std::begin(replace_add.buffer),std::end(replace_add.buffer),std::begin(overlay_add.buffer)),"Replace/Overlay Additive equivalence including black and fractional weight");
    runtime.Clear(); shot.action["blend"]="alpha"; Consume(runtime,{shot},solid(ColorRGB(200,0,0)));
    f=Frame(runtime,1000); Check(f.buffer[0]==110 && f.buffer[1]==10,"Alpha half blends all covered channels");
    for(bool reverse:{false,true}) {
        runtime.Clear(); auto a=Decision("a"),b=Decision("b"); a.rule_order=2; b.rule_order=1;
        if(reverse) a.action["priority"]=0;
        auto factory=[&](const json& ref) { return solid(ref["name"]=="a"?ColorRGB(100,0,0):ColorRGB(200,0,0))(ref); };
        a.action["effect"]["name"]="a"; b.action["effect"]["name"]="b";
        Consume(runtime,{a,b},factory); Check(Frame(runtime,0).buffer[0]==(reverse?200:100),"priority then config order sort, not input order");
    }
    runtime.Clear(); auto a=Decision("a"),b=Decision("b");
    Consume(runtime,{a},solid(ColorRGB(100,0,0))); Consume(runtime,{b},solid(ColorRGB(200,0,0)));
    Check(Frame(runtime,0).buffer[0]==200,"instance sequence final tie breaker");
    Consume(runtime,{a},solid(ColorRGB(150,0,0))); Check(Frame(runtime,0).buffer[0]==150,"restart sequence newer on exact tuple tie");
    EffectEngine engine; Keymap keymap; auto profile=std::make_shared<Profile>(); profile->base_effect=std::make_shared<Paint>(ColorRGB(10,0,0));
    engine.SetActiveProfile(profile); auto p=Decision("p",true),t=Decision("t"); p.action["priority"]=100; t.action["priority"]=-100;
    Consume(engine.GetAutomationEffects(),{p},solid(ColorRGB(50,0,0)));
    engine.TickAt(0,f,keymap); Check(f.buffer[0]==50,"persistent renders after base");
    Consume(engine.GetAutomationEffects(),{t},solid(ColorRGB(200,0,0)));
    engine.TickAt(0,f,keymap); Check(f.buffer[0]==200,"lower-priority transient class renders after persistent");
    engine.GetAutomationEffects().Clear();
    profile=std::make_shared<Profile>(); profile->base_effect=std::make_shared<HealthGradient>(); engine.SetActiveProfile(profile);
    GsiState gsi; gsi.UpdateFromPayloadAt({{"player",{{"state",{{"health",100}}}}}},0);
    Consume(engine.GetAutomationEffects(),{Decision()},solid(ColorRGB(255,255,255)));
    engine.TickAt(1000,f,keymap,&gsi); Check(f.buffer[0]==127 && f.buffer[1]==177 && f.buffer[2]==127,"golden flash half over health 100");
    gsi.UpdateFromPayloadAt({{"player",{{"state",{{"health",20}}}}}},1001);
    engine.TickAt(1000,f,keymap,&gsi); Check(f.buffer[0]==167 && f.buffer[1]==137 && f.buffer[2]==127,"golden health changes immediately under same flash opacity");
    engine.TickAt(1200,f,keymap,&gsi); Check(f.buffer[0]==80 && f.buffer[1]==20 && f.buffer[2]==0,"fade completion reveals latest health not captured framebuffer");
}
void ResolutionAndEvaluation(const fs::path& dir) {
    json effect={{"type","trigger_effect"},{"effect",{{"kind","profile_effect"},{"name","paint"}}},{"lifetime","one_shot"}};
    json rule={{"id","kill"},{"model","automation_v2"},{"when",{{"mode","event"},{"condition",{{"event","event.kill"}}}}},{"action",effect}};
    json config={{"default_profile","paint"},{"profiles",{{"paint",{{"type","static"},{"color",{200,100,50}},{"brightness",0},{"fps",10},{"keys",{{"all",{0,0,0}}}}}}}},
        {"orchestration",{{"rules",json::array({rule})}}}};
    auto file=dir/"config.json"; std::ofstream(file)<<config.dump(); RuleEngine rules;
    Check(rules.LoadConfig(file.string()),"load runtime recipe config");
    auto ref=effect["effect"]; auto first=ResolveAutomationEffect(ref,rules),second=ResolveAutomationEffect(ref,rules);
    Check(first && second && first.GetEffect()!=second.GetEffect() && first.GetEffect()!=rules.GetProfile("paint")->base_effect,"fresh recipe instances never share base effect");
    Keymap keymap; Check(keymap.LoadFromJson("tests/fixtures/calibrated_keymap.json"),"load real keymap"); FrameBuffer frame; frame.Clear(); first.GetEffect()->Render(0,frame,keymap);
    bool colored=false; for(auto channel:frame.buffer) colored|=channel!=0;
    Check(colored,"profile_effect excludes brightness/key overrides");
    rules.GetProfile("paint")->Render(0,frame,keymap); bool black=true; for(auto channel:frame.buffer) black&=channel==0;
    Check(black,"full profile still applies brightness");
    AutomationEffectRuntime runtime; GsiState gsi;
    auto factory=[&](const json& reference) { return ResolveAutomationEffect(reference,rules); };
    auto tick=[&](uint64_t time) { const auto evaluation=rules.EvaluateAutomation(gsi,[]{return "cs2.exe";},time);
        runtime.Consume(evaluation,factory,time,runtime.Revision()); };
    gsi.UpdateFromPayloadAt({{"player",{{"state",{{"round_kills",0}}}}}},100); tick(100);
    Check(runtime.ActiveCount()==0,"actual RuleEngine startup seeds");
    gsi.UpdateFromPayloadAt({{"player",{{"state",{{"round_kills",1}}}}}},200); tick(200);
    Check(runtime.ActiveCount()==1,"actual RuleEngine event admission creates runtime instance");
    const auto activations=runtime.TakeActivations();
    Check(activations.size()==1 && activations[0].rule_id=="kill" &&
        activations[0].packet_sequence==2 && activations[0].at_ms>0,
        "kill admission has a matching successful layer activation marker");
    Check(runtime.TakeActivations().empty(),"activation markers drain once");
    auto rising=config;
    rising["orchestration"]["rules"][0]["when"]={{"mode","rising"},{"condition",{{"field","player.state.health"},{"op","<"},{"value",15}}}};
    std::ofstream(file)<<rising.dump(); Check(rules.LoadConfig(file.string()),"load rising runtime rule");
    gsi.UpdateFromPayloadAt({{"player",{{"state",{{"health",20}}}}}},300); tick(300);
    Check(runtime.ActiveCount()==0,"config rebuild cancels previous event and seeds rising");
    gsi.UpdateFromPayloadAt({{"player",{{"state",{{"health",10}}}}}},400); tick(400);
    Check(runtime.ActiveCount()==1,"actual RuleEngine rising admission creates runtime instance");
    auto persistent=rising;
    persistent["orchestration"]["rules"][0]["when"]["mode"]="state";
    persistent["orchestration"]["rules"][0]["action"]["lifetime"]="while_true";
    std::ofstream(file)<<persistent.dump(); Check(rules.LoadConfig(file.string()),"load persistent runtime rule");
    tick(400); Check(runtime.ActiveCount()==1,"state True starts persistent after rebuild");
    tick(3400); Check(runtime.ActiveCount()==1,"fresh boundary keeps persistent");
    tick(3401); Check(runtime.ActiveCount()==0,"RuleEngine stale Unknown removes persistent without packet");
    for(const auto bad: {0,60001}) { auto invalid=config; invalid["orchestration"]["rules"][0]["action"]["watchdog_ms"]=bad;
        std::ofstream(file)<<invalid.dump(); Check(!rules.LoadConfig(file.string()),"invalid watchdog rejects candidate"); }
    for(const auto good: {1,60000}) { auto valid=config; valid["orchestration"]["rules"][0]["action"]["watchdog_ms"]=good;
        std::ofstream(file)<<valid.dump(); Check(rules.LoadConfig(file.string()),"watchdog boundary accepts"); }
    for(const auto& invalid_value : json::array({true, 1.5, "5000", -1})) {
        auto invalid=config; invalid["orchestration"]["rules"][0]["action"]["watchdog_ms"]=invalid_value;
        std::ofstream(file)<<invalid.dump(); Check(!rules.LoadConfig(file.string()),"non-integer watchdog rejected");
    }
    persistent["orchestration"]["rules"][0]["action"]["watchdog_ms"]=5000;
    std::ofstream(file)<<persistent.dump(); Check(!rules.LoadConfig(file.string()),"persistent watchdog rejected");
    Check(!ResolveAutomationEffect({{"kind","plugin"},{"name","nonexistent_stage3_plugin"}},rules),"typed missing plugin returns no black placeholder");
    runtime.Clear(); auto missing=Decision(); missing.action["effect"]["name"]="nonexistent_stage3_plugin";
    missing.action["composition"]="replace"; std::ostringstream diagnostics;
    {
        struct Capture {
            std::streambuf* old;
            explicit Capture(std::ostringstream& target) : old(std::cout.rdbuf(target.rdbuf())) {}
            ~Capture() { std::cout.rdbuf(old); }
        } capture(diagnostics);
        for(int i=0;i<100;++i) Consume(runtime,{missing},factory,i,100);
    }
    const auto messages=diagnostics.str();
    Check(std::count(messages.begin(),messages.end(),'\n')==1,"real missing plugin failures produce one bounded diagnostic, no loader spam");
    Check(Frame(runtime,100).buffer[0]==20,"real unresolved Replace action preserves lower frame");
}
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOOPENFILEERRORBOX);
    const auto dir=fs::temp_directory_path()/("aura-stage3-"+std::to_string(GetCurrentProcessId()));
    try {
        Check(argc==2,"fixture directory argument"); fs::create_directories(dir);
        HostLifecycle(); DllLifecycle(argv[1]); Capabilities(argv[1]); Generation(argv[1],dir); Composition(); ResolutionAndEvaluation(dir);
        fs::remove_all(dir); std::cout<<"PASS: "<<checks<<" Stage 3 runtime assertions\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL after "<<checks<<": "<<error.what()<<"\n"; return 1; }
}
