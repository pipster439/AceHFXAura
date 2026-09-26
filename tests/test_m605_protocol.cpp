#include "aura/hardware/m605_key_mapping.h"
#include "aura/hardware/m605_protocol.h"
#include "aura/hardware/m605_runtime.h"
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace aura {
struct M605RuntimeTestAccess {
    static std::unique_ptr<M605Runtime> Create(
        std::unique_ptr<m605::detail::Transport> transport,
        std::unique_ptr<m605::detail::SettleWait> wait) {
        return std::unique_ptr<M605Runtime>(new M605Runtime(std::move(transport), std::move(wait)));
    }
    static std::unique_ptr<M605Runtime> Create(
        std::unique_ptr<m605::detail::Transport> transport,
        std::unique_ptr<m605::detail::SettleWait> wait,
        std::unique_ptr<m605::detail::SafetyLatch> latch) {
        return std::unique_ptr<M605Runtime>(new M605Runtime(
            std::move(transport), std::move(wait), std::move(latch)));
    }
};
}

namespace {
// Independent Stage 7A expectations. Do not derive this list from the
// production mapping table or the HAL's 600-slot index formula.
constexpr std::array<std::pair<uint16_t, uint16_t>, 68> kExpectedPhysicalKeys{{
    {0x0100, 0x0001}, {0x0600, 0x0002}, {0x0700, 0x0003},
    {0x0101, 0x0004}, {0x0102, 0x0005}, {0x0103, 0x0006},
    {0x0104, 0x0007}, {0x0105, 0x0008}, {0x0106, 0x0009},
    {0x0107, 0x000a}, {0x0108, 0x000b}, {0x0109, 0x000c},
    {0x010a, 0x000d}, {0x0706, 0x000f}, {0x0608, 0x004b},
    {0x0200, 0x0010}, {0x0601, 0x0011}, {0x0701, 0x0012},
    {0x0201, 0x0013}, {0x0202, 0x0014}, {0x0203, 0x0015},
    {0x0204, 0x0016}, {0x0205, 0x0017}, {0x0206, 0x0018},
    {0x0207, 0x0019}, {0x0208, 0x001a}, {0x0209, 0x001b},
    {0x020a, 0x001c}, {0x0607, 0x001d}, {0x0708, 0x004c},
    {0x0300, 0x001e}, {0x0602, 0x001f}, {0x0702, 0x0020},
    {0x0301, 0x0021}, {0x0302, 0x0022}, {0x0303, 0x0023},
    {0x0304, 0x0024}, {0x0305, 0x0025}, {0x0306, 0x0026},
    {0x0307, 0x0027}, {0x0308, 0x0028}, {0x0309, 0x0029},
    {0x0707, 0x002b}, {0x060b, 0x0055},
    {0x0400, 0x002c}, {0x0703, 0x002e}, {0x0401, 0x002f},
    {0x0501, 0x0030}, {0x0402, 0x0031}, {0x0403, 0x0032},
    {0x0404, 0x0033}, {0x0405, 0x0034}, {0x0406, 0x0035},
    {0x0407, 0x0036}, {0x0408, 0x0037}, {0x040a, 0x0039},
    {0x070a, 0x0053}, {0x070b, 0x0056},
    {0x0500, 0x003a}, {0x0604, 0x003b}, {0x0704, 0x003c},
    {0x0503, 0x003d}, {0x0507, 0x003e}, {0x0508, 0x009f},
    {0x050a, 0x0040}, {0x060a, 0x004f}, {0x050b, 0x0054},
    {0x040b, 0x0059},
}};

bool TestExhaustiveVerifiedMapping() {
    if (kExpectedPhysicalKeys.size() != 68 ||
        aura::m605::VerifiedM605PhysicalKeyCount() != 68) return false;
    std::set<uint16_t> logical_ids;
    std::set<uint16_t> wire_ids;
    for (const auto& [logical, wire] : kExpectedPhysicalKeys) {
        if (!logical_ids.insert(logical).second || !wire_ids.insert(wire).second ||
            aura::m605::WireIdForLogicalKey(logical) != wire ||
            aura::m605::SpeedTapWireIdForLogicalKey(logical) != wire ||
            !aura::m605::IsVerifiedM605WireId(wire)) return false;
    }
    if (logical_ids.size() != 68 || wire_ids.size() != 68) return false;
    for (const uint16_t unknown :
         std::array<uint16_t, 8>{0x0000, 0xffff, 0x0001, 0x010b,
                                 0x0409, 0x0509, 0x0609, 0x0705}) {
        if (aura::m605::WireIdForLogicalKey(unknown) ||
            aura::m605::SpeedTapWireIdForLogicalKey(unknown)) return false;
    }
    for (const uint16_t unknown_wire :
         std::array<uint16_t, 4>{0x0000, 0x000e, 0x009e, 0x00ff}) {
        if (aura::m605::IsVerifiedM605WireId(unknown_wire)) return false;
    }
    return aura::m605::WireIdForLogicalKey(0x0508) == 0x009f &&
           aura::m605::WireIdForLogicalKey(0x050a) == 0x0040;
}

class FakeTransport final : public aura::m605::detail::Transport {
public:
    bool stage_succeeds = true;
    bool apply_succeeds = true;
    int fail_stage_call = 0;
    std::function<bool()> stage_precondition;
    bool IsConnected() const override { return connected_; }
    bool Connect() override { Record("connect"); connected_ = true; return true; }
    bool WriteStage(const aura::m605::Report& report) override {
        if (stage_precondition && !stage_precondition()) return false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stage_reports_.push_back(report);
        }
        if (report[2] == 0x2d) Record("stage-analog");
        else if (report[2] == 0x54 && report[3] == 1) Record("stage-rt-press");
        else if (report[2] == 0x54 && report[3] == 2) Record("stage-rt-release");
        else if (report[2] == 0x59) Record("stage-deadzone");
        else if (report[2] == 0x52) Record("stage-reset-all-deadzone");
        else if (report[2] == 0x55) Record(report[9] ? "stage-speedtap-pair-on" : "stage-speedtap-pair-off");
        else if (report[2] == 0x56) Record("stage-speedtap-profile-reset");
        else if (report[2] == 0x57) Record(report[5] ? "stage-speedtap-master-on" : "stage-speedtap-master-off");
        else if (report[2] == 0x23) Record("stage-dks-" + std::to_string(report[10]));
        else if (report[5] == 0x31) Record("stage-v");
        else Record("stage-c");
        std::unique_lock<std::mutex> lock(stage_mutex_);
        const int this_call = ++stage_calls_;
        stage_cv_.notify_all();
        stage_cv_.wait(lock, [this, this_call] {
            return block_after_stage_call_ != this_call || stage_released_;
        });
        return stage_succeeds && this_call != fail_stage_call;
    }
    bool WriteApply(const aura::m605::Report& report) override {
        if (report != aura::m605::BuildRuntimeApply()) return false;
        Record("apply"); return apply_succeeds;
    }
    void Disconnect() override { connected_ = false; Record("disconnect"); }
    std::string GetLastError() const override { return "simulated write failure"; }
    std::vector<std::string> Events() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_;
    }
    std::vector<aura::m605::Report> StageReports() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stage_reports_;
    }
    void RecordTiming(const char* event) { Record(event); }
    void BlockAfterStageCall(int count) {
        std::lock_guard<std::mutex> lock(stage_mutex_);
        block_after_stage_call_ = count;
        stage_released_ = false;
    }
    bool AwaitStageCall(int count) {
        std::unique_lock<std::mutex> lock(stage_mutex_);
        return stage_cv_.wait_for(lock, std::chrono::seconds(3),
                                  [this, count] { return stage_calls_ >= count; });
    }
    void ReleaseStages() {
        std::lock_guard<std::mutex> lock(stage_mutex_);
        stage_released_ = true;
        stage_cv_.notify_all();
    }

private:
    void Record(std::string event) {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(std::move(event));
    }
    bool connected_ = false; // accessed only by the runtime worker
    mutable std::mutex mutex_;
    std::vector<std::string> events_;
    std::vector<aura::m605::Report> stage_reports_;
    std::mutex stage_mutex_;
    std::condition_variable stage_cv_;
    int stage_calls_ = 0;
    int block_after_stage_call_ = 0;
    bool stage_released_ = false;
};

enum class BlockPhase { None, BetweenDksStages, BeforeApply, AfterApply };

class FakeWait final : public aura::m605::detail::SettleWait {
public:
    FakeWait(FakeTransport* io, BlockPhase block) : io_(io), block_(block) {}
    void WaitBetweenDksStages() override { WaitAt(BlockPhase::BetweenDksStages); }
    void WaitBeforeApply() override { WaitAt(BlockPhase::BeforeApply); }
    void WaitAfterApply() override { WaitAt(BlockPhase::AfterApply); }
    bool AwaitBetween(int count) { return Await(count, BlockPhase::BetweenDksStages); }
    int BetweenCount() const { return Count(BlockPhase::BetweenDksStages); }
    bool AwaitBefore(int count) { return Await(count, BlockPhase::BeforeApply); }
    bool AwaitAfter(int count) { return Await(count, BlockPhase::AfterApply); }
    int BeforeCount() const { return Count(BlockPhase::BeforeApply); }
    int AfterCount() const { return Count(BlockPhase::AfterApply); }
    void BlockOn(BlockPhase phase) {
        std::lock_guard<std::mutex> lock(mutex_);
        block_ = phase;
        released_ = false;
    }
    void Release() {
        std::lock_guard<std::mutex> lock(mutex_);
        released_ = true;
        cv_.notify_all();
    }

private:
    void WaitAt(BlockPhase phase) {
        io_->RecordTiming(phase == BlockPhase::BetweenDksStages ? "dks-wait" :
                          phase == BlockPhase::BeforeApply ? "pre-wait" : "post-wait");
        std::unique_lock<std::mutex> lock(mutex_);
        if (phase == BlockPhase::BetweenDksStages) ++between_count_;
        else if (phase == BlockPhase::BeforeApply) ++before_count_;
        else ++after_count_;
        cv_.notify_all();
        cv_.wait(lock, [this, phase] { return block_ != phase || released_; });
    }
    bool Await(int count, BlockPhase phase) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, std::chrono::seconds(3), [this, count, phase] {
            return (phase == BlockPhase::BetweenDksStages ? between_count_ :
                    phase == BlockPhase::BeforeApply ? before_count_ : after_count_) >= count;
        });
    }
    int Count(BlockPhase phase) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return phase == BlockPhase::BetweenDksStages ? between_count_ :
               phase == BlockPhase::BeforeApply ? before_count_ : after_count_;
    }

    FakeTransport* io_;
    BlockPhase block_ = BlockPhase::None;
    bool released_ = false;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    int between_count_ = 0;
    int before_count_ = 0;
    int after_count_ = 0;
};

struct LatchState { bool armed = false; int arms = 0; int clears = 0; };
class FakeLatch final : public aura::m605::detail::SafetyLatch {
public:
    explicit FakeLatch(std::shared_ptr<LatchState> state) : state_(std::move(state)) {}
    bool IsQuarantined() const override { return state_->armed; }
    bool Arm() override {
        if (state_->armed) return false;
        state_->armed = true;
        ++state_->arms;
        return true;
    }
    bool Clear() override {
        if (!state_->armed) return false;
        state_->armed = false;
        ++state_->clears;
        return true;
    }
private:
    std::shared_ptr<LatchState> state_;
};

