#include "engine/net/connection.h"
#include "engine/core/logger.h"

namespace fate {

uint16_t ConnectionManager::addClient(const NetAddress& address, float currentTime) {
    // Reject if at capacity
    if (clients_.size() >= maxClients_) {
        LOG_WARN("Connection", "Max client limit reached (%zu), rejecting new connection", maxClients_);
        return 0;
    }

    // Per-IP limit check
    std::string ip = address.ipString();
    size_t currentCount = connectionsPerIP_[ip];
    if (currentCount >= maxPerIP_) {
        LOG_WARN("Connection", "Per-IP limit reached (%zu) for %s, rejecting", maxPerIP_, ip.c_str());
        return 0;
    }

    // Find unused ID (skip 0, skip in-use IDs)
    while (nextClientId_ == 0 || clients_.count(nextClientId_)) {
        ++nextClientId_;
        if (nextClientId_ == 0) nextClientId_ = 1;
    }

    uint16_t id = nextClientId_++;
    if (nextClientId_ == 0) nextClientId_ = 1;

    ClientConnection conn;
    conn.clientId = id;
    conn.address = address;
    conn.sessionToken = generateToken();
    conn.lastHeartbeat = currentTime;

    clients_.emplace(id, std::move(conn));
    addressToClient_[address] = id;
    connectionsPerIP_[ip]++;
    return id;
}

void ConnectionManager::removeClient(uint16_t clientId) {
    auto it = clients_.find(clientId);
    if (it != clients_.end()) {
        // Decrement per-IP counter
        std::string ip = it->second.address.ipString();
        auto ipIt = connectionsPerIP_.find(ip);
        if (ipIt != connectionsPerIP_.end()) {
            if (ipIt->second <= 1) connectionsPerIP_.erase(ipIt);
            else ipIt->second--;
        }
        addressToClient_.erase(it->second.address);
        if (it->second.playerEntityId != 0) {
            entityToClient_.erase(it->second.playerEntityId);
            entityToClientLow32_.erase(static_cast<uint32_t>(it->second.playerEntityId));
        }
        clients_.erase(it);
    }
}

size_t ConnectionManager::getConnectionsForIP(const std::string& ip) const {
    auto it = connectionsPerIP_.find(ip);
    return (it != connectionsPerIP_.end()) ? it->second : 0;
}

ClientConnection* ConnectionManager::findById(uint16_t clientId) {
    auto it = clients_.find(clientId);
    if (it == clients_.end()) return nullptr;
    return &it->second;
}

ClientConnection* ConnectionManager::findByEntity(uint64_t playerEntityId) {
    auto it = entityToClient_.find(playerEntityId);
    return it != entityToClient_.end() ? findById(it->second) : nullptr;
}

const ClientConnection* ConnectionManager::findByEntity(uint64_t playerEntityId) const {
    auto it = entityToClient_.find(playerEntityId);
    if (it == entityToClient_.end()) return nullptr;
    auto cit = clients_.find(it->second);
    return cit != clients_.end() ? &cit->second : nullptr;
}

ClientConnection* ConnectionManager::findByEntityLow32(uint32_t entityId) {
    auto it = entityToClientLow32_.find(entityId);
    return it != entityToClientLow32_.end() ? findById(it->second) : nullptr;
}

void ConnectionManager::mapEntity(uint64_t playerEntityId, uint16_t clientId) {
    entityToClient_[playerEntityId] = clientId;
    entityToClientLow32_[static_cast<uint32_t>(playerEntityId)] = clientId;
}

void ConnectionManager::unmapEntity(uint64_t playerEntityId) {
    entityToClient_.erase(playerEntityId);
    entityToClientLow32_.erase(static_cast<uint32_t>(playerEntityId));
}

ClientConnection* ConnectionManager::findByAddress(const NetAddress& address) {
    auto it = addressToClient_.find(address);
    if (it == addressToClient_.end()) return nullptr;
    return findById(it->second);
}

bool ConnectionManager::validateToken(const NetAddress& address, uint32_t token) {
    auto* client = findByAddress(address);
    if (!client) return false;
    return client->sessionToken == token;
}

void ConnectionManager::heartbeat(uint16_t clientId, float currentTime) {
    auto* client = findById(clientId);
    if (client) {
        client->lastHeartbeat = currentTime;
    }
}

std::vector<uint16_t> ConnectionManager::getTimedOutClients(float currentTime, float timeoutSeconds) {
    std::vector<uint16_t> result;
    for (auto& [id, client] : clients_) {
        if (currentTime - client.lastHeartbeat > timeoutSeconds) {
            result.push_back(id);
        }
    }
    return result;
}

uint32_t ConnectionManager::generateToken() {
    std::uniform_int_distribution<uint32_t> dist(1, UINT32_MAX);
    return dist(rng_);
}

} // namespace fate
