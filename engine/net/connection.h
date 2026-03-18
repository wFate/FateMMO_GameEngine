#pragma once
#include "engine/net/socket.h"
#include "engine/net/reliability.h"
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <random>

namespace fate {

struct ClientConnection {
    uint16_t clientId = 0;
    NetAddress address;
    uint32_t sessionToken = 0;
    float lastHeartbeat = 0.0f;
    ReliabilityLayer reliability;
};

class ConnectionManager {
public:
    uint16_t addClient(const NetAddress& address);
    void removeClient(uint16_t clientId);
    ClientConnection* findById(uint16_t clientId);
    ClientConnection* findByAddress(const NetAddress& address);
    bool validateToken(const NetAddress& address, uint32_t token);
    void heartbeat(uint16_t clientId, float currentTime);
    std::vector<uint16_t> getTimedOutClients(float currentTime, float timeoutSeconds);
    size_t clientCount() const { return clients_.size(); }

    template<typename F>
    void forEach(F&& fn) {
        for (auto& [id, client] : clients_) fn(client);
    }

private:
    uint16_t nextClientId_ = 1;
    std::unordered_map<uint16_t, ClientConnection> clients_;
    std::mt19937 rng_{std::random_device{}()};
    uint32_t generateToken();
};

} // namespace fate