struct Fixture {
    FakeTransport* io;
    FakeWait* wait;
    std::unique_ptr<aura::M605Runtime> runtime;
};

Fixture MakeFixture(BlockPhase block = BlockPhase::None) {
    auto io = std::make_unique<FakeTransport>();
    auto wait = std::make_unique<FakeWait>(io.get(), block);
    auto* io_ptr = io.get();
    auto* wait_ptr = wait.get();
    return {io_ptr, wait_ptr, aura::M605RuntimeTestAccess::Create(std::move(io), std::move(wait))};
}

bool TestPersistentSafetyLatch() {
    const auto state = std::make_shared<LatchState>();
    auto make = [&](BlockPhase block) {
        auto io = std::make_unique<FakeTransport>();
        io->stage_precondition = [state] { return state->armed; };
        auto wait = std::make_unique<FakeWait>(io.get(), block);
        auto* io_ptr = io.get();
        auto* wait_ptr = wait.get();
        return Fixture{io_ptr, wait_ptr, aura::M605RuntimeTestAccess::Create(
            std::move(io), std::move(wait), std::make_unique<FakeLatch>(state))};
    };
    {
        auto fixture = make(BlockPhase::AfterApply);
        if (fixture.runtime->SetPerKeyActuation(0xffff, 1.0).get() || state->arms != 0)
            return false; // preflight never arms
        auto result = fixture.runtime->SetPerKeyActuation(0x0402, 1.0);
        if (!fixture.wait->AwaitAfter(1) || !state->armed || state->arms != 1 ||
            state->clears != 0 || !fixture.runtime->GetAppliedRuntimeState().per_key_actuation_raw.empty())
            return false;
        if (fixture.runtime->GetHealth() != aura::M605RuntimeHealth::TransactionInProgress ||
            fixture.runtime->IsPersistentSafetyQuarantined())
            return false;
        fixture.wait->Release();
        if (!result.get() || state->armed || state->clears != 1 ||
            fixture.runtime->GetHealth() != aura::M605RuntimeHealth::Clean ||
            fixture.runtime->IsPersistentSafetyQuarantined()) return false;
    }
    {
        auto fixture = make(BlockPhase::None);
        fixture.io->stage_succeeds = false;
        if (fixture.runtime->SetPerKeyActuation(0x0402, 1.0).get() ||
            !state->armed || state->arms != 2 || state->clears != 1 ||
            fixture.runtime->GetHealth() != aura::M605RuntimeHealth::IndeterminateStagedState ||
            !fixture.runtime->IsPersistentSafetyQuarantined())
            return false;
    }
    {
        auto fixture = make(BlockPhase::None); // simulated daemon restart
        if (fixture.runtime->GetHealth() != aura::M605RuntimeHealth::PersistentSafetyQuarantine ||
            !fixture.runtime->IsPersistentSafetyQuarantined() ||
            fixture.runtime->SetAnalogEffect(0, true).get() || !fixture.io->Events().empty() ||
            !fixture.runtime->AcknowledgeExternalResynchronization() || state->armed ||
            fixture.runtime->GetHealth() != aura::M605RuntimeHealth::Clean ||
            fixture.runtime->IsPersistentSafetyQuarantined())
            return false;
        fixture.io->apply_succeeds = false;
        if (fixture.runtime->SetAnalogEffect(0, true).get() || !state->armed ||
            fixture.runtime->GetHealth() != aura::M605RuntimeHealth::IndeterminateStagedState ||
            !fixture.runtime->IsPersistentSafetyQuarantined())
            return false;
    }
    return true;
}

struct ArmControlLatch final : public aura::m605::detail::SafetyLatch {
    bool allow_arm = false;
    bool armed = false;
    int arms = 0;
    int arm_attempts = 0;
    bool IsQuarantined() const override { return armed; }
    bool Arm() override {
        ++arm_attempts;
        if (!allow_arm || armed) return false;
        armed = true;
        ++arms;
        return true;
    }
    bool Clear() override {
        if (!armed) return false;
        armed = false;
        return true;
    }
};

bool TestSafetyLatchArmFailure() {
    auto io = std::make_unique<FakeTransport>();
    auto* io_ptr = io.get();
    auto wait = std::make_unique<FakeWait>(io_ptr, BlockPhase::None);
    auto latch = std::make_unique<ArmControlLatch>();
    auto* latch_ptr = latch.get();

    latch_ptr->allow_arm = true;
    auto runtime = aura::M605RuntimeTestAccess::Create(
        std::move(io), std::move(wait), std::move(latch));

    // 1. Establish prior known shadow with working latch
    if (!runtime->SetPerKeyActuation(0x0402, 1.0).get()) return false;
    const auto shadow_before = runtime->GetAppliedRuntimeState();
    if (shadow_before.per_key_actuation_raw.count(0x0402) == 0 ||
        shadow_before.per_key_actuation_raw.at(0x0402) != 10 ||
        runtime->GetHealth() != aura::M605RuntimeHealth::Clean ||
        runtime->IsPersistentSafetyQuarantined())
        return false;

    const size_t stages_before = io_ptr->StageReports().size();
    const size_t events_before = io_ptr->Events().size();

    // 2. Disallow arm to simulate latch arm failure
    latch_ptr->allow_arm = false;
    auto failed_future = runtime->SetPerKeyActuation(0x0501, 2.0);
    const bool result = failed_future.get();
    if (result != false) return false; // future == false

    // Verify:
    // - WriteStage count == 0
    if (io_ptr->StageReports().size() != stages_before) return false;
    // - Apply count == 0 (no new event at all)
    if (io_ptr->Events().size() != events_before) return false;
    // - health ends Clean
    if (runtime->GetHealth() != aura::M605RuntimeHealth::Clean) return false;
    // - persistent safety quarantine is false
    if (runtime->IsPersistentSafetyQuarantined()) return false;
    // - prior known shadow remains intact
    const auto shadow_after = runtime->GetAppliedRuntimeState();
    if (shadow_after.per_key_actuation_raw != shadow_before.per_key_actuation_raw) return false;
    // - last_error explicitly states that the durable safety latch could not be established and NO device write was attempted
    const auto last_err = runtime->GetLastError();
    if (last_err.find("Durable safety latch could not be established; no device write was attempted") == std::string::npos)
        return false;

    // 3. Subsequent transaction may retry if latch later becomes available
    latch_ptr->allow_arm = true;
    auto retry_future = runtime->SetPerKeyActuation(0x0501, 2.0);
    if (!retry_future.get()) return false;
    if (runtime->GetHealth() != aura::M605RuntimeHealth::Clean) return false;
    const auto shadow_retry = runtime->GetAppliedRuntimeState();
    if (shadow_retry.per_key_actuation_raw.count(0x0501) == 0 ||
        shadow_retry.per_key_actuation_raw.at(0x0501) != 20)
        return false;

    return true;
}

bool Check(const aura::m605::Report& actual, const aura::m605::Report& expected,
           const char* label) {
    if (actual.size() != 65 || actual != expected) {
        std::cerr << "FAIL: " << label << "\n";
        return false;
    }
    return true;
}

bool TestSuccessfulTransactionAndShadow() {
    auto fixture = MakeFixture(BlockPhase::BeforeApply);
    auto result = fixture.runtime->SetPerKeyActuation(0x0402, 4.0);
    const bool entered = fixture.wait->AwaitBefore(1);
    const bool before_apply = entered &&
        fixture.runtime->GetHealth() == aura::M605RuntimeHealth::TransactionInProgress &&
        fixture.runtime->GetAppliedRuntimeState().per_key_actuation_raw.empty() &&
        fixture.io->Events() == std::vector<std::string>{"connect", "stage-v", "pre-wait"};
    fixture.wait->Release();
    const bool committed = result.get();
    const auto state = fixture.runtime->GetAppliedRuntimeState();
    return before_apply && committed && fixture.wait->BeforeCount() == 1 &&
        fixture.wait->AfterCount() == 1 &&
        fixture.io->Events() == std::vector<std::string>{
            "connect", "stage-v", "pre-wait", "apply", "post-wait"} &&
        state.per_key_actuation_raw.at(0x0402) == 40 &&
        fixture.runtime->GetHealth() == aura::M605RuntimeHealth::Clean;
}

bool TestPostApplyBoundaryAndRgbSerialization() {
    auto fixture = MakeFixture(BlockPhase::AfterApply);
    auto first = fixture.runtime->SetPerKeyActuation(0x0402, 4.0);
    const bool entered = fixture.wait->AwaitAfter(1);
    auto second = fixture.runtime->SetAnalogEffect(0, true);
    const bool pending = entered &&
        fixture.runtime->GetHealth() == aura::M605RuntimeHealth::TransactionInProgress &&
        fixture.runtime->GetAppliedRuntimeState().per_key_actuation_raw.empty() &&
        first.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout &&
        second.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout &&
        fixture.io->Events() == std::vector<std::string>{
            "connect", "stage-v", "pre-wait", "apply", "post-wait"};

    // PushFrame takes the same lock before checking the disconnected backend.
    // This call can never write hardware, but must wait behind the transaction.
    aura::NativeHidBackend disconnected_lighting;
    aura::FrameBuffer frame;
    std::promise<void> push_started;
    auto started = push_started.get_future();
    std::promise<bool> push_done;
    auto pushed = push_done.get_future();
    std::thread lighting([&] {
        push_started.set_value();
        push_done.set_value(disconnected_lighting.PushFrame(frame, {1}));
    });
    started.wait();
    const bool rgb_blocked = pushed.wait_for(std::chrono::milliseconds(30)) ==
        std::future_status::timeout;

    fixture.wait->Release();
    const bool both_committed = first.get() && second.get();
    lighting.join();
    const auto state = fixture.runtime->GetAppliedRuntimeState();
    return pending && rgb_blocked && both_committed && !pushed.get() &&
        state.per_key_actuation_raw.at(0x0402) == 40 &&
        state.static_analog_effect == true &&
        fixture.wait->BeforeCount() == 2 && fixture.wait->AfterCount() == 2;
}

bool TestPriorShadowUnchangedUntilPostSettleFinishes() {
    auto fixture = MakeFixture();
    if (!fixture.runtime->SetPerKeyActuation(0x0402, 1.0).get()) return false;
    const auto prior = fixture.runtime->GetAppliedRuntimeState();
    if (prior.per_key_actuation_raw.at(0x0402) != 10) return false;

    fixture.wait->BlockOn(BlockPhase::AfterApply);
    auto update = fixture.runtime->SetPerKeyActuation(0x0402, 4.0);
    const bool entered = fixture.wait->AwaitAfter(2);
    const auto during = fixture.runtime->GetAppliedRuntimeState();
    const bool unchanged = entered && during.per_key_actuation_raw == prior.per_key_actuation_raw &&
        update.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout;
    fixture.wait->Release();
    const bool succeeded = update.get();
    const auto after = fixture.runtime->GetAppliedRuntimeState();
    return unchanged && succeeded && after.per_key_actuation_raw.at(0x0402) == 40 &&
        fixture.wait->BeforeCount() == 2 && fixture.wait->AfterCount() == 2;
}

bool TestStageFailure() {
    auto fixture = MakeFixture();
    fixture.io->stage_succeeds = false;
    const bool succeeded = fixture.runtime->SetPerKeyActuation(0x0402, 1.0).get();
    return !succeeded && fixture.wait->BeforeCount() == 0 &&
        fixture.wait->AfterCount() == 0 &&
        fixture.io->Events() == std::vector<std::string>{"connect", "stage-v", "disconnect"} &&
        fixture.runtime->GetHealth() == aura::M605RuntimeHealth::IndeterminateStagedState &&
        fixture.runtime->GetAppliedRuntimeState().per_key_actuation_raw.empty();
}

