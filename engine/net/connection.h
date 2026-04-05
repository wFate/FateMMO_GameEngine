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

    // In-memory trade state (avoids DB round-trip for guard checks)
    int activeTradeSessionId = 0;       // 0 = not in trade
    std::string tradePartnerCharId;     // partner's character_id

    // Safe return point — set when entering instanced/event content (dungeon,
    // arena, battlefield).  On disconnect the save uses this instead of the
    // temporary event scene so the player isn't stranded on next login.
    struct ReturnPoint {
        std::string scene;
        float x = 0.0f, y = 0.0f;
    };
    ReturnPoint eventReturnPoint;  // non-empty scene = active

    // Invite prompt busy state — prevents concurrent invites
    bool hasActivePrompt = false;
    float guildInviteExpiresAt = 0.0f;      // gameTime when invite auto-expires
    int pendingGuildInviteId = 0;           // guild ID of pending invite
    std::string pendingGuildInviteFromCharId; // who sent the guild invite
};

class ConnectionManager {
public:
    uint16_t addClient(const NetAddress& address, float currentTime = 0.0f);
    void removeClient(uint16_t clientId);
    ClientConnection* findById(uint16_t clientId);
    ClientConnection* findByAddress(const NetAddress& address);
    ClientConnection* findByEntity(uint64_t playerEntityId);
    const ClientConnection* findByEntity(uint64_t playerEntityId) const;
    ClientConnection* findByEntityLow32(uint32_t entityId);
    void mapEntity(uint64_t playerEntityId, uint16_t clientId);
    void unmapEntity(uint64_t playerEntityId);
    bool validateToken(const NetAddress& address, uint32_t token);
    void heartbeat(uint16_t clientId, float currentTime);
    std::vector<uint16_t> getTimedOutClients(float currentTime, float timeoutSeconds);
    size_t clientCount() const { return clients_.size(); }

    void setMaxClients(size_t max) { maxClients_ = max; }
    size_t getMaxClients() const { return maxClients_; }
    void setMaxConnectionsPerIP(size_t max) { maxPerIP_ = max; }
    size_t getConnectionsForIP(const std::string& ip) const;

    template<typename F>
    void forEach(F&& fn) {
        for (auto& [id, client] : clients_) fn(client);
    }

    // Returns clientId for a given character_id, or 0 if not online
    uint16_t findClientByCharacterId(const std::string& charId) const {
        for (const auto& [id, client] : clients_) {
            if (client.character_id == charId) return id;
        }
        return 0;
    }

private:
    uint16_t nextClientId_ = 1;
    size_t maxClients_ = 2000;  // default cap to prevent DoS
    size_t maxPerIP_ = 5;       // max concurrent connections from same IP (family play)
    std::unordered_map<uint16_t, ClientConnection> clients_;
    std::unordered_map<std::string, size_t> connectionsPerIP_; // IP string -> active count
    std::unordered_map<NetAddress, uint16_t, NetAddressHash> addressToClient_;
    std::unordered_map<uint64_t, uint16_t> entityToClient_;      // playerEntityId → clientId
    std::unordered_map<uint32_t, uint16_t> entityToClientLow32_; // lower-32 → clientId (arena interop)
    std::mt19937 rng_{std::random_device{}()};
    uint32_t generateToken();
};

} // namespace fate
