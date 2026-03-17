#include <doctest/doctest.h>

#if defined(ENGINE_MEMORY_DEBUG)
#include "engine/memory/allocator_registry.h"

TEST_CASE("AllocatorRegistry add and query") {
    auto& reg = fate::AllocatorRegistry::instance();
    // Clear any prior state
    while (!reg.all().empty()) {
        reg.remove(reg.all().front().name);
    }

    size_t testVal = 42;
    reg.add({
        .name = "TestArena",
        .type = fate::AllocatorType::Arena,
        .getUsed = [&]() -> size_t { return testVal; },
        .getCommitted = [&]() -> size_t { return 100; },
        .getReserved = [&]() -> size_t { return 1000; },
    });

    CHECK(reg.all().size() == 1);
    CHECK(std::string(reg.all()[0].name) == "TestArena");
    CHECK(reg.all()[0].getUsed() == 42);

    testVal = 99;
    CHECK(reg.all()[0].getUsed() == 99);
}

TEST_CASE("AllocatorRegistry remove by name") {
    auto& reg = fate::AllocatorRegistry::instance();
    while (!reg.all().empty()) {
        reg.remove(reg.all().front().name);
    }

    reg.add({ .name = "A", .type = fate::AllocatorType::Arena });
    reg.add({ .name = "B", .type = fate::AllocatorType::Pool });
    CHECK(reg.all().size() == 2);

    reg.remove("A");
    CHECK(reg.all().size() == 1);
    CHECK(std::string(reg.all()[0].name) == "B");

    reg.remove("nonexistent"); // no-op
    CHECK(reg.all().size() == 1);
}

#endif // ENGINE_MEMORY_DEBUG