bool TestApplyFailureLocksRuntime() {
    auto fixture = MakeFixture();
    fixture.io->apply_succeeds = false;
    const bool succeeded = fixture.runtime->SetPerKeyActuation(0x0402, 4.0).get();
    const auto before_rejections = fixture.io->Events();
    const bool actuation_rejected = !fixture.runtime->SetPerKeyActuation(0x0501, 1.0).get();
    const bool analog_rejected = !fixture.runtime->SetAnalogEffect(0, true).get();
    return !succeeded && fixture.wait->BeforeCount() == 1 &&
        fixture.wait->AfterCount() == 0 &&
        before_rejections == std::vector<std::string>{
            "connect", "stage-v", "pre-wait", "apply", "disconnect"} &&
        fixture.io->Events() == before_rejections && actuation_rejected && analog_rejected &&
        fixture.runtime->GetHealth() == aura::M605RuntimeHealth::IndeterminateStagedState &&
        fixture.runtime->GetAppliedRuntimeState().per_key_actuation_raw.empty() &&
        !fixture.runtime->GetAppliedRuntimeState().static_analog_effect.has_value() &&
        fixture.runtime->GetLastError().find("unknown") != std::string::npos;
}

bool TestFailureInvalidatesEarlierShadowAndQueuedWork() {
    auto fixture = MakeFixture();
    if (!fixture.runtime->SetPerKeyActuation(0x0402, 1.0).get()) return false;
    if (fixture.runtime->GetAppliedRuntimeState().per_key_actuation_raw.at(0x0402) != 10) {
        return false;
    }
    fixture.io->apply_succeeds = false;
    const bool failed = !fixture.runtime->SetAnalogEffect(0, true).get();
    const auto state = fixture.runtime->GetAppliedRuntimeState();
    return failed && state.per_key_actuation_raw.empty() &&
        !state.static_analog_effect.has_value() &&
        fixture.runtime->GetHealth() == aura::M605RuntimeHealth::IndeterminateStagedState;
}

bool TestFailureCancelsQueuedWork() {
    auto fixture = MakeFixture(BlockPhase::BeforeApply);
    fixture.io->apply_succeeds = false;
    auto first = fixture.runtime->SetPerKeyActuation(0x0402, 4.0);
    const bool entered = fixture.wait->AwaitBefore(1);
    auto queued = fixture.runtime->SetAnalogEffect(0, true);
    fixture.wait->Release();
    const bool failed = !first.get() && !queued.get();
    return entered && failed && fixture.wait->BeforeCount() == 1 &&
        fixture.wait->AfterCount() == 0 &&
        fixture.io->Events() ==
            std::vector<std::string>{"connect", "stage-v", "pre-wait", "apply", "disconnect"};
}

bool TestFifo() {
    auto fixture = MakeFixture(BlockPhase::BeforeApply);
    auto first = fixture.runtime->SetPerKeyActuation(0x0402, 4.0);
    const bool entered = fixture.wait->AwaitBefore(1);
    auto second = fixture.runtime->SetAnalogEffect(0, true);
    auto third = fixture.runtime->SetPerKeyActuation(0x0501, 1.0);
    fixture.wait->Release();
    const bool all_succeeded = first.get() && second.get() && third.get();
    return entered && all_succeeded && fixture.wait->BeforeCount() == 3 &&
        fixture.wait->AfterCount() == 3 &&
        fixture.io->Events() == std::vector<std::string>{
            "connect", "stage-v", "pre-wait", "apply", "post-wait",
            "stage-analog", "pre-wait", "apply", "post-wait",
            "stage-c", "pre-wait", "apply", "post-wait"};
}

bool TestShutdownCancelsQueuedWork() {
    auto fixture = MakeFixture(BlockPhase::AfterApply);
    auto in_flight = fixture.runtime->SetPerKeyActuation(0x0402, 4.0);
    const bool entered = fixture.wait->AwaitAfter(1);
    auto queued_actuation = fixture.runtime->SetPerKeyActuation(0x0501, 1.0);
    auto queued_analog = fixture.runtime->SetAnalogEffect(0, true);
    std::thread stopper([&] { fixture.runtime->Stop(); });
    const bool cancelled =
        queued_actuation.wait_for(std::chrono::seconds(3)) == std::future_status::ready &&
        queued_analog.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
    const bool stopped_during_post_wait =
        fixture.io->Events() == std::vector<std::string>{
            "connect", "stage-v", "pre-wait", "apply", "post-wait"} &&
        in_flight.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout;
    fixture.wait->Release();
    stopper.join();
    return entered && cancelled && !queued_actuation.get() && !queued_analog.get() &&
        stopped_during_post_wait && in_flight.get() &&
        fixture.wait->BeforeCount() == 1 && fixture.wait->AfterCount() == 1 &&
        fixture.io->Events() == std::vector<std::string>{
            "connect", "stage-v", "pre-wait", "apply", "post-wait"} &&
        fixture.runtime->GetHealth() == aura::M605RuntimeHealth::Stopped &&
        !fixture.runtime->SetAnalogEffect(0, false).get();
}

bool TestDestructorCancelsQueuedWork() {
    auto fixture = MakeFixture(BlockPhase::BeforeApply);
    auto in_flight = fixture.runtime->SetPerKeyActuation(0x0402, 4.0);
    const bool entered = fixture.wait->AwaitBefore(1);
    auto queued = fixture.runtime->SetAnalogEffect(0, true);
    std::thread destroyer([runtime = std::move(fixture.runtime)]() mutable {
        runtime.reset();
    });
    const bool cancelled = queued.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
    const bool before_release = fixture.io->Events() ==
        std::vector<std::string>{"connect", "stage-v", "pre-wait"};
    fixture.wait->Release();
    destroyer.join(); // Fake transport and waiter are now destroyed.
    return entered && cancelled && !queued.get() && before_release && in_flight.get();
}

bool TestInvalidInputsDoNotTouchTransport() {
    auto fixture = MakeFixture();
    const bool rejected = !fixture.runtime->SetPerKeyActuation(0x0409, 1.0).get() &&
        !fixture.runtime->SetPerKeyActuation(0x0402, 4.1).get() &&
        !fixture.runtime->SetAnalogEffect(1, true).get() &&
        !fixture.runtime->SetPerKeyRapidTrigger(0x0409, 0.8, 0.6).get() &&
        !fixture.runtime->SetPerKeyRapidTrigger(0x0402, 0.0, 0.6).get() &&
        !fixture.runtime->SetPerKeyRapidTrigger(0x0402, 0.8, 2.6).get() &&
        !fixture.runtime->DisablePerKeyRapidTrigger(0x0402, 0.4, 0.0).get() &&
        !fixture.runtime->SetPerKeyDeadzone(0x0409, 0.2, 0.3).get() &&
        !fixture.runtime->SetPerKeyDeadzone(0x0402, 0.6, 0.3).get() &&
        !fixture.runtime->SetPerKeyDeadzone(0x0402, 0.2, -0.1).get() &&
        !fixture.runtime->ResetAllPerKeyDeadzoneOverrides(6, 0).get() &&
        !fixture.runtime->ResetAllPerKeyDeadzoneOverrides(1, 6).get() &&
        !fixture.runtime->ResetAllPerKeyDeadzoneOverrides(1, 0, 1).get() &&
        !fixture.runtime->SetSpeedTapPair(0x0602, 0x0602).get() &&
        !fixture.runtime->SetSpeedTapPair(0x0602, 0x0705).get() &&
        !fixture.runtime->DisableSpeedTapPair(0x0705, 0x0301).get();
    return rejected && fixture.io->Events().empty() && fixture.wait->BeforeCount() == 0 &&
        fixture.wait->AfterCount() == 0 &&
        fixture.runtime->GetHealth() == aura::M605RuntimeHealth::Clean;
}

bool TestRapidTriggerFirstStageBoundary() {
    auto fixture = MakeFixture();
    fixture.io->BlockAfterStageCall(1);
    auto result = fixture.runtime->SetPerKeyRapidTrigger(0x0402, 0.8, 0.6);
    const bool first_stage = fixture.io->AwaitStageCall(1) &&
        fixture.io->Events() == std::vector<std::string>{"connect", "stage-rt-press"} &&
        fixture.wait->BeforeCount() == 0 && fixture.wait->AfterCount() == 0 &&
        fixture.runtime->GetAppliedRuntimeState().per_key_rapid_trigger.empty() &&
        result.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout;
    fixture.io->ReleaseStages();
    const bool success = result.get();
    return first_stage && success && fixture.io->Events() == std::vector<std::string>{
        "connect", "stage-rt-press", "stage-rt-release", "pre-wait", "apply", "post-wait"};
}

bool TestRapidTriggerSecondStageAndPostApplyBoundary() {
    auto fixture = MakeFixture(BlockPhase::AfterApply);
    fixture.io->BlockAfterStageCall(2);
    auto result = fixture.runtime->SetPerKeyRapidTrigger(0x0402, 0.8, 0.6);
    const bool second_stage = fixture.io->AwaitStageCall(2) &&
        fixture.io->Events() == std::vector<std::string>{
            "connect", "stage-rt-press", "stage-rt-release"} &&
        fixture.wait->BeforeCount() == 0 &&
        fixture.runtime->GetAppliedRuntimeState().per_key_rapid_trigger.empty();
    fixture.io->ReleaseStages();
    const bool after_apply = fixture.wait->AwaitAfter(1) &&
        fixture.io->Events() == std::vector<std::string>{
            "connect", "stage-rt-press", "stage-rt-release", "pre-wait", "apply", "post-wait"} &&
        fixture.wait->BeforeCount() == 1 && fixture.wait->AfterCount() == 1 &&
        fixture.runtime->GetAppliedRuntimeState().per_key_rapid_trigger.empty() &&
        result.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout;
    fixture.wait->Release();
    const bool success = result.get();
    const auto state = fixture.runtime->GetAppliedRuntimeState();
    const auto rt = state.per_key_rapid_trigger.at(0x0402);
    return second_stage && after_apply && success && rt.enabled &&
        rt.press_raw == 8 && rt.release_raw == 6 &&
        fixture.runtime->GetHealth() == aura::M605RuntimeHealth::Clean;
}

bool TestRapidTriggerDisableAndFifo() {
    auto fixture = MakeFixture(BlockPhase::BeforeApply);
    auto enabled = fixture.runtime->SetPerKeyRapidTrigger(0x0402, 0.8, 0.6);
    const bool entered = fixture.wait->AwaitBefore(1);
    auto disabled = fixture.runtime->DisablePerKeyRapidTrigger(0x0402, 0.4, 0.2);
    auto deadzone = fixture.runtime->SetPerKeyDeadzone(0x0402, 0.2, 0.3);
    const bool queued = fixture.io->Events() == std::vector<std::string>{
        "connect", "stage-rt-press", "stage-rt-release", "pre-wait"};
    fixture.wait->Release();
    const bool all_succeeded = enabled.get() && disabled.get() && deadzone.get();
    const auto state = fixture.runtime->GetAppliedRuntimeState();
    const auto rt = state.per_key_rapid_trigger.at(0x0402);
    const auto dz = state.per_key_deadzone.at(0x0402);
    return entered && queued && all_succeeded && !rt.enabled &&
        rt.press_raw == 4 && rt.release_raw == 2 &&
        dz.top_raw == 2 && dz.bottom_raw == 3 &&
        fixture.wait->BeforeCount() == 3 && fixture.wait->AfterCount() == 3 &&
        fixture.io->Events() == std::vector<std::string>{
            "connect", "stage-rt-press", "stage-rt-release", "pre-wait", "apply", "post-wait",
            "stage-rt-press", "stage-rt-release", "pre-wait", "apply", "post-wait",
            "stage-deadzone", "pre-wait", "apply", "post-wait"};
}

