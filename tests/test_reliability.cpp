#include <doctest/doctest.h>
#include "engine/net/reliability.h"

using namespace fate;

TEST_CASE("ReliabilityLayer: track sent packets") {
    ReliabilityLayer rel;
    CHECK(rel.nextLocalSequence() == 0);
    CHECK(rel.nextLocalSequence() == 1);
}

TEST_CASE("ReliabilityLayer: process incoming acks") {
    ReliabilityLayer rel;
    uint8_t d0[] = {0x01}, d1[] = {0x02}, d2[] = {0x03};
    uint16_t s0 = rel.nextLocalSequence();
    rel.trackReliable(s0, d0, 1);
    uint16_t s1 = rel.nextLocalSequence();
    rel.trackReliable(s1, d1, 1);
    uint16_t s2 = rel.nextLocalSequence();
    rel.trackReliable(s2, d2, 1);
    CHECK(rel.pendingReliableCount() == 3);

    rel.processAck(s2, 0x0003); // ack s2, bits cover s1 and s0
    CHECK(rel.pendingReliableCount() == 0);
}

TEST_CASE("ReliabilityLayer: sequence wrap-safe comparison") {
    CHECK(sequenceGreaterThan(1, 0));
    CHECK(sequenceGreaterThan(0, 65535));
    CHECK(!sequenceGreaterThan(65535, 0));
}

TEST_CASE("ReliabilityLayer: build ack fields") {
    ReliabilityLayer rel;
    rel.onReceive(10);
    rel.onReceive(9);
    rel.onReceive(7); // missing 8

    uint16_t ack; uint32_t ackBits;
    rel.buildAckFields(ack, ackBits);
    CHECK(ack == 10);
    CHECK((ackBits & 0x01) == 1); // bit 0 = seq 9
    CHECK((ackBits & 0x02) == 0); // bit 1 = seq 8 (missing)
    CHECK((ackBits & 0x04) == 4); // bit 2 = seq 7
}

TEST_CASE("ReliabilityLayer: retransmit list") {
    ReliabilityLayer rel;
    uint8_t data[] = {0x01, 0x02, 0x03};
    uint16_t s0 = rel.nextLocalSequence();
    rel.trackReliable(s0, data, 3);

    auto needs = rel.getRetransmits(0.25f);
    CHECK(needs.size() == 1);
    CHECK(needs[0].sequence == s0);
    CHECK(needs[0].data.size() == 3);
}
