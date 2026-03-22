#include <doctest/doctest.h>
#include "engine/ecs/world.h"
#include "game/components/game_components.h"
#include "game/components/transform.h"
#include "game/components/sprite_component.h"
#include "game/components/player_controller.h"
#include "game/components/faction_component.h"
#include "game/components/pet_component.h"
#include "game/components/box_collider.h"

using namespace fate;

TEST_CASE("DungeonTransfer: entity can be created in separate worlds") {
    World w1, w2;
    auto h1 = w1.createEntityH("test1");
    auto h2 = w2.createEntityH("test2");
    CHECK(w1.getEntity(h1) != nullptr);
    CHECK(w2.getEntity(h2) != nullptr);
    // Entities in different worlds are independent
    w1.destroyEntity(h1);
    w1.processDestroyQueue();
    CHECK(w1.getEntity(h1) == nullptr);
    CHECK(w2.getEntity(h2) != nullptr); // still alive in w2
}

TEST_CASE("DungeonTransfer: component data survives copy between worlds") {
    World src, dst;
    auto h = src.createEntityH("player");
    auto* e = src.getEntity(h);
    auto* t = src.addComponentToEntity<Transform>(e);
    t->position = {100.0f, 200.0f};

    // Simulate transfer: snapshot, create in dst, copy
    Vec2 savedPos = t->position;
    auto h2 = dst.createEntityH("player");
    auto* e2 = dst.getEntity(h2);
    auto* t2 = dst.addComponentToEntity<Transform>(e2);
    t2->position = savedPos;

    CHECK(t2->position.x == doctest::Approx(100.0f));
    CHECK(t2->position.y == doctest::Approx(200.0f));
}

TEST_CASE("DungeonTransfer: destroying source doesn't affect destination") {
    World src, dst;
    auto h1 = src.createEntityH("player");
    auto* e1 = src.getEntity(h1);
    src.addComponentToEntity<Transform>(e1)->position = {1.0f, 2.0f};

    auto h2 = dst.createEntityH("player");
    auto* e2 = dst.getEntity(h2);
    dst.addComponentToEntity<Transform>(e2)->position = {3.0f, 4.0f};

    src.destroyEntity(h1);
    src.processDestroyQueue();
    CHECK(dst.getEntity(h2) != nullptr);
    CHECK(dst.getEntity(h2)->getComponent<Transform>()->position.x == doctest::Approx(3.0f));
}

TEST_CASE("DungeonTransfer: CharacterStats deep copy between worlds") {
    World src, dst;
    auto h1 = src.createEntityH("player");
    auto* e1 = src.getEntity(h1);
    auto* cs1 = src.addComponentToEntity<CharacterStatsComponent>(e1);
    cs1->stats.characterName = "TestHero";
    cs1->stats.level = 25;
    cs1->stats.currentHP = 500;
    cs1->stats.maxHP = 600;
    cs1->stats.currentScene = "overworld";
    cs1->stats.honor = 500;

    // Snapshot and copy to dst
    CharacterStats savedStats = cs1->stats;
    savedStats.currentScene = "dungeon_01";

    auto h2 = dst.createEntityH("player");
    auto* e2 = dst.getEntity(h2);
    auto* cs2 = dst.addComponentToEntity<CharacterStatsComponent>(e2);
    cs2->stats = savedStats;

    // Verify deep copy
    CHECK(cs2->stats.characterName == "TestHero");
    CHECK(cs2->stats.level == 25);
    CHECK(cs2->stats.currentHP == 500);
    CHECK(cs2->stats.maxHP == 600);
    CHECK(cs2->stats.currentScene == "dungeon_01");
    CHECK(cs2->stats.honor == 500);

    // Mutating dst doesn't affect src
    cs2->stats.honor = 0;
    CHECK(cs1->stats.honor == 500);
}

TEST_CASE("DungeonTransfer: inventory deep copy between worlds") {
    World src, dst;
    auto h1 = src.createEntityH("player");
    auto* e1 = src.getEntity(h1);
    auto* inv1 = src.addComponentToEntity<InventoryComponent>(e1);
    inv1->inventory.initialize("TestHero", 0);

    // Add a test item
    ItemInstance item;
    item.itemId = "test_sword";
    item.displayName = "Test Sword";
    item.quantity = 1;
    inv1->inventory.addItem(item);

    // Copy to dst
    Inventory savedInv = inv1->inventory;

    auto h2 = dst.createEntityH("player");
    auto* e2 = dst.getEntity(h2);
    auto* inv2 = dst.addComponentToEntity<InventoryComponent>(e2);
    inv2->inventory = savedInv;

    // Verify the item transferred
    CHECK(inv2->inventory.usedSlots() > 0);

    // Source still has its item
    CHECK(inv1->inventory.usedSlots() > 0);
}