bool TestRapidTriggerStageFailures() {
    for (int failed_stage : {1, 2}) {
        auto fixture = MakeFixture();
        fixture.io->fail_stage_call = failed_stage;
        const bool success = fixture.runtime->SetPerKeyRapidTrigger(0x0402, 0.8, 0.6).get();
        const auto expected = failed_stage == 1
            ? std::vector<std::string>{"connect", "stage-rt-press", "disconnect"}
            : std::vector<std::string>{
                "connect", "stage-rt-press", "stage-rt-release", "disconnect"};
        if (success || fixture.io->Events() != expected ||
            fixture.wait->BeforeCount() != 0 || fixture.wait->AfterCount() != 0 ||
            fixture.runtime->GetHealth() != aura::M605RuntimeHealth::IndeterminateStagedState ||
            !fixture.runtime->GetAppliedRuntimeState().per_key_rapid_trigger.empty() ||
            fixture.runtime->SetPerKeyDeadzone(0x0402, 0.2, 0.3).get()) return false;
    }
    return true;
}

bool TestRapidTriggerApplyFailureInvalidatesShadow() {
    auto fixture = MakeFixture();
    if (!fixture.runtime->SetPerKeyDeadzone(0x0402, 0.2, 0.3).get()) return false;
    fixture.io->apply_succeeds = false;
    const bool success = fixture.runtime->SetPerKeyRapidTrigger(0x0402, 0.8, 0.6).get();
    const auto state = fixture.runtime->GetAppliedRuntimeState();
    return !success && state.per_key_deadzone.empty() && state.per_key_rapid_trigger.empty() &&
        fixture.runtime->GetHealth() == aura::M605RuntimeHealth::IndeterminateStagedState &&
        fixture.wait->BeforeCount() == 2 && fixture.wait->AfterCount() == 1 &&
        fixture.io->Events() == std::vector<std::string>{
            "connect", "stage-deadzone", "pre-wait", "apply", "post-wait",
            "stage-rt-press", "stage-rt-release", "pre-wait", "apply", "disconnect"};
}

bool TestResetAllDeadzoneShadowBoundary() {
    auto fixture = MakeFixture();
    if (!fixture.runtime->SetPerKeyDeadzone(0x0402, 0.2, 0.3).get()) return false;
    fixture.wait->BlockOn(BlockPhase::AfterApply);
    auto reset = fixture.runtime->ResetAllPerKeyDeadzoneOverrides(1, 0);
    const bool post_wait = fixture.wait->AwaitAfter(2) &&
        fixture.runtime->GetAppliedRuntimeState().per_key_deadzone.at(0x0402).bottom_raw == 3 &&
        reset.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout &&
        fixture.io->Events() == std::vector<std::string>{
            "connect", "stage-deadzone", "pre-wait", "apply", "post-wait",
            "stage-reset-all-deadzone", "pre-wait", "apply", "post-wait"};
    fixture.wait->Release();
    return post_wait && reset.get() &&
        fixture.runtime->GetAppliedRuntimeState().per_key_deadzone.empty() &&
        fixture.runtime->GetHealth() == aura::M605RuntimeHealth::Clean;
}

bool TestResetAllDeadzoneFailureInvalidatesShadow() {
    auto fixture = MakeFixture();
    if (!fixture.runtime->SetPerKeyDeadzone(0x0402, 0.2, 0.3).get()) return false;
    fixture.io->apply_succeeds = false;
    const bool success = fixture.runtime->ResetAllPerKeyDeadzoneOverrides(1, 0).get();
    return !success && fixture.runtime->GetAppliedRuntimeState().per_key_deadzone.empty() &&
        fixture.runtime->GetHealth() == aura::M605RuntimeHealth::IndeterminateStagedState &&
        fixture.wait->BeforeCount() == 2 && fixture.wait->AfterCount() == 1;
}

bool TestStopDuringRapidTriggerPostWait() {
    auto fixture = MakeFixture(BlockPhase::AfterApply);
    auto in_flight = fixture.runtime->SetPerKeyRapidTrigger(0x0402, 0.8, 0.6);
    const bool entered = fixture.wait->AwaitAfter(1);
    auto queued = fixture.runtime->ResetAllPerKeyDeadzoneOverrides(1, 0);
    std::thread stopper([&] { fixture.runtime->Stop(); });
    const bool cancelled = queued.wait_for(std::chrono::seconds(3)) == std::future_status::ready &&
        !queued.get();
    const bool still_in_flight =
        in_flight.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout &&
        fixture.io->Events() == std::vector<std::string>{
            "connect", "stage-rt-press", "stage-rt-release", "pre-wait", "apply", "post-wait"};
    fixture.wait->Release();
    stopper.join();
    return entered && cancelled && still_in_flight && in_flight.get() &&
        fixture.runtime->GetHealth() == aura::M605RuntimeHealth::Stopped &&
        fixture.io->Events() == std::vector<std::string>{
            "connect", "stage-rt-press", "stage-rt-release", "pre-wait", "apply", "post-wait"};
}

bool TestSpeedTapPairPostSettleAndTargetedDisable() {
    auto fixture = MakeFixture(BlockPhase::AfterApply);
    auto first = fixture.runtime->SetSpeedTapPair(0x0602, 0x0301); // A+D
    const bool entered = fixture.wait->AwaitAfter(1);
    const bool pending = entered &&
        fixture.runtime->GetAppliedRuntimeState().speedtap_pair_submissions.empty() &&
        first.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout &&
        fixture.io->Events() == std::vector<std::string>{
            "connect", "stage-speedtap-pair-on", "pre-wait", "apply", "post-wait"};
    fixture.wait->Release();
    if (!pending || !first.get()) return false;
    if (!fixture.runtime->SetSpeedTapPair(0x0701, 0x0702).get() ||
        !fixture.runtime->DisableSpeedTapPair(0x0701, 0x0702).get()) return false;
    const auto state = fixture.runtime->GetAppliedRuntimeState();
    return state.speedtap_pair_submissions.size() == 2 &&
        state.speedtap_pair_submissions.at({0x0602, 0x0301}) &&
        !state.speedtap_pair_submissions.at({0x0701, 0x0702}) &&
        !state.speedtap_master.has_value() &&
        state.speedtap_pair_knowledge ==
            aura::M605AppliedRuntimeState::SpeedTapPairKnowledge::Unknown &&
        fixture.wait->BeforeCount() == 3 && fixture.wait->AfterCount() == 3 &&
        fixture.io->Events() == std::vector<std::string>{
            "connect", "stage-speedtap-pair-on", "pre-wait", "apply", "post-wait",
            "stage-speedtap-pair-on", "pre-wait", "apply", "post-wait",
            "stage-speedtap-pair-off", "pre-wait", "apply", "post-wait"};
}

bool TestSpeedTapProfileResetPreservesMasterKnowledge() {
    auto fixture = MakeFixture();
    if (!fixture.runtime->SetSpeedTapPair(0x0602, 0x0301).get() ||
        !fixture.runtime->SetSpeedTapPair(0x0701, 0x0702).get() ||
        !fixture.runtime->SetSpeedTapMaster(true).get()) return false;
    fixture.wait->BlockOn(BlockPhase::AfterApply);
    auto reset = fixture.runtime->ResetSpeedTapRuntimeToProfile();
    const bool during = fixture.wait->AwaitAfter(4) &&
        fixture.runtime->GetAppliedRuntimeState().speedtap_pair_submissions.size() == 2 &&
        fixture.runtime->GetAppliedRuntimeState().speedtap_master == true &&
        reset.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout;
    fixture.wait->Release();
    if (!during || !reset.get()) return false;
    const auto after_reset = fixture.runtime->GetAppliedRuntimeState();
    if (!after_reset.speedtap_pair_submissions.empty() ||
        after_reset.speedtap_pair_knowledge !=
            aura::M605AppliedRuntimeState::SpeedTapPairKnowledge::ProfileBaselineUnknown ||
        after_reset.speedtap_master != true) return false;
    if (!fixture.runtime->SetSpeedTapMaster(false).get()) return false;
    const auto final = fixture.runtime->GetAppliedRuntimeState();
    return final.speedtap_pair_submissions.empty() && final.speedtap_master == false &&
        final.speedtap_pair_knowledge ==
            aura::M605AppliedRuntimeState::SpeedTapPairKnowledge::ProfileBaselineUnknown &&
        fixture.wait->BeforeCount() == 5 && fixture.wait->AfterCount() == 5 &&
        fixture.io->Events() == std::vector<std::string>{
            "connect", "stage-speedtap-pair-on", "pre-wait", "apply", "post-wait",
            "stage-speedtap-pair-on", "pre-wait", "apply", "post-wait",
            "stage-speedtap-master-on", "pre-wait", "apply", "post-wait",
            "stage-speedtap-profile-reset", "pre-wait", "apply", "post-wait",
            "stage-speedtap-master-off", "pre-wait", "apply", "post-wait"};
}

bool TestSpeedTapMasterPostSettleBoundary() {
    auto fixture = MakeFixture(BlockPhase::AfterApply);
    auto master = fixture.runtime->SetSpeedTapMaster(true);
    const bool entered = fixture.wait->AwaitAfter(1);
    const auto during = fixture.runtime->GetAppliedRuntimeState();
    const bool pending = entered && !during.speedtap_master.has_value() &&
        during.speedtap_pair_submissions.empty() &&
        master.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout &&
        fixture.io->Events() == std::vector<std::string>{
            "connect", "stage-speedtap-master-on", "pre-wait", "apply", "post-wait"};
    fixture.wait->Release();
    if (!pending || !master.get()) return false;
    const auto after = fixture.runtime->GetAppliedRuntimeState();
    return after.speedtap_master == true && after.speedtap_pair_submissions.empty() &&
        fixture.wait->BeforeCount() == 1 && fixture.wait->AfterCount() == 1;
}

bool TestSpeedTapStageAndApplyFailures() {
    {
        auto fixture = MakeFixture();
        fixture.io->stage_succeeds = false;
        const bool success = fixture.runtime->SetSpeedTapPair(0x0602, 0x0301).get();
        if (success || fixture.wait->BeforeCount() != 0 || fixture.wait->AfterCount() != 0 ||
            fixture.runtime->GetHealth() != aura::M605RuntimeHealth::IndeterminateStagedState ||
            fixture.io->Events() != std::vector<std::string>{
                "connect", "stage-speedtap-pair-on", "disconnect"} ||
            fixture.runtime->SetSpeedTapMaster(true).get()) return false;
    }
    {
        auto fixture = MakeFixture();
        if (!fixture.runtime->SetSpeedTapPair(0x0602, 0x0301).get()) return false;
        fixture.io->apply_succeeds = false;
        const bool success = fixture.runtime->ResetSpeedTapRuntimeToProfile().get();
        const auto state = fixture.runtime->GetAppliedRuntimeState();
        if (success || !state.speedtap_pair_submissions.empty() ||
            state.speedtap_pair_knowledge !=
                aura::M605AppliedRuntimeState::SpeedTapPairKnowledge::Unknown ||
            state.speedtap_master.has_value() ||
            fixture.wait->BeforeCount() != 2 || fixture.wait->AfterCount() != 1 ||
            fixture.runtime->GetHealth() != aura::M605RuntimeHealth::IndeterminateStagedState ||
            fixture.runtime->SetSpeedTapPair(0x0701, 0x0702).get()) return false;
    }
    return true;
}

