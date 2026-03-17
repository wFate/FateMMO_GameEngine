#pragma once
#include <vector>
#include <functional>
#include <cstddef>

namespace fate {

// ==========================================================================
// CommandBuffer — deferred structural changes during iteration
//
// Queue commands (entity add/remove/migrate) while iterating archetypes,
// then execute all at once after iteration completes.
// ==========================================================================
class CommandBuffer {
public:
    void push(std::function<void()> cmd) {
        commands_.push_back(std::move(cmd));
    }

    void execute() {
        for (auto& cmd : commands_) {
            cmd();
        }
        commands_.clear();
    }

    bool empty() const { return commands_.empty(); }
    size_t size() const { return commands_.size(); }

private:
    std::vector<std::function<void()>> commands_;
};

} // namespace fate
