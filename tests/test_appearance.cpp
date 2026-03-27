#include <doctest/doctest.h>
#include "game/components/game_components.h"
#include "engine/ecs/world.h"

using namespace fate;

TEST_CASE("AppearanceComponent: default values") {
    AppearanceComponent a;
    CHECK(a.gender == 0);
    CHECK(a.hairstyle == 0);
    CHECK(a.bodyTexture == nullptr);
    CHECK(a.hairTexture == nullptr);
    CHECK(a.armorTexture == nullptr);
    CHECK(a.hatTexture == nullptr);
    CHECK(a.weaponTexture == nullptr);
    CHECK(a.dirty == true);
}

TEST_CASE("AppearanceComponent: add to entity") {
    World world;
    auto* e = world.createEntity("test");
    auto* a = e->addComponent<AppearanceComponent>();
    a->gender = 1;
    a->hairstyle = 2;
    CHECK(e->getComponent<AppearanceComponent>()->gender == 1);
    CHECK(e->getComponent<AppearanceComponent>()->hairstyle == 2);
}
