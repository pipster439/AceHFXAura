#include "aura/hardware/m605_runtime.h"
#include <chrono>
#include <utility>

namespace aura {

// The only production implementation of the internal transport seam. It
// reuses NativeHidBackend's endpoint validation and overlapped write path.
class M605NativeTransport final : public m605::detail::Transport {
public:
    bool IsConnected() const override { return backend_.IsConnected(); }
    bool Connect() override { return backend_.Connect(); }
    bool WriteStage(const m605::Report& report) override { return backend_.SendReport(report); }
    bool WriteApply() override { return backend_.SendReport(m605::BuildRuntimeApply()); }
    void Disconnect() override { backend_.Disconnect(); }
    std::string GetLastError() const override { return backend_.GetLastError(); }

private:
    NativeHidBackend backend_;
};

namespace {
class VendorSettleWait final : public m605::detail::SettleWait {
public:
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
    "external re-synchronization is required before creating a new runtime";
}

M605Runtime::M605Runtime()
    : M605Runtime(std::make_unique<M605NativeTransport>(),
                  std::make_unique<VendorSettleWait>()) {}

M605Runtime::M605Runtime(std::unique_ptr<m605::detail::Transport> transport,
                         std::unique_ptr<m605::detail::SettleWait> settle_wait)
    : transport_(std::move(transport)),
      settle_wait_(std::move(settle_wait)),
      worker_(&M605Runtime::WorkerLoop, this) {}

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
    job.stage = *report;
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
    job.stage = *report;
    job.kind = Kind::Analog;
    job.value = enabled ? 1 : 0;
    return Enqueue(std::move(job));
}

std::future<bool> M605Runtime::Enqueue(Job job) {
    auto future = job.completion.get_future();
    bool queued = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            last_error_ = "M605 runtime is stopped";
        } else if (health_ == M605RuntimeHealth::IndeterminateStagedState) {
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
            if (stopping_ || health_ == M605RuntimeHealth::IndeterminateStagedState) {
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
    if (!transport_->WriteStage(job.stage)) {
        const std::string error = transport_->GetLastError();
        transport_->Disconnect();
        MarkIndeterminate("stage write did not complete: " + error);
        return false;
    }
    settle_wait_->WaitBeforeApply();
    if (!transport_->WriteApply()) {
        const std::string error = transport_->GetLastError();
        transport_->Disconnect();
        MarkIndeterminate("apply write did not complete after a successful stage: " + error);
        return false;
    }
    // WriteFile success establishes host submission only. Keep the shared
    // device lock and transaction health until vendor-compatible settling ends.
    settle_wait_->WaitAfterApply();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (job.kind == Kind::Actuation) {
            applied_state_.per_key_actuation_raw[job.logical_key_id] = job.value;
        } else {
            applied_state_.static_analog_effect = job.value != 0;
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

M605AppliedRuntimeState M605Runtime::GetAppliedRuntimeState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return applied_state_;
}

std::string M605Runtime::GetLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

} // namespace aura
