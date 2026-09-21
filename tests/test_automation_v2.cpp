#include "gsi/gsi_adapter.h"
#include "config/rule_engine.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace aura;
using json = nlohmann::json;
namespace {
int checks = 0;
void Check(bool ok, const std::string& msg) { ++checks; if (!ok) throw std::runtime_error(msg); }
json Field(std::string key, json value, std::string op = "==") { return {{"field",key},{"op",op},{"value",value}}; }
json Event(std::string key) { return {{"event", key}}; }
json Logic(std::string op, json children) { return {{"type",op},{"conditions",children}}; }
json Low() { return Field("player.state.health", 15, "<"); }
json EffectRule(std::string id, std::string mode, json condition) {
    return {{"id",id},{"model","automation_v2"},{"when",{{"mode",mode},{"condition",condition}}},
        {"action",{{"type","trigger_effect"},{"effect",{{"kind","profile_effect"},{"name","low"}}},
        {"lifetime",mode == "state" ? "while_true" : "one_shot"}}}};
}
json ProfileRule(std::string id, json condition, std::string profile = "low") {
    auto r = EffectRule(id,"state",condition); r["action"] = {{"type","activate_profile"},{"profile",profile}}; return r;
}
json Config(json rules = json::array()) {
    return {{"default_profile","desktop"}, {"profiles",{{"desktop",{{"type","static"}}},{"low",{{"type","static"}}},{"legacy",{{"type","static"}}}}},
        {"orchestration",{{"rules", rules}}}};
}
json Payload(int hp, int kills = 0, int hs = 0) {
    return {{"player",{{"state",{{"health",hp},{"armor",100},{"round_kills",kills},{"round_killhs",hs}}}}}};
}
struct Harness {
    RuleEngine engine; GsiState gsi; std::filesystem::path file; std::string proc = "cs2.exe";
    explicit Harness(const std::filesystem::path& dir, json config) : file(dir/"v2.json") { Load(config); }
    bool Write(json config) { std::ofstream(file) << config.dump(); return engine.LoadConfig(file.string()); }
    void Load(json config) { Check(Write(config), "load valid config"); }
    AutomationEvaluation Tick(uint64_t now) { return engine.EvaluateAutomation(gsi,[&]{return proc;},now); }
    void Packet(uint64_t time, int hp, int kills = 0, int hs = 0) { gsi.UpdateFromPayloadAt(Payload(hp,kills,hs),time); }
};
size_t Shots(const AutomationEvaluation& e, const std::string& id = "") {
    size_t n = 0; for (const auto& d : e.decisions) if (d.admitted && (id.empty() || d.rule_id == id)) ++n; return n;
}
ConditionTruth Truth(const AutomationEvaluation& e, const std::string& id) {
    for (const auto& d : e.decisions) if (d.rule_id == id && !d.admitted) return d.eligibility;
    throw std::runtime_error("missing decision " + id);
}
void StateAndRising(const std::filesystem::path& dir) {
    auto config = Config(json::array({ProfileRule("profile",Low()),EffectRule("rise","rising",Low()),EffectRule("persist","state",Low())}));
    Harness h(dir,config);
    int count = 0; uint64_t t = 100;
    for (int hp : {20,10,8,20,10}) {
        h.Packet(t,hp); auto out = h.Tick(t); count += static_cast<int>(Shots(out,"rise"));
        Check((out.profile->name == "low") == (hp < 15), "state profile trace");
        Check(Truth(out,"persist") == (hp < 15 ? ConditionTruth::True : ConditionTruth::False), "persistent eligibility");
        t += 100;
    }
    Check(count == 2,"rising exactly twice");
    h.gsi.Clear(); h.Packet(700,10); auto initial = h.Tick(700);
    Check(initial.profile->name == "low" && Shots(initial)==0,"initial/reconnect low seeds");
    Check(Truth(h.Tick(3700),"persist") == ConditionTruth::True,"3000 fresh boundary");
    auto stale = h.Tick(3701);
    Check(stale.profile->name == "desktop" && Truth(stale,"persist") == ConditionTruth::Unknown,"3001 expires without packet");
    Check(h.gsi.IsActive(),"legacy status remains active independent of decision time");
    h.Packet(3800,10); Check(Shots(h.Tick(3800))==0,"recovery true no rising");
    h.gsi.UpdateFromPayloadAt({{"player",{{"state",{{"armor",100}}}}}},3900);
    Check(Truth(h.Tick(3900),"persist")==ConditionTruth::Unknown,"missing unknown");
    h.Packet(4000,10); Check(Shots(h.Tick(4000))==0,"missing to true seeds");
    h.Packet(4100,20); h.Tick(4100);
    config["orchestration"]["rules"][1]["when"]["condition"]["value"] = 21;
    h.Load(config); Check(Shots(h.Tick(4101))==0,"semantic edit true seeds");
    config["orchestration"]["rules"][1]["enabled"] = false; h.Load(config); h.Packet(4200,10); h.Tick(4200);
    config["orchestration"]["rules"][1]["enabled"] = true; h.Load(config); Check(Shots(h.Tick(4201))==0,"enable seeds");
    // Unchanged reload retains a valid false baseline.
    config["orchestration"]["rules"][1]["when"]["condition"] = Low(); h.Load(config);
    h.Packet(4300,20); h.Tick(4300); h.Load(config); h.Packet(4400,10);
    Check(Shots(h.Tick(4400),"rise")==1,"unchanged reload preserves baseline");
    // Scope exit cannot arm an edge for later replay.
    config["orchestration"]["rules"][1]["scope"] = Field("process.name","cs2.exe"); h.Load(config);
    h.Packet(4500,20); h.Tick(4500); h.proc="desktop.exe"; h.Packet(4600,10); h.Tick(4600);
    h.proc="cs2.exe"; Check(Shots(h.Tick(4700))==0,"scope recovery true seeds");
}
void TruthAndSnapshot(const std::filesystem::path& dir) {
    // V2 process comparison is symmetric for suffix presence and case, in both
    // scope and condition, without requiring connected telemetry.
    for (const auto* foreground : {"cs2", "cs2.exe", "CS2.EXE"}) {
        for (const auto* target : {"cs2", "cs2.exe", "CS2.EXE"}) {
            auto rule=ProfileRule("canonical",Field("process",target));
            rule["scope"]=Field("process.name",target);
            Harness process(dir,Config(json::array({rule}))); process.proc=foreground;
            Check(process.Tick(100).profile->name=="low","process suffix/case canonicalization in scope and condition");
            const auto snapshot=AutomationInputSnapshot::Capture(foreground,process.gsi.GetAutomationTelemetry(),100,3000);
            Check(ConditionNode::FromAutomationJson(Field("process_name",target,"!="),false).Evaluate(snapshot).truth==ConditionTruth::False,
                  "canonical process inequality is false");
            process.proc="cs2.exe.other";
            Check(process.Tick(101).profile->name=="desktop","process suffix matching is not substring matching");
        }
    }
    Check(ConditionNode::FromAutomationJson(Event("event.flash"),true).ToJson()==Event("event.flashed"), "truthful event alias canonicalization");
    bool rejected_mvp=false;
    try { ConditionNode::FromAutomationJson(Event("event.round_mvp"),true); } catch (...) { rejected_mvp=true; }
    Check(rejected_mvp,"no invented MVP occurrence");
    GsiState g;
    g.UpdateFromPayloadAt(Payload(10),100);
    auto s = AutomationInputSnapshot::Capture("cs2.exe",g.GetAutomationTelemetry(),100,3000);
    auto node = ConditionNode::FromAutomationJson(Logic("and",json::array({Field("process","cs2"),Low()})),false);
    g.UpdateFromPayloadAt(Payload(99),101); g.SetForegroundProcess("other.exe");
    Check(node.Evaluate(s).truth == ConditionTruth::True,"immutable snapshot survives live foreground/telemetry changes");
    auto missing = Field("missing",1);
    Check(ConditionNode::FromAutomationJson(Logic("not",json::array({missing})),false).Evaluate(s).truth==ConditionTruth::Unknown,"NOT unknown");
    Check(ConditionNode::FromAutomationJson(Logic("and",json::array({missing,Field("process","other")})),false).Evaluate(s).truth==ConditionTruth::False,"false dominates AND");
    Check(ConditionNode::FromAutomationJson(Logic("or",json::array({missing,Field("process","cs2")})),false).Evaluate(s).truth==ConditionTruth::True,"true dominates OR");
    Check(ConditionNode::FromAutomationJson(Field("player.state.health","10"),false).Evaluate(s).truth==ConditionTruth::Unknown,"type invalid unknown");
    Check(ConditionNode::FromAutomationJson(Field("player.state.health",10.00001),false).Evaluate(s).truth==ConditionTruth::True,"valid numeric tolerance retained");
    Harness h(dir,Config(json::array({ProfileRule("proc",Logic("or",json::array({Low(),Field("process","cs2")})))})));
    auto disconnected = h.Tick(100);
    Check(disconnected.profile->name=="low","process branch while disconnected");
    h.Load(Config(json::array({ProfileRule("snapshot",Logic("and",json::array({Low(),Field("process","cs2")})))})));
    h.Packet(200,10); int captures=0;
    auto out=h.engine.EvaluateAutomation(h.gsi,[&]{++captures; h.gsi.UpdateFromPayloadAt(Payload(99),201); return "cs2.exe";},200);
    Check(captures==2,"one foreground capture per packet/current batch");
    Check(out.profile->name=="low","admitted snapshots immutable despite capture-time update");
}
void Events(const std::filesystem::path& dir) {
    auto kill=EffectRule("kill","event",Event("event.kill")); kill["scope"]=Field("process","cs2.exe");
    auto head=EffectRule("both","event",Logic("and",json::array({Event("event.kill"),Event("event.headshot")})));
    auto nothead=EffectRule("body","event",Logic("and",json::array({Event("event.kill"),Logic("not",json::array({Event("event.headshot")}))})));
    auto witness=EffectRule("witness","event",Logic("or",json::array({Field("process","cs2"),Event("event.kill")})));
    auto negative=EffectRule("negative","event",Logic("not",json::array({Event("event.headshot")})));
    Harness h(dir,Config(json::array({kill,head,nothead,witness,negative})));
    h.Packet(100,100); h.Tick(100);
    h.Packet(200,100,1); h.Packet(500,100,2);
    auto two=h.Tick(500);
    Check(Shots(two,"kill")==2 && Shots(two,"body")==2,"two packets before tick retain two admissions");
    Check(Shots(two,"both")==0 && Shots(two,"negative")==0,"same batch and positive witness");
    uint64_t prior_sequence=0;
    for(const auto& d:two.decisions) if(d.admitted) {
        Check(d.packet_sequence>=prior_sequence,"global packet-major admission order"); prior_sequence=d.packet_sequence;
    }
    h.Packet(600,100,2); Check(Shots(h.Tick(600))==0,"duplicate yields no occurrence despite public pulse");
    h.Packet(700,90,2); auto damage=h.Tick(700);
    Check(Shots(damage,"witness")==0,"process OR kill cannot witness unrelated damage");
    Check(Shots(damage,"body")==0,"kill AND NOT headshot requires a positive kill occurrence, not just absent headshot");
    h.Packet(800,90,3,1); auto hs=h.Tick(800);
    Check(Shots(hs,"both")==1 && Shots(hs,"body")==0 && Shots(hs,"kill")==1,"kill headshot cooccurrence");
    h.Packet(900,90,5,1); Check(Shots(h.Tick(900),"kill")==1,"counter delta two one occurrence");
    h.proc="desktop.exe"; h.Tick(950); h.Packet(1000,90,6,1); h.proc="cs2.exe";
    Check(Shots(h.Tick(1000),"kill")==0,"scope entry discards preentry packets");
    h.Packet(1100,90,7,1); h.proc="desktop.exe";
    Check(Shots(h.Tick(1100),"kill")==0,"foreground at dispatch not occurrence time");
    h.proc="cs2.exe"; h.Tick(1200); h.Tick(4101);
    h.Packet(4200,90,8,1); Check(Shots(h.Tick(4200),"kill")==0,"stale recovery occurrence discarded");
    h.Packet(4300,90,9,1); Check(Shots(h.Tick(4300),"kill")==1,"next recovered occurrence admitted");
    h.Packet(4400,90,10,1); Check(Shots(h.Tick(8000))==0,"expired packet cannot borrow freshness");
    h.gsi.Clear(); h.Packet(8100,90,10,1); h.Packet(8200,90,11,1);
    Check(Shots(h.Tick(8200))==0,"new epoch no historical replay");
    for(int i=0;i<260;++i) h.Packet(8300+i,90,12+i,1);
    auto overflow=h.Tick(8560);
    Check(overflow.rebased && overflow.dropped_input_batches==4 && Shots(overflow)==0,"overflow drops oldest and rebases all rules");
    h.Packet(8600,90,272,1); Check(Shots(h.Tick(8600),"kill")==1,"post overflow fresh admission");
    h.Packet(8700,100,0,0); Check(Shots(h.Tick(8700),"kill")==0,"counter reset is not a kill");
    h.Packet(8800,100,1,0); Check(Shots(h.Tick(8800),"kill")==1,"kill after counter reset");
    auto drain=h.gsi.DrainAutomationInputs(); Check(drain.batches.empty(),"global drain consumed exactly once");
    // Initial engine sees an existing stream: never replays it.
    Harness startup(dir,Config(json::array({kill})));
    startup.Packet(1,100); startup.Packet(2,100,1); Check(Shots(startup.Tick(2))==0,"startup history discarded");
}
void PacketCoherenceAndLegacy(const std::filesystem::path& dir) {
    GsiState concurrent;
    std::atomic<bool> start{false};
    std::vector<std::thread> writers;
    for (int worker=0;worker<4;++worker) writers.emplace_back([&] {
        while (!start.load()) std::this_thread::yield();
        for (int i=0;i<25;++i) concurrent.UpdateFromPayload(Payload(100));
    });
    start.store(true);
    for (auto& writer:writers) writer.join();
    const auto received=concurrent.DrainAutomationInputs();
    Check(received.batches.size()==100 && received.dropped_batches==0,"concurrent ingress retains each packet");
    bool ordered=true; uint64_t receipt=0, sequence=0;
    for (const auto& packet:received.batches) {
        ordered &= packet->telemetry->source_epoch==1 && packet->telemetry->received_at_ms>=receipt &&
            packet->telemetry->packet_sequence==++sequence;
        receipt=packet->telemetry->received_at_ms;
    }
    Check(ordered,"locked production receipt sampling preserves timestamp/sequence order");
    GsiState bounded;
    for (uint64_t i=0;i<260;++i) bounded.UpdateFromPayloadAt(Payload(100,static_cast<int>(i)),i);
    const auto retained=bounded.DrainAutomationInputs();
    Check(retained.batches.size()==256 && retained.dropped_batches==4 && retained.overflowed,"exact input buffer bound");
    Check(retained.batches.front()->telemetry->packet_sequence==5 && retained.batches.back()->telemetry->packet_sequence==260,"overflow drops oldest observations");
    auto rule=EffectRule("packet","event",Logic("and",json::array({Event("event.kill"),Low()})));
    auto scoped=EffectRule("scoped","event",Event("event.kill")); scoped["scope"]=Low();
    Harness h(dir,Config(json::array({rule,scoped})));
    h.Packet(100,10); h.Tick(100);
    h.Packet(200,10,1); h.Packet(300,20,2);
    auto result=h.Tick(300);
    Check(Shots(result,"packet")==1,"buffered condition uses its own telemetry");
    Check(Shots(result,"scoped")==1,"earlier valid packet scope not overwritten by latest scope exit");
    Check(result.decisions[0].packet_sequence==2,"admission retains source cursor");
    // An expired kill packet must not borrow a fresh duplicate packet's receipt time.
    h.Packet(400,10,3); h.Packet(3601,10,3); Check(Shots(h.Tick(3601))==0,"old occurrence cannot borrow newest freshness");
    auto snapshot=h.gsi.GetAutomationTelemetry();
    Check(snapshot->source_epoch==1 && snapshot->packet_sequence==5 && snapshot->received_at_ms==3601,"telemetry metadata");
    auto admitted=AutomationInputSnapshot::Capture("test.exe",snapshot,3701,3000);
    Check(admitted.admitted_at_ms==3701 && admitted.telemetry_age_ms==100 && admitted.automation_fresh,"snapshot freshness metadata");
    h.Packet(15000,10,4); Check(Shots(h.Tick(15000))==0,"heartbeat reconnect seeds without Clear");
    Check(h.gsi.GetAutomationTelemetry()->source_epoch==2,"heartbeat reconnect epoch");
    auto process=EffectRule("process","rising",Field("process","code.exe"));
    Harness pure(dir,Config(json::array({process})));
    pure.proc="other.exe"; pure.Tick(1); pure.proc="code.exe";
    Check(Shots(pure.Tick(2))==1,"pure process rising without GSI");
    auto gated=EffectRule("gated","state",Low()); gated["scope"]=Field("process","other.exe");
    Harness gate(dir,Config(json::array({gated})));
    Check(Truth(gate.Tick(1),"gated")==ConditionTruth::False,"false scope dominates unknown condition");
    Harness zero(dir,Config(json::array({EffectRule("zero","rising",Low())})));
    zero.Packet(0,20); zero.Tick(0); zero.Packet(3001,10);
    Check(Shots(zero.Tick(3001))==0,"zero receipt is valid; gap recovery must seed");
    // Legacy != missing and NOT retain their historical boolean rules; new NOT is unknown.
    Harness old(dir,Config(json::array({{{"target_profile","legacy"},{"condition",{{"not",Field("absent",1)}}}}})));
    Check(old.Tick(3001).profile->name=="legacy","legacy NOT missing still true");
    auto legacy=Config(); legacy["gsi_bindings"]=json::array({{{"field","player.state.health"},{"operator","<"},{"value","15"},{"profile","legacy"}}});
    old.Load(legacy); old.Packet(1,10);
    Check(old.Tick(5000).profile->name=="legacy","legacy coercion/freshness unchanged above v2 3s");
    legacy["orchestration"]["rules"]=json::array({{{"target_profile","low"},{"condition",Low()},{"model",42}}});
    old.Load(legacy); Check(old.Tick(5000).profile->name=="low","legacy unknown model metadata preserved");
}
void CandidateValidation(const std::filesystem::path& dir) {
    const auto active=ProfileRule("active",Field("process","cs2.exe"));
    Harness h(dir,Config(json::array({active})));
    auto reject=[&](const json& candidate, const std::string& label) {
        Check(!h.Write(candidate),label+" rejected");
        const auto plan=h.engine.GetEvaluationPlan();
        Check(plan.size()==1 && plan.front().automation.id=="active",label+" retains exact previous plan");
        Check(h.Tick(100).profile->name=="low",label+" retains active profile");
    };
    reject(Config(json::array({ProfileRule("typo",Field("process","cs2.exe"),"loow")})),"missing activate profile");
    for (auto mode:{"state","rising","event"}) {
        auto effect=EffectRule("typo",mode,std::string(mode)=="event"?Event("event.kill"):Low());
        effect["action"]["effect"]["name"]="loow";
        reject(Config(json::array({effect})),std::string("missing profile_effect ")+mode);
        effect["enabled"]=false;
        reject(Config(json::array({effect})),std::string("disabled missing profile_effect ")+mode);
    }
    auto removed=Config(json::array({active})); removed["profiles"].erase("low");
    reject(removed,"reference exists only in old runtime");
    for (bool dnd:{false,true}) {
        for (auto mode:{"state","rising","event"}) {
            auto effect=EffectRule("dnd",mode,std::string(mode)=="event"?Event("event.kill"):Low());
            effect["dnd"]=dnd;
            reject(Config(json::array({effect})),std::string("effect DND presence ")+mode+(dnd?" true":" false"));
        }
    }
    // Roots count as depth 1: 31 NOT nodes plus a leaf have depth 32.
    auto chain=[](unsigned depth) {
        json node=Field("process","cs2.exe");
        for(unsigned level=1;level<depth;++level) node=Logic("not",json::array({node}));
        return node;
    };
    for (bool scope:{false,true}) {
        auto at_limit=active;
        if(scope) at_limit["scope"]=chain(32); else at_limit["when"]["condition"]=chain(32);
        Check(h.Write(Config(json::array({at_limit}))),scope?"scope depth 32 accepted":"condition depth 32 accepted");
        h.Load(Config(json::array({active})));
        if(scope) at_limit["scope"]=chain(33); else at_limit["when"]["condition"]=chain(33);
        reject(Config(json::array({at_limit})),scope?"scope depth 33":"condition depth 33");
    }
    auto wide=[](unsigned leaves) {
        json children=json::array();
        for(unsigned i=0;i<leaves;++i) children.push_back(Field("process","cs2.exe"));
        return Logic("and",children);
    };
    // Shared budget: condition root + 254 leaves + one scope leaf = 256.
    auto at_limit=ProfileRule("active",wide(254)); at_limit["scope"]=Field("process","cs2.exe");
    h.Load(Config(json::array({at_limit})));
    Check(h.Tick(100).profile->name=="low","256 combined nodes accepted and evaluated");
    h.Load(Config(json::array({active})));
    at_limit["when"]["condition"]=wide(255);
    reject(Config(json::array({at_limit})),"257 combined nodes");
    // Reverse the split to ensure scope and condition do not get separate budgets.
    at_limit=active; at_limit["scope"]=wide(254);
    h.Load(Config(json::array({at_limit})));
    Check(h.Tick(100).profile->name=="low","256 combined nodes with wide scope accepted");
    h.Load(Config(json::array({active})));
    at_limit["scope"]=wide(255);
    reject(Config(json::array({at_limit})),"257 combined nodes with wide scope");
    // No implicit node for omitted scope; budget resets between rules.
    auto full=ProfileRule("full",wide(255));
    auto full2=full; full2["id"]="full2";
    h.Load(Config(json::array({full,full2})));
    Check(h.engine.GetEvaluationPlan().size()==2,"256 nodes per rule rather than per config");
    h.Load(Config(json::array({active})));
    full["when"]["condition"]=wide(256);
    reject(Config(json::array({full})),"257 condition-only nodes");
    auto disabled=ProfileRule("disabled",Low(),"loow"); disabled["enabled"]=false;
    reject(Config(json::array({disabled})),"disabled missing activate profile");
    for(bool dnd:{false,true}) {
        auto profile=active; profile["dnd"]=dnd;
        h.Load(Config(json::array({profile})));
        Check(h.Tick(100).suppress_web_ui==dnd,"profile DND boolean retained");
    }
    auto newly_defined=ProfileRule("new",Field("process","cs2.exe"),"new_profile");
    auto candidate=Config(json::array({newly_defined})); candidate["profiles"]["new_profile"]={{"type","static"}};
    h.Load(candidate); Check(h.Tick(100).profile->name=="new_profile","reference resolves from full candidate profiles");
    auto plugin=EffectRule("plugin","event",Event("event.kill"));
    plugin["action"]["effect"]={{"kind","plugin"},{"name","stage2_uninstalled_plugin"}};
    h.Load(Config(json::array({plugin})));
    Check(h.engine.GetEvaluationPlan().front().automation.action["effect"]["kind"]=="plugin","plugin resolution deferred to Stage 3");
    // Legacy parser still has no new v2 limits or shared AST budget.
    auto legacy=Config(json::array({{{"target_profile","low"},{"condition",chain(33)}}}));
    h.Load(legacy); Check(h.Tick(100).profile->name=="low","legacy depth 33 retains boolean behavior");
    legacy["orchestration"]["rules"][0]["condition"]=wide(256);
    h.Load(legacy); Check(h.Tick(100).profile->name=="low","legacy 257-node AST remains valid");
}
void ValidationAndPrecedence(const std::filesystem::path& dir) {
    auto cfg=Config(json::array({ProfileRule("v2",Field("process","cs2.exe"))}));
    cfg["rules"]=json::array({{{"process","cs2.exe"},{"profile","legacy"},{"suppress_web_ui",true}}});
    cfg["gsi_bindings"]=json::array({{{"field","player.state.health"},{"operator","<"},{"value",50},{"profile","legacy"}}});
    cfg["orchestration"]["event_overlays"]=json::array({{{"event","event.kill"},{"effect","low"}}});
    Harness h(dir,cfg); h.Packet(100,10);
    auto out=h.Tick(100); Check(out.profile->name=="low" && out.suppress_web_ui,"v2 orchestration precedes GSI/application; DND orthogonal");
    Check(h.engine.GetEvaluationPlan().size()==4 && h.engine.GetEventOverlayRules().size()==1,"one plan entry per source; legacy overlay sole executor");
    Check(out.decisions.size()==1 && Shots(out)==0 && out.profile_plan_entries_evaluated==2,"one arbitration pass; legacy entries not translated into duplicate admissions");
    auto legacy=json{{"id","legacy-orch"},{"target_profile","legacy"},{"condition",json::object()}};
    cfg["orchestration"]["rules"].insert(cfg["orchestration"]["rules"].begin(),legacy); h.Load(cfg);
    Check(h.Tick(101).profile->name=="legacy","legacy orchestration before v2 at config order");
    std::swap(cfg["orchestration"]["rules"][0],cfg["orchestration"]["rules"][1]); h.Load(cfg);
    Check(h.Tick(102).profile->name=="low","v2 before legacy at config order");
    auto plan=h.engine.GetEvaluationPlan();
    Check(plan[0].config_order==0 && plan[1].config_order==1 && plan[1].compatibility==CompatibilityPolicy::LegacyBoolean,"explicit provenance/order/compatibility");
    std::vector<json> invalid;
    auto bad=ProfileRule("bad",Low()); bad["when"]["mode"]="event"; invalid.push_back(bad);
    bad["when"]["mode"]="rising"; invalid.push_back(bad);
    bad=EffectRule("bad","state",Low()); bad["action"]["lifetime"]="one_shot"; invalid.push_back(bad);
    for(auto mode:{"rising","event"}) { bad=EffectRule("bad",mode,mode==std::string("event")?Event("event.kill"):Low()); bad["action"]["lifetime"]="while_true"; invalid.push_back(bad); }
    for(auto policy:{"stack","queue"}) { bad=EffectRule("bad","event",Event("event.kill")); bad["action"]["retrigger"]=policy; invalid.push_back(bad); }
    bad=EffectRule("bad","event",Event("event.kill")); bad["action"]["lifetime"]="latch_until_scope_exit"; invalid.push_back(bad);
    bad=ProfileRule("bad",Low()); bad["action"]["activation"]="latch_until_scope_exit"; invalid.push_back(bad);
    bad=EffectRule("bad","state",Event("event.kill")); invalid.push_back(bad);
    bad=EffectRule("bad","event",Event("event.kill")); bad["scope"]=Event("event.kill"); invalid.push_back(bad);
    bad=EffectRule("bad","state",Field("event.kill",true)); invalid.push_back(bad);
    bad=EffectRule("bad","state",Field("player.state.health",1,"bogus")); invalid.push_back(bad);
    for(const auto& record:invalid) {
        Check(!h.Write(Config(json::array({record}))),"unsupported new record rejected");
        Check(h.Tick(103).profile->name=="low","invalid reload retains old plan");
    }
    auto ignore=EffectRule("ignore","event",Event("event.kill")); ignore["action"]["retrigger"]="ignore_while_active";
    auto restart=EffectRule("restart","event",Event("event.kill")); restart["action"]["retrigger"]="restart";
    h.Load(Config(json::array({ignore,restart})));
    Check(h.engine.GetEvaluationPlan().size()==2,"approved retrigger policies accepted as metadata only");
    Check(!h.Write(Config(json::array({ignore,ignore}))),"duplicate stable id rejected");
    auto custom=Config(json::array({ProfileRule("fresh",Low())})); custom["orchestration"]["automation_freshness_ms"]=100;
    h.Load(custom); Check(h.Tick(200).profile->name=="low" && h.Tick(201).profile->name=="desktop","custom freshness boundary");
    custom["orchestration"]["automation_freshness_ms"]=0; Check(!h.Write(custom),"invalid freshness rejected");
}
}
int main() {
    const auto dir=std::filesystem::temp_directory_path()/("aura-v2-conformance-"+std::to_string(GetCurrentProcessId()));
    try {
        std::filesystem::create_directories(dir);
        StateAndRising(dir); TruthAndSnapshot(dir); Events(dir); PacketCoherenceAndLegacy(dir); ValidationAndPrecedence(dir); CandidateValidation(dir);
        std::filesystem::remove_all(dir);
        std::cout << "PASS: " << checks << " deterministic Automation v2 assertions\n"; return 0;
    } catch(const std::exception& e) { std::cerr << "FAIL after " << checks << ": " << e.what() << "\n"; return 1; }
}