bool TestSpeedTapFifoAndStop() {
    auto fixture = MakeFixture(BlockPhase::AfterApply);
    auto in_flight = fixture.runtime->SetSpeedTapPair(0x0602, 0x0301);
    const bool entered = fixture.wait->AwaitAfter(1);
    auto queued_master = fixture.runtime->SetSpeedTapMaster(true);
    auto queued_reset = fixture.runtime->ResetSpeedTapRuntimeToProfile();
    std::thread stopper([&] { fixture.runtime->Stop(); });
    const bool cancelled =
        queued_master.wait_for(std::chrono::seconds(3)) == std::future_status::ready &&
        queued_reset.wait_for(std::chrono::seconds(3)) == std::future_status::ready &&
        !queued_master.get() && !queued_reset.get();
    const bool still_in_flight =
        in_flight.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout &&
        fixture.io->Events() == std::vector<std::string>{
            "connect", "stage-speedtap-pair-on", "pre-wait", "apply", "post-wait"};
    fixture.wait->Release();
    stopper.join();
    return entered && cancelled && still_in_flight && in_flight.get() &&
        fixture.runtime->GetHealth() == aura::M605RuntimeHealth::Stopped &&
        fixture.io->Events() == std::vector<std::string>{
            "connect", "stage-speedtap-pair-on", "pre-wait", "apply", "post-wait"};
}

bool TestSpeedTapFifo() {
    auto fixture = MakeFixture(BlockPhase::BeforeApply);
    auto first = fixture.runtime->SetSpeedTapPair(0x0602, 0x0301);
    const bool entered = fixture.wait->AwaitBefore(1);
    auto second = fixture.runtime->SetSpeedTapMaster(true);
    auto third = fixture.runtime->ResetSpeedTapRuntimeToProfile();
    const bool only_first_staged = fixture.io->Events() == std::vector<std::string>{
        "connect", "stage-speedtap-pair-on", "pre-wait"};
    fixture.wait->Release();
    const bool succeeded = first.get() && second.get() && third.get();
    const auto state = fixture.runtime->GetAppliedRuntimeState();
    return entered && only_first_staged && succeeded &&
        fixture.wait->BeforeCount() == 3 && fixture.wait->AfterCount() == 3 &&
        state.speedtap_pair_submissions.empty() && state.speedtap_master == true &&
        state.speedtap_pair_knowledge ==
            aura::M605AppliedRuntimeState::SpeedTapPairKnowledge::ProfileBaselineUnknown &&
        fixture.io->Events() == std::vector<std::string>{
            "connect", "stage-speedtap-pair-on", "pre-wait", "apply", "post-wait",
            "stage-speedtap-master-on", "pre-wait", "apply", "post-wait",
            "stage-speedtap-profile-reset", "pre-wait", "apply", "post-wait"};
}

bool TestImportedKeyProtocolAndRuntime() {
    using aura::m605::Report;
    using namespace aura::m605;

    Report esc_actuation{};
    esc_actuation[1] = 0x51; esc_actuation[2] = 0x4f;
    esc_actuation[5] = 0x01; esc_actuation[7] = 10;
    const auto esc = BuildPerKeyActuation(0x0100, 1.0);
    if (!esc || !Check(*esc, esc_actuation, "Esc actuation") ||
        !aura::NativeHidBackend::IsSupportedOutputReport(*esc)) return false;

    Report fn_actuation = esc_actuation;
    fn_actuation[5] = 0x9f;
    const auto fn = BuildPerKeyActuation(0x0508, 1.0);
    if (!fn || !Check(*fn, fn_actuation, "Fn actuation") ||
        !aura::NativeHidBackend::IsSupportedOutputReport(*fn)) return false;

    Report enter_press{};
    enter_press[1] = 0x51; enter_press[2] = 0x54;
    enter_press[3] = 1; enter_press[5] = 0x2b;
    enter_press[7] = 8; enter_press[9] = 1;
    Report enter_release = enter_press;
    enter_release[3] = 2; enter_release[7] = 6;
    const auto enter_rt = BuildPerKeyRapidTriggerStages(0x0707, 0.8, 0.6, true);
    if (!enter_rt || !Check((*enter_rt)[0], enter_press, "Enter RT press") ||
        !Check((*enter_rt)[1], enter_release, "Enter RT release") ||
        !aura::NativeHidBackend::IsSupportedOutputReport((*enter_rt)[0]) ||
        !aura::NativeHidBackend::IsSupportedOutputReport((*enter_rt)[1])) return false;
    const auto right_shift_rt = BuildPerKeyRapidTriggerStages(0x040a, 0.8, 0.6, true);
    if (!right_shift_rt || (*right_shift_rt)[0][5] != 0x39 ||
        !aura::NativeHidBackend::IsSupportedOutputReport((*right_shift_rt)[0])) return false;

    Report fn_deadzone{};
    fn_deadzone[1] = 0x51; fn_deadzone[2] = 0x59;
    fn_deadzone[5] = 0x9f; fn_deadzone[7] = 3; fn_deadzone[8] = 2;
    const auto fn_dz = BuildPerKeyDeadzone(0x0508, 0.2, 0.3);
    if (!fn_dz || !Check(*fn_dz, fn_deadzone, "Fn deadzone") ||
        !aura::NativeHidBackend::IsSupportedOutputReport(*fn_dz)) return false;
    const auto page_down_dz = BuildPerKeyDeadzone(0x070b, 0.2, 0.3);
    if (!page_down_dz || (*page_down_dz)[5] != 0x56 ||
        !aura::NativeHidBackend::IsSupportedOutputReport(*page_down_dz)) return false;

    Report arrow_pair{};
    arrow_pair[1] = 0x51; arrow_pair[2] = 0x55;
    arrow_pair[5] = 0x4f; arrow_pair[7] = 0x56; arrow_pair[9] = 1;
    const auto arrows = BuildSpeedTapPair(0x060a, 0x070b, 1);
    if (!arrows || !Check(*arrows, arrow_pair, "LeftArrow+PageDown SpeedTap") ||
        !aura::NativeHidBackend::IsSupportedOutputReport(*arrows)) return false;

    Report rejected = fn_actuation;
    rejected[5] = 0x9e;
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return false;
    rejected[5] = 0xff;
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return false;
    rejected = enter_press; rejected[5] = 0x9e;
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return false;
    rejected = fn_deadzone; rejected[5] = 0xff;
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return false;
    rejected = arrow_pair; rejected[7] = 0x9e;
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return false;

    auto fixture = MakeFixture();
    if (!fixture.runtime->SetPerKeyActuation(0x0100, 1.0).get() ||
        !fixture.runtime->SetPerKeyRapidTrigger(0x0707, 0.8, 0.6).get() ||
        !fixture.runtime->DisablePerKeyRapidTrigger(0x0707, 0.4, 0.2).get() ||
        !fixture.runtime->SetPerKeyDeadzone(0x0508, 0.2, 0.3).get() ||
        !fixture.runtime->SetSpeedTapPair(0x060a, 0x070b).get() ||
        !fixture.runtime->DisableSpeedTapPair(0x060a, 0x070b).get()) return false;
    const auto reports = fixture.io->StageReports();
    Report inherited_press = enter_press;
    inherited_press[7] = 4; inherited_press[9] = 0;
    Report inherited_release = enter_release;
    inherited_release[7] = 2; inherited_release[9] = 0;
    Report arrow_pair_off = arrow_pair;
    arrow_pair_off[9] = 0;
    return reports.size() == 8 && reports[0] == esc_actuation &&
        reports[1] == enter_press && reports[2] == enter_release &&
        reports[3] == inherited_press && reports[4] == inherited_release &&
        reports[5] == fn_deadzone && reports[6] == arrow_pair &&
        reports[7] == arrow_pair_off &&
        fixture.wait->BeforeCount() == 6 && fixture.wait->AfterCount() == 6;
}

aura::m605::DksConfig MakeDksConfig() {
    using namespace aura::m605;
    DksConfig config = StandardDksConfiguration(0x0402);
    config.slots[1].target = DksTarget::LogicalKey(0x0600);
    config.slots[1].down_start = DksTriggerState::Tap;
    config.slots[2].target = DksTarget::LogicalKey(0x0700);
    config.slots[2].down_end = DksTriggerState::Tap;
    return config;
}

bool TestDksExactPacketsAndAllowlist() {
    using namespace aura::m605;
    const auto config = MakeDksConfig();
    const auto stages = BuildPerKeyDksStages(config);
    if (!stages) return false;
    const std::array<uint8_t, 4> targets{{0xff, 0x02, 0x03, 0xff}};
    const std::array<uint8_t, 4> masks{{0xf8, 0x40, 0x10, 0x00}};
    for (size_t i = 0; i < 4; ++i) {
        Report expected{};
        expected[1] = 0x51; expected[2] = 0x23;
        expected[3] = 0x31; expected[5] = 0x0a; expected[6] = 0x24;
        expected[7] = targets[i]; expected[9] = masks[i];
        expected[10] = static_cast<uint8_t>(i + 1);
        if (!Check((*stages)[i], expected, "DKS exact 65-byte stage") ||
            !aura::NativeHidBackend::IsSupportedOutputReport((*stages)[i])) return false;
    }
    const auto standard = BuildPerKeyDksStages(StandardDksConfiguration(0x0402));
    if (!standard) return false;
    for (size_t i = 0; i < 4; ++i) {
        Report expected = (*stages)[i];
        expected[7] = 0xff;
        expected[9] = i == 0 ? 0xf8 : 0;
        if (!Check((*standard)[i], expected, "DKS standard rewrite")) return false;
    }
    const auto fn_source = BuildPerKeyDksStages(StandardDksConfiguration(0x0508));
    if (!fn_source || (*fn_source)[0][3] != 0x9f || (*fn_source)[0][4] != 0) return false;

    Report rejected = (*stages)[0];
    rejected[3] = 0x9e; // unknown source
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return false;
    rejected = (*stages)[0]; rejected[7] = 0x9e; // unknown target
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return false;
    rejected = (*stages)[0]; rejected[5] = 0; // start below range
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return false;
    rejected = (*stages)[0]; rejected[6] = 41; // end above range
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return false;
    rejected = (*stages)[0]; rejected[5] = 37; // inverted thresholds
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return false;
    rejected = (*stages)[0]; rejected[10] = 0;
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return false;
    rejected[10] = 5;
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return false;
    rejected = (*stages)[0]; rejected[11] = 1; // reserved
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return false;
    rejected = (*stages)[0]; rejected[64] = 1;
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return false;
    return true;
}

