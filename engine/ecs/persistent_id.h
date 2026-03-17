#pragma once
#include <cstdint>
#include <chrono>
#include <atomic>
#include <functional>

namespace fate {

// ==========================================================================
// PersistentId — 64-bit globally unique entity ID for serialization
//
// Layout: zoneId:16 | creationTime:32 | sequence:16
//
// The zoneId identifies which zone created the entity. creationTime is
// seconds since epoch (truncated to 32 bits). sequence disambiguates
// entities created within the same second. On sequence wrap (65536),
// the clock is re-read to advance creationTime.
// ==========================================================================
class PersistentId {
public:
    PersistentId() : value_(0) {}

    explicit PersistentId(uint64_t raw) : value_(raw) {}

    static PersistentId null() { return PersistentId(0); }

    // Generate a new unique ID for the given zone.
    // Thread-safe: uses atomics for sequence counter.
    static PersistentId generate(uint16_t zoneId) {
        uint16_t seq = sequence_.fetch_add(1, std::memory_order_relaxed);
        if (seq == 0) {
            // Sequence wrapped — re-read clock to advance creationTime
            lastTime_.store(currentTimestamp(), std::memory_order_relaxed);
        }

        uint32_t time = lastTime_.load(std::memory_order_relaxed);
        if (time == 0) {
            // First call — initialize
            time = currentTimestamp();
            lastTime_.store(time, std::memory_order_relaxed);
        }

        uint64_t id = (static_cast<uint64_t>(zoneId) << 48)
                     | (static_cast<uint64_t>(time) << 16)
                     | static_cast<uint64_t>(seq);
        return PersistentId(id);
    }

    uint64_t value() const { return value_; }
    uint16_t zoneId() const { return static_cast<uint16_t>(value_ >> 48); }
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
