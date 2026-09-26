#include "aura/hardware/m605_runtime.h"
#include <chrono>
#include <utility>
#include <windows.h>

namespace aura {

// The only production implementation of the internal transport seam. It
// reuses NativeHidBackend's endpoint validation and overlapped write path.
class M605NativeTransport final : public m605::detail::Transport {
public:
    bool IsConnected() const override { return backend_.IsConnected(); }
    bool Connect() override { return backend_.Connect(); }
    bool WriteStage(const m605::Report& report) override { return backend_.SendReport(report); }
    bool WriteApply(const m605::Report& report) override { return backend_.SendReport(report); }
    void Disconnect() override { backend_.Disconnect(); }
    std::string GetLastError() const override { return backend_.GetLastError(); }

private:
    NativeHidBackend backend_;
};

namespace {
class FileSafetyLatch final : public m605::detail::SafetyLatch {
public:
    FileSafetyLatch() {
        wchar_t buffer[32768]{};
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, 32768);
        if (length == 0 || length >= 32768) return; // fail closed
        directory_ = std::wstring(buffer, length) + L"\\Aura";
        path_ = directory_ + L"\\m605-mutation-in-progress.latch";
        valid_ = true;
    }
    bool IsQuarantined() const override {
        if (!valid_) return true;
        const DWORD attributes = GetFileAttributesW(path_.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES) return true;
        const DWORD error = GetLastError();
        return error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND;
    }
    bool Arm() override {
        if (!valid_) return false;
        if (!CreateDirectoryW(directory_.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            return false;
        HANDLE file = CreateFileW(path_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        constexpr char marker[] = "M605 mutation in progress; external resynchronization required\n";
        DWORD written = 0;
        const bool complete = WriteFile(file, marker, sizeof(marker) - 1, &written, nullptr) &&
            written == sizeof(marker) - 1 && FlushFileBuffers(file);
        CloseHandle(file);
        // If creation succeeded but durability failed, leave the file unsafe.
        return complete;
    }
    bool Clear() override { return valid_ && DeleteFileW(path_.c_str()); }
private:
    bool valid_ = false;
    std::wstring directory_;
    std::wstring path_;
};

// Private two-argument constructor is used by deterministic transport tests.
class MemorySafetyLatch final : public m605::detail::SafetyLatch {
public:
    bool IsQuarantined() const override { return armed_; }
    bool Arm() override { if (armed_) return false; armed_ = true; return true; }
    bool Clear() override { if (!armed_) return false; armed_ = false; return true; }
private:
    bool armed_ = false;
};

class VendorSettleWait final : public m605::detail::SettleWait {
public:
    void WaitBetweenDksStages() override { std::this_thread::sleep_for(std::chrono::milliseconds(30)); }
    void WaitBeforeApply() override { std::this_thread::sleep_for(std::chrono::milliseconds(210)); }
    void WaitAfterApply() override { std::this_thread::sleep_for(std::chrono::milliseconds(400)); }
};

std::future<bool> RejectedOperation() {
    std::promise<bool> promise;
    auto future = promise.get_future();
    promise.set_value(false);
    return future;
}

constexpr const char* kUnknownStateError =
    "M605 device configuration state is unknown after an incomplete transaction; "
    "explicit external resynchronization acknowledgment is required before more writes";
}

M605Runtime::M605Runtime()
    : M605Runtime(std::make_unique<M605NativeTransport>(),
                  std::make_unique<VendorSettleWait>(),
                  std::make_unique<FileSafetyLatch>()) {}

M605Runtime::M605Runtime(std::unique_ptr<m605::detail::Transport> transport,
                         std::unique_ptr<m605::detail::SettleWait> settle_wait)
    : M605Runtime(std::move(transport), std::move(settle_wait),
                  std::make_unique<MemorySafetyLatch>()) {}

M605Runtime::M605Runtime(std::unique_ptr<m605::detail::Transport> transport,
                         std::unique_ptr<m605::detail::SettleWait> settle_wait,
                         std::unique_ptr<m605::detail::SafetyLatch> safety_latch)
    : transport_(std::move(transport)),
      settle_wait_(std::move(settle_wait)),
      safety_latch_(std::move(safety_latch)) {
    if (safety_latch_->IsQuarantined()) {
        health_ = M605RuntimeHealth::PersistentSafetyQuarantine;
        last_error_ = "Persistent M605 safety quarantine: external device resynchronization must be acknowledged";
    }
    worker_ = std::thread(&M605Runtime::WorkerLoop, this);
}

M605Runtime::~M605Runtime() { Stop(); }

void M605Runtime::Stop() {
    std::lock_guard<std::mutex> stop_lock(stop_mutex_);
    std::queue<Job> cancelled;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        if (health_ != M605RuntimeHealth::TransactionInProgress) {
            health_ = M605RuntimeHealth::Stopped;
        }
        queue_.swap(cancelled);
    }
    ResolveCancelled(cancelled);
    queue_cv_.notify_one();
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        health_ = M605RuntimeHealth::Stopped;
    }
}

std::future<bool> M605Runtime::SetPerKeyActuation(uint16_t logical_key_id, double millimeters) {
    auto report = m605::BuildPerKeyActuation(logical_key_id, millimeters);
    if (!report) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (health_ == M605RuntimeHealth::Clean ||
            health_ == M605RuntimeHealth::TransactionInProgress) {
            last_error_ = "Unverified key or actuation outside 0.1..4.0 mm";
        }
        return RejectedOperation();
    }
    Job job{};
    job.stages[0] = *report;
    job.stage_count = 1;
    job.kind = Kind::Actuation;
    job.logical_key_id = logical_key_id;
    job.value = (*report)[7];
    return Enqueue(std::move(job));
}

