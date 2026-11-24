#include "aura/hardware/m605_runtime.h"
#include "aura/native_hid_backend.h"
#include <atomic>
#include <functional>
#include <iostream>
#include <psapi.h>
#include <stdexcept>
#include <vector>
#include <fstream>
#include "third_party/json.hpp"

namespace aura {
struct M605RuntimeTestAccess {
    static bool Stopping(M605Runtime& runtime) {
        std::lock_guard<std::mutex> lock(runtime.mutex_); return runtime.stopping_;
    }
    static std::unique_ptr<M605Runtime> Create(std::unique_ptr<m605::detail::Transport> io,
        std::unique_ptr<m605::detail::SettleWait> wait,
        std::unique_ptr<m605::detail::SafetyLatch> latch) {
        return std::unique_ptr<M605Runtime>(new M605Runtime(std::move(io), std::move(wait), std::move(latch)));
    }
};
}
namespace {
using namespace aura;
using namespace aura::m605;
int checks = 0;
void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
Report Basic(uint8_t patch = 0x59) {
    Report report{}; report[1] = 0x12; report[5] = patch;
    report[7] = 1; report[9] = 6; report[11] = 1; return report;
}
struct Latch final : detail::SafetyLatch {
    bool armed = false;
    int arms = 0, clears = 0;
    bool IsQuarantined() const override { return armed; }
    bool Arm() override { ++arms; if (armed) return false; armed = true; return true; }
    bool Clear() override { ++clears; armed = false; return true; }
};
struct Wait final : detail::SettleWait {
    std::function<void()> before_apply;
    void WaitBetweenDksStages() override {}
    void WaitBeforeApply() override { if (before_apply) before_apply(); }
    void WaitAfterApply() override {}
};
struct Io final : detail::Transport {
    std::atomic<bool> connected{false}, current{true};
    bool endpoint = true, read_ok = true, stage_ok = true, apply_ok = true;
    Report report = Basic();
    std::atomic<int> stages{0}, applies{0}, selectors{0}, queries{0};
    std::function<void()> after_read;
    bool IsConnected() const override { return connected; }
    bool IsCurrentSession() const override { return current && endpoint; }
    bool Connect() override { connected = endpoint; current = true; return endpoint; }
    void Disconnect() override { connected = false; }
    FirmwareCompatibility ReadFirmwareCompatibility() override {
        ++queries;
        auto value = read_ok ? ParseBasicInfoFirmware(report.data(), report.size()) :
            FirmwareCompatibility{FirmwareState::ReadFailed, {}, "FirmwareReadFailed: synthetic timeout/cancel"};
        if (after_read) after_read();
        return value;
    }
    bool QueryBasicInfo(Report& value) override { value = report; return read_ok; }
    bool WriteStage(const Report& value) override {
        if (value[1] == 0x51 && value[2] == 0) { ++selectors; if (stage_ok) report[11] = value[5]; }
        else ++stages;
        return stage_ok && current && connected;
    }
    bool WriteApply(const Report&) override { ++applies; return apply_ok && current && connected; }
    std::string GetLastError() const override { return "synthetic endpoint/read/write fault"; }
};
struct Fixture {
    Io* io; Latch* latch; Wait* wait;
    std::unique_ptr<M605Runtime> runtime;
    explicit Fixture(bool quarantine = false) {
        auto i = std::make_unique<Io>(); io = i.get();
        auto l = std::make_unique<Latch>(); latch = l.get(); l->armed = quarantine;
        auto w = std::make_unique<Wait>(); wait = w.get();
        runtime = M605RuntimeTestAccess::Create(std::move(i), std::move(w), std::move(l));
    }
};
using Operation = std::function<bool(M605Runtime&)>;
const std::vector<std::pair<const char*, Operation>> operations = {
    {"per-key actuation", [](auto& r) { return r.SetPerKeyActuation(0x0402, 1).get(); }},
    {"global actuation", [](auto& r) { return r.SetGlobalActuation(1).get(); }},
    {"actuation reset", [](auto& r) { return r.ResetAllPerKeyActuationOverrides(1).get(); }},
    {"RT pair", [](auto& r) { return r.SetPerKeyRapidTrigger(0x0402, .4, .4).get(); }},
    {"RT unified", [](auto& r) { return r.SetPerKeyRapidTriggerUnified(0x0402, .4, true).get(); }},
    {"RT press", [](auto& r) { return r.SetPerKeyRapidTriggerPress(0x0402, .4, true).get(); }},
    {"RT release", [](auto& r) { return r.SetPerKeyRapidTriggerRelease(0x0402, .4, true).get(); }},
    {"RT disable", [](auto& r) { return r.DisablePerKeyRapidTrigger(0x0402, .4, .4).get(); }},
    {"deadzone", [](auto& r) { return r.SetPerKeyDeadzone(0x0402, .1, .1).get(); }},
    {"global deadzone", [](auto& r) { return r.SetGlobalDeadzone(.1, .1).get(); }},
    {"deadzone reset", [](auto& r) { return r.ResetAllPerKeyDeadzoneOverrides(1, 1).get(); }},
    {"DKS", [](auto& r) { return r.SetPerKeyDks(StandardDksConfiguration(0x0402)).get(); }},
    {"DKS restore", [](auto& r) { return r.RestorePerKeyDksToStandard(0x0402).get(); }},
    {"SpeedTap pair", [](auto& r) { return r.SetSpeedTapPair(0x0602, 0x0301).get(); }},
    {"SpeedTap disable", [](auto& r) { return r.DisableSpeedTapPair(0x0602, 0x0301).get(); }},
    {"SpeedTap master", [](auto& r) { return r.SetSpeedTapMaster(true).get(); }},
    {"SpeedTap restore", [](auto& r) { return r.ResetSpeedTapRuntimeToProfile().get(); }},
    {"analog configuration", [](auto& r) { return r.SetAnalogEffect(0, true).get(); }},
    {"bank selector", [](auto& r) { return r.SelectHardwareProfileSlot(2).success; }},
};
void Denied(Fixture& f) {
    Check(f.io->stages == 0 && f.io->applies == 0 && f.io->selectors == 0, "denial emitted a configuration report");
    Check(f.latch->arms == 0 && f.latch->clears == 0, "denial touched durable safety latch");
    const auto state = f.runtime->GetAppliedRuntimeState();
    Check(state.per_key_actuation_raw.empty() && !state.global_actuation_raw && state.per_key_dks.empty() &&
        state.per_key_rapid_trigger.empty() && state.per_key_deadzone.empty() && !state.speedtap_master,
        "denial manufactured SessionApplied state");
}
void Parser() {
    auto good = Basic();
    Check(ParseBasicInfoFirmware(good.data(), good.size()).version == "1.00.59", "official hex byte order");
    Check(ParseBasicInfoFirmware(good.data(), good.size()).Supported(), "known firmware rejected");
    for (auto patch : {0x58, 0x60, 0x3b}) {
        auto r = Basic(static_cast<uint8_t>(patch));
        Check(ParseBasicInfoFirmware(r.data(), r.size()).state == FirmwareState::Unsupported, "unverified version admitted");
    }
    for (size_t size : {size_t(0), size_t(64), size_t(66)})
        Check(!ParseBasicInfoFirmware(good.data(), size).Supported(), "bad size accepted");
    Check(!ParseBasicInfoFirmware(nullptr, 65).Supported(), "null accepted");
    for (size_t offset : {size_t(0),size_t(1),size_t(2),size_t(3),size_t(4),size_t(9),size_t(11)}) {
        auto r = good; r[offset] = 0xff;
        Check(ParseBasicInfoFirmware(r.data(), r.size()).state == FirmwareState::Unknown, "wrong envelope accepted");
    }
    for (auto offset : {5,6,7,8}) {
        auto r = good; r[offset] = 0xff;
        Check(!ParseBasicInfoFirmware(r.data(), r.size()).Supported(), "damaged version admitted");
    }
    auto zero = good; zero[5] = zero[6] = zero[7] = zero[8] = 0;
    Check(ParseBasicInfoFirmware(zero.data(), zero.size()).state == FirmwareState::Unknown, "empty version accepted");
    Check(NativeHidBackend::IsTargetLightingEndpoint(0x0b05,0x1b7e,0xff00,1,65,L"hid&mi_01#fixture"), "valid endpoint rejected");
    Check(!NativeHidBackend::IsTargetLightingEndpoint(0x0b06,0x1b7e,0xff00,1,65,L"hid&mi_01#fixture"), "wrong VID");
    Check(!NativeHidBackend::IsTargetLightingEndpoint(0x0b05,0x1b7f,0xff00,1,65,L"hid&mi_01#fixture"), "wrong PID");
    Check(!NativeHidBackend::IsTargetLightingEndpoint(0x0b05,0x1b7e,0xff00,1,65,L"hid&mi_02#fixture"), "wrong endpoint");
    Check(!IsConfigurationMutation(BuildBasicInfoQuery()), "read-only query gated as mutation");
    Report rgb{}; rgb[1]=0xc0; rgb[2]=0x81;
    Check(!IsConfigurationMutation(rgb), "RGB incorrectly uses magnetic gate");
    Check(IsConfigurationMutation(BuildRuntimeApply()), "Apply bypass");
}
void Windows() {
    auto good = Basic(), unrelated = good, echo = BuildBasicInfoQuery(); unrelated[1] = 0x25;
    using Result = BasicInfoReplyWindow::Result;
    BasicInfoReplyWindow w(7, 1000);
    Check(w.Consider(unrelated.data(),65,7,1001,true)==Result::Ignore,"unrelated response accepted");
    Check(w.Consider(echo.data(),65,7,1002,true)==Result::Ignore,"echo accepted");
    Check(w.Consider(good.data(),64,7,1003,true)==Result::Ignore,"short response accepted");
    Check(w.Consider(good.data(),65,7,1099,true)==Result::Accept,"known timely reply rejected");
    Check(w.Consider(good.data(),65,7,1100,true)==Result::Expired,"late reply accepted");
    Check(w.Consider(good.data(),65,6,1004,true)==Result::StaleSession,"old epoch accepted");
    Check(w.Consider(good.data(),65,7,1004,false)==Result::StaleSession,"disconnect/cancel accepted");
    BasicInfoReplyWindow clock(7,1000);
    Check(clock.Consider(good.data(),65,7,999,true)==Result::Expired,"backwards clock accepted");
    BasicInfoReplyWindow flood(7,1000);
    for(int i=0;i<64;++i) Check(flood.Consider(unrelated.data(),65,7,1001,true)==Result::Ignore,"budget ended early");
    Check(flood.Consider(good.data(),65,7,1001,true)==Result::Expired,"unbounded traffic");
    // Duplicate same-session valid replies are indistinguishable on this wire.
    // Do not label a local epoch as a hardware transaction ID.
}
void Captures(const char* path) {
    const auto fixture = nlohmann::json::parse(std::ifstream(path));
    Check(fixture.at("schema_version")==1,"capture fixture schema");
    Check(fixture.at("samples").size()==62,"capture sample count");
    for(const auto& sample : fixture.at("samples")) {
        const auto hex=sample.at("payload_hex").get<std::string>();
        Check(hex.size()==128,"capture USB payload length");
        Report host{};
        for(size_t i=0;i<64;++i) host[i+1]=static_cast<uint8_t>(std::stoul(hex.substr(i*2,2),nullptr,16));
        const auto firmware=ParseBasicInfoFirmware(host.data(),host.size());
        Check(firmware.Supported() && firmware.version=="1.00.59","historical known firmware rejected by production parser");
    }
    std::cout<<"VERIFIED_CAPTURE replay: 62 historical payloads, host framing reconstructed from STATIC_PROTOCOL\n";
}
void Matrix() {
    for (const auto& [name, run] : operations) {
        Fixture known;
        Check(run(*known.runtime), name);
        Check(known.io->stages + known.io->selectors > 0, "known case did no configuration work");
        Check(known.io->selectors || (known.io->applies == 1 && known.latch->arms == 1 && known.latch->clears == 1), "known transaction lifecycle");
        for (int fault=0; fault<8; ++fault) {
            Fixture bad;
            if (fault==0) bad.io->report = Basic(0x58);
            if (fault==1) bad.io->report = Basic(0x60);
            if (fault==2) bad.io->report[7] = 0; // Unknown firmware
            if (fault==3) bad.io->read_ok = false;
            if (fault==4) bad.io->report[0] = 1;
            if (fault==5) bad.io->report[3] = 1;
            if (fault==6) bad.io->endpoint = false;
            if (fault==7) bad.io->after_read = [&] { bad.io->current = false; };
            Check(!run(*bad.runtime), "negative matrix admitted configuration");
            Denied(bad);
        }
        Fixture quarantined(true);
        Check(!run(*quarantined.runtime), "persistent quarantine bypass");
        Denied(quarantined); Check(quarantined.latch->armed, "quarantine cleared");
    }
    Fixture failed_stage; failed_stage.io->stage_ok=false;
    Check(!failed_stage.runtime->SetPerKeyActuation(0x0402,1).get(),"stage failure success");
    Check(failed_stage.io->applies==0 && failed_stage.latch->armed,"stage failure failed to quarantine");
    Fixture failed_apply; failed_apply.io->apply_ok=false;
    Check(!failed_apply.runtime->SetPerKeyActuation(0x0402,1).get(),"Apply failure success");
    Check(failed_apply.latch->armed && failed_apply.runtime->GetHealth()==M605RuntimeHealth::IndeterminateStagedState,"Apply quarantine lost");
    Fixture lost_before_apply; lost_before_apply.wait->before_apply=[&] { lost_before_apply.io->current=false; };
    Check(!lost_before_apply.runtime->SetPerKeyActuation(0x0402,1).get(),"disconnect before Apply succeeded");
    Check(lost_before_apply.latch->armed,"partial transaction must remain quarantined");
    Fixture exception;
    exception.io->after_read=[] { throw std::runtime_error("synthetic reader exception"); };
    Check(!exception.runtime->SetPerKeyActuation(0x0402,1).get(),"reader exception admitted mutation");
    Denied(exception);
    Check(exception.runtime->GetDiagnosticSnapshot().firmware.state==FirmwareState::ReadFailed,"exception lost diagnostic category");
    Fixture stopped_read;
    std::thread stopper;
    stopped_read.io->after_read=[&] {
        stopper=std::thread([&] { stopped_read.runtime->Stop(); });
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(!M605RuntimeTestAccess::Stopping(*stopped_read.runtime) && std::chrono::steady_clock::now()<until) std::this_thread::yield();
    };
    const bool stopped_result=stopped_read.runtime->SetPerKeyActuation(0x0402,1).get();
    if(stopper.joinable()) stopper.join();
    Check(!stopped_result,"Stop during acquisition admitted mutation"); Denied(stopped_read);
}
void NonBlockingPresenceObservation() {
    Fixture f;
    std::mutex mutex; std::condition_variable cv;
    bool entered=false, release=false, admitted=true;
    f.io->after_read=[&] {
        std::unique_lock<std::mutex> lock(mutex);
        entered=true; cv.notify_all();
        cv.wait(lock,[&] { return release; });
    };
    std::thread probe([&] { admitted=f.runtime->PrepareTransportSession(); });
    bool observed;
    { std::unique_lock<std::mutex> lock(mutex);
      observed=cv.wait_for(lock,std::chrono::seconds(3),[&] { return entered; }); }
    const auto epoch=f.runtime->GetSessionGeneration();
    f.io->current=false;
    f.runtime->RefreshTransportPresence(); // deliberately busy; must not block
    const auto busy_epoch=f.runtime->GetSessionGeneration();
    { std::lock_guard<std::mutex> lock(mutex); release=true; cv.notify_all(); }
    probe.join();
    Check(observed && busy_epoch==epoch,"busy presence observation must defer without changing epoch");
    Check(!admitted,"removed session admitted by preflight");
    f.runtime->RefreshTransportPresence(); // joined probe proves device lock released
    Check(f.runtime->GetSessionGeneration()>epoch,"idle presence observation failed to invalidate epoch");
    Check(!f.runtime->GetDiagnosticSnapshot().firmware.Supported(),"idle removal retained firmware authority");
    Denied(f);
}
void ReconnectAndConcurrentStress() {
    DWORD baseline=0;
    for(int batch=0;batch<4;++batch) {
        for(int cycle=0;cycle<100;++cycle) {
            Fixture f;
            Check(f.runtime->SetPerKeyActuation(0x0402,1).get(),"stress known rejected");
            // Worker fulfills the promise before its DeviceWriteMutex guard is
            // destroyed. Presence refresh intentionally uses try_lock, so wait
            // for the existing blocking read-only preflight to cross that lock
            // boundary before injecting removal. No mutation/assertion relaxed.
            Check(f.runtime->PrepareTransportSession(),"stress device-lock barrier rejected");
            const auto epoch=f.runtime->GetSessionGeneration();
            f.io->current=false;
            f.runtime->RefreshTransportPresence();
            Check(f.runtime->GetSessionGeneration()>epoch,"reconnect failed to invalidate epoch");
            Check(!f.runtime->GetDiagnosticSnapshot().firmware.Supported(),"stale firmware retained");
            const auto before=f.io->stages.load();
            f.io->report=Basic(0x58);
            auto a=f.runtime->SetPerKeyActuation(0x0402,2);
            auto b=f.runtime->SetGlobalDeadzone(.1,.1);
            Check(!a.get()&&!b.get(),"queued unsupported operations admitted");
            Check(f.io->stages==before && f.io->applies==1,"reconnect emitted writes");
            f.io->report=Basic();
            auto c=f.runtime->SetPerKeyActuation(0x0402,1);
            auto d=f.runtime->SetGlobalDeadzone(.1,.1);
            Check(c.get()&&d.get(),"supported reconnect false rejection");
            f.runtime->Stop(); f.runtime->Stop();
            Check(!f.io->connected && !f.runtime->GetDiagnosticSnapshot().firmware.Supported(),"Stop retained authorization");
            Check(!f.runtime->SetPerKeyActuation(0x0402,1).get(),"Stopped runtime accepted job");
        }
        DWORD handles=0; PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb=sizeof(memory);
        Check(GetProcessHandleCount(GetCurrentProcess(),&handles)!=0,"handle sample failed");
        Check(K32GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory))!=0,"private sample failed");
        if(batch==0) baseline=handles;
        else Check(handles<=baseline+2,"handle growth across offline stress batches");
        std::cout<<"firmware stress cycles="<<(batch+1)*100<<" handles="<<handles<<" private_bytes="<<memory.PrivateUsage<<'\n';
    }
    Fixture concurrent;
    auto a=concurrent.runtime->SetPerKeyActuation(0x0402,1);
    std::thread other([&] { concurrent.runtime->PrepareTransportSession(); });
    const bool success=a.get(); other.join();
    Check(success && concurrent.io->stages==1 && concurrent.io->applies==1,"concurrent preflight interleaved configuration");
}
}
int main(int argc, char** argv) {
    try { Parser(); Windows(); Matrix(); NonBlockingPresenceObservation(); ReconnectAndConcurrentStress(); if(argc==2) Captures(argv[1]); }
    catch(const std::exception& e) { std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n'; return 1; }
    std::cout<<"PASS firmware gate checks="<<checks<<"; no physical HID operations\n";
    return 0;
}
