#pragma once
#include "config/rule_engine.h"
#include "config/automation_limits.h"
#include "engine/plugin_manager.h"
#include <functional>
#include <mutex>
#include <set>
#include <tuple>
#include <deque>

namespace aura {
// Resolution reuses the existing profile constructor and PluginManager registry.
TriggeredEffectInstance ResolveAutomationEffect(const nlohmann::json& reference, const RuleEngine& rules);
TriggeredEffectInstance CreateProfileEffectInstance(const Profile& profile);
std::string ProfileEffectIdentity(const Profile& profile);
std::string ProfilePluginName(const Profile& profile);
// Host-owned immutable source: never contains an Effect or live telemetry.
class PreparedEffectSource {
public:
    explicit operator bool() const { return bool(generation_) || !recipe_.empty(); }
    TriggeredEffectInstance Create() const;
    static PreparedEffectSource Plugin(std::shared_ptr<const PluginEntry> generation) {
        PreparedEffectSource result; result.generation_=std::move(generation); return result;
    }
    static PreparedEffectSource Recipe(std::string recipe) {
        PreparedEffectSource result; result.recipe_=std::move(recipe); return result;
    }
private:
    std::shared_ptr<const PluginEntry> generation_;
    std::string recipe_;
};
PreparedEffectSource PrepareAutomationEffect(const nlohmann::json& reference, const RuleEngine& rules);

// Executes already-made decisions only. No GsiState or condition evaluator here.
class AutomationEffectRuntime {
public:
    ~AutomationEffectRuntime() { Clear(); }
    using Resolver = std::function<TriggeredEffectInstance(const nlohmann::json&)>;
    using Preparer = std::function<PreparedEffectSource(const nlohmann::json&)>;
    using LayerOrder = std::tuple<int, size_t, uint64_t>;
    uint64_t Revision() const;
    void Clear(); // explicit shutdown/reset; production reload uses reconciliation
    void Consume(const AutomationEvaluation& evaluation, const Resolver& resolve, uint64_t now,
                 uint64_t expected_revision, const Preparer& prepare = {});
    void Apply(bool persistent, uint64_t now, FrameBuffer& lower, const Keymap& keymap, const IGsiReader* gsi = nullptr);
    size_t ActiveCount() const;
    size_t DiagnosticCount() const;
    size_t PendingCount() const;
    struct Counters { uint64_t capacity_drops{0}, expired{0}, unavailable{0}, factory_failures{0}, cancelled{0}; };
    Counters GetCounters() const;
private:
    struct Layer {
        std::string id;
        TriggeredEffectInstance instance;
        bool persistent{false}, replace{false}, additive{false}, capable{false};
        uint64_t started{0}, duration{1200}, fade{400}, attack{0}, watchdog{0};
        LayerOrder order;
        std::string semantic_identity, recipe_identity, attempted_identity;
    };
    struct Token {
        std::string id, semantic_identity;
        nlohmann::json action;
        size_t rule_order{0};
        uint64_t admitted{0};
        PreparedEffectSource source;
    };
    Layer MakeLayer(const std::string& id, const nlohmann::json& action, size_t order,
                    TriggeredEffectInstance instance, uint64_t now);
    std::deque<Token> pending_;
    Counters counters_;
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
