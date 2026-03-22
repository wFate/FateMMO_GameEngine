#pragma once
#include "engine/ecs/world.h"
#include "engine/core/logger.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace fate {

class ServerSpawnManager;

struct DungeonInstance {
    uint32_t instanceId = 0;
    std::string sceneId;
    int partyId = -1;
    World world;
    std::shared_ptr<ServerSpawnManager> spawnManager;
    float elapsedTime = 0.0f;
    float timeoutSeconds = 1800.0f;  // 30 min default
    std::vector<uint16_t> playerClientIds;

    DungeonInstance(uint32_t id, const std::string& scene, int party)
        : instanceId(id), sceneId(scene), partyId(party) {}
};

class DungeonManager {
public:
    inline uint32_t createInstance(const std::string& sceneId, int partyId) {
        uint32_t id = nextInstanceId_++;
        auto inst = std::make_unique<DungeonInstance>(id, sceneId, partyId);
        LOG_INFO("DungeonManager", "Created instance %u for scene '%s' party %d",
                 id, sceneId.c_str(), partyId);
        if (partyId >= 0) partyToInstance_[partyId] = id;
        instances_[id] = std::move(inst);
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
        }
    }

    inline std::vector<uint32_t> getExpiredInstances() const {
        std::vector<uint32_t> expired;
        for (const auto& [id, inst] : instances_) {
            if (inst->elapsedTime >= inst->timeoutSeconds && inst->playerClientIds.empty()) {
                expired.push_back(id);
            }
        }
        return expired;
    }

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
    uint32_t nextInstanceId_ = 1;
    std::unordered_map<uint32_t, std::unique_ptr<DungeonInstance>> instances_;
    std::unordered_map<int, uint32_t> partyToInstance_;
    std::unordered_map<uint16_t, uint32_t> clientToInstance_;
};

} // namespace fate
