#pragma once
#include "third_party/json.hpp"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace aura {
// Single policy worker. No binding resolution, Profile persistence, planner,
// transport, M605 or lighting dependency. The activation seam MUST revalidate
// the token inside the authoritative Profile mutation gate.
class DeviceProfileAutomationCoordinator {
public:
    using Json = nlohmann::json;
    using Clock = std::function<int64_t()>;
    using SnapshotProvider = std::function<Json()>;
    using Activator = std::function<Json(const Json&, const std::function<bool()>&)>;
    using Logger = std::function<void(const Json&)>;
    DeviceProfileAutomationCoordinator(SnapshotProvider snapshot, Activator activate,
        Clock clock = {}, Logger logger = {});
    ~DeviceProfileAutomationCoordinator();
    void Start();
    void Wake();
    void Stop(); // finish admitted activation; never abort a staged transaction
    void Tick(); // deterministic policy entry for fake-clock tests; single flight
    Json Snapshot() const; // cached read only; does not tick/preflight
private:
    static std::string DecisionKey(const Json& decision);
    void Run();
    SnapshotProvider snapshot_;
    Activator activate_;
    Clock clock_;
    Logger logger_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_;
    std::atomic<bool> stopping_{false};
    bool wake_ = false, in_flight_ = false;
    std::string completed_key_, attempt_key_, health_key_;
    uint32_t retry_count_ = 0;
    int64_t retry_at_ = 0;
    Json state_;
};
}
