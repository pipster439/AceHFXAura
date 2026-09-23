#pragma once

#include <string>
#include <mutex>
#include "third_party/json.hpp"

namespace aura {

struct RuntimeStatusSnapshot {
    std::string instance_id;
    std::string product_version;
    std::string config_path;
    uint32_t process_id = 0;
    bool web_suppressed = false;
    bool hardware_connected = false;
    std::string adapter_state = "uninitialized";
    std::string configured_backend = "auto";
    std::string active_backend = "unknown";
    std::string device_path;
    std::string last_hardware_error;

    std::string active_profile = "(None)";
    int target_fps = 25;
    bool dry_run = false;

    bool gsi_active = false;
    std::string gsi_source = "real";
    std::string foreground_process;

    nlohmann::json ToJson() const {
        nlohmann::json j;
        j["status"] = "ok";
        j["api_version"] = 1;
        j["identity"] = {{"service", "aura_daemon"}, {"process_id", process_id},
            {"instance_id", instance_id}, {"product_version", product_version}, {"config_path", config_path}};
        j["studio_web"] = {{"suppressed", web_suppressed}};

        j["hardware"] = {
            {"connected", hardware_connected},
            {"state", adapter_state},
            {"configured_backend", configured_backend},
            {"active_backend", active_backend},
            {"device_path", device_path},
            {"last_error", last_hardware_error}
        };

        j["runtime"] = {
            {"active_profile", active_profile},
            {"fps", target_fps},
            {"dry_run", dry_run},
            {"foreground_process", foreground_process}
        };

        j["gsi"] = {
            {"active", gsi_active}, {"source", gsi_source}
        };

        return j;
    }
};

class RuntimeStatusStore {
public:
    RuntimeStatusStore() = default;

    void Update(const RuntimeStatusSnapshot& snapshot) {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_ = snapshot;
    }

    RuntimeStatusSnapshot GetSnapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return snapshot_;
    }

private:
    mutable std::mutex mutex_;
    RuntimeStatusSnapshot snapshot_;
};

} // namespace aura
