#include <doctest/doctest.h>
#include "engine/ui/ui_node.h"
#include "engine/ui/ui_anchor.h"

using namespace fate;

TEST_CASE("UINode: computeLayout respects minSize") {
    UINode node("test", "panel");
    UIAnchor a;
    a.preset = AnchorPreset::TopLeft;
    a.size = {50.0f, 30.0f};
    a.minSize = {100.0f, 80.0f};
    node.setAnchor(a);

    node.computeLayout({0, 0, 800, 600}, 1.0f);
    CHECK(node.computedRect().w == doctest::Approx(100.0f));
    CHECK(node.computedRect().h == doctest::Approx(80.0f));
}

TEST_CASE("UINode: computeLayout respects maxSize") {
    UINode node("test", "panel");
    UIAnchor a;
    a.preset = AnchorPreset::TopLeft;
    a.size = {500.0f, 400.0f};
    a.maxSize = {300.0f, 200.0f};
    node.setAnchor(a);

    node.computeLayout({0, 0, 800, 600}, 1.0f);
    CHECK(node.computedRect().w == doctest::Approx(300.0f));
    CHECK(node.computedRect().h == doctest::Approx(200.0f));
}

TEST_CASE("UINode: computeLayout no clamping when minSize/maxSize are zero") {
    UINode node("test", "panel");
    UIAnchor a;
    a.preset = AnchorPreset::TopLeft;
    a.size = {200.0f, 150.0f};
    node.setAnchor(a);

    node.computeLayout({0, 0, 800, 600}, 1.0f);
    CHECK(node.computedRect().w == doctest::Approx(200.0f));
    CHECK(node.computedRect().h == doctest::Approx(150.0f));
}

TEST_CASE("UINode: computeLayout minSize scales with layout scale") {
    UINode node("test", "panel");
    UIAnchor a;
    a.preset = AnchorPreset::TopLeft;
    a.size = {50.0f, 30.0f};
    a.minSize = {100.0f, 80.0f};
    node.setAnchor(a);

    // scale=2: size 100x60 (50*2, 30*2), min 200x160 (100*2, 80*2)
    node.computeLayout({0, 0, 1600, 1200}, 2.0f);
    CHECK(node.computedRect().w == doctest::Approx(200.0f));
    CHECK(node.computedRect().h == doctest::Approx(160.0f));
}
