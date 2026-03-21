#include <doctest/doctest.h>
#include "server/rate_limiter.h"
using namespace fate;

TEST_CASE("TokenBucket allows burst up to capacity") {
    TokenBucket bucket;
    bucket.tokens = 5.0f;
    bucket.capacity = 5.0f;
    bucket.refillRate = 1.0f;
    bucket.lastTime = 0.0;

    // Should be able to consume 5 tokens (full burst)
    for (int i = 0; i < 5; ++i) {
        CHECK(bucket.tryConsume(0.0) == true);
    }
    // 6th should fail
    CHECK(bucket.tryConsume(0.0) == false);
}

TEST_CASE("TokenBucket refills over time") {
    TokenBucket bucket;
    bucket.tokens = 0.0f;
    bucket.capacity = 5.0f;
    bucket.refillRate = 5.0f; // 5 tokens/sec
    bucket.lastTime = 0.0;

    // After 1 second, should have 5 tokens available
    CHECK(bucket.tryConsume(1.0) == true);
    CHECK(bucket.tryConsume(1.0) == true);
    CHECK(bucket.tryConsume(1.0) == true);
    CHECK(bucket.tryConsume(1.0) == true);
    CHECK(bucket.tryConsume(1.0) == true);
    // Bucket should now be empty again (all consumed at t=1.0)
    // No more time has passed so should fail
    CHECK(bucket.tryConsume(1.0) == false);
}

TEST_CASE("TokenBucket caps at capacity") {
    // Use increasing timestamps: 0, 100, 200, 300
    TokenBucket bucket;
    bucket.tokens = 0.0f;
    bucket.capacity = 5.0f;
    bucket.refillRate = 1.0f; // 1 token/sec
    bucket.lastTime = 0.0;

    // At t=100: elapsed=100s, refill would be 100 tokens but capped at 5.
    // Consume 5 tokens at t=100 (no time passes between calls so no extra refill).
    CHECK(bucket.tryConsume(100.0) == true);
    CHECK(bucket.tryConsume(100.0) == true);
    CHECK(bucket.tryConsume(100.0) == true);
    CHECK(bucket.tryConsume(100.0) == true);
    CHECK(bucket.tryConsume(100.0) == true);
    // Bucket is now empty — 6th at t=100 fails
    CHECK(bucket.tryConsume(100.0) == false);

    // At t=200: elapsed=100s again, refill capped at 5 again
    CHECK(bucket.tryConsume(200.0) == true);
    CHECK(bucket.tryConsume(200.0) == true);
    CHECK(bucket.tryConsume(200.0) == true);
    CHECK(bucket.tryConsume(200.0) == true);
    CHECK(bucket.tryConsume(200.0) == true);
    // Still capped, not 100 tokens available
    CHECK(bucket.tryConsume(200.0) == false);

    // At t=300: same pattern
    CHECK(bucket.tryConsume(300.0) == true);
}

TEST_CASE("ClientRateLimiter blocks flooding CmdUseSkill") {
    ClientRateLimiter limiter;
    double now = 1000.0;

    // CmdUseSkill has burst=3, so first 3 should succeed
    CHECK(limiter.check(PacketType::CmdUseSkill, now) == RateLimitResult::Ok);
    CHECK(limiter.check(PacketType::CmdUseSkill, now) == RateLimitResult::Ok);
    CHECK(limiter.check(PacketType::CmdUseSkill, now) == RateLimitResult::Ok);
    // 4th should be dropped
    CHECK(limiter.check(PacketType::CmdUseSkill, now) == RateLimitResult::Dropped);
}

TEST_CASE("ClientRateLimiter tracks violations for disconnect") {
    ClientRateLimiter limiter;
    double now = 2000.0;

    // CmdUseSkill disconnectAt=50. Exhaust burst (3) then spam drops.
    // First 3 are Ok, then each subsequent is a violation.
    limiter.check(PacketType::CmdUseSkill, now);
    limiter.check(PacketType::CmdUseSkill, now);
    limiter.check(PacketType::CmdUseSkill, now);

    // Send 47 more (all dropped, each incrementing violations)
    RateLimitResult result = RateLimitResult::Ok;
    for (int i = 0; i < 47; ++i) {
        result = limiter.check(PacketType::CmdUseSkill, now);
    }
    // At 47 violations we should still be Dropped
    CHECK(result == RateLimitResult::Dropped);

    // One more should push us to 48... keep going until disconnect
    // disconnectAt=50 so we need 50 violations total
    limiter.check(PacketType::CmdUseSkill, now);
    limiter.check(PacketType::CmdUseSkill, now);
    result = limiter.check(PacketType::CmdUseSkill, now);
    CHECK(result == RateLimitResult::Disconnect);
}

TEST_CASE("ClientRateLimiter movement has higher burst") {
    ClientRateLimiter limiter;
    double now = 3000.0;

    // CmdMove burst=65 (raised for 60fps client sends)
    for (int i = 0; i < 65; ++i) {
        CHECK(limiter.check(PacketType::CmdMove, now) == RateLimitResult::Ok);
    }
    // 66th should be dropped
    CHECK(limiter.check(PacketType::CmdMove, now) == RateLimitResult::Dropped);
}

TEST_CASE("ClientRateLimiter unknown packet types get default limits") {
    ClientRateLimiter limiter;
    double now = 4000.0;

    // Default burst=10
    for (int i = 0; i < 10; ++i) {
        CHECK(limiter.check(0xFF, now) == RateLimitResult::Ok);
    }
    // 11th should be dropped
    CHECK(limiter.check(0xFF, now) == RateLimitResult::Dropped);
}