std::future<bool> M605Runtime::SetAnalogEffect(uint8_t effect_id, bool enabled) {
    auto report = m605::BuildAnalogEffect(effect_id, enabled ? 1 : 0);
    if (!report) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (health_ == M605RuntimeHealth::Clean ||
            health_ == M605RuntimeHealth::TransactionInProgress) {
            last_error_ = "Only Static effect ID 0 has verified Analog Effect support";
        }
        return RejectedOperation();
    }
    Job job{};
    job.stages[0] = *report;
    job.stage_count = 1;
    job.kind = Kind::Analog;
    job.value = enabled ? 1 : 0;
    return Enqueue(std::move(job));
}

std::future<bool> M605Runtime::SetPerKeyRapidTrigger(
    uint16_t logical_key_id, double press_mm, double release_mm) {
    return EnqueueRapidTrigger(logical_key_id, press_mm, release_mm, true);
}

std::future<bool> M605Runtime::DisablePerKeyRapidTrigger(
    uint16_t logical_key_id, double inherited_press_mm, double inherited_release_mm) {
    return EnqueueRapidTrigger(logical_key_id, inherited_press_mm, inherited_release_mm, false);
}

std::future<bool> M605Runtime::EnqueueRapidTrigger(
    uint16_t logical_key_id, double press_mm, double release_mm, bool enabled) {
    auto reports = m605::BuildPerKeyRapidTriggerStages(
        logical_key_id, press_mm, release_mm, enabled);
    if (!reports) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (health_ == M605RuntimeHealth::Clean ||
            health_ == M605RuntimeHealth::TransactionInProgress) {
            last_error_ = "Unverified key or Rapid Trigger value outside 0.1..2.5 mm";
        }
        return RejectedOperation();
    }
    Job job{};
    job.stages[0] = (*reports)[0];
    job.stages[1] = (*reports)[1];
    job.stage_count = 2;
    job.kind = Kind::RapidTrigger;
    job.logical_key_id = logical_key_id;
    return Enqueue(std::move(job));
}

std::future<bool> M605Runtime::SetPerKeyDeadzone(
    uint16_t logical_key_id, double top_mm, double bottom_mm) {
    auto report = m605::BuildPerKeyDeadzone(logical_key_id, top_mm, bottom_mm);
    if (!report) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (health_ == M605RuntimeHealth::Clean ||
            health_ == M605RuntimeHealth::TransactionInProgress) {
            last_error_ = "Unverified key or Deadzone value outside 0.0..0.5 mm";
        }
        return RejectedOperation();
    }
    Job job{};
    job.stages[0] = *report;
    job.stage_count = 1;
    job.kind = Kind::Deadzone;
    job.logical_key_id = logical_key_id;
    return Enqueue(std::move(job));
}

std::future<bool> M605Runtime::ResetAllPerKeyDeadzoneOverrides(
    uint8_t global_bottom_raw, uint8_t global_top_raw, uint8_t layer) {
    auto report = m605::BuildResetAllPerKeyDeadzoneOverrides(
        global_bottom_raw, global_top_raw, layer);
    if (!report) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (health_ == M605RuntimeHealth::Clean ||
            health_ == M605RuntimeHealth::TransactionInProgress) {
            last_error_ = "Reset ALL per-key Deadzone overrides requires global raw 0..5 and layer 0";
        }
        return RejectedOperation();
    }
    Job job{};
    job.stages[0] = *report;
    job.stage_count = 1;
    job.kind = Kind::ResetAllDeadzone;
    return Enqueue(std::move(job));
}

