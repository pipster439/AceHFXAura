#pragma once
#include "engine/plugin_manager.h"
#include "third_party/json.hpp"
#include "third_party/httplib.h"
#include <deque>
#include <future>

namespace aura {
// Source-only programs live separately from trusted native DLLs. HTTP preparation
// cannot publish; the existing render owner commits catalog actions at a boundary.
class ProgramCatalog {
public:
    struct Result { int status; nlohmann::json body; };
    ProgramCatalog(std::filesystem::path root, PluginManager& manager);
    Result List() const;
    Result Receipt(const std::string& operation_id) const;
    Result ExecuteText(const std::string& request);
    Result Execute(const nlohmann::json& request);
    bool ApplyAtFrameBoundary();
    void RestoreEnabled();
    void RegisterRoutes(httplib::Server& server);
    void SetBeforeWriteForTesting(std::function<void()> hook) { before_write_ = std::move(hook); }
private:
    struct Command;
    std::filesystem::path root_;
    PluginManager& manager_;
    mutable std::mutex mutex_;
    std::mutex writer_mutex_;
    nlohmann::json catalog_;
    std::string diagnostic_;
    std::deque<std::shared_ptr<Command>> queue_;
    std::function<void()> before_write_;
    std::string disk_revision_;
    Result Commit(const nlohmann::json& request, const std::shared_ptr<const EffectProgram>& program, const std::string& digest);
    bool Publish(std::function<bool()> action);
    void Write(const nlohmann::json& value);
};
}
