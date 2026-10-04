#pragma once
#include "aura/hardware/m605_rt_gate.h"

#include "aura/hardware/m605_protocol.h"
#include "aura/native_hid_backend.h"
#include <chrono>
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
#include <vector>
#include <functional>

namespace aura {

namespace m605::detail {
// Internal seam for deterministic tests. Normal application code uses only
// M605Runtime's typed setters; no vendor-command send API is exposed.
class Transport {
public:
    virtual ~Transport() = default;
    virtual bool IsConnected() const = 0;
    // Read-only preflight of the existing handle against a currently opened
    // validated HID interface. False means the handle cannot be reused.
    virtual bool IsCurrentSession() const = 0;
    virtual bool Connect() = 0;
    virtual bool WriteStage(const Report& report) = 0;
    virtual bool WriteApply(const Report& report) = 0;
    virtual bool QueryBasicInfo(Report&) { return false; } // unsupported seams fail closed
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

struct HardwareSlotResult {
    bool success = false, selector_sent = false;
    std::optional<uint8_t> requested_slot, observed_slot;
    uint64_t session_generation = 0;
    unsigned query_count = 0;
    double total_ms = 0;
    std::string observed_at_utc, error;
};

enum class M605RuntimeHealth {
    Clean,
    TransactionInProgress,
    IndeterminateStagedState,
    PersistentSafetyQuarantine,
    Stopped
};

// Cumulative host-side timings for completed worker jobs. These measure
// software/transport calls and mandated waits, not firmware acknowledgements.
struct M605TimingSnapshot {
    uint64_t transactions = 0;
    double total_ms = 0;
    double queue_wait_ms = 0;
    double device_lock_wait_ms = 0;
    double connect_ms = 0;
    double safety_latch_ms = 0;
    double stage_submit_ms = 0;
    double inter_stage_settle_ms = 0;
    double pre_apply_settle_ms = 0;
    double apply_submit_ms = 0;
    double post_apply_settle_ms = 0;
};

// AceHFXAura submitted stage + apply and completed both settle intervals.
// This is not device readback, MCU acknowledgement or physical confirmation.
struct M605AppliedRuntimeState {
    std::map<uint16_t, uint8_t> per_key_actuation_raw; // logical ID -> 0.1 mm units
    bool per_key_actuation_table_known = false; // host ownership, only after type 1 reset
    std::optional<bool> static_analog_effect;
    struct RapidTrigger {
        bool enabled = false;
        uint8_t press_raw = 0;
        uint8_t release_raw = 0;
        bool press_known = false, release_known = false;
        bool continuous = false; // Captured OFF only; no production ON API.
    };
    struct Deadzone {
        uint8_t top_raw = 0;
        uint8_t bottom_raw = 0;
    };
    std::map<uint16_t, RapidTrigger> per_key_rapid_trigger; // logical IDs
    std::map<uint16_t, Deadzone> per_key_deadzone; // runtime-known overrides only
    bool per_key_deadzone_table_known = false; // true only after verified resetType 4
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
    // Legacy compatibility DTO only. No production writer populates this field.
    struct GlobalRapidTrigger {
        bool separate_mode = false;
        uint8_t press_raw = 0;
        uint8_t release_raw = 0;
        uint8_t top_raw = 0;
        uint8_t bottom_raw = 0;
    };
    std::optional<GlobalRapidTrigger> global_rapid_trigger;
};

// Cached host observations only. Reading this snapshot never probes/opens HID.
struct M605SessionTransition {
    uint64_t from_generation = 0, to_generation = 0;
    std::string reason;
};
struct M605DiagnosticSnapshot {
    M605RuntimeHealth health;
    bool persistent_safety_quarantine = false;
    bool transport_open_at_last_observation = false;
    uint64_t session_generation = 0, last_open_generation = 0;
    size_t queued_jobs = 0;
    M605AppliedRuntimeState session_applied;
    M605TimingSnapshot timing;
    std::string last_error;
    std::vector<M605SessionTransition> transitions;
    std::string last_failed_kind, last_failed_error;
    uint16_t last_failed_logical_id = 0;
    uint64_t last_failed_generation = 0;
    HardwareRtGateObservation hardware_rt_gate; // passive input; NOT SessionApplied
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
    std::future<bool> SetPerKeyRapidTriggerUnified(uint16_t key, double sensitivity, bool enabled);
    std::future<bool> SetPerKeyRapidTriggerPress(uint16_t key, double press, bool enabled);
    std::future<bool> SetPerKeyRapidTriggerRelease(uint16_t key, double release, bool enabled);
    // Explicit stored values are required; there is no firmware RT inheritance
    // or verified readback. Disable stages both values with
    // the enable flag clear; it is not a separate reset opcode.
    std::future<bool> DisablePerKeyRapidTrigger(
        uint16_t logical_key_id, double inherited_press_mm, double inherited_release_mm);
    std::future<bool> SetPerKeyDeadzone(
        uint16_t logical_key_id, double top_mm, double bottom_mm);
    // Clear layer-0 Actuation overrides and carry trusted common actuation.
    std::future<bool> ResetAllPerKeyActuationOverrides(double common_millimeters);
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
    // Compatibility rejection stub: never queues or writes 51 53.
    std::future<bool> SetGlobalRapidTrigger(
        double press_mm, double release_mm, double top_mm, double bottom_mm, bool separate_mode);

