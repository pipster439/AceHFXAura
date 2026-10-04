#pragma once
#include <third_party/json.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
namespace gatea {
using Json=nlohmann::json;
enum class State {NotStarted,BaselineCaptured,ComActivated,PreEnumerationComplete,SwitchModePending,
    SwitchModeReturned,PostEnumerationComplete,ReleasePending,ReleaseReturned,RestorationCheckComplete,
    Completed,Failed,RecoveryRequired};
const char* Name(State state);
struct CallResult {
    std::string execution="NotAttempted";
    Json hresult=nullptr;
    Json exceptionCode=nullptr;
    std::string reason;
    bool Succeeded() const {return execution=="Succeeded";}
    Json ToJson() const;
};
struct Identity {bool valid=false;unsigned integrityRid=0;bool elevated=false;unsigned pid=0,session=0;std::string sid;bool inJob=true;bool sessionActive=true;};
bool IsMedium(const Identity& identity);
enum class Mode {DryRun,Execute,Invalid};
Mode ParseMode(const std::vector<std::string>& args);
inline constexpr unsigned Categories[]={0,0x10000,0x20000,0x30000,0x40000,0x50000,0x60000,0x70000,0x80000,0x120000,0x2f0000};
inline constexpr int OwnershipTtlMs=5000;
inline constexpr int HardMaximumTtlMs=10000;
bool ValidTtl(int milliseconds);
class IJournal {
public:
    virtual ~IJournal()=default;
    virtual void Append(Json entry)=0;
};
// Fixed-function adapter: token, RGB, fan, arbitrary method/CLSID/path APIs deliberately do not exist.
class IAuraGateAApi {
public:
    virtual ~IAuraGateAApi()=default;
    virtual void ThreadEnter()=0;
    virtual void ThreadExit() noexcept=0;
    virtual Json CaptureMetadata()=0;
    virtual CallResult ActivateSdk2()=0;
    virtual Json Enumerate(unsigned category,bool metadata)=0;
    virtual bool ConsumeOneShotApproval()=0;
    virtual CallResult AcquireOnce()=0;
    virtual CallResult ReleaseOnce(unsigned reviewedReserve)=0;
};
struct Review {bool releaseValueKnown=false;unsigned reserve=0;bool sdkMatches=false;std::string abiHash,analysisHash;bool executionEnabled=false;};
struct Result {State state=State::NotStarted;bool dryRun=true;bool switchAttempted=false,switchReturned=false,releaseAttempted=false,releaseReturned=false;bool callsOutstanding=false;std::string reason;};
struct StopSignal {std::atomic_bool requested=false;};
class Experiment {
public:
    Experiment(std::shared_ptr<IAuraGateAApi> api,std::shared_ptr<IJournal> journal,Identity identity,Review review,
        std::shared_ptr<StopSignal> stop=std::make_shared<StopSignal>());
    Result Run(Mode mode);
private:
    std::shared_ptr<IAuraGateAApi> _api;
    std::shared_ptr<IJournal> _journal;
    Identity _identity;
    Review _review;
    std::shared_ptr<StopSignal> _stop;
};
}
