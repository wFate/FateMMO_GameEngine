#include <doctest/doctest.h>
#include "game/shared/collection_system.h"

using namespace fate;

TEST_CASE("CollectionOwnItem matches item id") {
    CachedCollection def;
    def.collectionId = 999;
    def.conditionType = "OwnItem";
    def.conditionTarget = "item_cades_compass";
    def.conditionValue = 1;

    PlayerCollectionState state;
    state.ownedItemIds.insert("item_cades_compass");

    CHECK(evaluateCollectionCondition(def, state));
}

TEST_CASE("CollectionOwnItem rejects when missing") {
    CachedCollection def;
    def.collectionId = 999;
    def.conditionType = "OwnItem";
    def.conditionTarget = "item_cades_compass";
    def.conditionValue = 1;

    PlayerCollectionState state;

    CHECK_FALSE(evaluateCollectionCondition(def, state));
}

TEST_CASE("CollectionOwnItem rejects different item") {
    CachedCollection def;
    def.collectionId = 999;
    def.conditionType = "OwnItem";
    def.conditionTarget = "item_cades_compass";
    def.conditionValue = 1;

    PlayerCollectionState state;
    state.ownedItemIds.insert("item_thread_of_fate");

    CHECK_FALSE(evaluateCollectionCondition(def, state));
}
