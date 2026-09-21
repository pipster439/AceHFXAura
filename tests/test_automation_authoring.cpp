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
        {"rules",Json::array({{{"process","CS2.EXE"},{"profile","base"},{"suppress_web_ui",true}},{{"process","code.exe"},{"profile","other"}}})},
        {"gsi_bindings",Json::array()}, {"unknown_root",{{"keep",Json::array({1,"two",nullptr})}}},
        {"orchestration",{{"unknown_metadata",42},{"rules",Json::array({{{"process","legacy.exe"},{"target_profile","base"},{"condition",Json::object()},{"unknown_legacy",true}}})},
            {"event_overlays",Json::array({{{"event","event.kill"},{"effect","base"},{"duration_ms",123}}})}}}};
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
    Check(listed.http_status==200 && records[0]["provenance"]=="legacy_orchestration" && records[1]["provenance"]=="legacy_event_overlay" && records[2]["provenance"]=="application","ordered provenance list");
    Check(before==h.Raw(),"list does not rewrite bytes");
    auto proposed=Rule();auto valid=h.Call("validate",{{"rule",proposed},{"position",0}});
    Check(valid.http_status==200 && valid.body["valid"]==true && h.Raw()==before,"validation is side effect free");
    auto created=h.Call("create",{{"rule",proposed},{"position",0}});Check(created.http_status==201,"create v2");
    Check(h.Read()["orchestration"]["rules"][0]==proposed,"explicit insertion order");
    auto view=h.Call("list").body["records"];
    Check(view[0]["provenance"]=="automation_v2" && view[0]["id"]=="v2" && view[0]["position"]==0 && view[1]["provenance"]=="legacy_orchestration","mixed view preserves V2 identity and config order");
    Check(h.service.Author("create",{{"rule",Rule("no-revision")}}).http_status==428,"all mutation requires explicit revision");
    aura::RuleEngine runtime;Check(runtime.LoadConfig(h.path.string()),"authoring result accepted by runtime");
    auto stale=h.service.Author("delete",{{"expected_revision",aura::ComputeFileRevision(before)},{"id","v2"}});
    Check(stale.http_status==409 && stale.body["current_revision"]==h.Revision(),"stale revision rejected");
    auto dup=h.Call("create",{{"rule",proposed}});Check(dup.http_status==422 && dup.body["error"]=="duplicate_id","duplicate id");
    auto original=h.Read();auto updated=h.Call("update",{{"id","v2"},{"patch",{{"description","cosmetic"},{"action",{{"profile","other"}}}}},{"position",1}});
    Check(updated.http_status==200,"sparse update by id and reorder");
    auto next=h.Read();Check(next["orchestration"]["rules"][1]["unknown_rule"]==proposed["unknown_rule"],"unknown rule data preserved");
    Check(next["rules"]==original["rules"] && next["unknown_root"]==original["unknown_root"] && next["orchestration"]["unknown_metadata"]==42 && next["orchestration"]["rules"][0]==original["orchestration"]["rules"][1],"unrelated and legacy data unchanged");
    const auto stable=h.Raw();
    for(const auto& patch:Json::array({{{"action",{{"profile","missing"}}}},{{"when",{{"mode","event"}}}},{{"id","changed"}}}))
        Check(h.Call("update",{{"id","v2"},{"patch",patch}}).http_status==422 && h.Raw()==stable,"invalid update publishes nothing");
    auto invalid=Rule("bad");invalid["when"]["condition"]={{"not",Json::object()}};
    Check(h.Call("create",{{"rule",invalid}}).http_status==422,"invalid AST rejected");
    auto shot=Rule("shot");shot["when"]={{"mode","event"},{"condition",{{"event","event.kill"}}}};
    shot["action"]={{"type","trigger_effect"},{"lifetime","one_shot"},{"effect",{{"kind","profile_effect"},{"name","base"}}},{"retrigger","queue"}};
    Check(h.Call("create",{{"rule",shot}}).http_status==422,"unsupported retrigger rejected");
    shot["action"]["retrigger"]="restart";shot["action"]["effect"]["name"]="missing";
    auto missing=h.Call("create",{{"rule",shot}});Check(missing.http_status==422 && missing.body["errors"][0]["path"]=="/rule/action/effect/name","reference diagnostic path");
    Check(h.Call("delete",{{"id","v2"}}).http_status==200 && h.Read()["orchestration"]["rules"].size()==1,"delete stable id preserves legacy");
    h.service.SetFileReplacerForTesting([](const fs::path&,const fs::path&){return false;});
    const auto failed=h.Raw();Check(h.Call("create",{{"rule",proposed}}).http_status==500 && h.Raw()==failed,"failed atomic replacement keeps bytes");
    h.service.SetFileReplacerForTesting({});
    auto unsupported=Config();unsupported["rules"].push_back(nullptr);h.Write(unsupported);auto untouched=h.Raw();
    auto rows=h.Call("list").body["records"];
    Check(rows.back()["provenance"]=="unsupported_application" && rows.back()["record"].is_null() && untouched==h.Raw(),"unsupported records remain visible without rewriting");
    auto ambiguous=Config();ambiguous["orchestration"]["rules"]={Rule("duplicate"),Rule("duplicate")};h.Write(ambiguous);untouched=h.Raw();
    Check(h.Call("delete",{{"id","duplicate"}}).http_status==422 && h.Raw()==untouched,"ambiguous on-disk ID is never resolved by array position");
}
void Shadow(Harness& h) {
    h.Write(Config());const auto before=h.Raw();
    for(const auto& process:{"cs2","cS2.eXe"}) {
        auto same=h.Call("create",{{"rule",Rule("shadow",process)}});
        Check(same.http_status==409 && same.body["shadowing"][0]["category"]=="equivalent_same_profile","same profile normalized shadow");
    }
    auto other=h.Call("validate",{{"rule",Rule("shadow","cs2","other")}});
    Check(other.http_status==409 && other.body["shadowing"][0]["category"]=="same_process_different_profile" && h.Raw()==before,"different profile shadow validation without publication");
    Check(h.Call("create",{{"rule",Rule("shadow","cs2","other")},{"acknowledge_shadowing",true}}).http_status==201,"explicit shadow acknowledgement allows write");
    auto held=h.Raw();Check(h.Call("update",{{"id","shadow"},{"patch",{{"description","edit"}}}}).http_status==409 && h.Raw()==held,"edit also requires acknowledgement");
    auto composite=Rule("composite");composite["when"]["condition"]={{"or",Json::array({{{"field","process.name"},{"value","CS2"}},{{"field","process"},{"value","other"}}})}};
    Check(h.Call("create",{{"rule",composite}}).http_status==409,"process-only compound AST uses runtime matcher");
}
void Promotion(Harness& h) {
    auto noncanonical=Config();noncanonical["rules"][0]["process"]="cs2";h.Write(noncanonical);const auto raw=h.Raw();
    Check(h.Call("propose",{{"source_index",0},{"id","p"}}).http_status==422 && h.Raw()==raw,"extensionless legacy semantics must not broaden silently");
    std::ifstream file("tests/fixtures/automation_v2_stage0/promotion_expectations.json");Json fixture;file>>fixture;
    for(const auto& item:fixture["cases"]) {
        auto config=Config();config["rules"][0]=item["input"];
        config["profiles"]["cs2_gamer"]={{"type","static"}};config["profiles"]["coding"]={{"type","static"}};h.Write(config);
        const auto before=h.Raw();auto result=h.Call("propose",{{"source_index",0},{"id","promoted"},{"position",0}});
        Check(before==h.Raw(),"proposal byte-wise read only");
        if(item.contains("expected")) {Check(result.http_status==422 && result.body["error"]=="conversion_blocked" && result.body["unmapped_fields"][0]=="custom_extra_tag","unknown field actionable refusal");continue;}
        Check(result.http_status==200,"proposal prepared");auto proposal=result.body["proposal"];
        Check(proposal["proposed_rule"]["dnd"]==item["input"].value("suppress_web_ui",false),"true/false/omitted DND mapping");
        if(item.contains("expected_rule_fields")) for(const auto& field:item["expected_rule_fields"].items()) Check(proposal["proposed_rule"][field.key()]==field.value(),"captured promotion fixture mapping");
        Check(!proposal["proposed_rule"]["action"].contains("dnd"),"DND is orthogonal metadata");
        auto altered=proposal;altered["source"]["profile"]="changed";
        Check(h.Call("promote",{{"proposal",altered},{"confirm",true}}).http_status==409 && before==h.Raw(),"source mismatch no conversion");
        Check(h.Call("promote",{{"proposal",proposal}}).http_status==422 && before==h.Raw(),"explicit confirmation required");
        auto final=h.Call("promote",{{"proposal",proposal},{"confirm",true}});
        Check(final.http_status==200,"confirmed atomic promotion");const auto after=h.Read();
        Check(after["rules"].size()==1 && after["rules"][0]==config["rules"][1] && after["orchestration"]["rules"][0]==proposal["proposed_rule"],"exact source removed target inserted once");
        Check(after["unknown_root"]==config["unknown_root"] && after["orchestration"]["rules"][1]==config["orchestration"]["rules"][0],"promotion preserves unrelated content");
        auto raw=h.Raw();Check(h.Call("promote",{{"proposal",proposal},{"confirm",true}}).http_status==409 && raw==h.Raw(),"proposal cannot be replayed against new revision");
        aura::RuleEngine runtime;Check(runtime.LoadConfig(h.path.string()),"converted config runtime accepted");
    }
    h.Write(Config());auto proposal=h.Call("propose",{{"source_index",0},{"id","p"}}).body["proposal"];
    auto edited=h.Read();edited["rules"][0]["profile"]="other";h.Write(edited);const auto bytes=h.Raw();
    Check(h.service.Author("promote",{{"expected_revision",proposal["expected_revision"]},{"proposal",proposal},{"confirm",true}}).http_status==409 && bytes==h.Raw(),"actual concurrent source edit leaves new source unchanged");
    // An observer in the atomic replace seam sees complete old + candidate states.
    h.Write(Config());proposal=h.Call("propose",{{"source_index",0},{"id","p"}}).body["proposal"];
    const auto original=h.Raw();h.service.SetFileReplacerForTesting([](const fs::path&,const fs::path&){return false;});
    Check(h.Call("promote",{{"proposal",proposal},{"confirm",true}}).http_status==500 && h.Raw()==original,"failed promotion replacement commits neither half");
    h.service.SetFileReplacerForTesting([&](const fs::path& from,const fs::path& to){
        Check(h.Read()["rules"].size()==2,"old config complete until replace");std::ifstream f(from);Json candidate;f>>candidate;
        Check(candidate["rules"].size()==1 && candidate["orchestration"]["rules"].back()["id"]=="p","candidate contains both conversion halves");f.close();
        return MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    });
    Check(h.Call("promote",{{"proposal",proposal},{"confirm",true}}).http_status==200,"single atomic promotion writer");h.service.SetFileReplacerForTesting({});
}
void Concurrent(Harness& h) {
    h.Write(Config());const auto revision=h.Revision();Service other(h.path);Service::AuthoringResult a,b;
    std::thread first([&]{a=h.service.Author("create",{{"expected_revision",revision},{"rule",Rule("first")}});});
    std::thread second([&]{b=other.Author("create",{{"expected_revision",revision},{"rule",Rule("second")}});});first.join();second.join();
    Check((a.http_status==201 && b.http_status==409)||(b.http_status==201 && a.http_status==409),"concurrent writers cannot lose updates");
}
int main() {
    const auto path=fs::temp_directory_path()/("aura-authoring-"+std::to_string(GetCurrentProcessId())+".json");
    try {Harness h(path);Crud(h);Shadow(h);Promotion(h);Concurrent(h);fs::remove(path);std::cout<<"PASS: "<<checks<<" Stage 5A authoring assertions\n";return 0;}
    catch(const std::exception& error){std::cerr<<"FAIL after "<<checks<<": "<<error.what()<<"\n";return 1;}
}
