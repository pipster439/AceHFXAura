#include "aura/hardware/m605_runtime.h"
#include <chrono>
#include <cstdio>
#include <utility>
#include <windows.h>

namespace aura {

// The only production implementation of the internal transport seam. It
// reuses NativeHidBackend's endpoint validation and overlapped write path.
class M605NativeTransport final : public m605::detail::Transport {
public:
    bool IsConnected() const override { return backend_.IsConnected(); }
    bool IsCurrentSession() const override { return backend_.ProbeCurrentM605Transport(); }
    bool Connect() override { return backend_.Connect(); }
    bool WriteStage(const m605::Report& report) override { return backend_.SendReport(report); }
    bool WriteApply(const m605::Report& report) override { return backend_.SendReport(report); }
    bool QueryBasicInfo(m605::Report& response) override { return backend_.QueryBasicInfo(response); }
    void Disconnect() override { backend_.Disconnect(); }
    std::string GetLastError() const override { return backend_.GetLastError(); }

private:
    NativeHidBackend backend_;
};

namespace {
using Clock = std::chrono::steady_clock;
double Milliseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
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

std::future<bool> M605Runtime::SetPerKeyRapidTriggerUnified(uint16_t key, double mm, bool enabled) {
    return EnqueueRapidTriggerSide(key, m605::BuildPerKeyRapidTriggerUnified(key, mm, enabled));
}
std::future<bool> M605Runtime::SetPerKeyRapidTriggerPress(uint16_t key, double mm, bool enabled) {
    return EnqueueRapidTriggerSide(key, m605::BuildPerKeyRapidTriggerPress(key, mm, enabled));
}
std::future<bool> M605Runtime::SetPerKeyRapidTriggerRelease(uint16_t key, double mm, bool enabled) {
    return EnqueueRapidTriggerSide(key, m605::BuildPerKeyRapidTriggerRelease(key, mm, enabled));
}
std::future<bool> M605Runtime::EnqueueRapidTriggerSide(uint16_t key, std::optional<m605::Report> report) {
    if (!report || !NativeHidBackend::IsSupportedOutputReport(*report)) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (health_ == M605RuntimeHealth::Clean || health_ == M605RuntimeHealth::TransactionInProgress)
            last_error_ = "Unverified key or Rapid Trigger value outside 0.1..2.5 mm";
        return RejectedOperation();
    }
    Job job{}; job.kind = Kind::RapidTrigger; job.logical_key_id = key;
    job.stages[0] = *report; job.stage_count = 1;
    return Enqueue(std::move(job));
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

std::future<bool> M605Runtime::ResetAllPerKeyActuationOverrides(double common_millimeters) {
    auto report = m605::BuildResetAllPerKeyActuationOverrides(common_millimeters);
    if (!report || !NativeHidBackend::IsSupportedOutputReport(*report)) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (health_ == M605RuntimeHealth::Clean ||
            health_ == M605RuntimeHealth::TransactionInProgress)
            last_error_ = "Reset per-key Actuation overrides requires common actuation 0.1..4.0 mm";
        return RejectedOperation();
    }
    Job job{};
    job.stages[0] = *report;
    job.stage_count = 1;
    job.kind = Kind::ResetAllActuation;
    job.value = (*report)[5];
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
    double, double, double, double, bool) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (health_ == M605RuntimeHealth::Clean || health_ == M605RuntimeHealth::TransactionInProgress)
        last_error_ = "Legacy 51 53 Rapid Trigger writer blocked: not physically validated";
    return RejectedOperation();
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
            job.queued_at = Clock::now();
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
        M605TimingSnapshot timing;
        timing.queue_wait_ms = Milliseconds(job.queued_at);

