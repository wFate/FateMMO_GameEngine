#pragma once
#include "engine/net/packet.h"
#include <cstdint>
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

    void trackReliable(uint16_t sequence, const uint8_t* data, size_t size);
    /// Returns true if this is a NEW packet, false if duplicate.
    bool onReceive(uint16_t remoteSequence);
    void buildAckFields(uint16_t& ack, uint16_t& ackBits) const;
    void processAck(uint16_t ack, uint16_t ackBits, float currentTime = 0.0f);
    std::vector<PendingPacket> getRetransmits(float currentTime, float retransmitDelay = 0.2f);
    void markRetransmitted(uint16_t sequence, float currentTime);

    size_t pendingReliableCount() const { return pending_.size(); }
    float rtt() const { return rtt_; }

    void reset() {
        localSequence_ = 0;
        remoteSequence_ = 0;
        receivedAny_ = false;
        receivedBits_ = 0;
        pending_.clear();
        rtt_ = 0.1f;
    }

private:
    uint16_t localSequence_ = 0;
    uint16_t remoteSequence_ = 0;
    bool receivedAny_ = false;
    uint32_t receivedBits_ = 0;
    std::vector<PendingPacket> pending_;
    float rtt_ = 0.1f;
};

} // namespace fate
