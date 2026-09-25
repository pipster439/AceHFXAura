#include "aura/hardware/m605_key_mapping.h"
#include "aura/hardware/m605_protocol.h"
#include "aura/hardware/m605_runtime.h"
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace aura {
struct M605RuntimeTestAccess {
    static std::unique_ptr<M605Runtime> Create(
        std::unique_ptr<m605::detail::Transport> transport,
        std::unique_ptr<m605::detail::SettleWait> wait) {
        return std::unique_ptr<M605Runtime>(new M605Runtime(std::move(transport), std::move(wait)));
    }
};
}

namespace {
class FakeTransport final : public aura::m605::detail::Transport {
public:
    bool stage_succeeds = true;
    bool apply_succeeds = true;
    int fail_stage_call = 0;
    bool IsConnected() const override { return connected_; }
    bool Connect() override { Record("connect"); connected_ = true; return true; }
    bool WriteStage(const aura::m605::Report& report) override {
        if (report[2] == 0x2d) Record("stage-analog");
        else if (report[2] == 0x54 && report[3] == 1) Record("stage-rt-press");
        else if (report[2] == 0x54 && report[3] == 2) Record("stage-rt-release");
        else if (report[2] == 0x59) Record("stage-deadzone");
        else if (report[2] == 0x52) Record("stage-reset-all-deadzone");
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
    bool WriteApply() override { Record("apply"); return apply_succeeds; }
    void Disconnect() override { connected_ = false; Record("disconnect"); }
    std::string GetLastError() const override { return "simulated write failure"; }
    std::vector<std::string> Events() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_;
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
    std::mutex stage_mutex_;
    std::condition_variable stage_cv_;
    int stage_calls_ = 0;
    int block_after_stage_call_ = 0;
    bool stage_released_ = false;
};

enum class BlockPhase { None, BeforeApply, AfterApply };

class FakeWait final : public aura::m605::detail::SettleWait {
public:
    FakeWait(FakeTransport* io, BlockPhase block) : io_(io), block_(block) {}
    void WaitBeforeApply() override { WaitAt(BlockPhase::BeforeApply); }
    void WaitAfterApply() override { WaitAt(BlockPhase::AfterApply); }
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
        io_->RecordTiming(phase == BlockPhase::BeforeApply ? "pre-wait" : "post-wait");
        std::unique_lock<std::mutex> lock(mutex_);
        if (phase == BlockPhase::BeforeApply) ++before_count_;
        else ++after_count_;
        cv_.notify_all();
        cv_.wait(lock, [this, phase] { return block_ != phase || released_; });
    }
    bool Await(int count, BlockPhase phase) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, std::chrono::seconds(3), [this, count, phase] {
            return (phase == BlockPhase::BeforeApply ? before_count_ : after_count_) >= count;
        });
    }
    int Count(BlockPhase phase) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return phase == BlockPhase::BeforeApply ? before_count_ : after_count_;
    }

    FakeTransport* io_;
    BlockPhase block_ = BlockPhase::None;
    bool released_ = false;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    int before_count_ = 0;
    int after_count_ = 0;
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
    const bool rejected = !fixture.runtime->SetPerKeyActuation(0x0401, 1.0).get() &&
        !fixture.runtime->SetPerKeyActuation(0x0402, 4.1).get() &&
        !fixture.runtime->SetAnalogEffect(1, true).get() &&
        !fixture.runtime->SetPerKeyRapidTrigger(0x0401, 0.8, 0.6).get() &&
        !fixture.runtime->SetPerKeyRapidTrigger(0x0402, 0.0, 0.6).get() &&
        !fixture.runtime->SetPerKeyRapidTrigger(0x0402, 0.8, 2.6).get() &&
        !fixture.runtime->DisablePerKeyRapidTrigger(0x0402, 0.4, 0.0).get() &&
        !fixture.runtime->SetPerKeyDeadzone(0x0401, 0.2, 0.3).get() &&
        !fixture.runtime->SetPerKeyDeadzone(0x0402, 0.6, 0.3).get() &&
        !fixture.runtime->SetPerKeyDeadzone(0x0402, 0.2, -0.1).get() &&
        !fixture.runtime->ResetAllPerKeyDeadzoneOverrides(6, 0).get() &&
        !fixture.runtime->ResetAllPerKeyDeadzoneOverrides(1, 6).get() &&
        !fixture.runtime->ResetAllPerKeyDeadzoneOverrides(1, 0, 1).get();
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
}

int main() {
    using namespace aura::m605;
    static_assert(std::tuple_size<Report>::value == 65);
    if (WireIdForLogicalKey(0x0402) != 0x0031 ||
        WireIdForLogicalKey(0x0501) != 0x0030 ||
        WireIdForLogicalKey(0x0401).has_value() ||
        WireIdForLogicalKey(0xffff).has_value()) return 1;

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

    Report rejected = v4;
    rejected[2] = 0x50; // unknown opcode
    if (aura::NativeHidBackend::IsSupportedOutputReport(rejected)) return 1;
    rejected = v4;
    rejected[5] = 0x32; // unverified Wire ID
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
    rejected[5] = 0x32; // unknown Wire ID
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
    Report rgb{};
    rgb[1] = 0xc0; rgb[2] = 0x81; rgb[3] = 15;
    if (!aura::NativeHidBackend::IsSupportedOutputReport(rgb)) return 1;
    rgb[3] = 16; // would require an iterator beyond byte 64
    if (aura::NativeHidBackend::IsSupportedOutputReport(rgb)) return 1;

    if (BuildPerKeyActuation(0x0401, 1.0) ||
        BuildPerKeyActuation(0x0402, 0.0) ||
        BuildPerKeyActuation(0x0402, 4.1) ||
        BuildPerKeyActuation(0x0402, std::numeric_limits<double>::quiet_NaN()) ||
        BuildAnalogEffect(1, 1) || BuildAnalogEffect(0, 2) ||
        BuildPerKeyRapidTriggerStages(0x0401, 0.8, 0.6, true) ||
        BuildPerKeyRapidTriggerStages(0x0402, 2.6, 0.6, true) ||
        BuildPerKeyDeadzone(0x0402, 0.6, 0.3) ||
        BuildResetAllPerKeyDeadzoneOverrides(1, 0, 1)) return 1;

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
        !TestStopDuringRapidTriggerPostWait()) {
        std::cerr << "FAIL: M605 runtime state machine\n";
        return 1;
    }

    std::cout << "M605 packet, mapping, and validation checks passed\n";
    return 0;
}
