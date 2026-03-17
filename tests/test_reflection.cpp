#include <doctest/doctest.h>
#include <nlohmann/json.hpp>
#include "engine/ecs/reflect.h"
#include "engine/ecs/component_registry.h"
#include "engine/ecs/component_meta.h"
#include "engine/core/types.h"

namespace {

struct TestPos {
    FATE_COMPONENT(TestPos)
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct TestMarker {
    FATE_COMPONENT(TestMarker)
};

struct TestVec2Comp {
    FATE_COMPONENT(TestVec2Comp)
    fate::Vec2 position;
    float speed = 1.0f;
};

struct Unreflected {
    FATE_COMPONENT(Unreflected)
    int val;
};

struct MetaTestComp {
    FATE_COMPONENT(MetaTestComp)
    float health = 100.0f;
    int level = 1;
    fate::Vec2 position;
};

struct CustomSerComp {
    FATE_COMPONENT(CustomSerComp)
    float value = 0.0f;
};

} // namespace

FATE_REFLECT(TestPos,
    FATE_FIELD(x, Float),
    FATE_FIELD(y, Float),
    FATE_FIELD(z, Float)
)

FATE_REFLECT_EMPTY(TestMarker)

FATE_REFLECT(TestVec2Comp,
    FATE_FIELD(position, Vec2),
    FATE_FIELD(speed, Float)
)

FATE_REFLECT(MetaTestComp,
    FATE_FIELD(health, Float),
    FATE_FIELD(level, Int),
    FATE_FIELD(position, Vec2)
)

FATE_REFLECT(CustomSerComp,
    FATE_FIELD(value, Float)
)

TEST_CASE("Reflection field count") {
    auto fields = fate::Reflection<TestPos>::fields();
    CHECK(fields.size() == 3);
}

TEST_CASE("Reflection field names and types") {
    auto fields = fate::Reflection<TestPos>::fields();
    CHECK(std::string(fields[0].name) == "x");
    CHECK(fields[0].type == fate::FieldType::Float);
    CHECK(std::string(fields[1].name) == "y");
    CHECK(std::string(fields[2].name) == "z");
}

TEST_CASE("Reflection field offset access") {
    TestPos pos;
    pos.x = 42.0f;
    pos.y = 99.0f;
    auto fields = fate::Reflection<TestPos>::fields();
    float* xPtr = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(&pos) + fields[0].offset);
    CHECK(*xPtr == 42.0f);
    float* yPtr = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(&pos) + fields[1].offset);
    CHECK(*yPtr == 99.0f);
    *xPtr = 7.0f;
    CHECK(pos.x == 7.0f);
}

TEST_CASE("Unreflected component has empty fields") {
    auto fields = fate::Reflection<Unreflected>::fields();
    CHECK(fields.empty());
}

TEST_CASE("FATE_REFLECT_EMPTY produces empty fields") {
    auto fields = fate::Reflection<TestMarker>::fields();
    CHECK(fields.empty());
}

TEST_CASE("Vec2 field reflection") {
    auto fields = fate::Reflection<TestVec2Comp>::fields();
    CHECK(fields.size() == 2);
    CHECK(std::string(fields[0].name) == "position");
    CHECK(fields[0].type == fate::FieldType::Vec2);
    CHECK(std::string(fields[1].name) == "speed");
    CHECK(fields[1].type == fate::FieldType::Float);
}

// ===========================================================================
// ComponentMeta tests
// ===========================================================================

TEST_CASE("ComponentMeta registration and lookup") {
    auto& reg = fate::ComponentMetaRegistry::instance();
    reg.registerComponent<MetaTestComp>();

    const auto* byName = reg.findByName("MetaTestComp");
    REQUIRE(byName != nullptr);
    CHECK(std::string(byName->name) == "MetaTestComp");
    CHECK(byName->id == fate::componentId<MetaTestComp>());
    CHECK(byName->size == sizeof(MetaTestComp));
    CHECK(byName->fields.size() == 3);

    const auto* byId = reg.findById(fate::componentId<MetaTestComp>());
    REQUIRE(byId != nullptr);
    CHECK(byId == byName);

    // Non-existent lookup returns nullptr
    CHECK(reg.findByName("DoesNotExist") == nullptr);
    CHECK(reg.findById(999999) == nullptr);
}

