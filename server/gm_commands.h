#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <sstream>
#include <cstdint>

namespace fate {

struct ParsedCommand {
    bool isCommand = false;
    std::string commandName;
    std::vector<std::string> args;
};

struct GMCommandParser {
    static ParsedCommand parse(const std::string& message) {
        ParsedCommand result;
        if (message.empty() || message[0] != '/') return result;
        if (message.size() <= 1) return result;

        std::istringstream iss(message.substr(1));
        iss >> result.commandName;
        if (result.commandName.empty()) return result;

        result.isCommand = true;
        std::string arg;
        while (iss >> arg) {
            result.args.push_back(arg);
        }
        return result;
    }
};

struct GMCommand {
    std::string name;
    int minRole = 1;
    std::function<void(uint16_t callerClientId, const std::vector<std::string>& args)> handler;
};

class GMCommandRegistry {
public:
    void registerCommand(GMCommand cmd) {
        commands_[cmd.name] = std::move(cmd);
    }

    const GMCommand* findCommand(const std::string& name) const {
        auto it = commands_.find(name);
        return it != commands_.end() ? &it->second : nullptr;
    }

    static bool hasPermission(int playerRole, int requiredRole) {
        return playerRole >= requiredRole;
    }

    size_t size() const { return commands_.size(); }

private:
    std::unordered_map<std::string, GMCommand> commands_;
};

} // namespace fate
