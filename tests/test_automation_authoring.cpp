#include "config/automation_service.h"
#include "config/rule_engine.h"
#include <fstream>
#include <iostream>
#include <thread>
using Json=nlohmann::json;
namespace fs=std::filesystem;
using Service=aura::AutomationControlService;
int checks=0;
void Check(bool ok,const char* message) { ++checks; if(!ok) throw std::runtime_error(message); }
Json Rule(std::string id="v2",std::string process="other.exe",std::string profile="base") {
    return {{"model","automation_v2"},{"id",id},{"when",{{"mode","state"},{"condition",{{"field","process"},{"op","=="},{"value",process}}}}},
        {"action",{{"type","activate_profile"},{"profile",profile}}},{"unknown_rule",{{"keep",7}}}};
}
Json Config() {
    return {{"default_profile","base"},{"profiles",{{"base",{{"type","static"}}},{"other",{{"type","static"}}}}},
        {"unknown_root",{{"keep",Json::array({1,"two",nullptr})}}},
        {"orchestration",{{"unknown_metadata",42},{"rules",Json::array({Rule("existing")})}}}};
}
struct Harness {
    fs::path path; Service service;
    Harness(fs::path p):path(std::move(p)),service(path) { Write(Config()); }
    void Write(const Json& config) { std::ofstream(path,std::ios::binary)<<config.dump(2); }
    std::string Raw() {std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
    Json Read() {return Json::parse(Raw());}
    std::string Revision() {return aura::ComputeFileRevision(Raw());}
    Service::AuthoringResult Call(const std::string& op,Json request=Json::object()) {request["expected_revision"]=Revision();return service.Author(op,request);}
};
void Crud(Harness& h) {
    const auto before=h.Raw();const auto listed=h.Call("list");const auto& records=listed.body["records"];
    Check(listed.http_status==200 && records.size()==1 && records[0]["id"]=="existing","ordered V2 list");
    Check(before==h.Raw(),"list does not rewrite bytes");
    auto proposed=Rule();auto valid=h.Call("validate",{{"rule",proposed},{"position",0}});
    Check(valid.http_status==200 && valid.body["valid"]==true && h.Raw()==before,"validation is side effect free");
    auto created=h.Call("create",{{"rule",proposed},{"position",0}});Check(created.http_status==201,"create v2");
    Check(h.Read()["orchestration"]["rules"][0]==proposed,"explicit insertion order");
    auto view=h.Call("list").body["records"];
    Check(view[0]["id"]=="v2" && view[0]["position"]==0 && view[1]["id"]=="existing","V2 identity and config order");
    Check(h.service.Author("create",{{"rule",Rule("no-revision")}}).http_status==428,"all mutation requires explicit revision");
    aura::RuleEngine runtime;Check(runtime.LoadConfig(h.path.string()),"authoring result accepted by runtime");
    auto stale=h.service.Author("delete",{{"expected_revision",aura::ComputeFileRevision(before)},{"id","v2"}});
    Check(stale.http_status==409 && stale.body["current_revision"]==h.Revision(),"stale revision rejected");
    auto dup=h.Call("create",{{"rule",proposed}});Check(dup.http_status==422 && dup.body["error"]=="duplicate_id","duplicate id");
    auto original=h.Read();auto updated=h.Call("update",{{"id","v2"},{"patch",{{"description","cosmetic"},{"action",{{"profile","other"}}}}},{"position",1}});
    Check(updated.http_status==200,"sparse update by id and reorder");
    auto next=h.Read();Check(next["orchestration"]["rules"][1]["unknown_rule"]==proposed["unknown_rule"],"unknown rule data preserved");
    Check(next["unknown_root"]==original["unknown_root"] && next["orchestration"]["unknown_metadata"]==42 && next["orchestration"]["rules"][0]==original["orchestration"]["rules"][1],"unrelated configuration unchanged");
    const auto stable=h.Raw();
    for(const auto& patch:Json::array({{{"action",{{"profile","missing"}}}},{{"when",{{"mode","event"}}}},{{"id","changed"}}}))
        Check(h.Call("update",{{"id","v2"},{"patch",patch}}).http_status==422 && h.Raw()==stable,"invalid update publishes nothing");
    auto invalid=Rule("bad");invalid["when"]["condition"]={{"not",Json::object()}};
    Check(h.Call("create",{{"rule",invalid}}).http_status==422,"invalid AST rejected");
    auto shot=Rule("shot");shot["when"]={{"mode","event"},{"condition",{{"event","event.kill"}}}};
    shot["action"]={{"type","trigger_effect"},{"lifetime","one_shot"},{"effect",{{"kind","profile_effect"},{"name","base"}}},{"retrigger","unknown_policy"}};
    Check(h.Call("create",{{"rule",shot}}).http_status==422,"unsupported retrigger rejected");
    for(const auto* policy:{"stack","queue"}) {
        shot["action"]["retrigger"]=policy;
        Check(h.Call("validate",{{"rule",shot}}).http_status==200,"advanced one-shot policy accepted");
        auto created=h.Call("create",{{"rule",shot}});Check(created.http_status==201 && created.body["rule"]["action"]["retrigger"]==policy,"advanced policy authoring round trip");
        Check(h.Call("delete",{{"id","shot"}}).http_status==200,"advanced rule delete round trip");
        auto sized=shot;sized["action"]["pending_ttl_ms"]=999;
        Check(h.Call("validate",{{"rule",sized}}).http_status==422,"advanced limits cannot be configured");
        auto persistent=shot;persistent["when"]={{"mode","state"},{"condition",{{"field","process"},{"value","other"}}}};persistent["action"]["lifetime"]="while_true";
        Check(h.Call("validate",{{"rule",persistent}}).http_status==422,"advanced policy invalid for persistent");
    }
    shot["action"]["retrigger"]="restart";shot["action"]["effect"]["name"]="missing";
    auto missing=h.Call("create",{{"rule",shot}});Check(missing.http_status==422 && missing.body["errors"][0]["path"]=="/rule/action/effect/name","reference diagnostic path");
    Check(h.Call("delete",{{"id","v2"}}).http_status==200 && h.Read()["orchestration"]["rules"].size()==1,"delete stable id preserves other V2 records");
    h.service.SetFileReplacerForTesting([](const fs::path&,const fs::path&){return false;});
    const auto failed=h.Raw();Check(h.Call("create",{{"rule",proposed}}).http_status==500 && h.Raw()==failed,"failed atomic replacement keeps bytes");
    h.service.SetFileReplacerForTesting({});
    auto unsupported=Config();unsupported["rules"].push_back(nullptr);h.Write(unsupported);auto untouched=h.Raw();
    Check(h.Call("list").http_status==422 && untouched==h.Raw(),"unsupported records rejected without rewriting");
    auto ambiguous=Config();ambiguous["orchestration"]["rules"]={Rule("duplicate"),Rule("duplicate")};h.Write(ambiguous);untouched=h.Raw();
    Check(h.Call("delete",{{"id","duplicate"}}).http_status==422 && h.Raw()==untouched,"ambiguous on-disk ID is never resolved by array position");
}
void AtomicAndCatalog(Harness& h) {
    h.Write(Config());
    const auto before=h.Raw();
    const auto catalog=h.Call("effects").body["effects"];
    Check(catalog.size()==2 && h.Raw()==before,"catalog is read-only projection");
    Check(catalog[0]["lifecycle"]["kind"]=="legacy_envelope" && catalog[1]["lifecycle"]["kind"]=="legacy_envelope",
          "profiles without native lifecycle expose compatibility envelope");
    const auto caps=h.service.AuthoringCapabilities();
    Check(caps["event_positive_witness_required"]==true,"event rules retain positive occurrence witness");
    Check(caps["event_metadata"].size()==caps["events"].size(),"every canonical event has authoring metadata");
    bool ace=false,kill=false;
    for (const auto& event:caps["event_metadata"]) {
        if (event["id"]=="event.ace") ace=event["label"].get<std::string>().find("推定")!=std::string::npos && event["description"].get<std::string>().find("不是 CS2")!=std::string::npos;
        if (event["id"]=="event.kill") kill=event["label"]=="击杀" && event["category"]=="战斗";
    }
    Check(ace && kill,"Chinese event labels and inferred ACE disclosure");
    bool health=false,process=false,phase=false;
    const Json numeric_operators={"==","!=","<","<=",">",">="};
    const Json boolean_operators={"==","!="};
    const Json string_operators={"==","!=","contains"};
    for (const auto& field:caps["field_metadata"]) {
        if (field["key"]=="player.state.health") health=field["type"]=="number";
        const auto& operators=field.at("operators");
        Check(operators.is_array() && !operators.empty(),"every curated field declares operators");
        if (field["key"]=="process.name") process=operators==boolean_operators;
        else if (field["type"]=="number") Check(operators==numeric_operators,"numeric field operator contract");
        else if (field["type"]=="bool") Check(operators==boolean_operators,"boolean field operator contract");
        else if (field["type"]=="string") Check(operators==string_operators,"string field operator contract");
        else Check(false,"curated field has a supported type");
        if (field["key"]=="round.phase") phase=field["values"]["freezetime"]=="冻结时间";
    }
    Check(health && process && phase,"field metadata retains canonical keys and typed values");
    h.service.SetFileReplacerForTesting([](const fs::path&,const fs::path&){return false;});
    Check(h.Call("replace",{{"rules",Json::array({Rule("replacement")})}}).http_status==500 && h.Raw()==before,"failed transaction retains whole prior config");
    h.service.SetFileReplacerForTesting({});
    Check(h.Call("replace",{{"rules",Json::array({Rule("second"),Rule("first")})}}).http_status==200,"atomic replace");
    Check(h.Read()["orchestration"]["rules"][0]["id"]=="second","config order retained");
    const auto stable=h.Raw();
    Check(h.Call("replace",{{"rules",Json::array({Rule("duplicate"),Rule("duplicate")})}}).http_status==422 && h.Raw()==stable,"duplicate transaction publishes nothing");
    auto shot=Rule("unpublished");shot["action"]={{"type","trigger_effect"},{"lifetime","while_true"},{"effect",{{"kind","plugin"},{"name","missing-plugin"}}}};
    Check(h.Call("create",{{"rule",shot}}).http_status==422 && h.Raw()==stable,"unavailable plugin cannot silently save");
    auto retired=Config();retired["rules"]=Json::array({{{"process","cs2.exe"},{"profile","base"}}});h.Write(retired);
    Check(h.Call("list").http_status==422,"authoring rejects retired configuration");
}
void EventWitnessAuthority(Harness& h) {
    h.Write(Config());const auto before=h.Raw();
    auto shot=Rule("witness");shot["when"]["mode"]="event";
    shot["action"]={{"type","trigger_effect"},{"lifetime","one_shot"},{"effect",{{"kind","profile_effect"},{"name","base"}}}};
    const Json kill={{"event","event.kill"}},health={{"field","player.state.health"},{"op","<"},{"value",20}};
    for(const auto& condition:Json::array({health,Json{{"not",kill}}})) {
        shot["when"]["condition"]=condition;
        auto result=h.Call("validate",{{"rule",shot}});
        Check(result.http_status==422 && result.body["message"].get<std::string>().find("positive occurrence")!=std::string::npos,"authoring validation explains missing witness");
        Check(h.Call("replace",{{"rules",Json::array({shot})}}).http_status==422 && h.Raw()==before,"authoring cannot persist unwitnessable event");
    }
    for(const auto& condition:Json::array({kill,Json{{"and",Json::array({kill,health})}},Json{{"or",Json::array({kill,health})}}})) {
        shot["when"]["condition"]=condition;
        Check(h.Call("validate",{{"rule",shot}}).http_status==200,"authoring accepts event witness including AND and OR");
    }
}
void Concurrent(Harness& h) {
    h.Write(Config());const auto revision=h.Revision();Service other(h.path);Service::AuthoringResult a,b;
    std::thread first([&]{a=h.service.Author("create",{{"expected_revision",revision},{"rule",Rule("first")}});});
    std::thread second([&]{b=other.Author("create",{{"expected_revision",revision},{"rule",Rule("second")}});});first.join();second.join();
    Check((a.http_status==201 && b.http_status==409)||(b.http_status==201 && a.http_status==409),"concurrent writers cannot lose updates");
}
int main() {
    const auto path=fs::temp_directory_path()/("aura-authoring-"+std::to_string(GetCurrentProcessId())+".json");
    try {Harness h(path);Crud(h);AtomicAndCatalog(h);Concurrent(h);EventWitnessAuthority(h);fs::remove(path);std::cout<<"PASS: "<<checks<<" V2 authoring assertions\n";return 0;}
    catch(const std::exception& error){std::cerr<<"FAIL after "<<checks<<": "<<error.what()<<"\n";return 1;}
}
