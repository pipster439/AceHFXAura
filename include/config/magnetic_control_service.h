#pragma once

#include "aura/hardware/m605_runtime.h"
#include "aura/runtime_status.h"
#include "config/magnetic_host_profile.h"
#include "third_party/httplib.h"
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>

namespace aura {

// Daemon-owned, narrow local control surface. M605Runtime remains the sole
// owner of packet construction, serialization, timing and applied shadow.
class MagneticControlService {
public:
    explicit MagneticControlService(std::shared_ptr<const RuntimeStatusStore> status_store,
        std::function<MagneticHostProfile()> host_profile =
            [] { return MagneticHostProfileProvider::LoadProduction(); },
        std::unique_ptr<M605Runtime> runtime = std::make_unique<M605Runtime>())
        : status_store_(std::move(status_store)), host_profile_(std::move(host_profile)),
          runtime_(std::move(runtime)) {}
    void RegisterRoutes(httplib::Server& server);
    void Stop() { runtime_->Stop(); }

private:
    bool HardwareAvailable() const;
    void WriteStatus(httplib::Response& response, bool result, int status_code,
                     std::string_view detail = {}) const;

    std::shared_ptr<const RuntimeStatusStore> status_store_;
    std::function<MagneticHostProfile()> host_profile_;
    std::unique_ptr<M605Runtime> runtime_;
    std::mutex write_mutex_; // Keep HTTP callers from piling up FIFO hardware jobs.
};

} // namespace aura