TEST_CASE("Auto-generated toJson from reflection") {
    auto& reg = fate::ComponentMetaRegistry::instance();
    // May already be registered from previous test; findByName is idempotent
    if (!reg.findByName("MetaTestComp"))
        reg.registerComponent<MetaTestComp>();

    MetaTestComp comp;
    comp.health = 75.5f;
    comp.level = 5;
    comp.position = {10.0f, 20.0f};

    const auto* meta = reg.findByName("MetaTestComp");
    REQUIRE(meta != nullptr);
    REQUIRE(meta->toJson);

    nlohmann::json j;
    meta->toJson(&comp, j);

    CHECK(j["health"].get<float>() == doctest::Approx(75.5f));
    CHECK(j["level"].get<int>() == 5);
    REQUIRE(j["position"].is_array());
    CHECK(j["position"][0].get<float>() == doctest::Approx(10.0f));
    CHECK(j["position"][1].get<float>() == doctest::Approx(20.0f));
}

TEST_CASE("Auto-generated fromJson from reflection") {
    auto& reg = fate::ComponentMetaRegistry::instance();
    if (!reg.findByName("MetaTestComp"))
        reg.registerComponent<MetaTestComp>();

    nlohmann::json j;
    j["health"] = 50.0f;
    j["level"] = 10;
    j["position"] = {3.0f, 4.0f};

    const auto* meta = reg.findByName("MetaTestComp");
    REQUIRE(meta != nullptr);
    REQUIRE(meta->fromJson);

    MetaTestComp comp;
    meta->fromJson(j, &comp);

    CHECK(comp.health == doctest::Approx(50.0f));
    CHECK(comp.level == 10);
    CHECK(comp.position.x == doctest::Approx(3.0f));
    CHECK(comp.position.y == doctest::Approx(4.0f));
}

TEST_CASE("Missing JSON field gets default value") {
    auto& reg = fate::ComponentMetaRegistry::instance();
    if (!reg.findByName("MetaTestComp"))
        reg.registerComponent<MetaTestComp>();

    // Only provide health, omit level and position
    nlohmann::json j;
    j["health"] = 25.0f;

    MetaTestComp comp; // defaults: health=100, level=1, position=(0,0)
    const auto* meta = reg.findByName("MetaTestComp");
    REQUIRE(meta != nullptr);
    meta->fromJson(j, &comp);

    CHECK(comp.health == doctest::Approx(25.0f)); // overwritten
    CHECK(comp.level == 1);                        // kept default
    CHECK(comp.position.x == doctest::Approx(0.0f)); // kept default
    CHECK(comp.position.y == doctest::Approx(0.0f));
}

TEST_CASE("Alias lookup") {
    auto& reg = fate::ComponentMetaRegistry::instance();
    if (!reg.findByName("MetaTestComp"))
        reg.registerComponent<MetaTestComp>();

    reg.registerAlias("TestMeta", "MetaTestComp");

    const auto* meta = reg.findByName("TestMeta");
    REQUIRE(meta != nullptr);
    CHECK(std::string(meta->name) == "MetaTestComp");
    CHECK(meta->id == fate::componentId<MetaTestComp>());
}

TEST_CASE("Custom serializer overrides auto") {
    auto& reg = fate::ComponentMetaRegistry::instance();

    bool customCalled = false;
    reg.registerComponent<CustomSerComp>(
        [&customCalled](const void* data, nlohmann::json& j) {
            customCalled = true;
            const auto* comp = static_cast<const CustomSerComp*>(data);
            j["custom_value"] = comp->value * 2.0f;
        },
        nullptr // use auto fromJson
    );

    CustomSerComp comp;
    comp.value = 5.0f;

    const auto* meta = reg.findByName("CustomSerComp");
    REQUIRE(meta != nullptr);
    REQUIRE(meta->toJson);

    nlohmann::json j;
    meta->toJson(&comp, j);

    CHECK(customCalled);
    CHECK(j["custom_value"].get<float>() == doctest::Approx(10.0f));
    // auto-generated key "value" should NOT be present
    CHECK_FALSE(j.contains("value"));
}
