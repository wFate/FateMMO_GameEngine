#include "engine/net/reliability.h"
#include <algorithm>

namespace fate {

void ReliabilityLayer::trackReliable(uint16_t sequence, const uint8_t* data, size_t size, float currentTime) {
    if (pending_.size() >= MAX_PENDING_PACKETS) {
        // Drop oldest unacked packet to prevent memory exhaustion
        pending_.erase(pending_.begin());
    }

    PendingPacket pkt;
    pkt.sequence = sequence;
    pkt.data.assign(data, data + size);
    pkt.timeSent = currentTime;
    pkt.lastRetransmit = currentTime;
    pkt.retransmitCount = 0;
    pending_.push_back(std::move(pkt));
}

bool ReliabilityLayer::onReceive(uint16_t remoteSequence) {
    if (!receivedAny_) {
        receivedAny_ = true;
        remoteSequence_ = remoteSequence;
        receivedBits_ = 0;
        return true; // first packet ever — always new
    }

    if (sequenceGreaterThan(remoteSequence, remoteSequence_)) {
        // New sequence is more recent — shift bits left by the difference
        int16_t diff = static_cast<int16_t>(remoteSequence - remoteSequence_);
        if (diff > 0) {
            receivedBits_ <<= diff;
            // Set bit for the old remoteSequence_ (now at offset diff-1)
            if (diff <= 32) {
                receivedBits_ |= (1u << (diff - 1));
            }
        }
        remoteSequence_ = remoteSequence;
        return true; // newer than anything we've seen — new
    } else if (remoteSequence == remoteSequence_) {
        // Exact duplicate of the most recent sequence
        return false;
    } else {
        // Older sequence — check if we already received it
        int16_t diff = static_cast<int16_t>(remoteSequence_ - remoteSequence);
        if (diff > 0 && diff <= 32) {
            uint32_t bit = (1u << (diff - 1));
            if (receivedBits_ & bit) {
                return false; // already received this sequence
            }
            receivedBits_ |= bit;
            return true; // old but not yet received
        }
        // Too old to track — assume duplicate
        return false;
    }
}

void ReliabilityLayer::buildAckFields(uint16_t& ack, uint32_t& ackBits) const {
    ack = remoteSequence_;
    ackBits = receivedBits_;
}

void ReliabilityLayer::processAck(uint16_t ack, uint32_t ackBits, float currentTime) {
    pending_.erase(
        std::remove_if(pending_.begin(), pending_.end(),
            [&](const PendingPacket& pkt) {
                bool acked = false;
                if (pkt.sequence == ack) {
                    acked = true;
                } else {
                    int16_t diff = static_cast<int16_t>(ack - pkt.sequence);
                    if (diff > 0 && diff <= 32) {
                        if (ackBits & (1u << (diff - 1))) {
                            acked = true;
                        }
                    }
                }
                if (acked && pkt.retransmitCount == 0 && currentTime > 0.0f) {
                    float sample = currentTime - pkt.timeSent;
                    rtt_ = rtt_ * 0.875f + sample * 0.125f;
                }
                return acked;
            }),
        pending_.end());
}

std::vector<PendingPacket> ReliabilityLayer::getRetransmits(float currentTime, float retransmitDelay) {
    std::vector<PendingPacket> result;
    for (const auto& pkt : pending_) {
        if (currentTime - pkt.lastRetransmit >= retransmitDelay) {
            result.push_back(pkt);
        }
    }
    return result;
}

void ReliabilityLayer::markRetransmitted(uint16_t sequence, float currentTime) {
    for (auto& pkt : pending_) {
        if (pkt.sequence == sequence) {
            pkt.lastRetransmit = currentTime;
            pkt.retransmitCount++;
            return;
        }
    }
}

} // namespace fate
