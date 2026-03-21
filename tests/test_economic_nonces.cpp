#include <doctest/doctest.h>
#include "server/nonce_manager.h"

TEST_SUITE("Economic Nonces") {

TEST_CASE("issued nonce can be consumed once") {
    NonceManager mgr;
    auto nonce = mgr.issue(1, 0.0f);
    CHECK(mgr.consume(1, nonce, 0.0f));
    CHECK_FALSE(mgr.consume(1, nonce, 0.0f)); // replay rejected
}

TEST_CASE("wrong client cannot consume nonce") {
    NonceManager mgr;
    auto nonce = mgr.issue(1, 0.0f);
    CHECK_FALSE(mgr.consume(2, nonce, 0.0f));
    CHECK(mgr.consume(1, nonce, 0.0f)); // original client still can
}

TEST_CASE("random value cannot be consumed") {
    NonceManager mgr;
    mgr.issue(1, 0.0f);
    CHECK_FALSE(mgr.consume(1, 0xDEADBEEFCAFEBABE, 0.0f));
}

TEST_CASE("expired nonce is rejected") {
    NonceManager mgr;
    auto nonce = mgr.issue(1, 0.0f);
    CHECK_FALSE(mgr.consume(1, nonce, 61.0f)); // 61 seconds later
}

TEST_CASE("nonce within expiry window succeeds") {
    NonceManager mgr;
    auto nonce = mgr.issue(1, 10.0f);
    CHECK(mgr.consume(1, nonce, 69.0f)); // 59 seconds later, under 60s limit
}

TEST_CASE("multiple nonces per client are independent") {
    NonceManager mgr;
    auto n1 = mgr.issue(1, 0.0f);
    auto n2 = mgr.issue(1, 0.0f);
    CHECK(n1 != n2);
    CHECK(mgr.consume(1, n2, 0.0f));
    CHECK(mgr.consume(1, n1, 0.0f));
}

TEST_CASE("removeClient clears all nonces") {
    NonceManager mgr;
    mgr.issue(1, 0.0f);
    mgr.issue(1, 0.0f);
    CHECK(mgr.pendingCount(1) == 2);
    mgr.removeClient(1);
    CHECK(mgr.pendingCount(1) == 0);
}

TEST_CASE("expireAll removes old nonces") {
    NonceManager mgr;
    mgr.issue(1, 0.0f);   // old
    mgr.issue(1, 50.0f);  // recent
    mgr.expireAll(65.0f);  // 65s game time: first expired, second still valid
    CHECK(mgr.pendingCount(1) == 1);
}

} // TEST_SUITE