bool TestDksBuilderValidation() {
    using namespace aura::m605;
    const auto base = MakeDksConfig();
    if (BuildPerKeyDksSlot(base, 0) || BuildPerKeyDksSlot(base, 5)) return false;
    auto config = base;
    config.start_mm = 0.1; config.end_mm = 4.0;
    auto bounds = BuildPerKeyDksStages(config);
    if (!bounds || (*bounds)[0][5] != 1 || (*bounds)[0][6] != 40) return false;
    config = base; config.start_mm = 0.0;
    if (BuildPerKeyDksStages(config)) return false;
    config = base; config.end_mm = 4.1;
    if (BuildPerKeyDksStages(config)) return false;
    config = base; config.start_mm = 3.7;
    if (BuildPerKeyDksStages(config)) return false;
    config = base; config.source_logical_key_id = 0xffff;
    if (BuildPerKeyDksStages(config)) return false;
    config = base; config.slots[2].target = DksTarget::LogicalKey(0xffff);
    if (BuildPerKeyDksStages(config)) return false;
    config = base; config.slots[3].target = DksTarget::LogicalKey(0x00ff);
    if (BuildPerKeyDksStages(config)) return false; // sentinel is typed, never a logical key
    config = base; config.slots[3].target = {DksTarget::Kind::DefaultSentinel, 1};
    if (BuildPerKeyDksStages(config)) return false;
    config = base; config.slots[3].up_end = static_cast<DksTriggerState>(4);
    if (BuildPerKeyDksStages(config)) return false;
    DksSlot slot{};
    slot.down_start = DksTriggerState::Hold;
    slot.down_end = DksTriggerState::Hold;
    slot.up_start = DksTriggerState::Release;
    if (EncodeDksTriggerMask(slot) != 0xf8) return false;
    slot = {}; slot.up_start = DksTriggerState::Tap;
    if (EncodeDksTriggerMask(slot) != 0x04) return false;
    slot = {}; slot.up_end = DksTriggerState::Tap;
    return EncodeDksTriggerMask(slot) == 0x01;
}

bool TestDksPreflightZeroActivity() {
    using namespace aura::m605;
    auto fixture = MakeFixture();
    const auto base = MakeDksConfig();
    auto invalid = base; invalid.slots[3].target = DksTarget::LogicalKey(0xffff);
    if (fixture.runtime->SetPerKeyDks(invalid).get()) return false;
    invalid = base; invalid.slots[2].target = DksTarget::LogicalKey(0xffff);
    if (fixture.runtime->SetPerKeyDks(invalid).get()) return false;
    invalid = base; invalid.source_logical_key_id = 0xffff;
    if (fixture.runtime->SetPerKeyDks(invalid).get()) return false;
    invalid = base; invalid.start_mm = 3.7;
    if (fixture.runtime->SetPerKeyDks(invalid).get()) return false;
    invalid = base; invalid.slots[3].up_end = static_cast<DksTriggerState>(4);
    if (fixture.runtime->SetPerKeyDks(invalid).get()) return false;
    return fixture.io->Events().empty() && fixture.io->StageReports().empty() &&
        fixture.wait->BetweenCount() == 0 && fixture.wait->BeforeCount() == 0 &&
        fixture.runtime->GetHealth() == aura::M605RuntimeHealth::Clean &&
        fixture.runtime->GetAppliedRuntimeState().per_key_dks.empty();
}

bool TestDksTransactionBoundaryAndFifo() {
    auto fixture = MakeFixture(BlockPhase::AfterApply);
    auto dks = fixture.runtime->SetPerKeyDks(MakeDksConfig());
    const bool entered = fixture.wait->AwaitAfter(1);
    auto next = fixture.runtime->SetPerKeyActuation(0x0402, 1.0);
    aura::NativeHidBackend disconnected_lighting;
    aura::FrameBuffer frame;
    std::promise<void> push_started;
    auto started = push_started.get_future();
    std::promise<bool> push_done;
    auto pushed = push_done.get_future();
    std::thread lighting([&] {
        push_started.set_value();
        push_done.set_value(disconnected_lighting.PushFrame(frame, {1}));
    });
    started.wait();
    const bool rgb_blocked = pushed.wait_for(std::chrono::milliseconds(30)) ==
        std::future_status::timeout;
    const bool pending = entered && rgb_blocked && fixture.io->StageReports().size() == 4 &&
        fixture.runtime->GetAppliedRuntimeState().per_key_dks.empty() &&
        dks.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout &&
        next.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout;
    fixture.wait->Release();
    const bool success = dks.get() && next.get();
    lighting.join();
    const auto state = fixture.runtime->GetAppliedRuntimeState();
    const auto events = fixture.io->Events();
    const auto reports = fixture.io->StageReports();
    bool only_dks_then_actuation = reports.size() == 5;
    if (only_dks_then_actuation) {
        for (size_t i = 0; i < 4; ++i) {
            only_dks_then_actuation &= reports[i][1] == 0x51 && reports[i][2] == 0x23;
        }
        only_dks_then_actuation &= reports[4][2] == 0x4f;
    }
    return pending && success && !pushed.get() && fixture.wait->BetweenCount() == 3 &&
        only_dks_then_actuation &&
        fixture.wait->BeforeCount() == 2 && fixture.wait->AfterCount() == 2 &&
        events == std::vector<std::string>{
            "connect", "stage-dks-1", "dks-wait", "stage-dks-2", "dks-wait",
            "stage-dks-3", "dks-wait", "stage-dks-4", "pre-wait", "apply",
            "post-wait", "stage-v", "pre-wait", "apply", "post-wait"} &&
        state.per_key_dks.at(0x0402).start_raw == 10 &&
        state.per_key_dks.at(0x0402).end_raw == 36 &&
        !state.per_key_dks.at(0x0402).standard_runtime_configuration &&
        fixture.runtime->GetHealth() == aura::M605RuntimeHealth::Clean;
}

bool TestDksShadowUnchangedThroughEveryStage() {
    for (int stage = 1; stage <= 4; ++stage) {
        auto fixture = MakeFixture();
        fixture.io->BlockAfterStageCall(stage);
        auto future = fixture.runtime->SetPerKeyDks(MakeDksConfig());
        const bool staged = fixture.io->AwaitStageCall(stage) &&
            fixture.io->StageReports().size() == static_cast<size_t>(stage) &&
            fixture.runtime->GetAppliedRuntimeState().per_key_dks.empty() &&
            fixture.runtime->GetHealth() == aura::M605RuntimeHealth::TransactionInProgress &&
            future.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout;
        fixture.io->ReleaseStages();
        if (!staged || !future.get() ||
            fixture.runtime->GetAppliedRuntimeState().per_key_dks.count(0x0402) != 1) return false;
    }
    return true;
}

bool TestDksStageAndApplyFailures() {
    for (int failed_stage : {1, 2, 3, 4}) {
        auto fixture = MakeFixture();
        fixture.io->fail_stage_call = failed_stage;
        auto queued = fixture.runtime->SetPerKeyDks(MakeDksConfig());
        if (queued.get() || fixture.io->StageReports().size() != static_cast<size_t>(failed_stage) ||
            fixture.wait->BetweenCount() != failed_stage - 1 ||
            fixture.wait->BeforeCount() != 0 || fixture.wait->AfterCount() != 0 ||
            fixture.runtime->GetHealth() != aura::M605RuntimeHealth::IndeterminateStagedState ||
            !fixture.runtime->GetAppliedRuntimeState().per_key_dks.empty() ||
            fixture.runtime->SetPerKeyActuation(0x0402, 1.0).get()) return false;
    }
    auto fixture = MakeFixture();
    fixture.io->apply_succeeds = false;
    const bool success = fixture.runtime->SetPerKeyDks(MakeDksConfig()).get();
    const auto events = fixture.io->Events();
    return !success && fixture.wait->BetweenCount() == 3 &&
        fixture.wait->BeforeCount() == 1 && fixture.wait->AfterCount() == 0 &&
        fixture.runtime->GetHealth() == aura::M605RuntimeHealth::IndeterminateStagedState &&
        fixture.runtime->GetAppliedRuntimeState().per_key_dks.empty() &&
        events.size() == 11 && events[9] == "apply" && events[10] == "disconnect";
}

bool TestDksFailureInvalidatesPriorShadow() {
    auto fixture = MakeFixture();
    if (!fixture.runtime->SetPerKeyDks(MakeDksConfig()).get()) return false;
    if (fixture.runtime->GetAppliedRuntimeState().per_key_dks.count(0x0402) != 1) return false;
    fixture.io->apply_succeeds = false;
    const bool result = fixture.runtime->RestorePerKeyDksToStandard(0x0402).get();
    return !result && fixture.runtime->GetAppliedRuntimeState().per_key_dks.empty() &&
        fixture.runtime->GetHealth() == aura::M605RuntimeHealth::IndeterminateStagedState;
}

bool TestDksStandardRestoreAndStop() {
    auto fixture = MakeFixture(BlockPhase::AfterApply);
    auto restore = fixture.runtime->RestorePerKeyDksToStandard(0x0402);
    if (!fixture.wait->AwaitAfter(1)) return false;
    auto queued = fixture.runtime->SetPerKeyDks(MakeDksConfig());
    std::thread stopper([&] { fixture.runtime->Stop(); });
    const bool cancelled = queued.wait_for(std::chrono::seconds(3)) == std::future_status::ready &&
        !queued.get() && fixture.io->StageReports().size() == 4 &&
        restore.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout;
    fixture.wait->Release();
    stopper.join();
    const auto state = fixture.runtime->GetAppliedRuntimeState();
    return cancelled && restore.get() &&
        state.per_key_dks.at(0x0402).standard_runtime_configuration &&
        fixture.runtime->GetHealth() == aura::M605RuntimeHealth::Stopped &&
        fixture.io->StageReports().size() == 4 && fixture.wait->BetweenCount() == 3;
}

