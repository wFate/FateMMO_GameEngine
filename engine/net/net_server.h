#pragma once
#include "engine/net/socket.h"
#include "engine/net/packet.h"
#include "engine/net/byte_stream.h"
#include "engine/net/connection.h"
#include "engine/core/logger.h"
#include <array>
#include <functional>
#include <string>

namespace fate {

// S141 Phase A — per-opcode network telemetry. Indexed by uint8_t packetType
// (0..255). Reset after each 5s flush so each window's totals are independent
// and can be correlated with the existing tick-stats line. The ServerApp tick
// loop reads + formats + resets these via NetServer::flushOpcodeStats().
//
// All counters are per-server (across all clients). Adding per-client tracking
// is a future refinement; the immediate question is "which opcode is filling
// the queue", which is answered by the global view.
struct OpcodeStats {
    // S143 audit revision — sentPackets/sentBytes are FRESH sends only. The
    // earlier scheme bumped them on retransmit too (so wire-bytes were
    // accurate) but that hid the fresh-vs-retx ratio. Restoring fresh-only
    // semantics + adding retransmitBytes makes both numbers visible. Total
    // wire = sentBytes + retransmitBytes.
    std::array<uint64_t, 256> sentPackets {};       // FRESH sends only (no retransmits)
    std::array<uint64_t, 256> sentBytes {};         // FRESH wire bytes only
    std::array<uint64_t, 256> backpressureDrops {}; // dropped at sendPacket congestion gate
    std::array<uint64_t, 256> retransmits {};       // re-sent by processRetransmits (count)
    std::array<uint64_t, 256> retransmitBytes {};   // re-sent wire bytes (matches retransmits)
    std::array<uint64_t, 256> hardEvictions {};     // evicted from oldest in trackReliable
    // S143 audit — per-opcode max depth in the current pending reliable queue,
    // sampled across all clients each tick. Without this, we can see TOTAL
    // maxPendingDepth and retransmit COUNT but cannot tell which opcode is
    // hogging the queue. EntityEnterBatch retx=3440 sustained could be 200
    // packets retrying 17× each, OR 3440 packets retrying once — the per-
    // opcode pending depth disambiguates.
    std::array<uint64_t, 256> pendingByOpcodeMax {}; // worst-tick per-opcode depth in window
    // S143 audit — oldest pending packet age (ms) per opcode in the window.
    // If 0xE0 packets sit unacked for many seconds, the queue is stuck on
    // them — distinguishes "stuck in queue" from "high churn through queue".
    std::array<uint32_t, 256> oldestPendingAgeMsMax {};
    // S143 audit — received-side counters. Without these, we cannot tell
    // whether CmdAckExtended is reaching the server during a retransmit
    // storm (which determines ACK-bug vs paced-streaming as the next fix).
    std::array<uint64_t, 256> receivedPackets {};
    std::array<uint64_t, 256> receivedBytes {};
    size_t maxPendingDepth = 0;                     // worst observed across all clients

    void reset() {
        sentPackets.fill(0);
        sentBytes.fill(0);
        backpressureDrops.fill(0);
        retransmits.fill(0);
        retransmitBytes.fill(0);
        hardEvictions.fill(0);
        pendingByOpcodeMax.fill(0);
        oldestPendingAgeMsMax.fill(0);
        receivedPackets.fill(0);
        receivedBytes.fill(0);
        maxPendingDepth = 0;
    }
};

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

    // S141 Phase A — per-opcode telemetry. The current window's snapshot is
    // exposed read-only; flushOpcodeStats() returns a formatted line covering
    // the last window and resets the counters in one operation.
    const OpcodeStats& opcodeStats() const { return opcodeStats_; }
    // Sample the deepest reliable queue across all clients into maxPendingDepth.
    // Cheap (linear scan over connection map). Call once per tick before flush.
    // S143 audit — currentTime passed in so per-opcode oldest-age can be
    // computed against the same clock as packet timeSent.
    void sampleMaxPendingDepth(float currentTime = 0.0f);
    // Format the last window's per-opcode counters. Returns an empty string
    // if there's nothing actionable (no sends + no drops + no evictions).
    // formatOpcodeStats() does NOT reset the counters — call it from the
    // congestion log to get an in-window snapshot. flushOpcodeStats() formats
    // and resets in one operation — call it from the 5s tick-stats flush.
    // Format: "Net stats (5s): tx=N pkts/M B | drops=K | evict=K | maxQ=Z |
    //         top: 0xAB(name)=BB B/PP pkts | 0xCD=...".
    std::string formatOpcodeStats() const;
    void        resetOpcodeStats() { opcodeStats_.reset(); }
    std::string flushOpcodeStats() {
        std::string s = formatOpcodeStats();
        resetOpcodeStats();
        return s;
    }

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
    OpcodeStats opcodeStats_;    // S141 Phase A — telemetry, reset every 5s by ServerApp

    void handleRawPacket(const NetAddress& from, const uint8_t* data, int size, float currentTime);
    void handleConnect(const NetAddress& from, const uint8_t* payload, size_t payloadSize, float currentTime);
    void sendPacket(ClientConnection& client, Channel channel, uint8_t packetType,
                    const uint8_t* payload, size_t payloadSize);
};

} // namespace fate
