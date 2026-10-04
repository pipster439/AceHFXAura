#pragma once
#include "Experiment.h"
#include <thread>
namespace gatea::tests {
struct MemoryJournal final:IJournal {
    std::mutex mutex;std::vector<Json> entries;
    bool failAfterSwitch=false;
    void Append(Json value) override {
        std::lock_guard lock(mutex);
        if(failAfterSwitch && value.value("event","")=="SwitchModeReturned") throw std::runtime_error("FakeJournalWriteFailure");
        entries.push_back(std::move(value));
    }
    bool Contains(const std::string& event) {std::lock_guard lock(mutex);for(auto& e:entries) if(e.value("event","")==event) return true;return false;}
};
struct FakeApi final:IAuraGateAApi {
    std::atomic_int activation=0,enumerations=0,acquire=0,release=0;
    std::atomic_bool consumed=false;
    bool denyActivation=false,acquireFailure=false,releaseThrow=false,releaseFailure=false,postEnumerationThrow=false,postEnumerationHold=false;
    int releaseDelayMs=0,nonzeroBaseline=0;
    std::atomic_llong acquiredAt=0,releasedAt=0;
    static long long Tick(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
    void ThreadEnter() override {}
    void ThreadExit() noexcept override {}
    Json CaptureMetadata() override {return {{"lightingService",{{"state",4},{"pid",123}}},{"wdlRegistry",{{"value",0}}},{"lampArrayInterfaces",Json::array()},{"topology","fakehash"},{"lastConfig","fakehash"}};}
    CallResult ActivateSdk2() override {++activation;return denyActivation?CallResult{"PermissionDenied",-2147024891,nullptr,"FakeDenied"}:CallResult{"Succeeded",0,nullptr,"FakeOnly"};}
    Json Enumerate(unsigned category,bool metadata) override {
        ++enumerations;
        if(metadata && postEnumerationThrow) throw std::runtime_error("FakePostEnumerationFailure");
        if(metadata && postEnumerationHold) {postEnumerationHold=false;std::this_thread::sleep_for(std::chrono::milliseconds(5500));}
        return {{"category",category},{"execution","Succeeded"},{"hresult",0},{"count",nonzeroBaseline},{"devices",Json::array()}};
    }
    bool ConsumeOneShotApproval() override {return !consumed.exchange(true);}
    CallResult AcquireOnce() override {++acquire;acquiredAt=Tick();return acquireFailure?CallResult{"Failed",-2147467259,nullptr,"FakePartialTransitionUnknown"}:CallResult{"Succeeded",0,nullptr,"FakeOnly"};}
    CallResult ReleaseOnce(unsigned reserve) override {
        ++release;releasedAt=Tick();if(reserve!=0) throw std::runtime_error("FakeWrongReserve");
        if(releaseDelayMs) std::this_thread::sleep_for(std::chrono::milliseconds(releaseDelayMs));
        if(releaseThrow) throw std::runtime_error("FakeReleaseException");
        return releaseFailure?CallResult{"Failed",-2147467259,nullptr,"FakeReleaseFailure"}:CallResult{"Succeeded",0,nullptr,"FakeOnly"};
    }
};
inline Identity Medium(){return {true,0x2000,false,1234,1,"S-1-5-21-1001",false};}
inline Review Reviewed(){return {true,0,true,"verified-fake-ABI","verified-fake-review",true};}
}
