#pragma once

#include "aura/hardware/m605_runtime.h"
#include "aura/runtime_status.h"
#include "config/magnetic_host_profile.h"
#include "third_party/httplib.h"
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <filesystem>

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
};

} // namespace aura
