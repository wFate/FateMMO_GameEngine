#include "engine/net/reliability.h"
#include "engine/core/logger.h"
#include <algorithm>

namespace fate {

void ReliabilityLayer::trackReliable(uint16_t sequence, const uint8_t* data, size_t size, float currentTime) {
    if (pending_.size() >= MAX_PENDING_PACKETS) {
        LOG_WARN("Reliability", "Dropping oldest unacked reliable packet (seq=%u) — pending queue full (%d)",
                 pending_.begin()->sequence, static_cast<int>(MAX_PENDING_PACKETS));
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
        // New sequence is more recent. Guard the shift: shifting a uint64_t
        // by >=64 is undefined behavior in C++, so zero the window when the
        // gap meets or exceeds its width.
        int16_t diff = static_cast<int16_t>(remoteSequence - remoteSequence_);
        if (diff > 0) {
            if (diff >= WINDOW_BITS) {
                receivedBits_ = 0;
            } else {
                receivedBits_ <<= diff;
                receivedBits_ |= (1ULL << (diff - 1));
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
        if (diff > 0 && diff <= WINDOW_BITS) {
            uint64_t bit = (1ULL << (diff - 1));
            if (receivedBits_ & bit) {
                return false; // already received this sequence
            }
            receivedBits_ |= bit;
            return true; // old but not yet received
        }
        // v9: retransmit of a packet older than the ACK window. Without this,
        // the peer's pending queue would hold this seq forever because our
        // inline ackBits can't cover it. Queue it for a CmdAckExtended so the
        // sender can drain it.
        pendingExtendedAcks_.push_back(remoteSequence);
        return false; // still considered a duplicate by caller — don't reprocess
    }
}

void ReliabilityLayer::buildAckFields(uint16_t& ack, uint64_t& ackBits) const {
    ack = remoteSequence_;
    ackBits = receivedBits_;
}

void ReliabilityLayer::processAck(uint16_t ack, uint64_t ackBits, float currentTime) {
    pending_.erase(
        std::remove_if(pending_.begin(), pending_.end(),
            [&](const PendingPacket& pkt) {
                bool acked = false;
                if (pkt.sequence == ack) {
                    acked = true;
                } else {
                    int16_t diff = static_cast<int16_t>(ack - pkt.sequence);
                    if (diff > 0 && diff <= WINDOW_BITS) {
                        if (ackBits & (1ULL << (diff - 1))) {
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

void ReliabilityLayer::processExtendedAck(const uint16_t* seqs, size_t count, float currentTime) {
    if (count == 0 || !seqs) return;
    // Small counts (typical: 1-20 stranded seqs per CmdAckExtended), so a
    // linear scan per seq is fine. O(count × pending_.size()) worst case.
    for (size_t i = 0; i < count; ++i) {
        uint16_t target = seqs[i];
        auto it = std::find_if(pending_.begin(), pending_.end(),
            [target](const PendingPacket& pkt) { return pkt.sequence == target; });
        if (it != pending_.end()) {
            if (it->retransmitCount == 0 && currentTime > 0.0f) {
                float sample = currentTime - it->timeSent;
                rtt_ = rtt_ * 0.875f + sample * 0.125f;
            }
            pending_.erase(it);
        }
    }
}

std::vector<const PendingPacket*> ReliabilityLayer::getRetransmits(float currentTime, float retransmitDelay) {
    std::vector<const PendingPacket*> result;
    for (const auto& pkt : pending_) {
        if (currentTime - pkt.lastRetransmit >= retransmitDelay) {
            result.push_back(&pkt);
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