TEST_CASE("DungeonTransfer: full player component set transfers between worlds") {
    World src, dst;

    // Build source player with multiple components
    auto h1 = src.createEntityH("player");
    auto* e1 = src.getEntity(h1);
    e1->setTag("player");

    src.addComponentToEntity<Transform>(e1)->position = {100.0f, 200.0f};
    src.addComponentToEntity<CharacterStatsComponent>(e1);
    src.addComponentToEntity<InventoryComponent>(e1);
    src.addComponentToEntity<SkillManagerComponent>(e1);
    src.addComponentToEntity<StatusEffectComponent>(e1);
    src.addComponentToEntity<CrowdControlComponent>(e1);
    src.addComponentToEntity<TargetingComponent>(e1);
    src.addComponentToEntity<FactionComponent>(e1);
    src.addComponentToEntity<PetComponent>(e1);
    src.addComponentToEntity<NameplateComponent>(e1);

    // Set values AFTER all components added (archetype migration invalidates pointers)
    auto* cs = e1->getComponent<CharacterStatsComponent>();
    cs->stats.characterName = "Hero";
    cs->stats.level = 10;
    cs->stats.currentScene = "overworld";
    auto* fac = e1->getComponent<FactionComponent>();
    fac->faction = Faction::Xyros;
    auto* np = e1->getComponent<NameplateComponent>();
    np->displayName = "Hero";
    np->displayLevel = 10;

    // Simulate transfer: snapshot all, create in dst
    CharacterStats savedStats = cs->stats;
    savedStats.currentScene = "dungeon_fire";
    Faction savedFaction = fac->faction;
    NameplateComponent savedNP = *np;
    Vec2 spawnPos = {50.0f, 60.0f};

    auto h2 = dst.createEntityH("player");
    auto* e2 = dst.getEntity(h2);
    e2->setTag("player");

    auto* t2 = dst.addComponentToEntity<Transform>(e2);
    t2->position = spawnPos;

    auto* cs2 = dst.addComponentToEntity<CharacterStatsComponent>(e2);
    cs2->stats = savedStats;

    dst.addComponentToEntity<InventoryComponent>(e2);
    dst.addComponentToEntity<SkillManagerComponent>(e2);
    dst.addComponentToEntity<StatusEffectComponent>(e2);
    dst.addComponentToEntity<CrowdControlComponent>(e2);
    dst.addComponentToEntity<TargetingComponent>(e2);
    auto* fac2 = dst.addComponentToEntity<FactionComponent>(e2);
    fac2->faction = savedFaction;
    dst.addComponentToEntity<PetComponent>(e2);
    auto* np2 = dst.addComponentToEntity<NameplateComponent>(e2);
    *np2 = savedNP;

    // Destroy source
    src.destroyEntity(h1);
    src.processDestroyQueue();

    // Verify destination entity survived with correct data
    auto* finalEntity = dst.getEntity(h2);
    REQUIRE(finalEntity != nullptr);

    auto* finalT = finalEntity->getComponent<Transform>();
    REQUIRE(finalT != nullptr);
    CHECK(finalT->position.x == doctest::Approx(50.0f));
    CHECK(finalT->position.y == doctest::Approx(60.0f));

    auto* finalCS = finalEntity->getComponent<CharacterStatsComponent>();
    REQUIRE(finalCS != nullptr);
    CHECK(finalCS->stats.characterName == "Hero");
    CHECK(finalCS->stats.level == 10);
    CHECK(finalCS->stats.currentScene == "dungeon_fire");

    auto* finalFac = finalEntity->getComponent<FactionComponent>();
    REQUIRE(finalFac != nullptr);
    CHECK(finalFac->faction == Faction::Xyros);

    auto* finalNP = finalEntity->getComponent<NameplateComponent>();
    REQUIRE(finalNP != nullptr);
    CHECK(finalNP->displayName == "Hero");
    CHECK(finalNP->displayLevel == 10);
}

TEST_CASE("DungeonTransfer: status effects and CC are cleared on transfer") {
    World src, dst;
    auto h1 = src.createEntityH("player");
    auto* e1 = src.getEntity(h1);

    // Add status effects to source
    auto* se = src.addComponentToEntity<StatusEffectComponent>(e1);
    se->effects.applyEffect(EffectType::AttackUp, 30.0f, 1.5f);
    CHECK(se->effects.hasEffect(EffectType::AttackUp));

    // On transfer, create FRESH components (not copies)
    auto h2 = dst.createEntityH("player");
    auto* e2 = dst.getEntity(h2);
    auto* se2 = dst.addComponentToEntity<StatusEffectComponent>(e2);
    // Fresh component — no effects
    CHECK_FALSE(se2->effects.hasEffect(EffectType::AttackUp));

    auto* cc2 = dst.addComponentToEntity<CrowdControlComponent>(e2);
    CHECK_FALSE(cc2->cc.isStunned());
}

TEST_CASE("DungeonTransfer: targeting is cleared on transfer") {
    World src, dst;
    auto h1 = src.createEntityH("player");
    auto* e1 = src.getEntity(h1);
    auto* tgt = src.addComponentToEntity<TargetingComponent>(e1);
    tgt->selectedTargetId = 42;
    tgt->targetType = TargetType::Mob;
    CHECK(tgt->hasTarget());

    // Fresh targeting in destination
    auto h2 = dst.createEntityH("player");
    auto* e2 = dst.getEntity(h2);
    auto* tgt2 = dst.addComponentToEntity<TargetingComponent>(e2);
    CHECK_FALSE(tgt2->hasTarget());
    CHECK(tgt2->selectedTargetId == 0);
}