        // Also held by PushFrame for the entire frame. A queued job only
        // becomes in-flight after acquiring this lock and rechecking Stop.
        const auto lock_start = Clock::now();
        std::lock_guard<std::mutex> device_lock(NativeHidBackend::DeviceWriteMutex());
        timing.device_lock_wait_ms = Milliseconds(lock_start);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_ || health_ == M605RuntimeHealth::IndeterminateStagedState ||
                health_ == M605RuntimeHealth::PersistentSafetyQuarantine) {
                job.completion.set_value(false);
                continue;
            }
            health_ = M605RuntimeHealth::TransactionInProgress;
        }

        const bool success = Execute(job, timing);
        timing.transactions = 1;
        timing.total_ms = Milliseconds(job.queued_at);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            timing_.transactions += timing.transactions;
            timing_.total_ms += timing.total_ms;
            timing_.queue_wait_ms += timing.queue_wait_ms;
            timing_.device_lock_wait_ms += timing.device_lock_wait_ms;
            timing_.connect_ms += timing.connect_ms;
            timing_.safety_latch_ms += timing.safety_latch_ms;
            timing_.stage_submit_ms += timing.stage_submit_ms;
            timing_.inter_stage_settle_ms += timing.inter_stage_settle_ms;
            timing_.pre_apply_settle_ms += timing.pre_apply_settle_ms;
            timing_.apply_submit_ms += timing.apply_submit_ms;
            timing_.post_apply_settle_ms += timing.post_apply_settle_ms;
            if (!success) {
                const char* names[] = {"KeyActuation", "Analog", "RapidTrigger", "KeyDeadzone",
                    "ResetAllDeadzone", "SpeedTapPair", "SpeedTapMaster", "SpeedTapProfileReset",
                    "Dks", "AllKeyActuation", "AllKeyDeadzone", "GlobalRapidTrigger",
                    "ResetAllPerKeyActuationOverrides"};
                last_failed_kind_ = names[static_cast<size_t>(job.kind)];
                last_failed_logical_id_ = job.logical_key_id;
                last_failed_generation_ = session_generation_;
                last_failed_error_ = last_error_;
            }
            if (stopping_) {
                health_ = M605RuntimeHealth::Stopped;
            } else if (health_ == M605RuntimeHealth::TransactionInProgress) {
                health_ = M605RuntimeHealth::Clean;
            }
        }
        job.completion.set_value(success);
    }
}

