#include <doctest/doctest.h>
#include <nlohmann/json.hpp>
#include "engine/ecs/world.h"
#include "engine/ecs/prefab.h"
#include "engine/ecs/component_meta.h"

// We test the snapshot/restore logic directly without the Editor singleton
// since Editor requires SDL/ImGui initialization

using namespace fate;

TEST_CASE("Entity snapshot round-trip preserves entity data") {
    World world;
    auto* e = world.createEntity("TestEntity");
    e->setTag("ground");

    // Snapshot
    auto json = PrefabLibrary::entityToJson(e);
    CHECK(json["name"] == "TestEntity");
    CHECK(json["tag"] == "ground");

    // Destroy original
    world.destroyEntity(e->handle());
    world.processDestroyQueue();
    CHECK(world.entityCount() == 0);

    // Restore
    auto* restored = PrefabLibrary::jsonToEntity(json, world);
    REQUIRE(restored != nullptr);
    CHECK(restored->name() == "TestEntity");
    CHECK(restored->tag() == "ground");
    CHECK(world.entityCount() == 1);
}

TEST_CASE("Multiple entity snapshot round-trip") {
    World world;
    world.createEntity("Entity1")->setTag("a");
    world.createEntity("Entity2")->setTag("b");
    world.createEntity("Entity3")->setTag("c");

    // Snapshot all
    nlohmann::json snapshot = nlohmann::json::array();
    world.forEachEntity([&](Entity* e) {
        snapshot.push_back(PrefabLibrary::entityToJson(e));
    });
    CHECK(snapshot.size() == 3);

    // Destroy all
    std::vector<EntityHandle> handles;
    world.forEachEntity([&](Entity* e) { handles.push_back(e->handle()); });
    for (auto h : handles) world.destroyEntity(h);
    world.processDestroyQueue();
    CHECK(world.entityCount() == 0);

    // Restore all
    for (auto& ej : snapshot) {
        PrefabLibrary::jsonToEntity(ej, world);
    }
    CHECK(world.entityCount() == 3);
}