std::future<bool> M605Runtime::SetSpeedTapPair(
    uint16_t logical_key_1, uint16_t logical_key_2) {
    return EnqueueSpeedTapPair(logical_key_1, logical_key_2, true);
}

std::future<bool> M605Runtime::DisableSpeedTapPair(
    uint16_t logical_key_1, uint16_t logical_key_2) {
    return EnqueueSpeedTapPair(logical_key_1, logical_key_2, false);
}

std::future<bool> M605Runtime::EnqueueSpeedTapPair(
    uint16_t logical_key_1, uint16_t logical_key_2, bool enabled) {
    auto report = m605::BuildSpeedTapPair(logical_key_1, logical_key_2, enabled ? 1 : 0);
    if (!report) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (health_ == M605RuntimeHealth::Clean ||
            health_ == M605RuntimeHealth::TransactionInProgress) {
            last_error_ = "SpeedTap pair requires two distinct verified logical keys";
        }
        return RejectedOperation();
    }
    Job job{};
    job.stages[0] = *report;
    job.stage_count = 1;
    job.kind = Kind::SpeedTapPair;
    job.logical_key_id = logical_key_1;
    job.other_key_id = logical_key_2;
    job.value = enabled ? 1 : 0;
    return Enqueue(std::move(job));
}

std::future<bool> M605Runtime::SetSpeedTapMaster(bool enabled) {
    auto report = m605::BuildSpeedTapMaster(enabled ? 1 : 0);
    Job job{};
    job.stages[0] = *report;
    job.stage_count = 1;
    job.kind = Kind::SpeedTapMaster;
    job.value = enabled ? 1 : 0;
    return Enqueue(std::move(job));
}

std::future<bool> M605Runtime::ResetSpeedTapRuntimeToProfile() {
    Job job{};
    job.stages[0] = m605::BuildResetSpeedTapRuntimeToProfile();
    job.stage_count = 1;
    job.kind = Kind::SpeedTapProfileReset;
    return Enqueue(std::move(job));
}

std::future<bool> M605Runtime::SetPerKeyDks(const m605::DksConfig& config) {
    return EnqueueDks(config, false);
}

std::future<bool> M605Runtime::RestorePerKeyDksToStandard(uint16_t logical_key_id) {
    return EnqueueDks(m605::StandardDksConfiguration(logical_key_id), true);
}

std::future<bool> M605Runtime::EnqueueDks(const m605::DksConfig& config,
                                          bool standard_rewrite) {
    // Build and validate the whole transaction before queueing or opening HID.
    const auto stages = m605::BuildPerKeyDksStages(config);
    const auto apply = m605::BuildRuntimeApply();
    bool valid = stages.has_value() &&
        NativeHidBackend::IsSupportedOutputReport(apply);
    if (valid) {
        for (const auto& stage : *stages) {
            if (!NativeHidBackend::IsSupportedOutputReport(stage)) valid = false;
        }
    }
    if (!valid) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (health_ == M605RuntimeHealth::Clean ||
            health_ == M605RuntimeHealth::TransactionInProgress) {
            last_error_ = "Invalid DKS source, target, threshold, trigger state or slot";
        }
        return RejectedOperation();
    }
    Job job{};
    job.stages = *stages;
    job.apply = apply;
    job.stage_count = 4;
    job.kind = Kind::Dks;
    job.logical_key_id = config.source_logical_key_id;
    job.dks_config = config;
    job.standard_dks_rewrite = standard_rewrite;
    return Enqueue(std::move(job));
}

std::future<bool> M605Runtime::SetGlobalActuation(double millimeters) {
    auto report = m605::BuildGlobalActuation(millimeters);
    if (!report || !NativeHidBackend::IsSupportedOutputReport(*report)) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (health_ == M605RuntimeHealth::Clean ||
            health_ == M605RuntimeHealth::TransactionInProgress) {
            last_error_ = "Global actuation outside 0.1..4.0 mm";
        }
        return RejectedOperation();
    }
    Job job{};
    job.stages[0] = *report;
    job.stage_count = 1;
    job.kind = Kind::GlobalActuation;
    job.value = (*report)[5];
    return Enqueue(std::move(job));
}

