#include "engine/net/connection.h"

namespace fate {

uint16_t ConnectionManager::addClient(const NetAddress& address) {
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
    conn.lastHeartbeat = 0.0f;

    clients_.emplace(id, std::move(conn));
    return id;
}

void ConnectionManager::removeClient(uint16_t clientId) {
    clients_.erase(clientId);
}

ClientConnection* ConnectionManager::findById(uint16_t clientId) {
    auto it = clients_.find(clientId);
    if (it == clients_.end()) return nullptr;
    return &it->second;
}

ClientConnection* ConnectionManager::findByAddress(const NetAddress& address) {
    for (auto& [id, client] : clients_) {
        if (client.address == address) return &client;
    }
    return nullptr;
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
