#include <doctest/doctest.h>
#include "engine/ecs/prefab_variant.h"

using namespace fate;
using json = nlohmann::json;

TEST_CASE("applyPrefabPatches replaces values") {
    json base = {{"components", {{"Health", {{"maxHp", 100}}}}}};
    json patches = json::array({
        {{"op", "replace"}, {"path", "/components/Health/maxHp"}, {"value", 200}}
    });
    auto result = applyPrefabPatches(base, patches);
    CHECK(result["components"]["Health"]["maxHp"] == 200);
}

TEST_CASE("applyPrefabPatches adds components") {
    json base = {{"components", {{"Health", {{"maxHp", 100}}}}}};
    json patches = json::array({
        {{"op", "add"}, {"path", "/components/MeleeAttack"}, {"value", {{"damage", 25}}}}
    });
    auto result = applyPrefabPatches(base, patches);
    CHECK(result["components"].contains("MeleeAttack"));
    CHECK(result["components"]["MeleeAttack"]["damage"] == 25);
    CHECK(result["components"]["Health"]["maxHp"] == 100); // preserved
}

TEST_CASE("applyPrefabPatches removes components") {
    json base = {{"components", {{"Health", {{"maxHp", 100}}}, {"Debug", {{"visible", true}}}}}};
    json patches = json::array({
        {{"op", "remove"}, {"path", "/components/Debug"}}
    });
    auto result = applyPrefabPatches(base, patches);
    CHECK(!result["components"].contains("Debug"));
    CHECK(result["components"].contains("Health"));
}

TEST_CASE("computePrefabDiff detects changes") {
    json base = {{"components", {{"Health", {{"maxHp", 100}}}}}};
    json modified = {{"components", {{"Health", {{"maxHp", 200}}}}}};
    auto patches = computePrefabDiff(base, modified);
    CHECK(patches.size() >= 1);
    // Apply the diff to base and verify it matches modified
    auto roundtrip = applyPrefabPatches(base, patches);
    CHECK(roundtrip == modified);
}

TEST_CASE("computePrefabDiff with additions") {
    json base = {{"components", {{"Health", {{"maxHp", 100}}}}}};
    json modified = {{"components", {{"Health", {{"maxHp", 100}}}, {"Attack", {{"dmg", 50}}}}}};
    auto patches = computePrefabDiff(base, modified);
    auto roundtrip = applyPrefabPatches(base, patches);
    CHECK(roundtrip == modified);
}

TEST_CASE("computePrefabDiff with removals") {
    json base = {{"components", {{"Health", {{"maxHp", 100}}}, {"Debug", {}}}}};
    json modified = {{"components", {{"Health", {{"maxHp", 100}}}}}};
    auto patches = computePrefabDiff(base, modified);
    auto roundtrip = applyPrefabPatches(base, patches);
    CHECK(roundtrip == modified);
}

TEST_CASE("empty patches produce same result") {
    json base = {{"name", "test"}, {"components", {}}};
    json patches = json::array();
    auto result = applyPrefabPatches(base, patches);
    CHECK(result == base);
}

TEST_CASE("PrefabVariant struct holds data") {
    PrefabVariant variant;
    variant.name = "WarriorPlayer";
    variant.parentName = "BasePlayer";
    variant.patches = json::array({
        {{"op", "replace"}, {"path", "/components/Health/maxHp"}, {"value", 200}}
    });
    CHECK(variant.name == "WarriorPlayer");
    CHECK(variant.parentName == "BasePlayer");
    CHECK(variant.patches.size() == 1);
}
