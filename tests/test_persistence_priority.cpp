#include <doctest/doctest.h>
#include "server/db/persistence_priority.h"

using namespace fate;

TEST_CASE("PersistenceQueue processes IMMEDIATE before NORMAL") {
    PersistenceQueue queue;
    queue.enqueue(5, PersistPriority::NORMAL, PersistType::Position, 0.0f);
    queue.enqueue(3, PersistPriority::IMMEDIATE, PersistType::Inventory, 0.0f);
    auto batch = queue.dequeue(1, 0.0f);
    REQUIRE(batch.size() == 1);
    CHECK(batch[0].clientId == 3);
}

TEST_CASE("PersistenceQueue processes older items first within same priority") {
    PersistenceQueue queue;
    queue.enqueue(1, PersistPriority::NORMAL, PersistType::Character, 10.0f);
    queue.enqueue(2, PersistPriority::NORMAL, PersistType::Character, 5.0f);
    auto batch = queue.dequeue(1, 15.0f);
    REQUIRE(batch.size() == 1);
    CHECK(batch[0].clientId == 2); // queued earlier
}

TEST_CASE("PersistenceQueue dequeue returns multiple items") {
    PersistenceQueue queue;
    queue.enqueue(1, PersistPriority::IMMEDIATE, PersistType::Inventory, 0.0f);
    queue.enqueue(2, PersistPriority::HIGH, PersistType::Character, 0.0f);
    queue.enqueue(3, PersistPriority::NORMAL, PersistType::Position, 0.0f);
    auto batch = queue.dequeue(10, 0.0f);
    CHECK(batch.size() == 3);
    CHECK(batch[0].priority == PersistPriority::IMMEDIATE);
    CHECK(batch[1].priority == PersistPriority::HIGH);
    CHECK(batch[2].priority == PersistPriority::NORMAL);
}

TEST_CASE("PersistenceQueue empty after full dequeue") {
    PersistenceQueue queue;
    queue.enqueue(1, PersistPriority::LOW, PersistType::Character, 0.0f);
    auto batch = queue.dequeue(10, 0.0f);
    CHECK(batch.size() == 1);
    CHECK(queue.empty());
}

TEST_CASE("PersistenceQueue dequeue respects maxCount") {
    PersistenceQueue queue;
    for (uint16_t i = 0; i < 20; ++i)
        queue.enqueue(i, PersistPriority::NORMAL, PersistType::Position, 0.0f);
    auto batch = queue.dequeue(5, 0.0f);
    CHECK(batch.size() == 5);
    CHECK(queue.size() == 15);
}

TEST_CASE("PersistenceQueue sets correct maxDelay per priority") {
    PersistenceQueue queue;
    queue.enqueue(1, PersistPriority::IMMEDIATE, PersistType::Inventory, 0.0f);
    queue.enqueue(2, PersistPriority::HIGH, PersistType::Character, 0.0f);
    queue.enqueue(3, PersistPriority::NORMAL, PersistType::Position, 0.0f);
    queue.enqueue(4, PersistPriority::LOW, PersistType::Character, 0.0f);
    auto batch = queue.dequeue(4, 0.0f);
    CHECK(batch[0].maxDelay == doctest::Approx(0.0f));
    CHECK(batch[1].maxDelay == doctest::Approx(5.0f));
    CHECK(batch[2].maxDelay == doctest::Approx(60.0f));
    CHECK(batch[3].maxDelay == doctest::Approx(300.0f));
}
