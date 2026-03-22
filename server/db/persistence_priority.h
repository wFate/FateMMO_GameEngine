#pragma once
#include <cstdint>
#include <queue>
#include <vector>

namespace fate {

enum class PersistPriority : uint8_t {
    IMMEDIATE = 0,  // gold/inventory/trades — flush this tick
    HIGH      = 1,  // level-ups, quest completions — within 5 seconds
    NORMAL    = 2,  // position, minor stats — within 30-60 seconds
    LOW       = 3   // cosmetics, preferences — on logout or every 5 min
};

enum class PersistType : uint8_t {
    Character, Inventory, Skills, Quests, Bank, Pet, Guild, Social, Position
};

struct PersistRequest {
    uint16_t clientId;
    PersistPriority priority;
    PersistType type;
    float queuedAt;       // game time when enqueued
    float maxDelay;       // max seconds before forced flush
};

class PersistenceQueue {
public:
    void enqueue(uint16_t clientId, PersistPriority priority,
                 PersistType type, float gameTime);

    // Dequeue up to maxCount requests, respecting priority + age
    std::vector<PersistRequest> dequeue(int maxCount, float gameTime);

    bool empty() const { return queue_.empty(); }
    size_t size() const { return queue_.size(); }

private:
    // L28: Promote starved requests that have exceeded their maxDelay
    void promoteStarved(float gameTime);

    struct PriorityCompare {
        bool operator()(const PersistRequest& a, const PersistRequest& b) const {
            if (a.priority != b.priority)
                return a.priority > b.priority; // lower enum = higher priority
            return a.queuedAt > b.queuedAt;     // older first within same tier
        }
    };
    std::priority_queue<PersistRequest, std::vector<PersistRequest>, PriorityCompare> queue_;
};

} // namespace fate
