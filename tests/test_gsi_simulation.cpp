#include "gsi/gsi_adapter.h"
#include "config/rule_engine.h"
#include "engine/effect_engine.h"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace aura;
using Json=nlohmann::json;
void Check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
Json EffectRule(const char* id,const char* mode,const char* effect,const char* retrigger="restart") {
    Json rule={{"id",id},{"model","automation_v2"},{"scope",{{"field","process"},{"value","cs2.exe"}}},
        {"when",{{"mode",mode},{"condition",std::string(mode)=="event"?Json{{"event","event.kill"}}:Json{{"field","player.state.health"},{"op","<"},{"value",20}}}}},
        {"action",{{"type","trigger_effect"},{"lifetime",std::string(mode)=="state"?"while_true":"one_shot"},
            {"effect",{{"kind","profile_effect"},{"name",effect}}},{"compatibility",{{"duration_ms",200},{"fade_out_ms",0}}}}}};
    if(std::string(mode)!="state")rule["action"]["retrigger"]=retrigger;
    return rule;
}
int main() {
    auto path=std::filesystem::temp_directory_path()/("simulation-"+std::to_string(GetCurrentProcessId())+".json");
    try {
        Json config={{"default_profile","base"},{"profiles",{{"base",{{"type","static"},{"color",{10,0,0}}}},
            {"red",{{"type","static"},{"color",{50,0,0}}}},{"white",{{"type","static"},{"color",{200,200,200}}}}}},
            {"orchestration",{{"rules",Json::array({EffectRule("health","state","red"),EffectRule("kill","event","white","stack")})}}}};
        auto rise=EffectRule("process-rise","rising","white");rise.erase("scope");rise["when"]["condition"]={{"field","process"},{"value","cs2.exe"}};
        config["orchestration"]["rules"].push_back(rise);
        std::ofstream(path)<<config.dump();RuleEngine rules;Check(rules.LoadConfig(path.string()),"load");
        GsiAdapter adapter;adapter.SetInstanceId("test-instance");EffectEngine effect;Keymap keymap;FrameBuffer frame;
        auto tick=[&](uint64_t now) {
            auto evaluation=adapter.EvaluateAutomation(rules,"desktop.exe",now);
            const auto freshness=adapter.SimulationStatus()["freshness"];
            Check(freshness["fresh"]==evaluation.automation_fresh && freshness["threshold_ms"]==evaluation.freshness_threshold_ms && freshness["evaluated_at_ms"]==now,"status projects exact Automation evaluation");
            Check(freshness["age_ms"]==(evaluation.telemetry_age_ms?Json(*evaluation.telemetry_age_ms):Json(nullptr)),"status age matches Automation snapshot including absent telemetry");
            if(evaluation.source_changed)effect.GetAutomationEffects().Clear();
            effect.ReconcileProfile(evaluation.profile);
            effect.GetAutomationEffects().Consume(evaluation,[&](const Json& reference){return ResolveAutomationEffect(reference,rules);},now,effect.GetAutomationEffects().Revision(),[&](const Json& reference){return PrepareAutomationEffect(reference,rules);});
            effect.TickAt(now,frame,keymap,&adapter.GetState());
            size_t shots=0;for(const auto& d:evaluation.decisions)shots+=d.admitted;
            return shots;
        };
        tick(1);adapter.QueueSimulation({{"enabled",true},{"bomb","planted"},{"round_kills",10}});
        Check(tick(10)==0 && adapter.GetState().GetRecentEvents().empty(),"source entry seeds all detector histories and process rising");
        Check(adapter.SimulationStatus()["foreground_process"]=="cs2.exe","simulated scope visible");
        Check(adapter.CurrentStatus()["source"]=="simulation" && adapter.CurrentStatus()["instance_id"]=="test-instance", "current telemetry labels source and instance atomically");
        adapter.QueueSimulation({{"health",10}});Check(tick(20)==0 && frame.buffer[0]==50,"health drives real persistent composition");
        adapter.AcceptLivePayload({{"player",{{"state",{{"health",100},{"round_kills",999}}}}}});
        Check(adapter.GetState().GetNumber("player.state.health")==10,"live state excluded");
        adapter.QueueSimulation({{"increment_kill",true}});Check(tick(30)==1 && frame.buffer[0]==200,"one increment one detector admission through transient composition");
        adapter.QueueSimulation({{"increment_kill",true}});adapter.QueueSimulation({{"increment_kill",true}});
        Check(tick(40)==1 && tick(50)==1,"queued increments remain separate occurrences");
        Check(effect.GetAutomationEffects().ActiveCount()==4,"three stacked transients plus persistent");
        Check(tick(1000)==0,"identical heartbeat never emits kill");
        adapter.QueueSimulation({{"heartbeat",false}});tick(1010);
        tick(3050);Check(adapter.SimulationStatus()["freshness"]["fresh"]==true && adapter.SimulationStatus()["freshness"]["age_ms"]==3000,"threshold boundary is Fresh");
        tick(3051);Check(adapter.SimulationStatus()["freshness"]["fresh"]==false && adapter.SimulationStatus()["freshness"]["age_ms"]==3001,"paused heartbeat becomes Stale one ms beyond threshold");
        tick(4011);
        Check(effect.GetAutomationEffects().ActiveCount()==0 && frame.buffer[0]==10,"paused heartbeat causes real stale cancellation");
        adapter.QueueSimulation({{"heartbeat",true}});Check(tick(4020)==0 && frame.buffer[0]==50,"recovery restores state without event replay");
        adapter.QueueSimulation({{"foreground_process","desktop.exe"}});tick(4030);Check(frame.buffer[0]==10,"simulated scope cancellation");
        adapter.QueueSimulation({{"enabled",false}});Check(tick(4040)==0,"exit has no synthetic rising");
        Check(adapter.CurrentStatus()["source"]=="real" && !adapter.CurrentStatus()["data"].contains("player.state.health") && adapter.CurrentStatus()["events"].empty() && adapter.CurrentStatus()["last_updated_sec"] == -1, "REAL starts without stale simulation fields");
        adapter.AcceptLivePayload({{"player",{{"state",{{"health",80},{"round_kills",1000}}}}},{"round",{{"bomb","exploded"},{"phase","over"}}}});
        auto real=adapter.GetState().GetAutomationTelemetry();
        Check(tick(real->received_at_ms)==0 && adapter.GetState().GetRecentEvents().empty(),"next real payload seeds all history without replay");
        // Queue policy uses the same detector/runtime path.
        config["orchestration"]["rules"][1]["action"]["retrigger"]="queue";
        config["orchestration"]["automation_freshness_ms"]=600;
        std::ofstream(path)<<config.dump();Check(rules.LoadConfig(path.string()),"queue config");
        adapter.QueueSimulation({{"enabled",true}});tick(5000);Check(adapter.SimulationStatus()["freshness"]["threshold_ms"]==600 && adapter.SimulationStatus()["heartbeat_ms"]==200,"reloaded threshold and heartbeat are authoritative");
        adapter.QueueSimulation({{"increment_kill",true}});tick(5010);
        adapter.QueueSimulation({{"increment_kill",true}});tick(5020);
        Check(effect.GetAutomationEffects().PendingCount()==1,"simulated event queues");
        adapter.QueueSimulation({{"enabled",false}});tick(5030);
        Check(effect.GetAutomationEffects().PendingCount()==0 && effect.GetAutomationEffects().ActiveCount()==0,"source exit clears active and queued effects");
        bool rejected=false;try{adapter.QueueSimulation({{"event.kill",true}});}catch(...){rejected=true;}Check(rejected,"direct synthetic event rejected");
        std::filesystem::remove(path);std::cout<<"PASS real GSI simulation and composition\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<"\n";std::filesystem::remove(path);return 1;}
}
