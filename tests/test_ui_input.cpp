#include <doctest/doctest.h>
#include "engine/ui/ui_manager.h"

using namespace fate;

TEST_CASE("UIManager: hitTest finds topmost node") {
    UIManager mgr;
    const char* json = R"({
        "screen": "test",
        "root": {
            "id": "panel", "type": "panel",
            "anchor": { "preset": "TopLeft", "size": [400, 300] },
            "children": [{
                "id": "btn", "type": "panel",
                "anchor": { "preset": "TopLeft", "offset": [10, 10], "size": [100, 40] }
            }]
        }
    })";
    CHECK(mgr.loadScreenFromString("test", json));
    mgr.computeLayout(1600, 900);

    auto* hit = mgr.hitTest({50, 30});
    CHECK(hit != nullptr);
    CHECK(hit->id() == "btn");

    auto* hit2 = mgr.hitTest({300, 200});
    CHECK(hit2 != nullptr);
    CHECK(hit2->id() == "panel");
}

TEST_CASE("UIManager: hover tracking updates node state") {
    UIManager mgr;
    const char* json = R"({
        "screen": "hover_test",
        "root": {
            "id": "btn", "type": "panel",
            "anchor": { "preset": "TopLeft", "size": [100, 50] }
        }
    })";
    mgr.loadScreenFromString("hover_test", json);
    mgr.computeLayout(1600, 900);

    auto* btn = mgr.getScreen("hover_test");
    CHECK_FALSE(btn->hovered());

    mgr.updateHover({50, 25});
    CHECK(btn->hovered());

    mgr.updateHover({200, 200});
    CHECK_FALSE(btn->hovered());
}

TEST_CASE("UIManager: focus tracking") {
    UIManager mgr;
    const char* json = R"({
        "screen": "focus_test",
        "root": {
            "id": "root", "type": "panel",
            "anchor": { "preset": "TopLeft", "size": [800, 600] },
            "children": [
                { "id": "a", "type": "button", "anchor": { "preset": "TopLeft", "offset": [0, 0], "size": [100, 100] } },
                { "id": "b", "type": "button", "anchor": { "preset": "TopLeft", "offset": [200, 0], "size": [100, 100] } }
            ]
        }
    })";
    mgr.loadScreenFromString("focus_test", json);
    mgr.computeLayout(1600, 900);

    mgr.handlePress({50, 50});
    CHECK(mgr.focusedNode() != nullptr);
    CHECK(mgr.focusedNode()->id() == "a");

    mgr.handlePress({250, 50});
    CHECK(mgr.focusedNode() != nullptr);
    CHECK(mgr.focusedNode()->id() == "b");
}
