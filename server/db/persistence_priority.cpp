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
    std::vector<PersistRequest> batch;
    batch.reserve(static_cast<size_t>(maxCount));

    while (!queue_.empty() && static_cast<int>(batch.size()) < maxCount) {
        batch.push_back(queue_.top());
        queue_.pop();
    }

    return batch;
}

} // namespace fate
