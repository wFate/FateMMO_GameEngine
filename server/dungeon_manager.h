#pragma once
#include "engine/ecs/world.h"
#include "engine/net/replication.h"
#include "engine/core/logger.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fate {

class ServerSpawnManager;

struct DungeonReturnPoint {
    std::string scene;
    float x = 0.0f, y = 0.0f;
};

struct DungeonInstance {
    uint32_t instanceId = 0;
    std::string sceneId;
    int partyId = -1;
    int difficultyTier = 1;

    // Isolated ECS
    World world;
    ReplicationManager replication;

    // Lifecycle
    float elapsedTime = 0.0f;
    float timeLimitSeconds = 600.0f;      // 10 minutes
    float celebrationTimer = -1.0f;       // set to 15.0f on boss kill
    bool completed = false;
    bool expired = false;

    // Player tracking
    std::vector<uint16_t> playerClientIds;
    std::unordered_map<uint16_t, DungeonReturnPoint> returnPoints;

    // Invite flow
    std::unordered_set<uint16_t> pendingAccepts;
    uint16_t leaderClientId = 0;
    float inviteTimer = 0.0f;
    static constexpr float INVITE_TIMEOUT = 30.0f;

    DungeonInstance(uint32_t id, const std::string& scene, int party, int tier)
        : instanceId(id), sceneId(scene), partyId(party), difficultyTier(tier) {}

    bool allAccepted() const { return pendingAccepts.empty(); }
    bool hasPlayers() const { return !playerClientIds.empty(); }
};

class ItemDefinitionCache;

class DungeonManager {
public:
    void setItemDefCache(const ItemDefinitionCache* cache) { itemDefCache_ = cache; }

    inline uint32_t createInstance(const std::string& sceneId, int partyId, int difficultyTier = 1) {
        uint32_t id = generateInstanceId();
        auto inst = std::make_unique<DungeonInstance>(id, sceneId, partyId, difficultyTier);
        LOG_INFO("DungeonManager", "Created instance %u for scene '%s' party %d",
                 id, sceneId.c_str(), partyId);
        if (partyId >= 0) partyToInstance_[partyId] = id;
        instances_[id] = std::move(inst);
        if (itemDefCache_) instances_[id]->replication.setItemDefCache(itemDefCache_);
        return id;
    }

    inline DungeonInstance* getInstance(uint32_t instanceId) {
        auto it = instances_.find(instanceId);
        return it != instances_.end() ? it->second.get() : nullptr;
    }

    inline const DungeonInstance* getInstance(uint32_t instanceId) const {
        auto it = instances_.find(instanceId);
        return it != instances_.end() ? it->second.get() : nullptr;
    }

    inline uint32_t getInstanceForParty(int partyId) const {
        auto it = partyToInstance_.find(partyId);
        return it != partyToInstance_.end() ? it->second : 0;
    }

    inline void destroyInstance(uint32_t instanceId) {
        auto it = instances_.find(instanceId);
        if (it == instances_.end()) return;
        auto& inst = it->second;
        LOG_INFO("DungeonManager", "Destroying instance %u (scene '%s', party %d)",
                 instanceId, inst->sceneId.c_str(), inst->partyId);
        if (inst->partyId >= 0) partyToInstance_.erase(inst->partyId);
        for (uint16_t cid : inst->playerClientIds) clientToInstance_.erase(cid);
        instances_.erase(it);
    }

    inline void tick(float dt) {
        for (auto& [id, inst] : instances_) {
            inst->elapsedTime += dt;
            inst->world.update(dt);
            inst->world.processDestroyQueue();
            if (inst->completed && inst->celebrationTimer > 0.0f) {
                inst->celebrationTimer -= dt;
            }
        }
    }

    std::vector<uint32_t> getTimedOutInstances() const {
        std::vector<uint32_t> result;
        for (const auto& [id, inst] : instances_) {
            if (!inst->completed && !inst->expired &&
                inst->elapsedTime >= inst->timeLimitSeconds) {
                result.push_back(id);
            }
        }
        return result;
    }

    std::vector<uint32_t> getCelebrationFinishedInstances() const {
        std::vector<uint32_t> result;
        for (const auto& [id, inst] : instances_) {
            if (inst->completed && inst->celebrationTimer <= 0.0f) {
                result.push_back(id);
            }
        }
        return result;
    }

    std::vector<uint32_t> getEmptyActiveInstances() const {
        std::vector<uint32_t> result;
        for (const auto& [id, inst] : instances_) {
            if (!inst->expired && inst->elapsedTime > 0.0f && inst->playerClientIds.empty()) {
                result.push_back(id);
            }
        }
        return result;
    }

    const auto& allInstances() const { return instances_; }
    auto& allInstances() { return instances_; }

    size_t instanceCount() const { return instances_.size(); }

    inline void addPlayer(uint32_t instanceId, uint16_t clientId) {
        auto* inst = getInstance(instanceId);
        if (!inst) return;
        inst->playerClientIds.push_back(clientId);
        clientToInstance_[clientId] = instanceId;
    }

    inline void removePlayer(uint32_t instanceId, uint16_t clientId) {
        auto* inst = getInstance(instanceId);
        if (!inst) return;
        auto& ids = inst->playerClientIds;
        ids.erase(std::remove(ids.begin(), ids.end(), clientId), ids.end());
        clientToInstance_.erase(clientId);
    }

    inline uint32_t getInstanceForClient(uint16_t clientId) const {
        auto it = clientToInstance_.find(clientId);
        return it != clientToInstance_.end() ? it->second : 0;
    }

private:
    const ItemDefinitionCache* itemDefCache_ = nullptr;
    uint32_t nextInstanceId_ = 1;
    std::mt19937 rng_{std::random_device{}()};
    std::unordered_map<uint32_t, std::unique_ptr<DungeonInstance>> instances_;
    std::unordered_map<int, uint32_t> partyToInstance_;
    std::unordered_map<uint16_t, uint32_t> clientToInstance_;

    uint32_t generateInstanceId() {
        uint32_t id;
        do {
            // Combine counter with random bits to be unpredictable but collision-resistant
            uint32_t counter = nextInstanceId_++;
            uint32_t rand = rng_();
            id = (rand & 0xFFFF0000u) | (counter & 0x0000FFFFu);
            if (id == 0) id = 1; // 0 is reserved for "no instance"
        } while (instances_.count(id));
        return id;
    }
};

} // namespace fate
