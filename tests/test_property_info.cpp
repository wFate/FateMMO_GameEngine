#include <doctest/doctest.h>
#include "engine/ecs/reflect.h"
#include "engine/ecs/component_meta.h"
#include "engine/core/types.h"
#include <nlohmann/json.hpp>
#include <string>

using namespace fate;

namespace {

struct TestWidget {
    float fontSize = 14.0f;
    int   maxLines = 5;
    bool  wordWrap = false;
    Color textColor = {1.0f, 0.9f, 0.7f, 1.0f};
    Vec2  padding = {4.0f, 8.0f};
    std::string label = "default";
};

} // namespace

FATE_REFLECT(TestWidget,
    FATE_PROPERTY(fontSize,  Float,  .displayName="Font Size", .category="Appearance",
                  .control=fate::EditorControl::Slider, .min=6.0f, .max=72.0f, .step=0.5f),
    FATE_PROPERTY(maxLines,  Int,    .displayName="Max Lines", .category="Layout"),
    FATE_PROPERTY(wordWrap,  Bool,   .displayName="Word Wrap", .category="Layout"),
    FATE_PROPERTY(textColor, Color,  .displayName="Text Color",.category="Colors"),
    FATE_PROPERTY(padding,   Vec2,   .displayName="Padding",   .category="Layout", .order=1),
    FATE_PROPERTY(label,     String, .displayName="Label",     .category="Content")
)

TEST_CASE("PropertyInfo: Reflection fields() returns correct count") {
    auto fields = Reflection<TestWidget>::fields();
    CHECK(fields.size() == 6);
}

TEST_CASE("PropertyInfo: metadata is preserved") {
    auto fields = Reflection<TestWidget>::fields();
    // fontSize field
    CHECK(std::string(fields[0].name) == "fontSize");
    CHECK(std::string(fields[0].displayName) == "Font Size");
    CHECK(std::string(fields[0].category) == "Appearance");
    CHECK(fields[0].control == EditorControl::Slider);
    CHECK(fields[0].min == doctest::Approx(6.0f));
    CHECK(fields[0].max == doctest::Approx(72.0f));
    CHECK(fields[0].step == doctest::Approx(0.5f));
    CHECK(fields[0].type == FieldType::Float);
}

TEST_CASE("PropertyInfo: autoToJson round-trip") {
    TestWidget w;
    w.fontSize = 24.0f;
    w.maxLines = 10;
    w.wordWrap = true;
    w.textColor = {0.5f, 0.6f, 0.7f, 1.0f};
    w.padding = {12.0f, 16.0f};
    w.label = "hello";

    auto fields = Reflection<TestWidget>::fields();

    nlohmann::json j;
    autoToJson(&w, j, fields);

    CHECK(j["fontSize"].get<float>() == doctest::Approx(24.0f));
    CHECK(j["maxLines"].get<int>() == 10);
    CHECK(j["wordWrap"].get<bool>() == true);
    CHECK(j["textColor"][0].get<float>() == doctest::Approx(0.5f));
    CHECK(j["textColor"][3].get<float>() == doctest::Approx(1.0f));
    CHECK(j["padding"][0].get<float>() == doctest::Approx(12.0f));
    CHECK(j["padding"][1].get<float>() == doctest::Approx(16.0f));
    CHECK(j["label"].get<std::string>() == "hello");

    // Round-trip: deserialize into fresh instance
    TestWidget w2;
    autoFromJson(j, &w2, fields);

    CHECK(w2.fontSize == doctest::Approx(24.0f));
    CHECK(w2.maxLines == 10);
    CHECK(w2.wordWrap == true);
    CHECK(w2.textColor.r == doctest::Approx(0.5f));
    CHECK(w2.padding.x == doctest::Approx(12.0f));
    CHECK(w2.label == "hello");
}

TEST_CASE("PropertyInfo: autoFromJson skips missing fields gracefully") {
    TestWidget w;
    w.fontSize = 99.0f;

    nlohmann::json j;
    j["maxLines"] = 42;
    // fontSize not in JSON — should keep default

    auto fields = Reflection<TestWidget>::fields();
    autoFromJson(j, &w, fields);

    CHECK(w.fontSize == doctest::Approx(99.0f));  // unchanged
    CHECK(w.maxLines == 42);                        // updated
}

TEST_CASE("PropertyInfo: FATE_FIELD still works (no metadata)") {
    PropertyInfo pi{"speed", 0, sizeof(float), FieldType::Float};
    CHECK(pi.displayName == nullptr);
    CHECK(pi.category == nullptr);
    CHECK(pi.control == EditorControl::Auto);
    CHECK(pi.min == 0.0f);
    CHECK(pi.max == 0.0f);
}
