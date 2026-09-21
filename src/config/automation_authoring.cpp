#include "config/automation_service.h"
#include "config/rule_engine.h"
#include <set>

namespace aura {
namespace {
using Json = nlohmann::json;
using Ordered = nlohmann::ordered_json;
using Result = AutomationControlService::AuthoringResult;
struct Failure { int status; std::string code, path, message, id; Json details = Json::object(); };
[[noreturn]] void Fail(int status, const std::string& code, const std::string& path,
                      const std::string& message, const std::string& id = "", Json details = Json::object()) {
    throw Failure{status, code, path, message, id, std::move(details)};
}
bool V2(const Ordered& value) { return value.is_object() && value.value("model", Json()) == "automation_v2"; }
std::string Id(const Json& value) { return value.is_object() && value.contains("id") && value["id"].is_string() ? value["id"].get<std::string>() : ""; }
Ordered& Rules(Ordered& root) {
    if (!root.contains("orchestration")) root["orchestration"] = Ordered::object();
    if (!root["orchestration"].is_object()) Fail(422,"invalid_container","/orchestration","Expected an object; repair the existing container explicitly");
    auto& orchestration = root["orchestration"];
    if (!orchestration.contains("rules")) orchestration["rules"] = Ordered::array();
    if (!orchestration["rules"].is_array()) Fail(422,"invalid_container","/orchestration/rules","Expected an array; existing content was not replaced");
    return orchestration["rules"];
}
size_t Find(const Ordered& rules, const std::string& id) {
    if (id.empty()) Fail(422,"required","/id","A stable rule ID is required");
    size_t found=rules.size();
    for (size_t i=0; i<rules.size(); ++i) if (V2(rules[i]) && Id(rules[i]) == id) {
        if (found!=rules.size()) Fail(422,"duplicate_id","/id","Existing duplicate IDs make this identity ambiguous; repair the invalid source config explicitly",id);
        found=i;
    }
    if (found!=rules.size()) return found;
    Fail(404,"rule_not_found","/id","No V2 rule with this ID",id);
}
size_t Position(const Json& request, size_t fallback, size_t size) {
    if (!request.contains("position")) return fallback;
    const auto& value = request["position"];
    if (!value.is_number_integer() || value < 0 || value > size)
        Fail(422,"invalid_position","/position","Position must be an insertion offset in the revision-bound orchestration array");
    return value.get<size_t>();
}
aura::AutomationRule Validate(const Ordered& value, const Ordered& root, const std::string& path) {
    const auto id = Id(value);
    if (!V2(value)) Fail(422,"invalid_model",path+"/model","Authoring accepts model automation_v2 only",id);
    aura::AutomationRule parsed;
    try { parsed = RuleEngine::ParseAutomationRule(Json(value)); }
    catch (const nlohmann::json::exception&) { Fail(422,"invalid_schema",path,"Required rule fields are missing or have the wrong type",id); }
    catch (const std::exception& error) { Fail(422,"invalid_schema",path,error.what(),id); }
    try {
        RuleEngine::ValidateAutomationReferences(parsed,[&](const std::string& name) {
            return root.contains("profiles") && root["profiles"].is_object() && root["profiles"].contains(name) && root["profiles"][name].is_object();
        });
    } catch (const std::exception& error) {
        Fail(422,"invalid_reference",path+(parsed.action.at("type")=="activate_profile" ? "/action/profile" : "/action/effect/name"),error.what(),id);
    }
    return parsed;
}
void ValidatePlan(const Ordered& root, const Ordered& rules) {
    std::set<std::string> ids;
    for (size_t i=0; i<rules.size(); ++i) if (V2(rules[i])) {
        const auto path="/orchestration/rules/"+std::to_string(i);
        const auto parsed=Validate(rules[i],root,path);
        if (!ids.insert(parsed.id).second) Fail(422,"duplicate_id",path+"/id","V2 IDs must be unique",parsed.id);
    }
}
Json Shadows(const aura::AutomationRule& rule, const Ordered& root) {
    Json conflicts=Json::array();
    if (rule.mode!="state" || rule.action.at("type")!="activate_profile" || rule.scope.UsesGsi() || rule.condition.UsesGsi()) return conflicts;
    if (!root.contains("rules") || !root["rules"].is_array()) return conflicts;
    for (size_t i=0; i<root["rules"].size(); ++i) {
        const auto& source=root["rules"][i];
        if (!source.is_object() || !source.contains("process") || !source["process"].is_string()) continue;
        const auto process=CanonicalizeProcessName(source["process"].get<std::string>());
        if (process.empty()) continue;
        AutomationInputSnapshot snapshot; snapshot.foreground_process=process;
        // Use the accepted condition evaluator, not a separate authoring matcher.
        if (rule.scope.Evaluate(snapshot).truth != ConditionTruth::True || rule.condition.Evaluate(snapshot).truth != ConditionTruth::True) continue;
        const bool same=source.contains("profile") && Json(source["profile"])==rule.action.at("profile");
        conflicts.push_back({{"source_index",i},{"source",source},{"process",process},
            {"category",same?"equivalent_same_profile":"same_process_different_profile"},
            {"precedence_change","orchestration_before_gsi_bindings_before_application"}});
    }
    return conflicts;
}
Json Proposal(const Ordered& root, const Json& request, const std::string& revision) {
    if (!request.contains("source_index") || !request["source_index"].is_number_integer() || request["source_index"]<0 ||
        !root.contains("rules") || !root["rules"].is_array() || request["source_index"]>=root["rules"].size())
        Fail(404,"source_not_found","/source_index","Source Application Rule is absent at this revision");
    const auto index=request["source_index"].get<size_t>();
    const auto& source=root["rules"][index];
    if (!source.is_object()) Fail(422,"conversion_blocked","/source","Source is not an Application Rule object");
    Json unmapped=Json::array();
    for (const auto& field:source.items()) if (field.key()!="process" && field.key()!="profile" && field.key()!="suppress_web_ui") unmapped.push_back(field.key());
    if (!unmapped.empty()) Fail(422,"conversion_blocked","/source","Unmapped fields must be resolved explicitly before conversion; no fields were discarded","",{{"unmapped_fields",unmapped}});
    if (!source.contains("process") || !source["process"].is_string() || !source.contains("profile") || !source["profile"].is_string() ||
        (source.contains("suppress_web_ui") && !source["suppress_web_ui"].is_boolean()))
        Fail(422,"conversion_blocked","/source","Process/profile/DND source types cannot be preserved");
    const auto raw=source["process"].get<std::string>();
    const auto canonical=CanonicalizeProcessName(raw);
    const auto lower=RuleEngine::ToLower(raw);
    // Historical id-less raw rules do not all have Phase 4's canonical spelling.
    // In particular their extensionless runtime match is asymmetric. Do not
    // broaden that behavior silently to V2's symmetric .exe equivalence.
    if (canonical.empty() || lower!=canonical)
        Fail(422,"conversion_blocked","/source/process","Noncanonical legacy process spelling cannot be losslessly promoted; explicitly normalize it through the Application Rule API first");
    Ordered candidate=root;
    auto& rules=Rules(candidate);
    const auto position=Position(request,rules.size(),rules.size());
    const auto id=request.value("id",std::string{});
    Json rule={{"id",id},{"model","automation_v2"},{"when",{{"mode","state"},{"condition",{{"field","process.name"},{"op","=="},{"value",raw}}}}},
        {"action",{{"type","activate_profile"},{"profile",source["profile"]}}},{"dnd",source.value("suppress_web_ui",false)}};
    auto parsed=Validate(Ordered(rule),candidate,"/proposed_rule");
    rules.insert(rules.begin()+position,Ordered(rule)); ValidatePlan(candidate,rules);
    return {{"expected_revision",revision},{"source_index",index},{"source",source},{"id",id},{"position",position},{"proposed_rule",rule},
        {"precedence_change",{{"from","application_after_gsi_bindings"},{"to","orchestration_in_config_order_before_gsi_bindings"}}},
        {"dnd_mapping",{{"source_present",source.contains("suppress_web_ui")},{"suppress_web_ui",source.value("suppress_web_ui",false)},{"dnd",rule["dnd"]}}},
        {"shadowing",Shadows(parsed,root)}};
}
}

nlohmann::json AutomationControlService::AuthoringCapabilities() {
    return {{"api_version",2},{"model","automation_v2"},{"stable_identity","id"},{"revision_field","expected_revision"},
        {"pairings",Json::array({{{"mode","state"},{"action","activate_profile"}},{{"mode","state"},{"action","trigger_effect"},{"lifetime","while_true"}},
            {{"mode","rising"},{"action","trigger_effect"},{"lifetime","one_shot"}},{{"mode","event"},{"action","trigger_effect"},{"lifetime","one_shot"}}})},
        {"retrigger",{"restart","ignore_while_active"}},{"effect_kinds",{"profile_effect","plugin"}},{"events",AutomationEventNames()},
        {"composition",{"overlay","replace"}},{"blend",{"alpha","additive"}},
        {"ast",{{"max_depth",32},{"max_nodes_per_rule",256},{"logic",{"and","or","not"}},{"comparison_operators",{"==","!=","<","<=",">",">=","contains"}},
            {"process_operators",{"==","!="}},{"process_fields",{"process","process.name","process_name"}},
            {"comparison_example",{{"field","player.state.health"},{"op","<"},{"value",15}}},{"event_example",{{"event","event.kill"}}},
            {"event_leaf_only_in","event.when.condition"},{"unknown_fields","GSI field absence evaluates Unknown"}}},
        {"dnd","rule-level metadata for state activate_profile only"},{"position","zero-based orchestration insertion offset after removing an updated rule; revision-bound, not identity"},
        {"scope","Optional process/state AST; event leaves are not allowed in scope"},{"event_positive_witness_required",true},
        {"watchdog_ms",{{"minimum",1},{"maximum",60000},{"applies_to","one_shot"}}},
        {"layer_priority","signed 32-bit integer for trigger_effect only; profile arbitration uses array order"},
        {"sparse_patch","JSON Merge Patch; id/model immutable"},{"shadow_acknowledgement","acknowledge_shadowing: true"},
        {"legacy_application_api","/api/automation/rules"},{"conversion_mapped_fields",{"process","profile","suppress_web_ui"}},
        {"conversion_display_metadata","No display fields mapped; all extra source fields block conversion"}};
}

AutomationControlService::AuthoringResult AutomationControlService::Author(const std::string& operation, const Json& request) {
    if (operation=="capabilities") return {200,AuthoringCapabilities()};
    std::string revision;
    try {
        if (!request.is_object()) Fail(400,"invalid_request","/","Request must be an object");
        NamedConfigLock lock(kConfigWriteMutexName,5000);
        if (!lock.IsAcquired()) Fail(503,"writer_busy","/","Configuration writer lock unavailable");
        std::string content;
        if (!ReadRawConfigFile(config_path_,content,revision)) Fail(500,"config_unavailable","/","Cannot read configuration");
        auto root=Ordered::parse(content);
        if (!root.is_object()) Fail(422,"invalid_config","/","Configuration must be an object");
        if (operation=="list") {
            Json records=Json::array();
            auto append=[&](const Ordered& array,const char* path,const char* provenance,int tier) {
                if (!array.is_array()) { records.push_back({{"provenance","unsupported_container"},{"path",path},{"record",array},{"read_only",true}}); return; }
                for (size_t i=0;i<array.size();++i) {
                    const bool v2=std::string(path)=="/orchestration/rules" && V2(array[i]);
                    const bool application=std::string(provenance)=="application" && array[i].is_object() &&
                        array[i].contains("process") && array[i]["process"].is_string() && array[i].contains("profile") && array[i]["profile"].is_string();
                    const bool effect=v2 && array[i].contains("action") && array[i]["action"].is_object() && array[i]["action"].value("type",Json())=="trigger_effect";
                    Json row={{"provenance",v2?"automation_v2":std::string(provenance)=="application"&&!application?"unsupported_application":provenance},
                        {"path",std::string(path)+"/"+std::to_string(i)},{"position",i},{"profile_precedence_tier",tier==0||effect?Json(nullptr):Json(tier)},{"record",array[i]},
                        {"edit_api",v2?"/api/automation/v2/rules":application?"/api/automation/rules":""}};
                    if (v2) row["id"]=Id(array[i]);
                    records.push_back(std::move(row));
                }
            };
            if (root.contains("orchestration") && root["orchestration"].is_object()) {
                const auto& o=root["orchestration"];
                if(o.contains("rules")) append(o["rules"],"/orchestration/rules","legacy_orchestration",1);
                if(o.contains("event_overlays")) append(o["event_overlays"],"/orchestration/event_overlays","legacy_event_overlay",0);
            } else if(root.contains("orchestration")) append(root["orchestration"],"/orchestration","unsupported_container",0);
            if(root.contains("gsi_bindings")) append(root["gsi_bindings"],"/gsi_bindings","legacy_gsi_binding",2);
            if(root.contains("rules")) append(root["rules"],"/rules","application",3);
            return {200,{{"status","ok"},{"revision",revision},{"records",records},{"profiles",root.value("profiles",Ordered::object())},
                {"precedence","Profile arbitration: orchestration array order, legacy GSI bindings, Application Rules, fallback; event overlays do not arbitrate profiles"}}};
        }
        if (!request.contains("expected_revision") || !request["expected_revision"].is_string() || request["expected_revision"].get<std::string>().empty())
            Fail(428,"revision_required","/expected_revision","Supply the revision returned by list");
        if (request["expected_revision"]!=revision) Fail(409,"revision_conflict","/expected_revision","Configuration changed; refresh and review again");
        if(operation=="propose") return {200,{{"status","ok"},{"revision",revision},{"proposal",Proposal(root,request,revision)}}};
        Json response={{"status","ok"}};
        if(operation=="promote") {
            if(request.value("confirm",Json())!=true) Fail(422,"confirmation_required","/confirm","Explicit proposal confirmation is required");
            const auto& proposal=request.at("proposal");
            if(proposal.at("expected_revision")!=revision) Fail(409,"revision_conflict","/proposal/expected_revision","Proposal was prepared against another revision");
            const auto verified=Proposal(root,proposal,revision);
            if(verified!=proposal) Fail(409,"proposal_changed","/proposal","Source or proposal changed; prepare and review a fresh proposal");
            auto& rules=Rules(root); const auto position=verified.at("position").get<size_t>();
            rules.insert(rules.begin()+position,Ordered(verified.at("proposed_rule")));
            root["rules"].erase(root["rules"].begin()+verified.at("source_index").get<size_t>());
            response["rule"]=verified.at("proposed_rule"); response["position"]=position;
        } else {
            auto& rules=Rules(root);
            if(operation=="delete") { const auto index=Find(rules,request.value("id",std::string{})); rules.erase(rules.begin()+index); }
            else if(operation=="create" || operation=="update" || operation=="validate") {
                const bool updating=operation=="update" || (operation=="validate" && request.contains("patch"));
                Ordered rule; size_t position=rules.size();
                if(updating) {
                    const auto id=request.value("id",std::string{}); position=Find(rules,id); rule=rules[position];
                    const auto& patch=request.at("patch");
                    if(!patch.is_object()) Fail(422,"invalid_patch","/patch","Sparse patch must be an object",id);
                    rule.merge_patch(Ordered(patch));
                    if(Id(rule)!=id || !V2(rule)) Fail(422,"immutable_identity","/patch/id","ID and model cannot be changed by a sparse update",id);
                    rules.erase(rules.begin()+position);
                } else rule=Ordered(request.at("rule"));
                const auto parsed=Validate(rule,root,"/rule");
                position=Position(request,position,rules.size());
                rules.insert(rules.begin()+position,rule); ValidatePlan(root,rules);
                const auto conflicts=Shadows(parsed,root);
                response["shadowing"]=conflicts;
                if(!conflicts.empty() && request.value("acknowledge_shadowing",Json())!=true)
                    Fail(409,"shadow_acknowledgement_required","/acknowledge_shadowing","Review the higher-precedence rule and explicitly acknowledge shadowing",parsed.id,{{"shadowing",conflicts}});
                response["rule"]=rule; response["position"]=position;
            } else Fail(404,"unknown_operation","/","Unknown authoring operation");
            ValidatePlan(root,rules);
        }
        if(operation=="validate") { response["valid"]=true; response["revision"]=revision; return {200,response}; }
        const auto formatted=root.dump(2);
        if(!AtomicWriteConfigFile(config_path_,formatted,file_replacer_)) Fail(500,"atomic_write_failed","/","Previous config retained; atomic replacement failed");
        response["revision"]=ComputeFileRevision(formatted);
        return {operation=="create"?201:200,response};
    } catch(const Failure& failure) {
        Json error={{"path",failure.path},{"rule_id",failure.id},{"code",failure.code},{"message",failure.message}};
        Json body={{"status","error"},{"error",failure.code},{"message",failure.message},{"errors",Json::array({error})},{"current_revision",revision}};
        body.update(failure.details); return {failure.status,body};
    } catch(const nlohmann::json::exception&) {
        return {422,{{"status","error"},{"error","invalid_request"},{"current_revision",revision},
            {"errors",Json::array({{{"path","/"},{"rule_id",""},{"code","invalid_request"},{"message","Required fields are missing or have the wrong type"}}})}}};
    } catch(const std::exception&) {
        return {500,{{"status","error"},{"error","authoring_failed"},{"current_revision",revision},
            {"errors",Json::array({{{"path","/"},{"rule_id",""},{"code","authoring_failed"},{"message","Authoring failed; configuration was not published"}}})}}};
    }
}

void AutomationControlService::RegisterAuthoringRoutes(httplib::Server& server) {
    auto handler=[this](std::string operation,bool write) {
        return [this,operation,write](const httplib::Request& req,httplib::Response& res) {
            if(write && !ValidateWriteRequestSecurity(req,res)) return;
            Json input=Json::object();
            if(write) {
                input=Json::parse(req.body,nullptr,false);
                if(input.is_discarded()) { res.status=400; res.set_content(R"({"status":"error","errors":[{"path":"/","rule_id":"","code":"invalid_json","message":"Request is not JSON"}]})","application/json"); return; }
            }
            const auto result=Author(operation,input); res.status=result.http_status;
            res.set_header("Cache-Control","no-store"); res.set_content(result.body.dump(),"application/json; charset=utf-8");
        };
    };
    server.Get("/api/automation/v2/capabilities",handler("capabilities",false));
    server.Get("/api/automation/v2/records",handler("list",false));
    server.Post("/api/automation/v2/rules",handler("create",true));
    server.Patch("/api/automation/v2/rules",handler("update",true));
    server.Delete("/api/automation/v2/rules",handler("delete",true));
    server.Post("/api/automation/v2/validate",handler("validate",true));
    server.Post("/api/automation/v2/promotions/propose",handler("propose",true));
    server.Post("/api/automation/v2/promotions/commit",handler("promote",true));
}
} // namespace aura