    // Cancels queued work and waits for the current transaction to finish.
    // No pending job may start a hardware write after Stop begins.
    void Stop();
    M605RuntimeHealth GetHealth() const;
    bool IsPersistentSafetyQuarantined() const;
    bool HasQueuedWork() const;
    // Developer/operator assertion that the physical device was externally
    // returned to a known-good state. This performs no device operation.
    bool AcknowledgeExternalResynchronization();

    M605AppliedRuntimeState GetAppliedRuntimeState() const;
    uint64_t GetSessionGeneration() const;
    // Before Profile diff planning, refresh a stale/removed handle without
    // submitting a stage. Also used by the worker before every transaction.
    bool PrepareTransportSession();
    // Observe idle removal on status reads, without opening or writing HID.
    void RefreshTransportPresence();
    M605TimingSnapshot GetTimingSnapshot() const;
    std::string GetLastError() const;
    M605DiagnosticSnapshot GetDiagnosticSnapshot() const;
    void ObserveHardwareRtGate(const HardwareRtGateObservation& observation);
    HardwareRtGateObservation GetHardwareRtGateObservation() const;
    // Independent unstaged operation: no Apply and no magnetic settle waits.
    HardwareSlotResult SelectHardwareProfileSlot(uint8_t slot,
        const std::function<bool()>& admission = [] { return true; });
    HardwareSlotResult QueryHardwareProfileSlot(); // explicit refresh only, never diagnostics GET
    HardwareSlotResult GetHardwareSlotObservation() const; // cached, pure read

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
        GlobalActuation, GlobalDeadzone, GlobalRapidTrigger, ResetAllActuation
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
        std::chrono::steady_clock::time_point queued_at{};
        std::promise<bool> completion;
    };

    std::future<bool> EnqueueRapidTriggerSide(uint16_t key, std::optional<m605::Report> report);
    std::future<bool> EnqueueRapidTrigger(
        uint16_t logical_key_id, double press_mm, double release_mm, bool enabled);
    std::future<bool> EnqueueSpeedTapPair(
        uint16_t logical_key_1, uint16_t logical_key_2, bool enabled);
    std::future<bool> EnqueueDks(const m605::DksConfig& config, bool standard_rewrite);
    std::future<bool> Enqueue(Job job);
    void WorkerLoop();
    bool Execute(const Job& job, M605TimingSnapshot& timing); // DeviceWriteMutex held
    bool EnsureTransportReady(M605TimingSnapshot& timing); // DeviceWriteMutex held
    void DiscardStaleTransport(); // DeviceWriteMutex held
    void MarkIndeterminate(std::string cause);
    HardwareSlotResult RunHardwareSlot(std::optional<uint8_t> requested,
        const std::function<bool()>& admission);
    void RecordSessionTransitionLocked(const char* reason); // mutex_ held; diagnostics only
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
    HardwareRtGateObservation hardware_rt_gate_;
    uint64_t session_generation_ = 0; // host transport epoch, never firmware readback
    bool transport_session_open_ = false; // DeviceWriteMutex protects lifecycle
    bool diagnostic_transport_open_ = false; // cached under mutex_; not a live presence probe
    uint64_t last_open_generation_ = 0;
    std::vector<M605SessionTransition> session_transitions_; // last 16, process-local
    std::string last_failed_kind_, last_failed_error_;
    uint16_t last_failed_logical_id_ = 0;
    uint64_t last_failed_generation_ = 0;
    M605TimingSnapshot timing_;
    std::string last_error_;
    HardwareSlotResult hardware_slot_observation_;
    std::thread worker_;
};

} // namespace aura
