#pragma once

#include "aura/hardware/m605_hall_telemetry.h"
#include "aura/hardware/m605_runtime.h"
#include "aura/runtime_status.h"
#include "config/magnetic_host_profile.h"
#include "third_party/httplib.h"
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <filesystem>
#include <chrono>

namespace aura {
class DeviceProfileRuntime;
class HardwareRtGateMonitor;
class DeviceProfileAutomationCoordinator;

// Daemon-owned, narrow local control surface. M605Runtime remains the sole
// owner of packet construction, serialization, timing and applied shadow.
class MagneticControlService {
public:
    explicit MagneticControlService(std::shared_ptr<const RuntimeStatusStore> status_store,
        std::function<MagneticHostProfile()> host_profile =
            [] { return MagneticHostProfileProvider::LoadProduction(); },
        std::unique_ptr<M605Runtime> runtime = std::make_unique<M605Runtime>(),
        std::filesystem::path profile_path = {}, std::filesystem::path legacy_config = {});
    ~MagneticControlService();
    void RegisterRoutes(httplib::Server& server);
    bool ObserveDeviceProfileForeground(const std::string& process_name);
    void ObserveHardwareSlot();
    bool WithProfileLightingOwnership(const std::function<bool()>& write_frame);
    void StartHardwareRtGateObservation(); // explicit production composition only
    void StartDeviceProfileAutomation(); // exactly one daemon worker; excluded from dry run
    void StopDeviceProfileAutomation();
    void Stop();
    m605::hall::HallTelemetrySnapshot GetHallSnapshot() const;
    void SyncAutoHallDemand(bool needed);

private:
    bool HardwareAvailable() const;
    void WriteStatus(httplib::Response& response, bool result, int status_code,
                     std::string_view detail = {}) const;

    std::shared_ptr<const RuntimeStatusStore> status_store_;
    std::function<MagneticHostProfile()> host_profile_;
    std::unique_ptr<M605Runtime> runtime_;
    std::mutex write_mutex_; // Keep HTTP callers from piling up FIFO hardware jobs.
    std::unique_ptr<DeviceProfileRuntime> profiles_;
    std::unique_ptr<HardwareRtGateMonitor> rt_gate_monitor_;
    std::unique_ptr<DeviceProfileAutomationCoordinator> automation_coordinator_;
    std::unique_ptr<m605::hall::M605HallTelemetry> hall_telemetry_;
    std::mutex hall_demand_mutex_;
    bool manual_hall_requested_{false};
    bool auto_hall_active_{false};
    std::chrono::steady_clock::time_point last_auto_hall_retry_{};
};

} // namespace aura
