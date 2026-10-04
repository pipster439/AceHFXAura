#include "NativeAuraApi.h"
#include "Journal.h"
#include "ReviewedEvidence.h"
#include <iostream>
#include <fstream>
#include <exception>
#include <windows.h>
#include <objbase.h>
namespace {
gatea::Journal* emergencyJournal=nullptr;
std::shared_ptr<gatea::StopSignal> stop=std::make_shared<gatea::StopSignal>();
std::atomic_bool finished=false;
BOOL WINAPI Control(DWORD event) {
    if(event!=CTRL_C_EVENT && event!=CTRL_BREAK_EVENT && event!=CTRL_CLOSE_EVENT && event!=CTRL_LOGOFF_EVENT && event!=CTRL_SHUTDOWN_EVENT) return FALSE;
    stop->requested=true;
    // Signal only: never invoke COM on Windows' control-handler thread. Windows may terminate earlier.
    for(int i=0;i<100 && !finished;++i) Sleep(10);
    return TRUE;
}
LONG WINAPI Failure(EXCEPTION_POINTERS* error) {
    if(emergencyJournal) emergencyJournal->Emergency("UnhandledNativeFailure",error->ExceptionRecord->ExceptionCode);
    return EXCEPTION_CONTINUE_SEARCH;
}
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    std::vector<std::string> args;for(int i=1;i<argc;++i) args.emplace_back(argv[i]);
    auto mode=gatea::ParseMode(args);
    if(mode==gatea::Mode::Invalid) {std::cerr<<"No arguments = DRY RUN. Only --execute-gate-a is accepted after separate approval.\n";return 2;}
    auto identity=gatea::CurrentIdentity();
    if(!gatea::IsMedium(identity)) {std::cerr<<"Medium interactive user token required; no COM activation.\n";return 3;}
    if(mode==gatea::Mode::Execute && !identity.sessionActive) {std::cerr<<"Execution blocked: interactive session is not active.\n";return 3;}
    // No Job Object is created or inherited by policy during execute. Parent disappearance is not a stop signal.
    if(mode==gatea::Mode::Execute && identity.inJob) {std::cerr<<"Execution blocked: inherited launcher Job.\n";return 3;}
    HANDLE mutex=CreateMutexW(nullptr,TRUE,L"Local\\AceHFXAura.AuraOwnershipGateA");
    if(!mutex || GetLastError()==ERROR_ALREADY_EXISTS) {if(mutex) CloseHandle(mutex);std::cerr<<"Another candidate process is present; stop.\n";return 3;}
    try {
        GUID guid{};if(FAILED(CoCreateGuid(&guid))) throw std::runtime_error("ExperimentIdFailed");
        wchar_t guidText[40]{};StringFromGUID2(guid,guidText,40);
        std::wstring wideId(guidText+1,36);std::string id;
        for(wchar_t digit:wideId) id.push_back(static_cast<char>(digit)); // StringFromGUID2 is ASCII hex punctuation.
        auto root=gatea::JournalRoot();auto directory=root/wideId;std::filesystem::create_directories(directory);
        auto journal=std::make_shared<gatea::Journal>((directory/L"journal.jsonl").wstring(),id);
        emergencyJournal=journal.get();SetUnhandledExceptionFilter(Failure);
        std::set_terminate([]{if(emergencyJournal) emergencyJournal->Emergency("UnhandledCppTerminate",0);std::abort();});
        SetConsoleCtrlHandler(Control,TRUE);
        std::ofstream observations(directory/L"HUMAN_OBSERVATIONS.md",std::ios::out|std::ios::binary);
        observations<<"# Human evidence (not supplied automatically)\n\nExperimentId: "<<id<<"\nVisualStateBefore: NOT_PROVIDED\nVisualStateDuringOwnership: NOT_PROVIDED\nVisualStateAfterRelease: NOT_PROVIDED\nObservedAt: NOT_PROVIDED\nObserver: NOT_PROVIDED\nRestorationVerifiedByHuman: UNKNOWN\n";
        observations.close();
        gatea::Experiment experiment(gatea::CreateNativeApi(root,id),journal,identity,gatea::CurrentReview(),stop);
        auto result=experiment.Run(mode);finished=true;
        journal->Append({{"state",gatea::Name(result.state)},{"event","ProcessFinalStatus"},{"dryRun",result.dryRun},
            {"switchAttempted",result.switchAttempted},{"switchReturned",result.switchReturned},{"releaseAttempted",result.releaseAttempted},
            {"releaseReturned",result.releaseReturned},{"callsOutstanding",result.callsOutstanding},{"reason",result.reason},
            {"ownershipTtlMs",gatea::OwnershipTtlMs},{"hardMaximumMs",gatea::HardMaximumTtlMs},{"restoration","Unknown"}});
        std::cout<<gatea::Json{{"experimentId",id},{"journal",(directory/L"journal.jsonl").string()},
            {"state",gatea::Name(result.state)},{"dryRun",result.dryRun},{"switchAttempted",result.switchAttempted},
            {"releaseAttempted",result.releaseAttempted},{"releaseReturned",result.releaseReturned},
            {"restorationVerified",nullptr},{"reason",result.reason}}.dump(2)<<"\n";
        SetConsoleCtrlHandler(Control,FALSE);emergencyJournal=nullptr;ReleaseMutex(mutex);CloseHandle(mutex);
        // Ordinary process return after RECOVERY_REQUIRED ends outstanding native threads, if any.
        // It is not a successful release, nor protection against native hang/ExitProcess/hard termination.
        return result.state==gatea::State::Completed?0:result.state==gatea::State::RecoveryRequired?4:3;
    } catch(const std::exception& e) {finished=true;std::cerr<<e.what()<<"\n";ReleaseMutex(mutex);CloseHandle(mutex);return 4;}
}