std::future<bool> M605Runtime::SetGlobalDeadzone(double top_mm, double bottom_mm) {
    auto report = m605::BuildGlobalDeadzone(top_mm, bottom_mm);
    if (!report || !NativeHidBackend::IsSupportedOutputReport(*report)) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (health_ == M605RuntimeHealth::Clean ||
            health_ == M605RuntimeHealth::TransactionInProgress) {
            last_error_ = "Global deadzone outside 0.0..0.5 mm";
        }
        return RejectedOperation();
    }
    Job job{};
    job.stages[0] = *report;
    job.stage_count = 1;
    job.kind = Kind::GlobalDeadzone;
    return Enqueue(std::move(job));
}

std::future<bool> M605Runtime::SetGlobalRapidTrigger(
    double press_mm, double release_mm, double top_mm, double bottom_mm, bool separate_mode) {
    auto report = m605::BuildGlobalRapidTrigger(press_mm, release_mm, top_mm, bottom_mm, separate_mode);
    if (!report || !NativeHidBackend::IsSupportedOutputReport(*report)) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (health_ == M605RuntimeHealth::Clean ||
            health_ == M605RuntimeHealth::TransactionInProgress) {
            last_error_ = "Global Rapid Trigger parameters invalid or outside supported range";
        }
        return RejectedOperation();
    }
    Job job{};
    job.stages[0] = *report;
    job.stage_count = 1;
    job.kind = Kind::GlobalRapidTrigger;
    return Enqueue(std::move(job));
}

std::future<bool> M605Runtime::Enqueue(Job job) {
    auto future = job.completion.get_future();
    bool queued = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            last_error_ = "M605 runtime is stopped";
        } else if (health_ == M605RuntimeHealth::IndeterminateStagedState ||
                   health_ == M605RuntimeHealth::PersistentSafetyQuarantine) {
            last_error_ = kUnknownStateError;
        } else {
            queue_.push(std::move(job));
            queued = true;
        }
    }
    if (queued) {
        queue_cv_.notify_one();
    } else {
        job.completion.set_value(false);
    }
    return future;
}

void M605Runtime::WorkerLoop() {
    for (;;) {
        Job job{};
        {
            std::unique_lock<std::mutex> lock(mutex_);
            queue_cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_) return; // Stop resolved every queued promise.
            job = std::move(queue_.front());
            queue_.pop();
        }

        // Also held by PushFrame for the entire frame. A queued job only
        // becomes in-flight after acquiring this lock and rechecking Stop.
        std::lock_guard<std::mutex> device_lock(NativeHidBackend::DeviceWriteMutex());
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_ || health_ == M605RuntimeHealth::IndeterminateStagedState ||
                health_ == M605RuntimeHealth::PersistentSafetyQuarantine) {
                job.completion.set_value(false);
                continue;
            }
            health_ = M605RuntimeHealth::TransactionInProgress;
        }

        const bool success = Execute(job);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) {
                health_ = M605RuntimeHealth::Stopped;
            } else if (health_ == M605RuntimeHealth::TransactionInProgress) {
                health_ = M605RuntimeHealth::Clean;
            }
        }
        job.completion.set_value(success);
    }
}

