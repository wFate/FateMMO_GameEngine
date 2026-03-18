#include <doctest/doctest.h>
#include "engine/job/fiber.h"

using namespace fate;

static bool fiberRan = false;

static void __stdcall testFiberProc(void* param) {
    fiberRan = true;
    fiber::switchTo(static_cast<FiberHandle>(param));
}

TEST_CASE("Fiber: create and switch") {
    FiberHandle mainFiber = fiber::convertThreadToFiber();
    REQUIRE(mainFiber != nullptr);

    FiberHandle child = fiber::create(65536, testFiberProc, mainFiber);
    REQUIRE(child != nullptr);

    fiberRan = false;
    fiber::switchTo(child);
    CHECK(fiberRan == true);

    fiber::destroy(child);
    fiber::convertFiberToThread();
}