bool M605Runtime::Execute(const Job& job, M605TimingSnapshot& timing) {
    const auto measure = [](auto&& call, double& destination) {
        const auto start = Clock::now();
        const bool result = call();
        destination += Milliseconds(start);
        return result;
    };
    if (job.stage_count == 0 || job.stage_count > job.stages.size()) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_ = "Invalid internal M605 stage count";
        return false; // Fail closed before opening a handle or writing a report.
    }
    if (!EnsureTransportReady(timing)) return false;
    // From this point a stage may be submitted. A crash after Arm remains
    // quarantined on the next daemon start, even before the first WriteFile.
    if (!measure([&] { return safety_latch_->Arm(); }, timing.safety_latch_ms)) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_ = "Durable safety latch could not be established; no device write was attempted";
        return false;
    }
    for (uint8_t i = 0; i < job.stage_count; ++i) {
        if (!measure([&] { return transport_->WriteStage(job.stages[i]); }, timing.stage_submit_ms)) {
            const std::string error = transport_->GetLastError();
            transport_->Disconnect();
            MarkIndeterminate("stage " + std::to_string(i + 1) +
                              " write did not complete: " + error);
            return false;
        }
        if (job.kind == Kind::Dks && i + 1 < job.stage_count) {
            measure([&] { settle_wait_->WaitBetweenDksStages(); return true; },
                timing.inter_stage_settle_ms);
        }
    }
    // RT stages are consecutive; DKS has three explicit inter-stage waits.
    // The pre-apply interval begins after the final stage, with the shared
    // device lock held throughout.
    measure([&] { settle_wait_->WaitBeforeApply(); return true; }, timing.pre_apply_settle_ms);
    if (!measure([&] { return transport_->WriteApply(job.apply); }, timing.apply_submit_ms)) {
        const std::string error = transport_->GetLastError();
        transport_->Disconnect();
        MarkIndeterminate("apply write did not complete after a successful stage: " + error);
        return false;
    }
    // WriteFile success establishes host submission only. Keep the shared
    // device lock and transaction health until vendor-compatible settling ends.
    measure([&] { settle_wait_->WaitAfterApply(); return true; }, timing.post_apply_settle_ms);
    if (!transport_->IsCurrentSession()) {
        transport_->Disconnect();
        MarkIndeterminate("transport removed or re-enumerated before the transaction settled");
        return false;
    }
    if (!measure([&] { return safety_latch_->Clear(); }, timing.safety_latch_ms)) {
        MarkIndeterminate("M605 sequence submitted but safety latch could not be cleared");
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (job.kind == Kind::Actuation) {
            applied_state_.per_key_actuation_raw[job.logical_key_id] = job.value;
        } else if (job.kind == Kind::ResetAllActuation) {
            applied_state_.global_actuation_raw = job.value;
            applied_state_.per_key_actuation_raw.clear();
            applied_state_.per_key_actuation_table_known = true;
        } else if (job.kind == Kind::Analog) {
            applied_state_.static_analog_effect = job.value != 0;
        } else if (job.kind == Kind::RapidTrigger) {
            auto& rt = applied_state_.per_key_rapid_trigger[job.logical_key_id];
            for (uint8_t i = 0; i < job.stage_count; ++i) {
                const auto& r = job.stages[i];
                rt.enabled = r[9] != 0; rt.continuous = false;
                if (r[3] == 0 || r[3] == 1) { rt.press_raw = r[7]; rt.press_known = true; }
                if (r[3] == 0 || r[3] == 2) { rt.release_raw = r[7]; rt.release_known = true; }
            }
        } else if (job.kind == Kind::Deadzone) {
            applied_state_.per_key_deadzone[job.logical_key_id] = {
                job.stages[0][8], job.stages[0][7]};
        } else if (job.kind == Kind::ResetAllDeadzone) {
            applied_state_.per_key_deadzone.clear();
            applied_state_.per_key_deadzone_table_known = true;
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
            // 51 50 changes the common base, not the 51 4F override table.
        } else if (job.kind == Kind::GlobalDeadzone) {
            applied_state_.global_deadzone = {job.stages[0][5], job.stages[0][6]};
            // 51 58 likewise does not prove per-key override removal.

        }
        last_error_.clear();
    }
    return true;
}

bool M605Runtime::EnsureTransportReady(M605TimingSnapshot& timing) {
    const auto start = Clock::now();
    DiscardStaleTransport();
    if (!transport_->IsConnected()) {
        if (!transport_->Connect()) {
            timing.connect_ms += Milliseconds(start);
            std::lock_guard<std::mutex> lock(mutex_);
            last_error_ = transport_->GetLastError();
            return false; // No stage or safety latch attempted.
        }
        transport_session_open_ = true;
        std::lock_guard<std::mutex> lock(mutex_);
        applied_state_ = {};
        ++session_generation_;
        diagnostic_transport_open_ = true;
        last_open_generation_ = session_generation_;
        RecordSessionTransitionLocked("transport_opened");
    }
    timing.connect_ms += Milliseconds(start);
    return true;
}

void M605Runtime::DiscardStaleTransport() {
    if (!transport_session_open_ ||
        (transport_->IsConnected() && transport_->IsCurrentSession())) return;
    transport_->Disconnect();
    transport_session_open_ = false;
    std::lock_guard<std::mutex> lock(mutex_);
    applied_state_ = {};
    ++session_generation_;
    diagnostic_transport_open_ = false;
    RecordSessionTransitionLocked("stale_transport_discarded");
}

void M605Runtime::RefreshTransportPresence() {
    std::unique_lock<std::mutex> device_lock(NativeHidBackend::DeviceWriteMutex(), std::try_to_lock);
    if (!device_lock.owns_lock()) return; // never interrupt an in-flight sequence
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || health_ != M605RuntimeHealth::Clean) return;
    }
    DiscardStaleTransport();
}

