#include "engine/program_catalog.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <atomic>
#include <iomanip>
#include <sstream>
using namespace aura;using Json=nlohmann::json;namespace fs=std::filesystem;
namespace {
unsigned checks=0;void Check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
std::string Id(unsigned n){std::ostringstream out;out<<std::hex<<std::setw(32)<<std::setfill('0')<<n;return out.str();}
Json Program(unsigned red){return {{"schema_version",1},{"entry",0},{"variables",0},{"slots",0},{"publication",{{"mode","continuous"},{"fade_out_ms",0}}},
 {"nodes",Json::array({{{"kind","action"},{"op","fill"},{"color",{{"op","rgb"},{"args",Json::array({{{"op","number"},{"value",red}},{{"op","number"},{"value",0}},{{"op","number"},{"value",0}}})}}},{"next",1}},{{"kind","end"}}})}};}
std::string Read(const fs::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void Write(const fs::path& path,const std::string& text){fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out<<text;}
struct Pump {
 ProgramCatalog& catalog;std::atomic<bool> stop{false};std::thread thread;
 explicit Pump(ProgramCatalog& c):catalog(c),thread([this]{while(!stop.load()){catalog.ApplyAtFrameBoundary();std::this_thread::yield();}}){}
 ~Pump(){stop=true;thread.join();}
};
unsigned Color(const TriggeredEffectInstance& effect){Check(bool(effect),"expected pinned instance");FrameBuffer frame;Keymap keys;effect.GetEffect()->Render(0,frame,keys);return frame.buffer[0];}
struct Harness {
 fs::path root;PluginManager manager;ProgramCatalog catalog;unsigned operation{1};
 explicit Harness(fs::path path):root(std::move(path)),catalog(root,manager){}
 Json Request(const char* operation_name,const std::string& id=Id(99)){return {{"operation",operation_name},{"operation_id",Id(operation++)},{"expected_revision",catalog.List().body["revision"].get<uint64_t>()},{"id",id}};}
 Json Install(unsigned red=20,const std::string& id=Id(99)){auto request=Request("install",id);request["name"]="Fixture";request["version"]="1.0";request["program"]=Program(red);return request;}
 ProgramCatalog::Result Apply(const Json& request){Pump pump(catalog);return catalog.Execute(request);}
};
void Lifecycle(const fs::path& root){
 Harness h(root);const auto id=Id(99),alias="ir_"+id;auto install=h.Install();
 install["source"]={{"name","fixture"},{"json",Json::object()},{"publication",install["program"]["publication"]}};
 auto installed=h.Apply(install);Check(installed.status==200&&installed.body["enabled"]==false&&!h.manager.HasPlugin(alias),"install only stages source data; explicit enable required");
 const auto first_hash=installed.body["active"].get<std::string>();const auto bytes=Read(root/"catalog.json");
 Check(h.catalog.Execute(install).body==installed.body&&Read(root/"catalog.json")==bytes,"same operation retry returns durable result without mutation");
 auto changed=install;changed["name"]="different";Check(h.catalog.Execute(changed).status==409,"operation ID payload conflict");
 Check(h.catalog.Receipt(install["operation_id"]).body==installed.body&&h.catalog.Receipt(Id(777)).status==404,"durable receipt lookup and absent result");
 auto enable=h.Request("enable");auto enabled=h.Apply(enable);Check(enabled.status==200&&h.manager.HasPlugin(alias),"enable owner publication");
 auto pinned=h.manager.CreateEffectInstance(alias);const auto original=pinned.GetGeneration();Check(Color(pinned)==20,"enabled actual interpreter frame");
 auto update=h.Request("update");update["name"]="Updated";update["version"]="2.0";update["program"]=Program(80);
 std::atomic<bool> on_owner{false};const auto caller=std::this_thread::get_id();
 h.catalog.SetBeforeWriteForTesting([&]{on_owner=std::this_thread::get_id()!=caller;throw std::runtime_error("disk fault");});
 auto failed=h.Apply(update);Check(failed.status==500&&h.manager.GetGeneration(alias)==original&&Color(pinned)==20&&!on_owner.load(),"update disk fault restores exact generation off frame owner");
 Check(h.catalog.Receipt(update["operation_id"]).status==404,"failed write creates no durable receipt");
 h.catalog.SetBeforeWriteForTesting({});auto updated=h.Apply(update);Check(updated.status==200&&Color(h.manager.CreateEffectInstance(alias))==80&&Color(pinned)==20,"retry same operation after known failure succeeds; old instance pinned");
 const auto current=h.manager.GetGeneration(alias);auto disable=h.Request("disable");
 h.catalog.SetBeforeWriteForTesting([]{throw std::runtime_error("disable disk fault");});
 Check(h.Apply(disable).status==500&&h.manager.GetGeneration(alias)==current,"disable failure restores exact prior generation and aliases");
 h.catalog.SetBeforeWriteForTesting({});Check(h.Apply(disable).status==200&&!h.manager.CreateEffectInstance(alias)&&Color(pinned)==20,"disable blocks new instances and retains running pinned instance");
 {PluginManager manager;ProgramCatalog restarted(root,manager);restarted.RestoreEnabled();Check(!manager.HasPlugin(alias)&&restarted.List().body["diagnostic"]=="","restart never activates disabled program");Check(restarted.Receipt(update["operation_id"]).body==updated.body,"receipt persists restart");}
 auto rollback=h.Request("rollback");rollback["hash"]=first_hash;Check(h.Apply(rollback).status==200&&!h.manager.HasPlugin(alias),"rollback selects retained version but does not implicitly enable");
 Check(h.Apply(h.Request("enable")).status==200&&Color(h.manager.CreateEffectInstance(alias))==20,"explicit enable restored version");
 {PluginManager manager;ProgramCatalog restarted(root,manager);restarted.RestoreEnabled();Check(manager.HasPlugin(alias)&&Color(manager.CreateEffectInstance(alias))==20,"restart verifies and restores enabled restricted generation");}
 Check(h.Apply(h.Request("uninstall")).status==200&&fs::exists(root/id/(first_hash+".aurair")),"uninstall retains immutable source versions");
 Check(h.Apply(h.Request("enable")).status==409,"removed record requires explicit retained rollback first");
 rollback=h.Request("rollback");rollback["hash"]=first_hash;Check(h.Apply(rollback).status==200&&!h.manager.HasPlugin(alias),"removed version restored disabled");
 auto unknown=h.Request("disable");unknown["program"]=Program(1);Check(h.catalog.Execute(unknown).status==422,"per-operation fields exact");
 Check(h.catalog.ExecuteText("{\"operation\":\"enable\",\"operation\":\"disable\"}").status==422,"duplicate request fields rejected");
 Check(h.catalog.ExecuteText(std::string(240*1024+1,' ')).status==413,"request size limit");
 auto conflict=h.Request("update");conflict["name"]="Fixture";conflict["version"]="3.0";conflict["program"]=Program(20);
 Check(h.Apply(conflict).status==409,"same content cannot change immutable version/source metadata");
 auto secret=h.Install(10,Id(100));secret["source"]={{"name","safe"},{"publication",secret["program"]["publication"]},{"json",{{"api_key","redacted-test"}}}};
 Check(h.Apply(secret).status==422,"source credential property rejected");secret["source"]["json"]={{"path","C:/private/example"}};Check(h.Apply(secret).status==422,"source private path rejected");
 secret["source"]["json"]={{"blocks",Json::array({{{"id","opaque:id"}}})}};Check(h.Apply(secret).status==200,"opaque block IDs may contain colon");
}
void IntegrityAndRecovery(const fs::path& root){
 Harness h(root);auto installed=h.Apply(h.Install());Check(installed.status==200,"integrity baseline install");auto enable=h.Request("enable");
 const auto hash=installed.body["active"].get<std::string>();const auto path=root/Id(99)/(hash+".aurair");Write(path,Program(90).dump());
 Check(h.Apply(enable).status==500&&!h.manager.HasPlugin("ir_"+Id(99)),"tampered immutable program never publishes");
 Write(path,Program(20).dump());Check(h.Apply(enable).status==200,"restored exact bytes permit explicit retry");
 const auto catalog_bytes=Read(root/"catalog.json");auto malformed=Json::parse(catalog_bytes);malformed["receipts"][0]["result"]["revision"]=99;Write(root/"catalog.json",malformed.dump());
 {PluginManager manager;ProgramCatalog invalid(root,manager);invalid.RestoreEnabled();Check(!invalid.List().body["diagnostic"].get<std::string>().empty()&&!manager.HasPlugin("ir_"+Id(99)),"malformed receipts disable startup mutations");}
 Write(root/"catalog.json",catalog_bytes);
 auto disable=h.Request("disable");auto old=h.manager.GetGeneration("ir_"+Id(99));
 h.catalog.SetBeforeWriteForTesting([&]{auto newer=h.manager.PrepareProgram("ir_"+Id(99),EffectProgram::Compile(Program(99)),root/"other.aurair");Check(h.manager.PublishReload(newer),"inject newer concurrent generation");throw std::runtime_error("failed persistence");});
 auto conflict=h.Apply(disable);Check(conflict.status==500&&conflict.body["error"]=="recovery_conflict"&&h.manager.GetGeneration("ir_"+Id(99))!=old,"stale recovery refuses overwriting newer generation");
 Check(h.catalog.Execute(h.Request("disable")).status==409,"recovery conflict disables further mutations");
}
void TimeoutAndExternal(const fs::path& root){
 Harness h(root);auto installed=h.Apply(h.Install());Check(installed.status==200,"timeout baseline");
 auto enable=h.Request("enable");auto timeout=h.catalog.Execute(enable);Check(timeout.status==409&&!h.manager.HasPlugin("ir_"+Id(99)),"unstarted frame publication times out without mutation");
 Check(!h.catalog.ApplyAtFrameBoundary()&&!h.manager.HasPlugin("ir_"+Id(99)),"cancelled queued command never applies later");
 Check(h.Apply(enable).status==200,"same operation retry after cancelled publication");
 auto bytes=Read(root/"catalog.json");Write(root/"catalog.json",bytes+" ");Check(h.Apply(h.Request("disable")).status==409&&h.manager.HasPlugin("ir_"+Id(99)),"external disk edit refuses mutation before publication");
}
void RestartAtomic(const fs::path& root){
 Harness h(root);auto a=h.Apply(h.Install(10,Id(1)));auto b=h.Apply(h.Install(30,Id(2)));Check(a.status==200&&b.status==200,"two program startup fixtures");
 Check(h.Apply(h.Request("enable",Id(1))).status==200&&h.Apply(h.Request("enable",Id(2))).status==200,"two enabled fixtures");
 Write(root/Id(2)/(b.body["active"].get<std::string>()+".aurair"),"broken");
 PluginManager manager;ProgramCatalog restarted(root,manager);restarted.RestoreEnabled();Check(!manager.HasPlugin("ir_"+Id(1))&&!manager.HasPlugin("ir_"+Id(2)),"startup validation failure retains no partial enabled registry");
}
void Links(const fs::path& root){
 fs::create_directories(root/"actual");const auto link=root/"link";
 if(CreateSymbolicLinkW(link.c_str(),(root/"actual").c_str(),SYMBOLIC_LINK_FLAG_DIRECTORY|SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)){
    PluginManager manager;ProgramCatalog catalog(link,manager);Check(!catalog.List().body["diagnostic"].get<std::string>().empty(),"symlink catalog root fails closed");
 }else std::cout<<"NOT_RUN: symlink creation unavailable, error "<<GetLastError()<<'\n';
}
}
int main(int argc,char** argv){try{Check(argc==2,"task scratch directory required");const auto root=fs::absolute(argv[1])/("catalog-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));fs::create_directories(root);fs::current_path(root);
 Lifecycle(root/"lifecycle");IntegrityAndRecovery(root/"integrity");TimeoutAndExternal(root/"timeouts");RestartAtomic(root/"restart");Links(root/"links");
 std::cout<<"PASS: "<<checks<<" program catalog transaction/receipt/recovery assertions\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL after "<<checks<<": "<<error.what()<<'\n';return 1;}}
