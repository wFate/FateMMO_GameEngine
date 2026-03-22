#pragma once
#include <array>
#include <cstdint>
#include <algorithm>
#include "engine/net/packet.h"

namespace fate {

// ============================================================================
// TokenBucket — simple token bucket for rate limiting
// ============================================================================
struct TokenBucket {
    float  tokens     = 0.0f;
    float  capacity   = 10.0f;
    float  refillRate = 5.0f;  // tokens per second
    double lastTime   = 0.0;

    // Refill tokens based on elapsed time, cap at capacity, then try to consume one.
    // Returns true if a token was successfully consumed.
    bool tryConsume(double now) {
        double elapsed = now - lastTime;
        if (elapsed > 0.0) {
            tokens += static_cast<float>(elapsed) * refillRate;
            if (tokens > capacity) tokens = capacity;
            lastTime = now;
        }
        if (tokens >= 1.0f) {
            tokens -= 1.0f;
            return true;
        }
        return false;
    }
};

// ============================================================================
// RateLimitResult
// ============================================================================
enum class RateLimitResult : uint8_t {
    Ok,
    Dropped,
    Disconnect
};

// ============================================================================
// ClientRateLimiter — per-client, per-packet-type token bucket array
// ============================================================================
class ClientRateLimiter {
public:
    ClientRateLimiter() {
        // Default limits for every slot
        for (int i = 0; i < 256; ++i) {
            buckets_[i].capacity   = 10.0f;
            buckets_[i].tokens     = 10.0f;
            buckets_[i].refillRate = 5.0f;
            buckets_[i].lastTime   = 0.0;
            disconnectThresholds_[i] = 100;
        }

        // Specific overrides
        configure(PacketType::CmdMove,          65.0f, 60.0f,  300); // client sends at 60fps
        configure(PacketType::CmdAction,         5.0f,  2.0f,  100);
        configure(PacketType::CmdUseSkill,       3.0f,  1.0f,   50);
        configure(PacketType::CmdChat,           3.0f,  0.33f,  30);
        configure(PacketType::CmdMarket,         3.0f,  0.5f,   50);
        configure(PacketType::CmdTrade,          5.0f,  2.0f,   50);
        configure(PacketType::CmdBounty,         2.0f,  0.5f,   30);
        configure(PacketType::CmdGuild,          3.0f,  1.0f,   30);
        configure(PacketType::CmdSocial,         3.0f,  1.0f,   30);
        configure(PacketType::CmdGauntlet,       3.0f,  1.0f,   30);
        configure(PacketType::CmdQuestAction,    5.0f,  2.0f,   50);
        configure(PacketType::CmdZoneTransition, 2.0f,  0.5f,   20);
        configure(PacketType::CmdRespawn,        2.0f,  0.33f,  20);
        configure(PacketType::CmdBank,           3.0f,  1.0f,   30);
        configure(PacketType::CmdSocketItem,      2.0f,  0.5f,   30);
        configure(PacketType::CmdStatEnchant,     2.0f,  0.5f,   30);
        configure(PacketType::CmdUseConsumable,    3.0f,  1.0f,   30);
        configure(PacketType::CmdRankingQuery,     2.0f,  0.5f,   20);
    }

    // Check a packet. Updates internal state and returns Ok/Dropped/Disconnect.
    RateLimitResult check(uint8_t packetType, double now) {
        // Decay violations after 60 seconds of no violations
        if (violations_ > 0 && (now - lastViolationTime_) >= 60.0) {
            violations_ = 0;
        }

        TokenBucket& bucket = buckets_[packetType];
        if (bucket.tryConsume(now)) {
            return RateLimitResult::Ok;
        }
        // Token exhausted — count as violation
        ++violations_;
        lastViolationTime_ = now;
        if (violations_ >= disconnectThresholds_[packetType]) {
            return RateLimitResult::Disconnect;
        }
        return RateLimitResult::Dropped;
    }

    uint32_t violations() const { return violations_; }

private:
    std::array<TokenBucket, 256> buckets_;
    std::array<uint32_t, 256>    disconnectThresholds_;
    uint32_t                     violations_ = 0;
    double                       lastViolationTime_ = 0.0;

    void configure(uint8_t type, float burst, float sustained, uint32_t disconnectAt) {
        buckets_[type].capacity   = burst;
        buckets_[type].tokens     = burst;
        buckets_[type].refillRate = sustained;
        buckets_[type].lastTime   = 0.0;
        disconnectThresholds_[type] = disconnectAt;
    }
};

} // namespace fate
