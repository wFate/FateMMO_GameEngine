#include <doctest/doctest.h>
#include "game/components/dropped_item_component.h"

using namespace fate;

TEST_CASE("DroppedItem: tryClaim succeeds once") {
    DroppedItemComponent drop;
    drop.itemId = "sword_01";
    CHECK(drop.tryClaim(42) == true);
    CHECK(drop.claimedBy == 42);
}

TEST_CASE("DroppedItem: tryClaim fails if already claimed") {
    DroppedItemComponent drop;
    drop.itemId = "sword_01";
    drop.tryClaim(42);
    CHECK(drop.tryClaim(99) == false);
    CHECK(drop.claimedBy == 42);
}

TEST_CASE("DroppedItem: releaseClaim allows reclaim") {
    DroppedItemComponent drop;
    drop.itemId = "sword_01";
    drop.tryClaim(42);
    drop.releaseClaim();
    CHECK(drop.tryClaim(99) == true);
    CHECK(drop.claimedBy == 99);
}
