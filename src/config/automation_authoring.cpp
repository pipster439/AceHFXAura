#include "config/automation_contract.h"
#include "engine/plugin_manager.h"
#include "config/automation_service.h"
#include "config/rule_engine.h"
#include "config/automation_limits.h"
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
        RuleEngine::ValidateAuthoringReferences(parsed, Json(root.value("profiles",Ordered::object())));
    } catch (const std::exception& error) {
        Fail(422,"invalid_reference",path+(parsed.action.at("type")=="activate_profile" ? "/action/profile" : "/action/effect/name"),error.what(),id);
    }
    return parsed;
}
void ValidatePlan(const Ordered& root, const Ordered& rules) {
    std::set<std::string> ids;
    for (size_t i=0; i<rules.size(); ++i) {
        const auto path="/orchestration/rules/"+std::to_string(i);
        const auto parsed=Validate(rules[i],root,path);
        if (!ids.insert(parsed.id).second) Fail(422,"duplicate_id",path+"/id","V2 IDs must be unique",parsed.id);
    }
}
}

nlohmann::json AutomationControlService::AuthoringCapabilities() {
    return {{"api_version",2},{"model","automation_v2"},{"stable_identity","id"},{"revision_field","expected_revision"},
        {"pairings",Json::array({{{"mode","state"},{"action","activate_profile"}},{{"mode","state"},{"action","trigger_effect"},{"lifetime","while_true"}},
            {{"mode","rising"},{"action","trigger_effect"},{"lifetime","one_shot"}},{{"mode","event"},{"action","trigger_effect"},{"lifetime","one_shot"}}})},
        {"retrigger",{"restart","ignore_while_active","stack","queue"}},{"effect_kinds",{"profile_effect","plugin"}},{"events",AutomationEventNames()},
        {"stack",{{"max_active_per_rule",AutomationRetriggerLimits::stack_active}}},
        {"queue",{{"max_pending_per_rule",AutomationRetriggerLimits::pending_per_rule},{"max_pending_global",AutomationRetriggerLimits::pending_global},{"pending_ttl_ms",AutomationRetriggerLimits::pending_ttl_ms}}},
        {"global",{{"max_active_v2",AutomationRetriggerLimits::active_global}}},
        {"composition",{"overlay","replace"}},{"blend",{"alpha","additive"}},
        {"ast",{{"max_depth",32},{"max_nodes_per_rule",256},{"logic",{"and","or","not"}},{"comparison_operators",{"==","!=","<","<=",">",">=","contains"}},
            {"process_operators",{"==","!="}},{"process_fields",{"process","process.name","process_name"}},
            {"comparison_example",{{"field","player.state.health"},{"op","<"},{"value",15}}},{"event_example",{{"event","event.kill"}}},
            {"event_leaf_only_in","event.when.condition"},{"unknown_fields","GSI field absence evaluates Unknown"}}},
        {"dnd","rule-level metadata for state activate_profile only"},{"position","zero-based orchestration insertion offset after removing an updated rule; revision-bound, not identity"},
        {"scope","Optional process/state AST; event leaves are not allowed in scope"},{"event_positive_witness_required",true},
        {"watchdog_ms",{{"minimum",1},{"maximum",60000},{"applies_to","one_shot"}}},
        {"layer_priority","signed 32-bit integer for trigger_effect only; profile arbitration uses array order"},
        {"sparse_patch","JSON Merge Patch; id/model immutable"}};
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
        try { ValidateAutomationContainers(Json(root)); }
        catch (const std::exception& error) { Fail(422,"migration_required","/",error.what()); }
        if (operation=="list") {
            Json records=Json::array();
            auto& rules=Rules(root);
            for (size_t i=0;i<rules.size();++i)
                records.push_back({{"id",Id(rules[i])},{"position",i},{"record",rules[i]}});
            return {200,{{"status","ok"},{"revision",revision},{"records",records},{"profiles",root.value("profiles",Ordered::object())}}};
        }
        if (operation=="effects") {
            Json effects=Json::array();
            if(root.contains("profiles")) for(const auto& item:root["profiles"].items()) {
                const auto& recipe=item.value();
                const auto plugin=recipe.value("type",std::string{})=="plugin" ?
                    recipe.value("plugin_name",recipe.value("plugin",recipe.value("effect",recipe.value("effect_name",recipe.value("plugin_path",std::string{}))))) : std::string{};
                const bool available=recipe.value("type",std::string{})!="plugin" || (!plugin.empty() && PluginManager::Instance().GetGeneration(plugin));
                effects.push_back({{"label",item.key()},{"reference",{{"kind","profile_effect"},{"name",item.key()}}},
                    {"available",available},{"reason",available?"":"Plugin is unpublished or unavailable"}});
            }
            for(const auto& name:PluginManager::Instance().GetLoadedPluginNames())
                effects.push_back({{"label",name},{"reference",{{"kind","plugin"},{"name",name}}},{"available",true},{"reason",""}});
            return {200,{{"revision",revision},{"effects",effects}}};
        }
        if (!request.contains("expected_revision") || !request["expected_revision"].is_string() || request["expected_revision"].get<std::string>().empty())
            Fail(428,"revision_required","/expected_revision","Supply the revision returned by list");
        if (request["expected_revision"]!=revision) Fail(409,"revision_conflict","/expected_revision","Configuration changed; refresh and review again");
        Json response={{"status","ok"}};
        {
            auto& rules=Rules(root);
            if(operation=="replace") {
                if (!request.contains("rules") || !request["rules"].is_array()) Fail(422,"invalid_rules","/rules","Rules must be an array");
                rules=Ordered(request["rules"]);
            }
            else if(operation=="delete") { const auto index=Find(rules,request.value("id",std::string{})); rules.erase(rules.begin()+index); }
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
            if(!IsAllowedLoopbackHost(req.get_header_value("Host"))) {res.status=403;return;}
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
    server.Put("/api/automation/v2/rules",handler("replace",true));
    server.Get("/api/automation/v2/effects",handler("effects",false));
}
} // namespace aura
