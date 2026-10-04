#include "FakeApi.h"
#include "Journal.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <concepts>
using namespace gatea;
using namespace gatea::tests;
namespace {
int count=0;
void Check(bool condition,const char* name){if(!condition) throw std::runtime_error(name);++count;std::cout<<"PASS "<<name<<"\n";}
Result Run(std::shared_ptr<FakeApi> api,Mode mode=Mode::Execute,Identity id=Medium(),Review review=Reviewed(),std::shared_ptr<MemoryJournal> journal=std::make_shared<MemoryJournal>(),std::shared_ptr<StopSignal> stop=std::make_shared<StopSignal>()) {
    return Experiment(api,journal,id,review,stop).Run(mode);
}
template<class T> concept HasToken=requires(T& t) {t.RequireTokenByType();};
template<class T> concept HasControl=requires(T& t) {t.RequireDeviceControlState();};
template<class T> concept HasRgb=requires(T& t) {t.Apply();t.put_Color(0);};
static_assert(!HasToken<IAuraGateAApi> && !HasControl<IAuraGateAApi> && !HasRgb<IAuraGateAApi>);
}
int main() {
    try {
        {
            auto api=std::make_shared<FakeApi>();auto journal=std::make_shared<MemoryJournal>();auto result=Run(api,Mode::DryRun,Medium(),Reviewed(),journal);
            Check(result.state==State::Completed && !result.switchAttempted && !result.releaseAttempted && api->acquire==0 && api->release==0 && journal->Contains("DRY_RUN"),"dry run never calls ownership");
        }
        Check(ParseMode({})==Mode::DryRun && ParseMode({"--execute-gate-a"})==Mode::Execute && ParseMode({"--execute"})==Mode::Invalid && ParseMode({"--execute-gate-a","--ttl=100"})==Mode::Invalid,"only exact fixed flag or dry run accepted");
        {
            auto api=std::make_shared<FakeApi>();auto result=Run(api,Mode::Invalid);Check(result.state==State::Failed && api->activation==0 && api->acquire==0,"missing or wrong flag blocks execute");
        }
        {
            for(unsigned rid:{0x1000u,0x2100u,0x3000u,0x4000u}) {
                auto api=std::make_shared<FakeApi>();auto id=Medium();id.integrityRid=rid;auto result=Run(api,Mode::Execute,id);
                Check(result.state==State::Failed && api->activation==0 && api->acquire==0,"non-medium integrity blocked before COM");
            }
            auto api=std::make_shared<FakeApi>();auto id=Medium();id.valid=false;Check(Run(api,Mode::Execute,id).state==State::Failed && api->activation==0,"invalid token blocked");
            id=Medium();id.elevated=true;Check(Run(api,Mode::Execute,id).state==State::Failed,"elevated user blocked");
            id=Medium();id.session=0;Check(Run(api,Mode::Execute,id).state==State::Failed,"session zero blocked");
            id=Medium();id.inJob=true;Check(Run(api,Mode::Execute,id).state==State::Failed,"inherited launcher job blocked");
            id=Medium();id.sessionActive=false;Check(Run(api,Mode::Execute,id).state==State::Failed,"inactive session blocks execution");
        }
        Check(OwnershipTtlMs==5000 && HardMaximumTtlMs==10000 && ValidTtl(5000) && ValidTtl(10000) && !ValidTtl(10001) && !ValidTtl(0) && !ValidTtl(-1),"TTL is fixed five seconds with hard ten-second limit");
        {
            auto api=std::make_shared<FakeApi>();auto review=Reviewed();review.releaseValueKnown=false;
            Check(Run(api,Mode::Execute,Medium(),review).state==State::Failed && api->acquire==0,"unknown reserve blocks execution");
            review=Reviewed();review.sdkMatches=false;Check(Run(api,Mode::Execute,Medium(),review).state==State::Failed && api->activation==0,"SDK drift blocks COM and ownership");
            review=Reviewed();review.executionEnabled=false;Check(Run(api,Mode::Execute,Medium(),review).state==State::Failed && api->activation==0,"closed reviewed gate blocks before COM");
        }
        {
            auto api=std::make_shared<FakeApi>();api->denyActivation=true;
            Check(Run(api).state==State::Failed && api->acquire==0 && api->release==0,"missing SDK2 prevents ownership");
        }
        {
            auto api=std::make_shared<FakeApi>();api->nonzeroBaseline=1;
            Check(Run(api).state==State::Failed && api->acquire==0 && api->release==0,"nonzero baseline blocks vendor internal reset risk");
        }
        {
            auto api=std::make_shared<FakeApi>();auto journal=std::make_shared<MemoryJournal>();auto result=Run(api,Mode::Execute,Medium(),Reviewed(),journal);
            Check(result.state==State::Completed && api->acquire==1 && api->release==1 && api->enumerations==33 && result.switchReturned && result.releaseReturned && journal->Contains("RestorationCheckComplete"),"fixed normal same-instance acquire enumerate release observations");
            auto second=Run(api);Check(second.state==State::Failed && api->acquire==1 && api->release==1,"one shot cannot be replayed without review");
        }
        {
            auto api=std::make_shared<FakeApi>();api->acquireFailure=true;
            auto result=Run(api);Check(result.state==State::Failed && api->release==1 && result.releaseReturned,"failed Switch follows partial-transition release policy");
        }
        {
            auto api=std::make_shared<FakeApi>();api->postEnumerationThrow=true;
            auto result=Run(api);Check(result.state==State::Failed && api->release==1 && result.releaseReturned,"post-acquire enumeration failure still releases once");
        }
        {
            auto api=std::make_shared<FakeApi>();auto journal=std::make_shared<MemoryJournal>();journal->failAfterSwitch=true;
            auto result=Run(api,Mode::Execute,Medium(),Reviewed(),journal);
            Check(result.state==State::RecoveryRequired && api->acquire==1 && api->release==1 && result.releaseReturned,"journal failure after acquire still attempts release before exit");
        }
        {
            auto api=std::make_shared<FakeApi>();api->releaseThrow=true;auto journal=std::make_shared<MemoryJournal>();
            auto result=Run(api,Mode::Execute,Medium(),Reviewed(),journal);
            Check(result.state==State::RecoveryRequired && api->release==1 && !result.releaseReturned && journal->Contains("RECOVERY_REQUIRED"),"release exception is recovery required not returned success");
        }
        {
            auto api=std::make_shared<FakeApi>();api->releaseFailure=true;
            Check(Run(api).state==State::RecoveryRequired && api->release==1,"release HRESULT failure is recovery required");
        }
        {
            auto api=std::make_shared<FakeApi>();api->releaseDelayMs=3000;auto journal=std::make_shared<MemoryJournal>();
            auto result=Run(api,Mode::Execute,Medium(),Reviewed(),journal);
            Check(result.state==State::RecoveryRequired && api->release==1 && !result.releaseReturned && result.callsOutstanding,"release timeout bounded and recorded recovery required");
            std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            std::lock_guard lock(journal->mutex);Check(journal->entries.back()["state"]=="RecoveryRequired","late release cannot erase recovery status");
        }
        {
            auto api=std::make_shared<FakeApi>();api->postEnumerationHold=true;
            auto result=Run(api);auto elapsed=api->releasedAt.load()-api->acquiredAt.load();
            Check(result.state==State::Completed && api->release==1 && elapsed>=4900 && elapsed<5300,"autonomous TTL attempts same-instance release during held fake read");
        }
        {
            auto api=std::make_shared<FakeApi>();auto stop=std::make_shared<StopSignal>();stop->requested=true;
            Check(Run(api,Mode::Execute,Medium(),Reviewed(),std::make_shared<MemoryJournal>(),stop).state==State::Failed && api->acquire==0,"console stop before acquire cannot acquire");
        }
        {
            wchar_t temp[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);auto path=std::filesystem::path(temp)/("AuraGateAJournal-"+std::to_string(GetCurrentProcessId())+".jsonl");
            if(std::filesystem::exists(path)) throw std::runtime_error("Use a fresh software journal test file");
            {
                Journal journal(path.wstring(),"aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa");
                journal.Append({{"execution","NotAttempted"},{"value",nullptr}});journal.Append({{"execution","Succeeded"},{"value",0}});journal.Append({{"execution","Failed"},{"value",nullptr}});
                std::ifstream file(path,std::ios::binary);std::string line;int lines=0;while(std::getline(file,line)) {auto value=Json::parse(line);Check(value["sequence"]==lines,"append-only journal retains sequence");++lines;}
                Check(lines==3,"journal distinguishes attempted zero from missing and failed values");
                bool bounded=false;try {journal.Append({{"oversized",std::string(JournalMaxEntryBytes,'x')}});} catch(const std::exception&){bounded=true;}
                Check(bounded,"journal rejects oversized entry");
                bool countBound=false;try {for(int i=0;i<300;++i) journal.Append({{"event","bounded"}});} catch(const std::exception&){countBound=true;}
                Check(countBound && std::filesystem::file_size(path)<=JournalMaxBytes,"journal entry count and bytes are bounded");
            }
            std::filesystem::remove(path);
        }
        Check(!HasToken<IAuraGateAApi> && !HasControl<IAuraGateAApi> && !HasRgb<IAuraGateAApi>,"fixed API interface has no token or RGB capability");
        {
            wchar_t current[MAX_PATH]{},temporary[MAX_PATH]{};GetModuleFileNameW(nullptr,current,MAX_PATH);GetTempPathW(MAX_PATH,temporary);
            auto directory=std::filesystem::path(temporary)/("AuraGateAOwner-"+std::to_string(GetCurrentProcessId()));
            if(!std::filesystem::create_directory(directory)) throw std::runtime_error("Fresh launcher fixture directory required");
            auto executable=std::filesystem::path(current).parent_path()/L"AuraGateAOwnerFixture.exe";
            std::wstring command=L"\""+executable.wstring()+L"\" launcher \""+directory.wstring()+L"\"";
            STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
            if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)) throw std::runtime_error("Software launcher fixture start failed");
            Check(WaitForSingleObject(process.hProcess,2000)==WAIT_OBJECT_0,"software launcher exits naturally before owner TTL");
            CloseHandle(process.hThread);CloseHandle(process.hProcess);
            std::ifstream launched(directory/L"launched.json");Json pid;launched>>pid;launched.close();
            HANDLE owner=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid["ownerPid"].get<DWORD>());
            Check(owner && WaitForSingleObject(owner,0)==WAIT_TIMEOUT,"owning fixture survives departed launcher");
            Check(WaitForSingleObject(owner,8000)==WAIT_OBJECT_0,"owning fixture finishes its own TTL cleanup");
            CloseHandle(owner);std::ifstream file(directory/L"result.json");Json result;file>>result;file.close();
            Check(result["state"]=="Completed" && result["releaseCalls"]==1 && result["releaseElapsedMs"].get<int>()>=4900 && result["releaseElapsedMs"].get<int>()<5300,"owner releases fake instance autonomously without UI parent");
            for(auto name:{L"journal.jsonl",L"launched.json",L"result.json"}) std::filesystem::remove(directory/name);
            std::filesystem::remove(directory); // Empty task-created directory only; no recursive deletion.
        }
        std::cout<<"PASSED "<<count<<" checks; all APIs were software fakes.\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
