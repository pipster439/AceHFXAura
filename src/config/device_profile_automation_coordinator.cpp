#include "config/device_profile_automation_coordinator.h"
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace aura {
namespace {
int64_t Now() { return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count(); }
}
DeviceProfileAutomationCoordinator::DeviceProfileAutomationCoordinator(
    SnapshotProvider snapshot, Activator activate, Clock clock, Logger logger)
    : snapshot_(std::move(snapshot)), activate_(std::move(activate)),
      clock_(clock ? std::move(clock) : Clock(Now)), logger_(std::move(logger)) {
    state_ = {{"coordinator_state", "WaitingForDecision"}, {"hardware_activation_allowed", true},
        {"activation_state", "Idle"}, {"activation_target", nullptr},
        {"activation_decision_sequence", 0}, {"activation_attempt", 0},
        {"last_attempted_decision_sequence", 0}, {"last_successful_decision_sequence", 0},
        {"activation_outcome", nullptr}, {"activation_error", nullptr},
        {"activation_reason", "Automation"}, {"activation_started_at", nullptr},
        {"activation_completed_at", nullptr}, {"retry_at", nullptr},
        {"clock_source", "monotonic milliseconds; process-local"},
        {"hardware_block_reasons", Json::array()}, {"stale_decision_count", 0}};
}
DeviceProfileAutomationCoordinator::~DeviceProfileAutomationCoordinator() { Stop(); }
std::string DeviceProfileAutomationCoordinator::DecisionKey(const Json& d) {
    return Json::array({d.value("decision_sequence", Json(0)),
        d.value("configuration_document_revision", Json(0)),
        d.value("manual_action_sequence", Json(0)), d.value("decision_foreground_process", Json()),
        d.value("resolved_profile_id", Json()), d.value("decision_kind", Json()),
        d.value("foreground_observation_sequence", Json(0))}).dump();
}
void DeviceProfileAutomationCoordinator::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (worker_.joinable() || stopping_) return;
    worker_ = std::thread([this] { Run(); });
}
void DeviceProfileAutomationCoordinator::Wake() {
    { std::lock_guard<std::mutex> lock(mutex_); wake_ = true; }
    cv_.notify_one();
}
void DeviceProfileAutomationCoordinator::Stop() {
    stopping_.store(true); cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    state_["coordinator_state"] = "Shutdown";
}
void DeviceProfileAutomationCoordinator::Run() {
    while (!stopping_) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            // Wake is coalesced. The periodic cache check also sees authoritative
            // health/generation recovery without adding a foreground observer.
            cv_.wait_for(lock, std::chrono::milliseconds(250), [this] { return wake_ || stopping_; });
            wake_ = false;
        }
        if (!stopping_) Tick();
    }
}
DeviceProfileAutomationCoordinator::Json DeviceProfileAutomationCoordinator::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_); return state_;
}
void DeviceProfileAutomationCoordinator::Tick() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || in_flight_) return;
        in_flight_ = true;
    }
    Json decision, result;
    std::string key;
    bool attempted = false;
    try {
        decision = snapshot_();
        key = DecisionKey(decision);
        const auto health = Json::array({decision.value("m605_session_generation", Json()),
            decision.value("runtime_health", Json()), decision.value("persistent_safety_quarantine", Json())}).dump();
        const auto now = clock_();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto finish = [&] { in_flight_ = false; };
            if (stopping_) { finish(); return; }
            std::string blocked;
            if (!decision.value("configuration_available", false)) blocked = "AutomationConfigurationUnavailable";
            else if (!decision.value("enabled", false)) blocked = "AutomationDisabled";
            else if (decision.value("manual_hold", false)) blocked = "ManualHold";
            else if (decision.value("debounce_pending", true)) blocked = "Debouncing";
            else if (decision.value("decision_kind", "") == "InvalidDecision") blocked = "TargetMissing";
            else if ((decision.value("decision_kind", "") != "Match" && decision.value("decision_kind", "") != "Fallback") ||
                decision.value("resolved_profile_id", Json()).is_null()) blocked = "NoDecision";
            else if (decision.value("foreground_process", Json()) != decision.value("decision_foreground_process", Json())) blocked = "StaleDecision";
            if (!blocked.empty()) {
                state_["coordinator_state"] = "WaitingForDecision";
                state_["hardware_block_reasons"] = Json::array({blocked});
                finish(); return;
            }
            if (key == completed_key_) { finish(); return; }
            if (key != attempt_key_) {
                retry_count_ = 0; retry_at_ = 0; attempt_key_ = key;
            }
            if (now < retry_at_ && health == health_key_) { finish(); return; }
            // Cached authority is only a wake/dedup signal, NEVER permission.
            health_key_ = health;
            state_["coordinator_state"] = "Preflight";
            state_["activation_state"] = "Preflight";
            state_["activation_target"] = decision.at("resolved_profile_id");
            state_["activation_foreground_process"] = decision.at("decision_foreground_process");
            state_["activation_matched_rule_id"] = decision.value("matched_rule_id", Json());
            state_["activation_configuration_revision"] = decision.at("configuration_document_revision");
            state_["activation_decision_sequence"] = decision.at("decision_sequence");
            state_["last_attempted_decision_sequence"] = decision.at("decision_sequence");
            state_["activation_attempt"] = state_.at("activation_attempt").get<uint64_t>() + 1;
            state_["activation_started_at"] = now;
            state_["activation_completed_at"] = nullptr;
            state_["activation_error"] = nullptr;
            state_["hardware_block_reasons"] = Json::array();
            state_["coordinator_state"] = "Activating";
            state_["activation_state"] = "Activating";
        }
        attempted = true;
        result = activate_(decision, [this] { return stopping_.load(); });
        // Selection acceptance can increment document revision. Consume only
        // that returned revision, not an unrelated concurrent edit/ManualHold.
        const auto after = snapshot_();
        auto accepted = decision;
        if (result.contains("document_revision")) accepted["configuration_document_revision"] = result.at("document_revision");
        const auto accepted_key = DecisionKey(accepted);
        if (DecisionKey(after) == accepted_key) key = accepted_key;
    } catch (const std::exception&) {
        // Classify bounded, non-private error; exception details may contain
        // paths from persistence, so never export ex.what() into diagnostics.
        result = {{"outcome", "failed"}, {"error", "Automation activation rejected by runtime"}};
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto outcome = result.value("outcome", "failed");
        state_["activation_outcome"] = outcome;
        state_["activation_error"] = result.value("error", Json());
        state_["activation_completed_at"] = clock_();
        state_["hardware_block_reasons"] = result.value("hardware_block_reasons", Json::array());
        state_["authoritative_preflight"] = result.value("authoritative_preflight", Json());
        state_["authoritative_runtime_result"] = result.value("authoritative_runtime_result", Json());
        if (outcome == "deferred") {
            state_["coordinator_state"] = "Deferred"; state_["activation_state"] = "Deferred";
            retry_at_ = clock_() + std::min<int64_t>(30000, 1000LL << std::min<uint32_t>(retry_count_++, 5));
            state_["retry_at"] = retry_at_; attempt_key_ = key;
        } else if (outcome == "blocked") {
            state_["coordinator_state"] = "Blocked"; state_["activation_state"] = "Blocked";
            // No timed quarantine/unhealthy loop. Only changed authority cache
            // wakes a blocked decision; each attempt rechecks runtime authority.
            retry_at_ = INT64_MAX; attempt_key_ = key; state_["retry_at"] = nullptr;
        } else {
            completed_key_ = key; state_["retry_at"] = nullptr;
            state_["coordinator_state"] = outcome == "succeeded" || outcome == "no-op" ? "Idle" : "Blocked";
            state_["activation_state"] = outcome;
            if (outcome == "succeeded" || outcome == "no-op")
                state_["last_successful_decision_sequence"] = decision.at("decision_sequence");
            if (outcome == "stale") state_["stale_decision_count"] = state_.at("stale_decision_count").get<uint64_t>() + 1;
        }
        in_flight_ = false;
    }
    if (logger_ && attempted) logger_(Snapshot());
}
}