bool TestGlobalSettings() {
    using namespace aura::m605;
    // 1. Packet structure and NativeHid allowlist checks
    Report expected_actuation{};
    expected_actuation[1] = 0x51; expected_actuation[2] = 0x50; expected_actuation[5] = 10;
    auto act = BuildGlobalActuation(1.0);
    if (!act || !Check(*act, expected_actuation, "Global Actuation 1.0mm") ||
        !aura::NativeHidBackend::IsSupportedOutputReport(*act)) return false;

    // Range checks
    if (BuildGlobalActuation(0.0) || BuildGlobalActuation(4.1) ||
        BuildGlobalActuation(-0.1) || BuildGlobalActuation(std::numeric_limits<double>::quiet_NaN()) ||
        BuildGlobalActuation(std::numeric_limits<double>::infinity()) ||
        BuildGlobalActuation(1.05)) return false;

    // Allowlist rejection checks
    Report rej = *act;
    rej[5] = 0; if (aura::NativeHidBackend::IsSupportedOutputReport(rej)) return false;
    rej[5] = 41; if (aura::NativeHidBackend::IsSupportedOutputReport(rej)) return false;
    rej = *act; rej[3] = 1; if (aura::NativeHidBackend::IsSupportedOutputReport(rej)) return false;
    rej = *act; rej[4] = 1; if (aura::NativeHidBackend::IsSupportedOutputReport(rej)) return false;
    rej = *act; rej[6] = 1; if (aura::NativeHidBackend::IsSupportedOutputReport(rej)) return false;
    rej = *act; rej[64] = 1; if (aura::NativeHidBackend::IsSupportedOutputReport(rej)) return false;

    // 2. Global Deadzone: Byte 5 = Top, Byte 6 = Bottom
    Report expected_dz{};
    expected_dz[1] = 0x51; expected_dz[2] = 0x58; expected_dz[5] = 0; expected_dz[6] = 1;
    auto dz = BuildGlobalDeadzone(0.0, 0.1);
    if (!dz || !Check(*dz, expected_dz, "Global Deadzone Top 0.0 Bottom 0.1") ||
        !aura::NativeHidBackend::IsSupportedOutputReport(*dz)) return false;

    auto dz_max = BuildGlobalDeadzone(0.5, 0.5);
    if (!dz_max || (*dz_max)[5] != 5 || (*dz_max)[6] != 5 ||
        !aura::NativeHidBackend::IsSupportedOutputReport(*dz_max)) return false;

    if (BuildGlobalDeadzone(-0.1, 0.1) || BuildGlobalDeadzone(0.0, -0.1) ||
        BuildGlobalDeadzone(0.6, 0.1) || BuildGlobalDeadzone(0.0, 0.6) ||
        BuildGlobalDeadzone(std::numeric_limits<double>::quiet_NaN(), 0.1) ||
        BuildGlobalDeadzone(0.1, std::numeric_limits<double>::infinity()) ||
        BuildGlobalDeadzone(0.15, 0.2)) return false;

    rej = *dz; rej[5] = 6; if (aura::NativeHidBackend::IsSupportedOutputReport(rej)) return false;
    rej = *dz; rej[6] = 6; if (aura::NativeHidBackend::IsSupportedOutputReport(rej)) return false;
    rej = *dz; rej[3] = 1; if (aura::NativeHidBackend::IsSupportedOutputReport(rej)) return false;
    rej = *dz; rej[7] = 1; if (aura::NativeHidBackend::IsSupportedOutputReport(rej)) return false;
    rej = *dz; rej[64] = 1; if (aura::NativeHidBackend::IsSupportedOutputReport(rej)) return false;

    // 3. Global Rapid Trigger: 51 53 <separate_mode> 00 <press> <release> <top> <bottom>
    Report expected_rt{};
    expected_rt[1] = 0x51; expected_rt[2] = 0x53; expected_rt[3] = 1;
    expected_rt[5] = 4; expected_rt[6] = 2; expected_rt[7] = 0; expected_rt[8] = 1;
    auto rt = BuildGlobalRapidTrigger(0.4, 0.2, 0.0, 0.1, true);
    if (!rt || !Check(*rt, expected_rt, "Global RT P0.4 R0.2 Top0.0 Bot0.1 SeparateMode=1") ||
        !aura::NativeHidBackend::IsSupportedOutputReport(*rt)) return false;

    auto rt_linked = BuildGlobalRapidTrigger(0.4, 0.4, 0.0, 0.1, false);
    if (!rt_linked || (*rt_linked)[3] != 0 ||
        !aura::NativeHidBackend::IsSupportedOutputReport(*rt_linked)) return false;

    if (BuildGlobalRapidTrigger(0.4, 0.2, 0.0, 0.1, false).has_value()) return false;

    if (BuildGlobalRapidTrigger(0.0, 0.2, 0.0, 0.1, true) ||
        BuildGlobalRapidTrigger(2.6, 0.2, 0.0, 0.1, true) ||
        BuildGlobalRapidTrigger(0.4, 0.0, 0.0, 0.1, true) ||
        BuildGlobalRapidTrigger(0.4, 2.6, 0.0, 0.1, true) ||
        BuildGlobalRapidTrigger(0.4, 0.2, 0.6, 0.1, true) ||
        BuildGlobalRapidTrigger(0.4, 0.2, 0.0, 0.6, true) ||
        BuildGlobalRapidTrigger(0.4, 0.2, -0.1, 0.1, true) ||
        BuildGlobalRapidTrigger(0.45, 0.2, 0.0, 0.1, true) ||
        BuildGlobalRapidTrigger(std::numeric_limits<double>::quiet_NaN(), 0.2, 0.0, 0.1, true)) return false;

    rej = *rt; rej[3] = 2; if (aura::NativeHidBackend::IsSupportedOutputReport(rej)) return false;
    rej = *rt; rej[4] = 1; if (aura::NativeHidBackend::IsSupportedOutputReport(rej)) return false;
    rej = *rt; rej[5] = 0; if (aura::NativeHidBackend::IsSupportedOutputReport(rej)) return false;
    rej = *rt; rej[5] = 26; if (aura::NativeHidBackend::IsSupportedOutputReport(rej)) return false;
    rej = *rt; rej[6] = 0; if (aura::NativeHidBackend::IsSupportedOutputReport(rej)) return false;
    rej = *rt; rej[6] = 26; if (aura::NativeHidBackend::IsSupportedOutputReport(rej)) return false;
    rej = *rt; rej[7] = 6; if (aura::NativeHidBackend::IsSupportedOutputReport(rej)) return false;
    rej = *rt; rej[8] = 6; if (aura::NativeHidBackend::IsSupportedOutputReport(rej)) return false;
    rej = *rt; rej[9] = 1; if (aura::NativeHidBackend::IsSupportedOutputReport(rej)) return false;
    rej = *rt; rej[64] = 1; if (aura::NativeHidBackend::IsSupportedOutputReport(rej)) return false;
    rej = *rt_linked; rej[5] = 4; rej[6] = 2; if (aura::NativeHidBackend::IsSupportedOutputReport(rej)) return false;

    // 4. Runtime lifecycle and shadow state
    const auto state = std::make_shared<LatchState>();
    auto io = std::make_unique<FakeTransport>();
    io->stage_precondition = [state] { return state->armed; };
    auto wait = std::make_unique<FakeWait>(io.get(), BlockPhase::None);
    auto* io_ptr = io.get();
    auto runtime = aura::M605RuntimeTestAccess::Create(
        std::move(io), std::move(wait), std::make_unique<FakeLatch>(state));

    // Global Actuation execution
    if (!runtime->SetGlobalActuation(1.2).get()) return false;
    if (state->arms != 1 || state->armed != false) return false;
    auto reports = io_ptr->StageReports();
    if (reports.size() != 1 || reports[0][2] != 0x50 || reports[0][5] != 12) return false;
    auto shadow = runtime->GetAppliedRuntimeState();
    if (!shadow.global_actuation_raw.has_value() || *shadow.global_actuation_raw != 12) return false;
    // CRITICAL: Ensure NO 68-key loop occurred!
    if (!shadow.per_key_actuation_raw.empty()) return false;

    // Global Deadzone execution
    if (!runtime->SetGlobalDeadzone(0.1, 0.2).get()) return false;
    if (state->arms != 2 || state->armed != false) return false;
    reports = io_ptr->StageReports();
    if (reports.size() != 2 || reports[1][2] != 0x58 ||
        reports[1][5] != 1 || reports[1][6] != 2) return false;
    shadow = runtime->GetAppliedRuntimeState();
    if (!shadow.global_deadzone.has_value() ||
        shadow.global_deadzone->top_raw != 1 || shadow.global_deadzone->bottom_raw != 2) return false;
    if (!shadow.per_key_deadzone.empty()) return false;

    // Global Rapid Trigger execution
    if (!runtime->SetGlobalRapidTrigger(0.5, 0.3, 0.0, 0.1, true).get()) return false;
    if (state->arms != 3 || state->armed != false) return false;
    reports = io_ptr->StageReports();
    if (reports.size() != 3 || reports[2][2] != 0x53 ||
        reports[2][3] != 1 || reports[2][5] != 5 || reports[2][6] != 3) return false;
    shadow = runtime->GetAppliedRuntimeState();
    if (!shadow.global_rapid_trigger.has_value() || !shadow.global_rapid_trigger->separate_mode ||
        shadow.global_rapid_trigger->press_raw != 5 || shadow.global_rapid_trigger->release_raw != 3 ||
        shadow.global_rapid_trigger->top_raw != 0 || shadow.global_rapid_trigger->bottom_raw != 1) return false;
    if (!shadow.per_key_rapid_trigger.empty()) return false;

    // Preflight failure: zero writes
    const auto stage_count_before = io_ptr->StageReports().size();
    if (runtime->SetGlobalActuation(0.0).get()) return false;
    if (runtime->SetGlobalDeadzone(0.6, 0.1).get()) return false;
    if (runtime->SetGlobalRapidTrigger(0.0, 0.2, 0.0, 0.1, true).get()) return false;
    if (runtime->SetGlobalRapidTrigger(0.4, 0.2, 0.0, 0.1, false).get()) return false;
    if (io_ptr->StageReports().size() != stage_count_before) return false;

    // Stage failure locks runtime
    io_ptr->stage_succeeds = false;
    if (runtime->SetGlobalActuation(1.0).get()) return false;
    if (runtime->GetHealth() != aura::M605RuntimeHealth::IndeterminateStagedState) return false;
    if (runtime->IsPersistentSafetyQuarantined() != true) return false;
    shadow = runtime->GetAppliedRuntimeState();
    if (shadow.global_actuation_raw.has_value() || shadow.global_deadzone.has_value() ||
        shadow.global_rapid_trigger.has_value()) return false;

    return true;
}
}

