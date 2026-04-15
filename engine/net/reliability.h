#pragma once
#include "engine/net/packet.h"
#include <cstdint>
#include <deque>
#include <vector>

namespace fate {

struct PendingPacket {
    uint16_t sequence = 0;
    std::vector<uint8_t> data;
    float timeSent = 0.0f;
    float lastRetransmit = 0.0f;
    int retransmitCount = 0;
};

class ReliabilityLayer {
public:
    uint16_t nextLocalSequence() { return localSequence_++; }
    uint16_t currentLocalSequence() const { return localSequence_; }

    void trackReliable(uint16_t sequence, const uint8_t* data, size_t size, float currentTime = 0.0f);
    /// Returns true if this is a NEW packet, false if duplicate.
    bool onReceive(uint16_t remoteSequence);
    void buildAckFields(uint16_t& ack, uint32_t& ackBits) const;
    void processAck(uint16_t ack, uint32_t ackBits, float currentTime = 0.0f);
    std::vector<const PendingPacket*> getRetransmits(float currentTime, float retransmitDelay = 0.2f);
    void markRetransmitted(uint16_t sequence, float currentTime);

    size_t pendingReliableCount() const { return pending_.size(); }
    float rtt() const { return rtt_; }

    // True when the pending queue is >75% full — client is likely dead or
    // severely lagged. Callers should skip sending new reliable packets
    // to avoid filling the queue and spamming drop warnings.
    bool isCongested() const { return pending_.size() >= MAX_PENDING_PACKETS * 3 / 4; }

    void reset() {
        localSequence_ = 0;
        remoteSequence_ = 0;
        receivedAny_ = false;
        receivedBits_ = 0;
        pending_.clear();
        rtt_ = 0.1f;
    }

private:
    static constexpr size_t MAX_PENDING_PACKETS = 256;

    uint16_t localSequence_ = 0;
    uint16_t remoteSequence_ = 0;
    bool receivedAny_ = false;
    uint32_t receivedBits_ = 0;
    std::deque<PendingPacket> pending_;
    float rtt_ = 0.1f;
};

} // namespace fate
