#pragma once
#include "engine/net/packet.h"
#include <cstdint>
#include <deque>
#include <vector>

namespace fate {

struct PendingPacket {
    uint16_t sequence = 0;
    uint8_t  packetType = 0;          // S141 — recorded so hard-eviction telemetry
                                      //        can attribute drops back to opcodes.
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

    // Returns the opcode of the evicted oldest packet if the queue was full
    // (so the caller can attribute the hard-eviction to that opcode in
    // telemetry), or 0 if no eviction happened.
    uint8_t trackReliable(uint16_t sequence, uint8_t packetType,
                          const uint8_t* data, size_t size,
                          float currentTime = 0.0f);
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

    // S143 audit — bucket the current pending reliable queue by packetType
    // into the caller's 256-entry array. Used by NetServer::sampleMaxPendingDepth
    // to prove which opcode is hogging the queue under retx storms. Reads
    // pending_.packetType only — no header re-parse, cheap.
    template <typename Array256>
    void tallyPendingByOpcode(Array256& out) const {
        for (const auto& pkt : pending_) {
            ++out[pkt.packetType];
        }
    }

    // S143 audit — for each opcode currently in pending_, find the oldest
    // (smallest timeSent) and update out[opcode] = max(out[opcode], age_ms).
    // age_ms is computed against the caller-supplied currentTime. Lets the
    // 5s stats line surface "0xE0 oldest is 8.4s" — direct evidence of
    // queue-stuck packets vs fast churn.
    template <typename Array256u32>
    void tallyOldestPendingAgeMs(float currentTime, Array256u32& out) const {
        // First pass: find oldest timeSent per opcode in this client's queue.
        float oldestPerOp[256];
        bool  hasOp[256] = {};
        for (auto& f : oldestPerOp) f = 0.0f;
        for (const auto& pkt : pending_) {
            uint8_t op = pkt.packetType;
            if (!hasOp[op] || pkt.timeSent < oldestPerOp[op]) {
                oldestPerOp[op] = pkt.timeSent;
                hasOp[op] = true;
            }
        }
        // Second pass: convert to ages, update window max.
        for (size_t i = 0; i < 256; ++i) {
            if (!hasOp[i]) continue;
            float ageSec = currentTime - oldestPerOp[i];
            if (ageSec < 0.0f) ageSec = 0.0f;
            uint32_t ageMs = static_cast<uint32_t>(ageSec * 1000.0f);
            if (ageMs > out[i]) out[i] = ageMs;
        }
    }

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