int main() {
    using namespace aura::m605;
    static_assert(std::tuple_size<Report>::value == 65);
    if (!TestExhaustiveVerifiedMapping()) return 1;
    if (WireIdForLogicalKey(0x0402) != 0x0031 ||
        WireIdForLogicalKey(0x0501) != 0x0030 ||
        WireIdForLogicalKey(0x0602) != 0x001f ||
        WireIdForLogicalKey(0x0409).has_value() ||
        WireIdForLogicalKey(0xffff).has_value()) return 1;
    if (SpeedTapWireIdForLogicalKey(0x0602) != 0x001f ||
        SpeedTapWireIdForLogicalKey(0x0301) != 0x0021 ||
        SpeedTapWireIdForLogicalKey(0x0701) != 0x0012 ||
        SpeedTapWireIdForLogicalKey(0x0702) != 0x0020 ||
        SpeedTapWireIdForLogicalKey(0x0402) != 0x0031 ||
        SpeedTapWireIdForLogicalKey(0x0501) != 0x0030 ||
        SpeedTapWireIdForLogicalKey(0x0705).has_value()) return 1;

    Report v4{};
    v4[1] = 0x51; v4[2] = 0x4f; v4[5] = 0x31; v4[7] = 0x28;
    auto actual_v4 = BuildPerKeyActuation(0x0402, 4.0);
    if (!actual_v4 || !Check(*actual_v4, v4, "V 4.0 mm") ||
        !aura::NativeHidBackend::IsSupportedOutputReport(*actual_v4)) return 1;

    Report v1 = v4;
    v1[7] = 0x0a;
    auto actual_v1 = BuildPerKeyActuation(0x0402, 1.0);
    if (!actual_v1 || !Check(*actual_v1, v1, "V 1.0 mm")) return 1;

    Report c1 = v1;
    c1[5] = 0x30;
    auto actual_c1 = BuildPerKeyActuation(0x0501, 1.0);
    if (!actual_c1 || !Check(*actual_c1, c1, "C mapping") ||
        !aura::NativeHidBackend::IsSupportedOutputReport(*actual_c1)) return 1;

    Report apply{};
    apply[1] = 0x50; apply[2] = 0x55;
    if (!Check(BuildRuntimeApply(), apply, "runtime apply") ||
        !aura::NativeHidBackend::IsSupportedOutputReport(apply)) return 1;

    Report analog_on{};
    analog_on[1] = 0x51; analog_on[2] = 0x2d; analog_on[6] = 1;
    auto actual_on = BuildAnalogEffect(0, 1);
    if (!actual_on || !Check(*actual_on, analog_on, "Static Analog ON") ||
        !aura::NativeHidBackend::IsSupportedOutputReport(*actual_on)) return 1;
    Report analog_off = analog_on;
    analog_off[6] = 0;
    auto actual_off = BuildAnalogEffect(0, 0);
    if (!actual_off || !Check(*actual_off, analog_off, "Static Analog OFF")) return 1;

    Report rt_press_on{};
    rt_press_on[1] = 0x51; rt_press_on[2] = 0x54; rt_press_on[3] = 1;
    rt_press_on[5] = 0x31; rt_press_on[7] = 8; rt_press_on[9] = 1;
    Report rt_release_on = rt_press_on;
    rt_release_on[3] = 2; rt_release_on[7] = 6;
    auto rt_on = BuildPerKeyRapidTriggerStages(0x0402, 0.8, 0.6, true);
    if (!rt_on || !Check((*rt_on)[0], rt_press_on, "V RT ON Press 0.8") ||
        !Check((*rt_on)[1], rt_release_on, "V RT ON Release 0.6") ||
        !aura::NativeHidBackend::IsSupportedOutputReport((*rt_on)[0]) ||
        !aura::NativeHidBackend::IsSupportedOutputReport((*rt_on)[1])) return 1;

    Report rt_press_off = rt_press_on;
    rt_press_off[7] = 4; rt_press_off[9] = 0;
    Report rt_release_off = rt_release_on;
    rt_release_off[7] = 2; rt_release_off[9] = 0;
    auto rt_off = BuildPerKeyRapidTriggerStages(0x0402, 0.4, 0.2, false);
    if (!rt_off || !Check((*rt_off)[0], rt_press_off, "V RT OFF Press inherited 0.4") ||
        !Check((*rt_off)[1], rt_release_off, "V RT OFF Release inherited 0.2")) return 1;

    Report deadzone{};
    deadzone[1] = 0x51; deadzone[2] = 0x59; deadzone[5] = 0x31;
    deadzone[7] = 3; deadzone[8] = 2; // Bottom first, Top second.
    auto actual_deadzone = BuildPerKeyDeadzone(0x0402, 0.2, 0.3);
    if (!actual_deadzone || !Check(*actual_deadzone, deadzone, "V DZ Top 0.2 Bottom 0.3") ||
        (*actual_deadzone)[7] != 3 || (*actual_deadzone)[8] != 2 ||
        !aura::NativeHidBackend::IsSupportedOutputReport(*actual_deadzone)) return 1;

    Report reset_all{};
    reset_all[1] = 0x51; reset_all[2] = 0x52; reset_all[3] = 4;
    reset_all[5] = 4; reset_all[7] = 3;
    auto actual_reset = BuildResetAllPerKeyDeadzoneOverrides(4, 3);
    if (!actual_reset || !Check(*actual_reset, reset_all, "RESET ALL DZ Bottom 4 Top 3") ||
        !aura::NativeHidBackend::IsSupportedOutputReport(*actual_reset)) return 1;
    reset_all[5] = 1; reset_all[7] = 0;
    auto zero_top_reset = BuildResetAllPerKeyDeadzoneOverrides(1, 0);
    if (!zero_top_reset || !Check(*zero_top_reset, reset_all,
                                  "RESET ALL DZ Bottom 1 Top 0")) return 1;

    Report speedtap_ad_on{};
    speedtap_ad_on[1] = 0x51; speedtap_ad_on[2] = 0x55;
    speedtap_ad_on[5] = 0x1f; speedtap_ad_on[7] = 0x21; speedtap_ad_on[9] = 1;
    auto ad_on = BuildSpeedTapPair(0x0602, 0x0301, 1);
    if (!ad_on || !Check(*ad_on, speedtap_ad_on, "SpeedTap A+D ON") ||
        !aura::NativeHidBackend::IsSupportedOutputReport(*ad_on)) return 1;
    Report speedtap_ad_off = speedtap_ad_on;
    speedtap_ad_off[9] = 0;
    auto ad_off = BuildSpeedTapPair(0x0602, 0x0301, 0);
    if (!ad_off || !Check(*ad_off, speedtap_ad_off, "SpeedTap A+D OFF") ||
        !aura::NativeHidBackend::IsSupportedOutputReport(*ad_off)) return 1;
    Report speedtap_ws_on = speedtap_ad_on;
    speedtap_ws_on[5] = 0x12; speedtap_ws_on[7] = 0x20;
    auto ws_on = BuildSpeedTapPair(0x0701, 0x0702, 1);
    if (!ws_on || !Check(*ws_on, speedtap_ws_on, "SpeedTap W+S ON") ||
        !aura::NativeHidBackend::IsSupportedOutputReport(*ws_on)) return 1;
    Report speedtap_profile_reset{};
    speedtap_profile_reset[1] = 0x51; speedtap_profile_reset[2] = 0x56;
    if (!Check(BuildResetSpeedTapRuntimeToProfile(), speedtap_profile_reset,
               "SpeedTap profile-baseline reset") ||
        !aura::NativeHidBackend::IsSupportedOutputReport(speedtap_profile_reset)) return 1;
    Report speedtap_master_on{};
    speedtap_master_on[1] = 0x51; speedtap_master_on[2] = 0x57;
    speedtap_master_on[5] = 1;
    auto master_on = BuildSpeedTapMaster(1);
    if (!master_on || !Check(*master_on, speedtap_master_on, "SpeedTap master ON") ||
        !aura::NativeHidBackend::IsSupportedOutputReport(*master_on)) return 1;
    Report speedtap_master_off = speedtap_master_on;
    speedtap_master_off[5] = 0;
    auto master_off = BuildSpeedTapMaster(0);
    if (!master_off || !Check(*master_off, speedtap_master_off, "SpeedTap master OFF") ||
        !aura::NativeHidBackend::IsSupportedOutputReport(*master_off)) return 1;

    Report rejected = v4;
    rejected[2] = 0xfe; // unknown opcode
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = v4;
    rejected[5] = 0x9e; // unknown Wire ID
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = analog_on;
    rejected[6] = 2; // invalid Analog flag
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = apply;
    rejected[64] = 1; // nonzero reserved byte
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = rt_press_on;
    rejected[3] = 3; // unknown RT selector
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = rt_press_on;
    rejected[7] = 26; // out-of-range RT raw
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = rt_press_on;
    rejected[8] = 1; // reserved RT byte
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = rt_press_on;
    rejected[9] = 2; // invalid RT flag
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = rt_press_on;
    rejected[5] = 0x9e; // unknown Wire ID
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = deadzone;
    rejected[7] = 6; // invalid Bottom
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = deadzone;
    rejected[8] = 6; // invalid Top
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = deadzone;
    rejected[9] = 1; // reserved byte
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = reset_all;
    rejected[3] = 3; // unknown resetType
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = reset_all;
    rejected[6] = 1; // unsupported layer
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = reset_all;
    rejected[7] = 6; // invalid global Top
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = speedtap_ad_on;
    rejected[3] = 1; // reserved pair byte
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = speedtap_ad_on;
    rejected[5] = 0x9e; // unknown first Wire ID
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = speedtap_ad_on;
    rejected[7] = 0x9e; // unknown second Wire ID
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = speedtap_ad_on;
    rejected[7] = 0x1f; // same key twice
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = speedtap_ad_on;
    rejected[9] = 2; // invalid pair flag
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = speedtap_ad_on;
    rejected[10] = 1; // reserved pair payload
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = speedtap_profile_reset;
    rejected[5] = 1; // reset is exact fixed packet
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = speedtap_profile_reset;
    rejected[64] = 1; // tail of fixed reset packet
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = speedtap_master_on;
    rejected[5] = 2; // invalid master status
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = speedtap_master_on;
    rejected[6] = 1; // reserved master payload
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = speedtap_master_on;
    rejected[64] = 1; // reserved master tail
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    Report rgb{};
    rgb[1] = 0xc0; rgb[2] = 0x81; rgb[3] = 15;
    if (!aura::NativeHidBackend::IsSupportedOutputReport(rgb)) return 1;
    rgb[3] = 16; // would require an iterator beyond byte 64
    if (aura::NativeHidBackend::IsSupportedOutputReport(rgb)) return 1;

    if (BuildPerKeyActuation(0x0409, 1.0) ||
        BuildPerKeyActuation(0x0402, 0.0) ||
        BuildPerKeyActuation(0x0402, 4.1) ||
        BuildPerKeyActuation(0x0402, std::numeric_limits<double>::quiet_NaN()) ||
        BuildAnalogEffect(1, 1) || BuildAnalogEffect(0, 2) ||
        BuildPerKeyRapidTriggerStages(0x0409, 0.8, 0.6, true) ||
        BuildPerKeyRapidTriggerStages(0x0402, 2.6, 0.6, true) ||
        BuildPerKeyDeadzone(0x0402, 0.6, 0.3) ||
        BuildResetAllPerKeyDeadzoneOverrides(1, 0, 1) ||
        BuildSpeedTapPair(0x0602, 0x0602, 1) ||
        BuildSpeedTapPair(0x0602, 0x0705, 1) ||
        BuildSpeedTapPair(0x0602, 0x0301, 2) ||
        BuildSpeedTapMaster(2)) return 1;

    if (!TestSuccessfulTransactionAndShadow() || !TestPostApplyBoundaryAndRgbSerialization() ||
        !TestPriorShadowUnchangedUntilPostSettleFinishes() ||
        !TestStageFailure() ||
        !TestApplyFailureLocksRuntime() || !TestFailureInvalidatesEarlierShadowAndQueuedWork() ||
        !TestFailureCancelsQueuedWork() || !TestFifo() ||
        !TestShutdownCancelsQueuedWork() || !TestDestructorCancelsQueuedWork() ||
        !TestInvalidInputsDoNotTouchTransport() ||
        !TestRapidTriggerFirstStageBoundary() ||
        !TestRapidTriggerSecondStageAndPostApplyBoundary() ||
        !TestRapidTriggerDisableAndFifo() || !TestRapidTriggerStageFailures() ||
        !TestRapidTriggerApplyFailureInvalidatesShadow() ||
        !TestResetAllDeadzoneShadowBoundary() ||
        !TestResetAllDeadzoneFailureInvalidatesShadow() ||
        !TestStopDuringRapidTriggerPostWait() ||
        !TestSpeedTapPairPostSettleAndTargetedDisable() ||
        !TestSpeedTapProfileResetPreservesMasterKnowledge() ||
        !TestSpeedTapMasterPostSettleBoundary() ||
        !TestSpeedTapStageAndApplyFailures() || !TestSpeedTapFifoAndStop() ||
        !TestSpeedTapFifo() || !TestImportedKeyProtocolAndRuntime() ||
        !TestDksExactPacketsAndAllowlist() || !TestDksBuilderValidation() ||
        !TestDksPreflightZeroActivity() || !TestDksTransactionBoundaryAndFifo() ||
        !TestDksShadowUnchangedThroughEveryStage() ||
        !TestDksStageAndApplyFailures() || !TestDksFailureInvalidatesPriorShadow() ||
        !TestDksStandardRestoreAndStop() || !TestPersistentSafetyLatch() ||
        !TestSafetyLatchArmFailure() || !TestGlobalSettings()) {
        std::cerr << "FAIL: M605 runtime state machine\n";
        return 1;
    }

    std::cout << "M605 packet, mapping, and validation checks passed\n";
    return 0;
}
