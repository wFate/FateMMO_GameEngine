#include <doctest/doctest.h>
#include "engine/render/render_graph.h"

TEST_CASE("RenderGraph pass add and execute order") {
    fate::RenderGraph graph;
    std::vector<int> order;

    graph.addPass({"First", true, [&](fate::RenderPassContext&) { order.push_back(1); }});
    graph.addPass({"Second", true, [&](fate::RenderPassContext&) { order.push_back(2); }});
    graph.addPass({"Third", true, [&](fate::RenderPassContext&) { order.push_back(3); }});

    fate::RenderPassContext ctx{};
    graph.execute(ctx);

    REQUIRE(order.size() == 3);
    CHECK(order[0] == 1);
    CHECK(order[1] == 2);
    CHECK(order[2] == 3);
}

TEST_CASE("RenderGraph disabled pass is skipped") {
    fate::RenderGraph graph;
    std::vector<int> order;

    graph.addPass({"A", true, [&](fate::RenderPassContext&) { order.push_back(1); }});
    graph.addPass({"B", false, [&](fate::RenderPassContext&) { order.push_back(2); }});
    graph.addPass({"C", true, [&](fate::RenderPassContext&) { order.push_back(3); }});

    fate::RenderPassContext ctx{};
    graph.execute(ctx);

    REQUIRE(order.size() == 2);
    CHECK(order[0] == 1);
    CHECK(order[1] == 3);
}

TEST_CASE("RenderGraph setPassEnabled toggles pass") {
    fate::RenderGraph graph;
    int count = 0;

    graph.addPass({"Toggle", true, [&](fate::RenderPassContext&) { count++; }});

    fate::RenderPassContext ctx{};
    graph.execute(ctx);
    CHECK(count == 1);

    graph.setPassEnabled("Toggle", false);
    graph.execute(ctx);
    CHECK(count == 1); // not incremented

    graph.setPassEnabled("Toggle", true);
    graph.execute(ctx);
    CHECK(count == 2);
}

TEST_CASE("RenderGraph removePass") {
    fate::RenderGraph graph;
    int count = 0;

    graph.addPass({"Remove", true, [&](fate::RenderPassContext&) { count++; }});
    graph.removePass("Remove");

    fate::RenderPassContext ctx{};
    graph.execute(ctx);
    CHECK(count == 0);
}
