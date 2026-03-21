#pragma once
#include <mutex>
#include <memory>
#include <unordered_map>
#include <string>

namespace fate {

class PlayerLockMap {
public:
    std::mutex& get(const std::string& characterId) {
        std::lock_guard<std::mutex> guard(mapMutex_);
        auto& ptr = locks_[characterId];
        if (!ptr) ptr = std::make_unique<std::mutex>();
        return *ptr;
    }

    void erase(const std::string& characterId) {
        std::lock_guard<std::mutex> guard(mapMutex_);
        locks_.erase(characterId);
    }

private:
    std::mutex mapMutex_;
    std::unordered_map<std::string, std::unique_ptr<std::mutex>> locks_;
};

} // namespace fate