bool M605Runtime::Execute(const Job& job) {
    if (job.stage_count == 0 || job.stage_count > job.stages.size()) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_ = "Invalid internal M605 stage count";
        return false; // Fail closed before opening a handle or writing a report.
    }
    if (!transport_->IsConnected()) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            applied_state_ = {}; // A new handle is not device readback.
        }
        if (!transport_->Connect()) {
            std::lock_guard<std::mutex> lock(mutex_);
            last_error_ = transport_->GetLastError();
            return false; // No stage attempted; a later request may retry.
        }
    }
    // From this point a stage may be submitted. A crash after Arm remains
    // quarantined on the next daemon start, even before the first WriteFile.
    if (!safety_latch_->Arm()) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_ = "Durable safety latch could not be established; no device write was attempted";
        return false;
    }
    for (uint8_t i = 0; i < job.stage_count; ++i) {
        if (!transport_->WriteStage(job.stages[i])) {
            const std::string error = transport_->GetLastError();
            transport_->Disconnect();
            MarkIndeterminate("stage " + std::to_string(i + 1) +
                              " write did not complete: " + error);
            return false;
        }
        if (job.kind == Kind::Dks && i + 1 < job.stage_count) {
            settle_wait_->WaitBetweenDksStages();
        }
    }
    // RT stages are consecutive; DKS has three explicit inter-stage waits.
    // The pre-apply interval begins after the final stage, with the shared
    // device lock held throughout.
    settle_wait_->WaitBeforeApply();
    if (!transport_->WriteApply(job.apply)) {
        const std::string error = transport_->GetLastError();
        transport_->Disconnect();
        MarkIndeterminate("apply write did not complete after a successful stage: " + error);
        return false;
    }
    // WriteFile success establishes host submission only. Keep the shared
    // device lock and transaction health until vendor-compatible settling ends.
    settle_wait_->WaitAfterApply();
    if (!safety_latch_->Clear()) {
        MarkIndeterminate("M605 sequence submitted but safety latch could not be cleared");
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (job.kind == Kind::Actuation) {
            applied_state_.per_key_actuation_raw[job.logical_key_id] = job.value;
        } else if (job.kind == Kind::Analog) {
            applied_state_.static_analog_effect = job.value != 0;
        } else if (job.kind == Kind::RapidTrigger) {
            applied_state_.per_key_rapid_trigger[job.logical_key_id] = {
                job.stages[0][9] != 0, job.stages[0][7], job.stages[1][7]};
        } else if (job.kind == Kind::Deadzone) {
            applied_state_.per_key_deadzone[job.logical_key_id] = {
                job.stages[0][8], job.stages[0][7]};
        } else if (job.kind == Kind::ResetAllDeadzone) {
            applied_state_.per_key_deadzone.clear();
        } else if (job.kind == Kind::SpeedTapPair) {
            applied_state_.speedtap_pair_submissions[
                {job.logical_key_id, job.other_key_id}] = job.value != 0;
        } else if (job.kind == Kind::SpeedTapMaster) {
            applied_state_.speedtap_master = job.value != 0;
        } else if (job.kind == Kind::SpeedTapProfileReset) {
            applied_state_.speedtap_pair_submissions.clear();
            applied_state_.speedtap_pair_knowledge =
                M605AppliedRuntimeState::SpeedTapPairKnowledge::ProfileBaselineUnknown;
        } else if (job.kind == Kind::Dks) {
            applied_state_.per_key_dks[job.logical_key_id] = {
                job.stages[0][5], job.stages[0][6], job.dks_config->slots,
                job.standard_dks_rewrite};
        } else if (job.kind == Kind::GlobalActuation) {
            applied_state_.global_actuation_raw = job.value;
        } else if (job.kind == Kind::GlobalDeadzone) {
            applied_state_.global_deadzone = {job.stages[0][5], job.stages[0][6]};
        } else if (job.kind == Kind::GlobalRapidTrigger) {
            applied_state_.global_rapid_trigger = {
                job.stages[0][3] != 0, job.stages[0][5], job.stages[0][6],
                job.stages[0][7], job.stages[0][8]};
        }
        last_error_.clear();
    }
    return true;
}

void M605Runtime::MarkIndeterminate(std::string cause) {
    std::queue<Job> cancelled;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        applied_state_ = {};
        health_ = M605RuntimeHealth::IndeterminateStagedState;
        last_error_ = std::move(cause) + "; " + kUnknownStateError;
        queue_.swap(cancelled);
    }
    ResolveCancelled(cancelled);
}

void M605Runtime::ResolveCancelled(std::queue<Job>& cancelled) {
    while (!cancelled.empty()) {
        cancelled.front().completion.set_value(false);
        cancelled.pop();
    }
}

M605RuntimeHealth M605Runtime::GetHealth() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return health_;
}

bool M605Runtime::IsPersistentSafetyQuarantined() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!safety_latch_ || !safety_latch_->IsQuarantined()) {
        return false;
    }
    return health_ != M605RuntimeHealth::TransactionInProgress;
}

bool M605Runtime::AcknowledgeExternalResynchronization() {
    std::lock_guard<std::mutex> stop_lock(stop_mutex_);
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ || health_ == M605RuntimeHealth::TransactionInProgress || !queue_.empty())
        return false;
    if (!safety_latch_->IsQuarantined() || !safety_latch_->Clear()) return false;
    applied_state_ = {};
    health_ = M605RuntimeHealth::Clean;
    last_error_.clear();
    return true;
}

M605AppliedRuntimeState M605Runtime::GetAppliedRuntimeState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return applied_state_;
}

std::string M605Runtime::GetLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

} // namespace aura
