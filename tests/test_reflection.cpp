#include <doctest/doctest.h>
#include "engine/ecs/reflect.h"
#include "engine/ecs/component_registry.h"
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
