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
#include <utility>

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
    virtual bool WriteApply(const Report& report) = 0;
    virtual void Disconnect() = 0;
    virtual std::string GetLastError() const = 0;
};

class SettleWait {
public:
    virtual ~SettleWait() = default;
    virtual void WaitBetweenDksStages() = 0;
    virtual void WaitBeforeApply() = 0;
    virtual void WaitAfterApply() = 0;
};

// Armed before a transaction can submit its first stage. An uncleared latch
// quarantines a newly created runtime; it is not device state or readback.
class SafetyLatch {
public:
    virtual ~SafetyLatch() = default;
    virtual bool IsQuarantined() const = 0;
    virtual bool Arm() = 0;
    virtual bool Clear() = 0;
};
} // namespace m605::detail

struct M605RuntimeTestAccess;

enum class M605RuntimeHealth {
    Clean,
    TransactionInProgress,
    IndeterminateStagedState,
    PersistentSafetyQuarantine,
    Stopped
};

// AceHFXAura submitted stage + apply and completed both settle intervals.
// This is not device readback, MCU acknowledgement or physical confirmation.
struct M605AppliedRuntimeState {
    std::map<uint16_t, uint8_t> per_key_actuation_raw; // logical ID -> 0.1 mm units
    std::optional<bool> static_analog_effect;
    struct RapidTrigger {
        bool enabled = false;
        uint8_t press_raw = 0;
        uint8_t release_raw = 0;
    };
    struct Deadzone {
        uint8_t top_raw = 0;
        uint8_t bottom_raw = 0;
    };
    std::map<uint16_t, RapidTrigger> per_key_rapid_trigger; // logical IDs
    std::map<uint16_t, Deadzone> per_key_deadzone; // runtime-known overrides only
    enum class SpeedTapPairKnowledge { Unknown, ProfileBaselineUnknown };
    // Only AceHFXAura submissions, not the complete device/profile pair table.
    std::map<std::pair<uint16_t, uint16_t>, bool> speedtap_pair_submissions;
    SpeedTapPairKnowledge speedtap_pair_knowledge = SpeedTapPairKnowledge::Unknown;
    std::optional<bool> speedtap_master;
    struct Dks {
        uint8_t start_raw = 0;
        uint8_t end_raw = 0;
        std::array<m605::DksSlot, 4> slots{}; // logical targets, not device readback
        bool standard_runtime_configuration = false;
    };
    std::map<uint16_t, Dks> per_key_dks;

    // Phase 6.1A Global Magnetic Settings SessionApplied fields:
    std::optional<uint8_t> global_actuation_raw;
    struct GlobalDeadzone {
        uint8_t top_raw = 0;
        uint8_t bottom_raw = 0;
    };
    std::optional<GlobalDeadzone> global_deadzone;
    struct GlobalRapidTrigger {
        bool separate_mode = false;
        uint8_t press_raw = 0;
        uint8_t release_raw = 0;
        uint8_t top_raw = 0;
        uint8_t bottom_raw = 0;
    };
    std::optional<GlobalRapidTrigger> global_rapid_trigger;
};

class M605Runtime {
public:
    M605Runtime();
    ~M605Runtime();
    M605Runtime(const M605Runtime&) = delete;
    M605Runtime& operator=(const M605Runtime&) = delete;

