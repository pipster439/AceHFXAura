#pragma once

#include "aura/hardware/m605_protocol.h"
#include "aura/native_hid_backend.h"
#include <condition_variable>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>

namespace aura {

namespace m605::detail {
// Internal seam for deterministic tests. Normal application code uses only
// M605Runtime's typed setters; no vendor-command send API is exposed.
class Transport {
public:
    virtual ~Transport() = default;
    virtual bool IsConnected() const = 0;
    virtual bool Connect() = 0;
    virtual bool WriteStage(const Report& report) = 0;
    virtual bool WriteApply() = 0;
    virtual void Disconnect() = 0;
    virtual std::string GetLastError() const = 0;
};

class SettleWait {
public:
    virtual ~SettleWait() = default;
    virtual void WaitBeforeApply() = 0;
    virtual void WaitAfterApply() = 0;
};
} // namespace m605::detail

struct M605RuntimeTestAccess;

enum class M605RuntimeHealth {
    Clean,
    TransactionInProgress,
    IndeterminateStagedState,
    Stopped
};

// AceHFXAura submitted stage + apply and completed both settle intervals.
// This is not device readback, MCU acknowledgement or physical confirmation.
struct M605AppliedRuntimeState {
    std::map<uint16_t, uint8_t> per_key_actuation_raw; // logical ID -> 0.1 mm units
    std::optional<bool> static_analog_effect;
};

class M605Runtime {
public:
    M605Runtime();
    ~M605Runtime();
    M605Runtime(const M605Runtime&) = delete;
    M605Runtime& operator=(const M605Runtime&) = delete;

    // A call is one hardware transaction with 210 ms before apply and 400 ms
    // after apply. Its future resolves only after the post-apply interval.
    // Do not call on every slider mouse-move. Await futures off the UI thread.
    std::future<bool> SetPerKeyActuation(uint16_t logical_key_id, double millimeters);
    std::future<bool> SetAnalogEffect(uint8_t effect_id, bool enabled);

    // Cancels queued work and waits for the current transaction to finish.
    // No pending job may start a hardware write after Stop begins.
    void Stop();
    M605RuntimeHealth GetHealth() const;

    M605AppliedRuntimeState GetAppliedRuntimeState() const;
    std::string GetLastError() const;

private:
    friend struct M605RuntimeTestAccess;
    M605Runtime(std::unique_ptr<m605::detail::Transport> transport,
                std::unique_ptr<m605::detail::SettleWait> settle_wait);
    enum class Kind { Actuation, Analog };
    struct Job {
        m605::Report stage;
        Kind kind;
        uint16_t logical_key_id = 0;
        uint8_t value = 0;
        std::promise<bool> completion;
    };

    std::future<bool> Enqueue(Job job);
    void WorkerLoop();
    bool Execute(const Job& job); // called with DeviceWriteMutex held
    void MarkIndeterminate(std::string cause);
    static void ResolveCancelled(std::queue<Job>& cancelled);

    std::unique_ptr<m605::detail::Transport> transport_;
    std::unique_ptr<m605::detail::SettleWait> settle_wait_;
    mutable std::mutex mutex_;
    std::mutex stop_mutex_;
    std::condition_variable queue_cv_;
    std::queue<Job> queue_;
    bool stopping_ = false;
    M605RuntimeHealth health_ = M605RuntimeHealth::Clean;
    M605AppliedRuntimeState applied_state_;
    std::string last_error_;
    std::thread worker_;
};

} // namespace aura
