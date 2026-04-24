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
    // Queue size must accommodate initial-replication bursts. v9 coalesces
    // SvEntityEnter into SvEntityEnterBatch (~12× reduction) and widens the
    // ACK window to 64 bits, but we keep the 2048 cap for headroom against
    // future bursts (raid mechanics, zone-wide broadcasts, etc.). Memory
    // cost: ~1MB per client at typical ~500-byte packets — fine for 2000 CCU.
    static constexpr size_t MAX_PENDING_PACKETS = 2048;

    // v9: ACK window widened from 32 → 64. Still not enough to cover a full
    // 231-entity scene-entry burst on its own, which is why v9 also adds
    // SvEntityEnterBatch (coalesce) and CmdAckExtended (explicit stranded-seq
    // ACK). These three together are what actually prevent permanent
    // stranding in the pending queue.
    static constexpr int WINDOW_BITS = 64;

    uint16_t nextLocalSequence() { return localSequence_++; }
    uint16_t currentLocalSequence() const { return localSequence_; }

    void trackReliable(uint16_t sequence, const uint8_t* data, size_t size, float currentTime = 0.0f);
    /// Returns true if this is a NEW packet, false if duplicate.
    bool onReceive(uint16_t remoteSequence);
    void buildAckFields(uint16_t& ack, uint64_t& ackBits) const;
    void processAck(uint16_t ack, uint64_t ackBits, float currentTime = 0.0f);
    // v9: process explicit seqs from CmdAckExtended — removes matching
    // pending entries even if they fall outside the 64-bit inline window.
    void processExtendedAck(const uint16_t* seqs, size_t count, float currentTime = 0.0f);
    std::vector<const PendingPacket*> getRetransmits(float currentTime, float retransmitDelay = 0.2f);
    void markRetransmitted(uint16_t sequence, float currentTime);

    // v9: client-side accumulator for retransmits that arrive outside the
    // 64-bit ACK window. onReceive() appends to this list when it hits the
    // "too old to track" branch. Callers flush this via CmdAckExtended.
    const std::vector<uint16_t>& drainPendingExtendedAcks() const { return pendingExtendedAcks_; }
    void clearPendingExtendedAcks() { pendingExtendedAcks_.clear(); }

    size_t pendingReliableCount() const { return pending_.size(); }
    float rtt() const { return rtt_; }

    // True when the pending queue is >75% full — client is likely dead or
    // severely lagged. Callers should skip sending new reliable packets
    // to avoid filling the queue and spamming drop warnings.
    bool isCongested() const { return pending_.size() >= MAX_PENDING_PACKETS * 3 / 4; }

    // Backpressure telemetry — incremented by NetServer::sendPacket so a
    // periodic log can prove the critical-lane bypass is actually firing.
    void noteNonCriticalDrop() { ++droppedNonCritical_; }
    void noteCriticalPreserve() { ++preservedCritical_; }
    uint32_t droppedNonCritical() const { return droppedNonCritical_; }
    uint32_t preservedCritical() const { return preservedCritical_; }
    void resetBackpressureCounters() {
        droppedNonCritical_ = 0;
        preservedCritical_ = 0;
    }

    void reset() {
        localSequence_ = 0;
        remoteSequence_ = 0;
        receivedAny_ = false;
        receivedBits_ = 0;
        pending_.clear();
        pendingExtendedAcks_.clear();
        rtt_ = 0.1f;
        droppedNonCritical_ = 0;
        preservedCritical_ = 0;
    }

private:
    uint16_t localSequence_ = 0;
    uint16_t remoteSequence_ = 0;
    bool receivedAny_ = false;
    uint64_t receivedBits_ = 0; // v9: widened 32→64 bit ACK window
    std::deque<PendingPacket> pending_;
    std::vector<uint16_t> pendingExtendedAcks_; // v9: stranded-seq receipts to flush
    float rtt_ = 0.1f;
    uint32_t droppedNonCritical_ = 0;
    uint32_t preservedCritical_ = 0;
};

} // namespace fate
