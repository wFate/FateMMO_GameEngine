#pragma once
#include <cstdint>
#include <chrono>
#include <atomic>
#include <functional>

namespace fate {

// ==========================================================================
// PersistentId — 64-bit globally unique entity ID for serialization
//
// Layout (16-bit zone field is split across two sub-fields for sharding):
//
//   bits 63..60  shardId   (4 bits — 16 shards max)
//   bits 59..48  localZone (12 bits — 4096 zones per shard)
//   bits 47..16  creationTime (seconds-since-epoch, 32 bits)
//   bits 15..0   sequence
//
// In single-shard deployments shardId is always 0 and the layout is byte-
// identical to the original (zoneId == localZone, since high 4 bits are 0).
// Cross-shard routing reads shardId() to dispatch CmdZoneTransition to the
// owning shard. Within a shard, localZoneId() acts like the old zoneId.
// ==========================================================================
class PersistentId {
public:
    static constexpr int kShardBits = 4;          // 16 shards
    static constexpr int kLocalZoneBits = 12;     // 4096 zones per shard
    static constexpr uint16_t kShardMask = 0x0F;  // 4 bits
    static constexpr uint16_t kLocalZoneMask = 0x0FFF; // 12 bits

    PersistentId() : value_(0) {}

    explicit PersistentId(uint64_t raw) : value_(raw) {}

    static PersistentId null() { return PersistentId(0); }

    // Generate a new unique ID for the given zone (legacy single-shard).
    // Equivalent to generate(0, zoneId). Existing call sites compile unchanged.
    static PersistentId generate(uint16_t zoneId) {
        return generate(0, zoneId);
    }

    // Sharded generate. shardId must fit in 4 bits, localZoneId in 12 bits.
    // Out-of-range values are masked silently — callers should validate.
    static PersistentId generate(uint8_t shardId, uint16_t localZoneId) {
        uint16_t composed = static_cast<uint16_t>(
            ((shardId & kShardMask) << kLocalZoneBits) |
            (localZoneId & kLocalZoneMask));

        uint16_t seq = sequence_.fetch_add(1, std::memory_order_relaxed);
        if (seq == 0) {
            lastTime_.store(currentTimestamp(), std::memory_order_relaxed);
        }

        uint32_t time = lastTime_.load(std::memory_order_relaxed);
        if (time == 0) {
            time = currentTimestamp();
            lastTime_.store(time, std::memory_order_relaxed);
        }

        uint64_t id = (static_cast<uint64_t>(composed) << 48)
                     | (static_cast<uint64_t>(time) << 16)
                     | static_cast<uint64_t>(seq);
        return PersistentId(id);
    }

    uint64_t value() const { return value_; }
    // Legacy accessor — returns the full 16-bit zone field. In sharded
    // deployments this carries shardId in the high 4 bits. Most call sites
    // use this and don't care about sharding; the new shardId()/localZoneId()
    // accessors below are for the routing layer.
    uint16_t zoneId() const { return static_cast<uint16_t>(value_ >> 48); }
    uint8_t  shardId() const {
        return static_cast<uint8_t>((value_ >> (48 + kLocalZoneBits)) & kShardMask);
    }
    uint16_t localZoneId() const {
        return static_cast<uint16_t>((value_ >> 48) & kLocalZoneMask);
    }
    uint32_t creationTime() const { return static_cast<uint32_t>(value_ >> 16); }
    uint16_t sequence() const { return static_cast<uint16_t>(value_ & 0xFFFF); }

    bool isNull() const { return value_ == 0; }
    explicit operator bool() const { return value_ != 0; }

    bool operator==(const PersistentId& o) const { return value_ == o.value_; }
    bool operator!=(const PersistentId& o) const { return value_ != o.value_; }
    bool operator<(const PersistentId& o) const { return value_ < o.value_; }
    bool operator>(const PersistentId& o) const { return value_ > o.value_; }
    bool operator<=(const PersistentId& o) const { return value_ <= o.value_; }
    bool operator>=(const PersistentId& o) const { return value_ >= o.value_; }

private:
    uint64_t value_;

    static uint32_t currentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        return static_cast<uint32_t>(secs);
    }

    static inline std::atomic<uint16_t> sequence_{0};
    static inline std::atomic<uint32_t> lastTime_{0};
};

} // namespace fate

// Hash support for use in unordered containers
template<>
struct std::hash<fate::PersistentId> {
    size_t operator()(const fate::PersistentId& id) const noexcept {
        return std::hash<uint64_t>{}(id.value());
    }
};
