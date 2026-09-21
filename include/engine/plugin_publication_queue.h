#pragma once
#include "engine/plugin_manager.h"
#include <future>
#include <deque>

namespace aura {
// Publication commands, not Automation actions. Preparation never mutates registry.
// Only the render owner calls ApplyAtFrameBoundary.
class PluginPublicationQueue {
    struct Command {
        PluginManager::PreparedReload prepared;
        std::mutex mutex;
        std::promise<bool> reply;
        bool cancelled{false}, done{false}, result{false};
    };
    std::mutex mutex_;
    std::deque<std::shared_ptr<Command>> pending_;
public:
    bool Submit(PluginManager::PreparedReload prepared, std::chrono::milliseconds timeout = std::chrono::milliseconds(1500)) {
        if (!prepared) return false;
        auto command = std::make_shared<Command>(); command->prepared = std::move(prepared);
        auto reply = command->reply.get_future();
        { std::lock_guard<std::mutex> lock(mutex_); if (pending_.size() >= 32) return false; pending_.push_back(command); }
        if (reply.wait_for(timeout) == std::future_status::ready) return reply.get();
        std::lock_guard<std::mutex> lock(command->mutex);
        if (command->done) return command->result;
        command->cancelled = true; return false;
    }
    bool ApplyAtFrameBoundary(PluginManager& manager) {
        std::deque<std::shared_ptr<Command>> commands;
        { std::lock_guard<std::mutex> lock(mutex_); commands.swap(pending_); }
        bool changed = false;
        for (auto& command : commands) {
            std::lock_guard<std::mutex> lock(command->mutex);
            command->result = !command->cancelled && manager.PublishReload(command->prepared);
            changed |= command->result; command->done = true; command->reply.set_value(command->result);
        }
        return changed;
    }
};
} // namespace aura
