#include "FakeApi.h"
#include "Journal.h"
#include <filesystem>
#include <fstream>
#include <windows.h>
using namespace gatea;
using namespace gatea::tests;
// Software fixture only; no native adapter, ASUS header/library or vendor activation linked.
int wmain(int argc,wchar_t** argv) {
    if(argc!=3) return 2;
    std::filesystem::path directory=argv[2];
    if(std::wstring(argv[1])==L"launcher") {
        wchar_t executable[MAX_PATH]{};GetModuleFileNameW(nullptr,executable,MAX_PATH);
        std::wstring command=L"\""+std::wstring(executable)+L"\" owner \""+directory.wstring()+L"\"";
        STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
        if(!CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)) return 3;
        std::ofstream(directory/L"launched.json")<<Json{{"ownerPid",process.dwProcessId},{"launcherPid",GetCurrentProcessId()},{"softwareOnly",true}}.dump();
        CloseHandle(process.hThread);CloseHandle(process.hProcess);return 0; // Natural launcher exit, not forced termination.
    }
    if(std::wstring(argv[1])!=L"owner") return 2;
    auto api=std::make_shared<FakeApi>();api->postEnumerationHold=true;
    auto journal=std::make_shared<Journal>((directory/L"journal.jsonl").wstring(),"bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb");
    auto identity=Medium();identity.pid=GetCurrentProcessId();
    auto result=Experiment(api,journal,identity,Reviewed()).Run(Mode::Execute);
    std::ofstream(directory/L"result.json")<<Json{{"state",Name(result.state)},{"acquireCalls",api->acquire.load()},{"releaseCalls",api->release.load()},
        {"releaseElapsedMs",api->releasedAt.load()-api->acquiredAt.load()},{"pid",GetCurrentProcessId()},{"softwareOnly",true}}.dump();
    return result.state==State::Completed?0:1;
}
