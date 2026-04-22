#pragma once
#include "engine/net/socket.h"
#include "engine/net/packet.h"
#include "engine/net/byte_stream.h"
#include "engine/net/connection.h"
#include "engine/core/logger.h"
#include <functional>

namespace fate {

class NetServer {
public:
    bool start(uint16_t port);
    void stop();

    // Call once per tick to drain incoming packets
    void poll(float currentTime);

    // Send to a specific client
    void sendTo(uint16_t clientId, Channel channel, uint8_t packetType,
                const uint8_t* payload, size_t payloadSize);

    // Send to all connected clients
    void broadcast(Channel channel, uint8_t packetType,
                   const uint8_t* payload, size_t payloadSize);

    // Retransmit unacked reliable packets
    void processRetransmits(float currentTime);

    // Returns IDs of timed-out clients
    std::vector<uint16_t> checkTimeouts(float currentTime, float timeoutSec = 10.0f);

    // Port the server is listening on
    uint16_t port() const { return socket_.port(); }

    ConnectionManager& connections() { return connections_; }

    // Send a ConnectReject to an address (used by ServerApp for invalid auth tokens)
    void sendConnectReject(const NetAddress& to, const std::string& reason);

    // Callbacks
    std::function<void(uint16_t clientId)> onClientConnected;      // handshake prep (crypto only)
    std::function<void(uint16_t clientId)> onClientDisconnected;
    std::function<void(uint16_t clientId, uint8_t packetType, ByteReader& payload)> onPacketReceived;
    // C4: fired after client sends an encrypted CmdAuthProof containing a valid auth token.
    // This is the point where the session is bound to an account + full DB load begins.
    std::function<void(uint16_t clientId, const AuthToken& token)> onAuthProofReceived;

private:
    NetSocket socket_;
    ConnectionManager connections_;
    float lastPollTime_ = 0.0f;  // cached from poll() for RTT tracking

    void handleRawPacket(const NetAddress& from, const uint8_t* data, int size, float currentTime);
    void handleConnect(const NetAddress& from, const uint8_t* payload, size_t payloadSize, float currentTime);
    void sendPacket(ClientConnection& client, Channel channel, uint8_t packetType,
                    const uint8_t* payload, size_t payloadSize);
};

} // namespace fate