bool M605Runtime::PrepareTransportSession() {
    std::lock_guard<std::mutex> device_lock(NativeHidBackend::DeviceWriteMutex());
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || health_ != M605RuntimeHealth::Clean || !queue_.empty()) return false;
    }
    M605TimingSnapshot ignored;
    return EnsureTransportReady(ignored);
}

HardwareSlotResult M605Runtime::SelectHardwareProfileSlot(uint8_t slot,
    const std::function<bool()>& admission) {
    if (!m605::BuildSelectHardwareProfileSlot(slot)) {
        HardwareSlotResult result; result.error = "Invalid hardware slot; expected1..6";
        return result;
    }
    return RunHardwareSlot(slot, admission);
}
HardwareSlotResult M605Runtime::QueryHardwareProfileSlot() {
    return RunHardwareSlot(std::nullopt, [] { return true; });
}
HardwareSlotResult M605Runtime::GetHardwareSlotObservation() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto result = hardware_slot_observation_;
    if (result.session_generation != session_generation_ || health_ != M605RuntimeHealth::Clean) {
        result.observed_slot.reset(); result.success = false;
    }
    return result;
}
HardwareSlotResult M605Runtime::RunHardwareSlot(std::optional<uint8_t> requested,
    const std::function<bool()>& admission) {
    const auto started = Clock::now();
    HardwareSlotResult result; result.requested_slot = requested;
    std::lock_guard<std::mutex> stop_lock(stop_mutex_);
    std::lock_guard<std::mutex> device_lock(NativeHidBackend::DeviceWriteMutex());
    const auto finish = [&] {
        result.total_ms = Milliseconds(started);
        std::lock_guard<std::mutex> lock(mutex_);
        result.session_generation = session_generation_;
        if (hardware_slot_observation_.observed_slot && result.observed_slot &&
            hardware_slot_observation_.observed_slot != result.observed_slot) applied_state_ = {};
        hardware_slot_observation_ = result;
        if (!result.success) last_error_ = result.error; else last_error_.clear();
        return result;
    };
    bool permitted;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        permitted = !stopping_ && health_ == M605RuntimeHealth::Clean && queue_.empty() &&
            !safety_latch_->IsQuarantined();
    }
    if (!permitted) { result.error = "M605 unhealthy, busy or safety quarantined"; return finish(); }
    M605TimingSnapshot ignored;
    if (!EnsureTransportReady(ignored)) { result.error = GetLastError(); return finish(); }
    const auto generation = GetSessionGeneration();
    const auto query = [&] {
        m605::Report report{}; ++result.query_count;
        if (!transport_->IsCurrentSession() || !transport_->QueryBasicInfo(report) || !transport_->IsCurrentSession()) {
            result.observed_slot.reset(); result.error = "BasicInfo unavailable: " + transport_->GetLastError(); return false;
        }
        result.observed_slot = m605::ParseBasicInfoActiveSlot(report.data(), report.size());
        if (!result.observed_slot) { result.error = "Malformed BasicInfo active slot"; return false; }
        SYSTEMTIME time{}; GetSystemTime(&time); char text[40]{};
        std::snprintf(text, sizeof(text), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
            time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds);
        result.observed_at_utc = text;
        return true;
    };
    if (!query()) return finish();
    if (!admission()) { result.error = "StaleDecisionBeforeSubmission"; return finish(); }
    if (!requested || result.observed_slot == requested) { result.success = true; return finish(); }
    // A bank switch invalidates all host magnetic submission knowledge even
    // if its write/verification subsequently fails. It is not a staged write.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        applied_state_ = {};
    }
    if (!transport_->IsCurrentSession() || !admission()) {
        result.error = "Stale hardware slot admission/session"; return finish();
    }
    result.selector_sent = true;
    if (!transport_->WriteStage(*m605::BuildSelectHardwareProfileSlot(*requested))) {
        result.observed_slot.reset(); result.error = "Hardware selector write failed: " + transport_->GetLastError();
        return finish();
    }
    // Captured valid selections: observable at100.199/100.460/100.880ms;
    // nearby official selections observed within141.652ms. Initial100ms,
    // then up to3 query windows100ms each with100ms spacing: <=600ms
    // policy budget plus connect/write/cancellation. No210/400ms Apply waits.
    for (unsigned i=0; i<3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!query()) return finish();
        if (GetSessionGeneration() != generation) {
            result.observed_slot.reset();
            result.error = "Hardware slot transport generation changed"; return finish();
        }
        if (result.observed_slot == requested) { result.success = true; return finish(); }
    }
    result.error = "Hardware slot verification mismatch";
    return finish();
}