    // A call is one hardware transaction with 210 ms before apply and 400 ms
    // after apply. DKS also has three 30 ms inter-stage waits. Its future
    // resolves only after the post-apply interval.
    // Do not call on every slider mouse-move. Await futures off the UI thread.
    std::future<bool> SetPerKeyActuation(uint16_t logical_key_id, double millimeters);
    std::future<bool> SetAnalogEffect(uint8_t effect_id, bool enabled);
    std::future<bool> SetPerKeyRapidTrigger(
        uint16_t logical_key_id, double press_mm, double release_mm);
    // Explicit inherited/global values are required; the device does not
    // provide a verified readback for them. Disable stages both values with
    // the enable flag clear; it is not a separate reset opcode.
    std::future<bool> DisablePerKeyRapidTrigger(
        uint16_t logical_key_id, double inherited_press_mm, double inherited_release_mm);
    std::future<bool> SetPerKeyDeadzone(
        uint16_t logical_key_id, double top_mm, double bottom_mm);
    // Destructive across the ENTIRE per-key Deadzone override table. The
    // caller supplies authoritative global raw values; only layer 0 is valid.
    std::future<bool> ResetAllPerKeyDeadzoneOverrides(
        uint8_t global_bottom_raw, uint8_t global_top_raw, uint8_t layer = 0);
    std::future<bool> SetSpeedTapPair(uint16_t logical_key_1, uint16_t logical_key_2);
    // Targeted disable of this ordered pair; does not reset other pairs.
    std::future<bool> DisableSpeedTapPair(uint16_t logical_key_1, uint16_t logical_key_2);
    // Independent master switch; OFF suspends behavior without deleting pairs.
    std::future<bool> SetSpeedTapMaster(bool enabled);
    // Restores runtime pair state toward the active-profile baseline. Baseline
    // pairs may remain active; this is not an empty-table or master-OFF API.
    std::future<bool> ResetSpeedTapRuntimeToProfile();
    // One fully preflighted four-stage transaction. Does not change RT state.
    std::future<bool> SetPerKeyDks(const m605::DksConfig& config);
    // Exact Stage 8B standard rewrite, not a factory/profile/default restore.
    std::future<bool> RestorePerKeyDksToStandard(uint16_t logical_key_id);

    // Phase 6.1A Global Magnetic Settings:
    std::future<bool> SetGlobalActuation(double millimeters);
    std::future<bool> SetGlobalDeadzone(double top_mm, double bottom_mm);
    std::future<bool> SetGlobalRapidTrigger(
        double press_mm, double release_mm, double top_mm, double bottom_mm, bool separate_mode);

    // Cancels queued work and waits for the current transaction to finish.
    // No pending job may start a hardware write after Stop begins.
    void Stop();
    M605RuntimeHealth GetHealth() const;
    bool IsPersistentSafetyQuarantined() const;
    // Developer/operator assertion that the physical device was externally
    // returned to a known-good state. This performs no device operation.
    bool AcknowledgeExternalResynchronization();

    M605AppliedRuntimeState GetAppliedRuntimeState() const;
    std::string GetLastError() const;

private:
    friend struct M605RuntimeTestAccess;
    M605Runtime(std::unique_ptr<m605::detail::Transport> transport,
                std::unique_ptr<m605::detail::SettleWait> settle_wait);
    M605Runtime(std::unique_ptr<m605::detail::Transport> transport,
                std::unique_ptr<m605::detail::SettleWait> settle_wait,
                std::unique_ptr<m605::detail::SafetyLatch> safety_latch);
    enum class Kind {
        Actuation, Analog, RapidTrigger, Deadzone, ResetAllDeadzone,
        SpeedTapPair, SpeedTapMaster, SpeedTapProfileReset, Dks,
        GlobalActuation, GlobalDeadzone, GlobalRapidTrigger
    };
    struct Job {
        std::array<m605::Report, 4> stages{};
        m605::Report apply = m605::BuildRuntimeApply();
        uint8_t stage_count = 0;
        Kind kind;
        uint16_t logical_key_id = 0;
        uint16_t other_key_id = 0;
        uint8_t value = 0;
        std::optional<m605::DksConfig> dks_config;
        bool standard_dks_rewrite = false;
        std::promise<bool> completion;
    };

    std::future<bool> EnqueueRapidTrigger(
        uint16_t logical_key_id, double press_mm, double release_mm, bool enabled);
    std::future<bool> EnqueueSpeedTapPair(
        uint16_t logical_key_1, uint16_t logical_key_2, bool enabled);
    std::future<bool> EnqueueDks(const m605::DksConfig& config, bool standard_rewrite);
    std::future<bool> Enqueue(Job job);
    void WorkerLoop();
    bool Execute(const Job& job); // called with DeviceWriteMutex held
    void MarkIndeterminate(std::string cause);
    static void ResolveCancelled(std::queue<Job>& cancelled);

    std::unique_ptr<m605::detail::Transport> transport_;
    std::unique_ptr<m605::detail::SettleWait> settle_wait_;
    std::unique_ptr<m605::detail::SafetyLatch> safety_latch_;
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
