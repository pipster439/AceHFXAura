#pragma once

#include "aura/hardware/m605_runtime.h"
#include "config/magnetic_host_profile.h"
#include "config/device_profile_binding_engine.h"
#include "config/daemon_process_identity.h"
#include "third_party/httplib.h"
#include "third_party/json.hpp"
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace aura {

// The daemon's sole document writer and magnetic Profile apply coordinator.
// Mutations/apply use the same gate as manual typed routes. Explicit cached
// decision/status observation methods never probe or acquire hardware authority.
class DeviceProfileRuntime {
public:
    using Json = nlohmann::json;
    DeviceProfileRuntime(std::filesystem::path path, std::filesystem::path legacy_config,
        M605Runtime& device, std::mutex& mutation_gate,
        std::function<MagneticHostProfile()> host_profile,
        std::function<bool()> available, DeviceProfileBindingEngine::Clock decision_clock = {},
        DaemonProcessIdentity process_identity = DaemonProcessIdentity::Current());
    void RegisterRoutes(httplib::Server& server);
    void ExternalMutationLocked(); // caller already holds mutation_gate_
    // Called only after a successful manual global typed submission, under the same gate.
    void RecordManualGlobalBaselineLocked(const std::string& field, const Json& value);
    Json State();
    Json Diagnostics(); // pure cached read: no presence probe, mutation, or HID operation
    bool ObserveAutomationForeground(const std::string& process_name); // decisions ONLY
    Json AutomationAdmissionSnapshot() const; // cached token, not write permission
    Json ActivateAutomationDecision(const Json& token, const std::function<bool()>& stopping);
    void SetAutomationCoordinatorDiagnostics(std::function<Json()> snapshot);
    Json ActivateProfile(const std::string& id, const std::string& reason, int64_t expected_revision,
        const Json& temporary = nullptr, const std::function<bool()>& cancelled = [] { return false; });

private:
    enum class Kind { DksStandard, RapidTriggerOff, ResetAllActuation, KeyActuation,
        ResetAllDeadzone, AllKeyDeadzone, KeyDeadzone, RapidTriggerOn, DksSet, PerKeyRtUnified, PerKeyRtPress, PerKeyRtRelease };
    struct Operation {
        Kind kind;
        uint16_t key = 0;
        Json value;
        bool restore = false;
        std::string Identity() const;
        Json Summary(bool succeeded, std::string error = {}, bool recovery = false) const;
    };
    struct Plan {
        std::vector<Operation> operations;
        std::map<std::string, Operation> target;
        std::vector<std::string> blockers;
        Json actuation_ownership;
        Json rapid_trigger_management;
    };

    static void Validate(const Json& document);
    static void ValidateMagnetic(const Json& magnetic);
    static bool ValidGuid(const std::string& text);
    static std::string NewGuid();
    static Json DefaultDocument(const std::filesystem::path& legacy_config);
    static std::map<std::string, Operation> Resolve(const Json& defaults, const Json& profile,
        const Json& temporary);
    Plan BuildPlan(const Json& profile, const Json& temporary, const MagneticHostProfile& host);
    bool Execute(const Operation& op, const MagneticHostProfile& host);
    bool Matches(const Operation& op, const M605AppliedRuntimeState& shadow) const;
    Json ActivateLocked(const std::string& id, const std::string& reason,
        const Json& temporary, const std::function<bool()>& cancelled,
        const std::function<bool()>& admission = {});
    bool AutomationTokenFreshLocked(const Json& token) const;
    Json AutomationDecisionSnapshotCached() const; // binding/coordinator caches only; no gate required
    void InvalidateLocked();
    void SynchronizeGenerationLocked();
    void CommitLocked(Json next, int64_t expected_revision);
    void MigrateLegacyRtLocked(); // document-only, backed up, revisioned; never touches device
    void TrackDeclaredFootprintLocked();
    void RefreshAutomationConfigurationLocked();
    Json StateLocked();
    Json StateSnapshotLocked() const;
    Json AutomationSnapshotLocked() const; // cached config/decision only, no baseline callback
    Json EffectiveBaselineLocked() const;
    Json HandleMutation(const std::string& action, const Json& request);
    void Reply(httplib::Response& response, const Json& body, int code) const;

    std::filesystem::path path_;
    M605Runtime& device_;
    std::mutex& mutation_gate_;
    std::function<MagneticHostProfile()> host_profile_;
    std::function<bool()> available_;
    DeviceProfileBindingEngine binding_engine_; // read-only decision subsystem, no actuator
    std::function<Json()> coordinator_snapshot_; // installed before worker/startup
    const DaemonProcessIdentity process_identity_;
    bool automation_configuration_available_ = true;
    Json document_;
    std::string load_error_;
    std::string import_warning_;
    Json legacy_rt_migration_; // cached load/migration result, not hardware state
    std::optional<std::string> active_;
    bool dirty_ = true;
    bool rt_submission_valid_ = false;
    uint64_t mutation_revision_ = 0;
    uint64_t session_generation_ = 0;
    std::string last_reason_;
    Json last_outcome_;
    Json last_activation_diagnostics_; // bounded to one activation; not runtime truth
    Json last_plan_diagnostics_;
    Json last_failed_profile_operation_; // retained across later successful activations
    std::map<std::string, Operation> applied_;
    // Pre-acquisition saved intent, never inferred from our own Profile writes.
    // Session-derived entries expire on generation/manual invalidation.
    std::map<uint16_t, Json> rt_prior_;
    std::map<uint16_t, std::string> rt_prior_source_;
    // Monotonic within the daemon process: unknown/partial submissions must
    // still be restored when the next Profile inherits the device baseline.
    std::set<uint16_t> actuation_footprint_;
    std::set<uint16_t> deadzone_footprint_;
    std::set<uint16_t> rapid_trigger_footprint_;
    std::set<uint16_t> dks_footprint_;
    bool all_key_actuation_footprint_ = false;
    bool all_key_deadzone_footprint_ = false;
};

} // namespace aura
