#pragma once
#include "third_party/json.hpp"
#include <stdexcept>
#include <string>

namespace aura {
// Shared by daemon and configuration writers. No historical executor/parser.
inline void ValidateAutomationContainers(const nlohmann::json& root) {
    if (!root.is_object()) throw std::runtime_error("Configuration must be an object");
    auto retired = [](const nlohmann::json& object, const char* key, const std::string& path) {
        if (object.contains(key) && (!object[key].is_array() || !object[key].empty()))
            throw std::runtime_error("migration_required: " + path +
                " is unsupported; use tools/legacy/migrate_automation_v2.py before startup");
    };
    retired(root, "rules", "/rules");
    retired(root, "gsi_bindings", "/gsi_bindings");
    if (!root.contains("orchestration")) return;
    const auto& orchestration = root["orchestration"];
    if (!orchestration.is_object()) throw std::runtime_error("/orchestration must be an object");
    retired(orchestration, "event_overlays", "/orchestration/event_overlays");
    if (!orchestration.contains("rules")) return;
    const auto& rules = orchestration["rules"];
    if (!rules.is_array()) throw std::runtime_error("/orchestration/rules must be an array");
    for (size_t i = 0; i < rules.size(); ++i)
        if (!rules[i].is_object() || rules[i].value("model", nlohmann::json()) != "automation_v2")
            throw std::runtime_error("migration_required: /orchestration/rules/" + std::to_string(i) +
                " requires model automation_v2; use tools/legacy/migrate_automation_v2.py");
}
}
