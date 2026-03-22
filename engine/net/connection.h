#pragma once
#include "engine/net/socket.h"
#include "engine/net/reliability.h"
#include "engine/net/aoi.h"
#include "engine/net/protocol.h"
#include "engine/net/auth_protocol.h"
#include "engine/net/packet_crypto.h"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <random>
#include <functional>

namespace fate {

// Hash function for NetAddress so it can be used as unordered_map key
struct NetAddressHash {
    size_t operator()(const NetAddress& addr) const {
        // FNV-1a hash over the raw address bytes
        size_t hash = 14695981039346656037ULL;
        auto* bytes = reinterpret_cast<const uint8_t*>(&addr.storage);
        for (int i = 0; i < addr.addrLen; ++i) {
            hash ^= static_cast<size_t>(bytes[i]);
            hash *= 1099511628211ULL;
        }
        return hash;
    }
};

struct ClientConnection {
    uint16_t clientId = 0;
    NetAddress address;
    uint32_t sessionToken = 0;
    float lastHeartbeat = 0.0f;
    ReliabilityLayer reliability;

    VisibilitySet aoi;
    std::unordered_map<uint64_t, SvEntityUpdateMsg> lastSentState; // keyed by PersistentId value
    uint64_t playerEntityId = 0; // PersistentId of this client's player entity

    int account_id = 0;
    std::string character_id;
    AuthToken authToken = {};  // populated from Connect packet payload
    PacketCrypto crypto;       // AEAD encrypt/decrypt for this session
    PacketCrypto::PublicKey clientPublicKey = {};  // DH public key from Connect payload
    bool hasClientPublicKey = false;
};

class ConnectionManager {
public:
    uint16_t addClient(const NetAddress& address, float currentTime = 0.0f);
    void removeClient(uint16_t clientId);
    ClientConnection* findById(uint16_t clientId);
    ClientConnection* findByAddress(const NetAddress& address);
    bool validateToken(const NetAddress& address, uint32_t token);
    void heartbeat(uint16_t clientId, float currentTime);
    std::vector<uint16_t> getTimedOutClients(float currentTime, float timeoutSeconds);
    size_t clientCount() const { return clients_.size(); }

    void setMaxClients(size_t max) { maxClients_ = max; }
    size_t getMaxClients() const { return maxClients_; }

    template<typename F>
    void forEach(F&& fn) {
        for (auto& [id, client] : clients_) fn(client);
    }

private:
    uint16_t nextClientId_ = 1;
    size_t maxClients_ = 2000; // default cap to prevent DoS
    std::unordered_map<uint16_t, ClientConnection> clients_;
    std::unordered_map<NetAddress, uint16_t, NetAddressHash> addressToClient_;
    std::mt19937 rng_{std::random_device{}()};
    uint32_t generateToken();
};

} // namespace fate