void M605Runtime::MarkIndeterminate(std::string cause) {
    std::queue<Job> cancelled;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        applied_state_ = {};
        health_ = M605RuntimeHealth::IndeterminateStagedState;
        ++session_generation_;
        diagnostic_transport_open_ = transport_->IsConnected(); // handle observation, no presence probe
        RecordSessionTransitionLocked("transaction_indeterminate");
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

bool M605Runtime::HasQueuedWork() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !queue_.empty();
}

bool M605Runtime::AcknowledgeExternalResynchronization() {
    std::lock_guard<std::mutex> stop_lock(stop_mutex_);
    std::unique_lock<std::mutex> device_lock(NativeHidBackend::DeviceWriteMutex(), std::try_to_lock);
    if (!device_lock.owns_lock()) return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || health_ == M605RuntimeHealth::TransactionInProgress || !queue_.empty() ||
            !safety_latch_->IsQuarantined()) return false;
    }
    // Operator confirmation of an external restore AND a newly validated
    // transport are required. Arrival alone never clears a safety latch.
    transport_->Disconnect();
    transport_session_open_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        diagnostic_transport_open_ = false;
    }
    if (!transport_->Connect()) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_ = "External resynchronization requires a fresh M605 transport: " +
            transport_->GetLastError();
        return false;
    }
    transport_session_open_ = true;
    std::lock_guard<std::mutex> lock(mutex_);
    applied_state_ = {};
    ++session_generation_; // freshly opened host session, even if latch clear fails
    diagnostic_transport_open_ = true;
    last_open_generation_ = session_generation_;
    RecordSessionTransitionLocked("external_resynchronization_transport_opened");
    if (!safety_latch_->Clear()) {
        last_error_ = "Fresh M605 transport opened but the persistent safety latch could not be cleared";
        return false;
    }
    health_ = M605RuntimeHealth::Clean;
    last_error_.clear();
    return true;
}

M605AppliedRuntimeState M605Runtime::GetAppliedRuntimeState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return applied_state_;
}

uint64_t M605Runtime::GetSessionGeneration() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return session_generation_;
}

M605TimingSnapshot M605Runtime::GetTimingSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return timing_;
}

std::string M605Runtime::GetLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

void M605Runtime::RecordSessionTransitionLocked(const char* reason) {
    if (session_transitions_.size() == 16) session_transitions_.erase(session_transitions_.begin());
    session_transitions_.push_back({session_generation_ - 1, session_generation_, reason});
}

M605DiagnosticSnapshot M605Runtime::GetDiagnosticSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {health_, safety_latch_ && safety_latch_->IsQuarantined() &&
        health_ != M605RuntimeHealth::TransactionInProgress,
        diagnostic_transport_open_, session_generation_, last_open_generation_,
        queue_.size(), applied_state_, timing_, last_error_, session_transitions_,
        last_failed_kind_, last_failed_error_, last_failed_logical_id_, last_failed_generation_, hardware_rt_gate_};
}

void M605Runtime::ObserveHardwareRtGate(const HardwareRtGateObservation& observation) {
    std::lock_guard<std::mutex> lock(mutex_);
    // A physical gate observation never changes host submissions or safety state.
    hardware_rt_gate_ = observation;
}
HardwareRtGateObservation M605Runtime::GetHardwareRtGateObservation() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hardware_rt_gate_;
}

} // namespace aura
