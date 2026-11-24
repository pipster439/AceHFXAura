#pragma once

#include "aura/hardware/m605_hall_telemetry.h"
#include "third_party/httplib.h"
#include <filesystem>
#include <functional>

namespace aura {

class PluginOverviewService {
public:
    PluginOverviewService(
        std::filesystem::path runtime_root,
        std::function<m605::hall::HallTelemetrySnapshot()> hall_snapshot_provider);

    void RegisterRoutes(httplib::Server& server);

private:
    std::string BuildOverviewJsonString() const;

    std::filesystem::path runtime_root_;
    std::function<m605::hall::HallTelemetrySnapshot()> hall_snapshot_provider_;
};

} // namespace aura
