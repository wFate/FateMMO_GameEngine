#include <doctest/doctest.h>
#include "engine/render/palette.h"

TEST_SUITE("Palette System") {

TEST_CASE("load palette from JSON") {
    const char* json = R"({
        "test_palette": {
            "colors": ["#ff0000", "#00ff00", "#0000ff",
                       "#ffffff", "#000000", "#808080",
                       "#ffff00", "#ff00ff", "#00ffff",
                       "#c0c0c0", "#800000", "#008000",
                       "#000080", "#808000", "#800080", "#008080"]
        }
    })";
    fate::PaletteRegistry registry;
    CHECK(registry.loadFromJson(json));
    CHECK(registry.has("test_palette"));
    auto p = registry.get("test_palette");
    REQUIRE(p != nullptr);
    CHECK(p->size() == 16);
}

TEST_CASE("palette color parsing") {
    const char* json = R"({ "rgb": { "colors": ["#ff0000", "#00ff00", "#0000ff"] } })";
    fate::PaletteRegistry registry;
    REQUIRE(registry.loadFromJson(json));
    auto p = registry.get("rgb");
    REQUIRE(p != nullptr);
    CHECK(p->size() == 3);
    CHECK((*p)[0].r == doctest::Approx(1.0f));
    CHECK((*p)[0].g == doctest::Approx(0.0f));
    CHECK((*p)[1].g == doctest::Approx(1.0f));
    CHECK((*p)[2].b == doctest::Approx(1.0f));
}

TEST_CASE("missing palette returns nullptr") {
    fate::PaletteRegistry registry;
    CHECK(registry.get("nonexistent") == nullptr);
    CHECK_FALSE(registry.has("nonexistent"));
}

TEST_CASE("multiple palettes loaded") {
    const char* json = R"({
        "a": { "colors": ["#ff0000"] },
        "b": { "colors": ["#00ff00"] },
        "c": { "colors": ["#0000ff"] }
    })";
    fate::PaletteRegistry registry;
    REQUIRE(registry.loadFromJson(json));
    CHECK(registry.has("a"));
    CHECK(registry.has("b"));
    CHECK(registry.has("c"));
    auto names = registry.names();
    CHECK(names.size() == 3);
}

TEST_CASE("invalid JSON returns false") {
    fate::PaletteRegistry registry;
    CHECK_FALSE(registry.loadFromJson("not valid json{{{"));
}

} // TEST_SUITE
