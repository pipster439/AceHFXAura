#pragma once
#include "config/rule_engine.h"
#include "engine/plugin_manager.h"
#include <functional>
#include <mutex>
#include <set>
#include <tuple>

namespace aura {
// Resolution reuses the existing profile constructor and PluginManager registry.
TriggeredEffectInstance ResolveAutomationEffect(const nlohmann::json& reference, const RuleEngine& rules);
TriggeredEffectInstance CreateProfileEffectInstance(const Profile& profile);
std::string ProfileEffectIdentity(const Profile& profile);
std::string ProfilePluginName(const Profile& profile);

// Executes already-made decisions only. No GsiState or condition evaluator here.
class AutomationEffectRuntime {
public:
    using Resolver = std::function<TriggeredEffectInstance(const nlohmann::json&)>;
    using LayerOrder = std::tuple<int, size_t, uint64_t>;
    uint64_t Revision() const;
    void Clear(); // explicit shutdown/reset; production reload uses reconciliation
    void Consume(const AutomationEvaluation& evaluation, const Resolver& resolve, uint64_t now,
                 uint64_t expected_revision);
    void Apply(bool persistent, uint64_t now, FrameBuffer& lower, const Keymap& keymap, const IGsiReader* gsi = nullptr);
    size_t ActiveCount() const;
    size_t DiagnosticCount() const;
private:
    struct Layer {
        std::string id;
        TriggeredEffectInstance instance;
        bool persistent{false}, replace{false}, additive{false}, capable{false};
        uint64_t started{0}, duration{1200}, fade{400}, attack{0}, watchdog{0};
        LayerOrder order;
        std::string semantic_identity, recipe_identity, attempted_identity;
    };
    void Diagnose(const std::string& id, const char* reason);
    bool Render(Layer& layer, uint64_t now, FrameBuffer& lower, const Keymap& keymap, const IGsiReader* gsi);
    mutable std::mutex mutex_;
    uint64_t revision_{0}, config_generation_{0}, sequence_{0};
    std::vector<Layer> layers_;
    std::set<std::string> true_intervals_;
    std::map<std::string, std::string> interval_semantics_;
    std::map<std::string, std::string> interval_recipes_;
    std::set<std::string> diagnostics_; // bounded to 64 messages until explicit reset
    FrameBuffer temporary_;
};
} // namespace aura
