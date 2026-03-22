#include "server/db/persistence_priority.h"

namespace fate {

void PersistenceQueue::enqueue(uint16_t clientId, PersistPriority priority,
                               PersistType type, float gameTime) {
    PersistRequest req;
    req.clientId = clientId;
    req.priority = priority;
    req.type = type;
    req.queuedAt = gameTime;

    switch (priority) {
        case PersistPriority::IMMEDIATE: req.maxDelay = 0.0f;   break;
        case PersistPriority::HIGH:      req.maxDelay = 5.0f;   break;
        case PersistPriority::NORMAL:    req.maxDelay = 60.0f;  break;
        case PersistPriority::LOW:       req.maxDelay = 300.0f; break;
    }

    queue_.push(req);
}

std::vector<PersistRequest> PersistenceQueue::dequeue(int maxCount, float gameTime) {
    // L28: Promote any requests that have exceeded their maxDelay
    promoteStarved(gameTime);

    std::vector<PersistRequest> batch;
    batch.reserve(static_cast<size_t>(maxCount));

    while (!queue_.empty() && static_cast<int>(batch.size()) < maxCount) {
        batch.push_back(queue_.top());
        queue_.pop();
    }

    return batch;
}

void PersistenceQueue::promoteStarved(float gameTime) {
    // Drain the priority queue, promote any starved requests to HIGH, re-insert all
    std::vector<PersistRequest> temp;
    temp.reserve(queue_.size());
    bool anyPromoted = false;

    while (!queue_.empty()) {
        auto req = queue_.top();
        queue_.pop();

        if (req.priority > PersistPriority::HIGH) {
            float waited = gameTime - req.queuedAt;
            if (waited >= req.maxDelay) {
                req.priority = PersistPriority::HIGH;
                anyPromoted = true;
            }
        }
        temp.push_back(std::move(req));
    }

    for (auto& r : temp) {
        queue_.push(std::move(r));
    }
    (void)anyPromoted; // suppress unused warning; useful for future logging
}

} // namespace fate
