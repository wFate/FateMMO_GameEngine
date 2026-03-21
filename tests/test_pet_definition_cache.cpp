#include <doctest/doctest.h>
#include "server/cache/pet_definition_cache.h"

using namespace fate;

TEST_SUITE("PetDefinitionCache") {

TEST_CASE("Unknown pet returns nullptr") {
    PetDefinitionCache cache;
    CHECK(cache.getDefinition("nonexistent") == nullptr);
}

TEST_CASE("Can add and retrieve definition") {
    PetDefinitionCache cache;
    PetDefinition wolf;
    wolf.petId = "pet_wolf";
    wolf.displayName = "Wolf";
    wolf.baseHP = 10;
    wolf.baseCritRate = 0.01f;
    cache.addDefinition(wolf);

    auto* found = cache.getDefinition("pet_wolf");
    REQUIRE(found != nullptr);
    CHECK(found->displayName == "Wolf");
    CHECK(found->baseHP == 10);
}

TEST_CASE("Size tracks definitions") {
    PetDefinitionCache cache;
    CHECK(cache.size() == 0);
    PetDefinition p;
    p.petId = "pet_test";
    cache.addDefinition(p);
    CHECK(cache.size() == 1);
}

} // TEST_SUITE
