#include <doctest/doctest.h>
#include "engine/memory/arena.h"
#include "engine/memory/scratch_arena.h"

TEST_CASE("Arena basic allocation") {
    fate::Arena arena(1024 * 1024);
    CHECK(arena.position() == 0);
    int* p = arena.pushType<int>(42);
    REQUIRE(p != nullptr);
    CHECK(*p == 42);
    CHECK(arena.position() > 0);
    arena.reset();
    CHECK(arena.position() == 0);
}

TEST_CASE("Arena pushArray") {
    fate::Arena arena(1024 * 1024);
    float* arr = arena.pushArray<float>(100);
    REQUIRE(arr != nullptr);
    for (int i = 0; i < 100; i++) arr[i] = (float)i;
    CHECK(arr[50] == 50.0f);
}

TEST_CASE("Arena resetTo") {
    fate::Arena arena(1024 * 1024);
    arena.pushType<int>(1);
    size_t saved = arena.position();
    arena.pushType<int>(2);
    CHECK(arena.position() > saved);
    arena.resetTo(saved);
    CHECK(arena.position() == saved);
}

TEST_CASE("FrameArena double buffer") {
    fate::FrameArena frame(1024 * 1024);
    int* a = frame.pushType<int>(1);
    frame.swap();
    int* b = frame.pushType<int>(2);
    CHECK(*a == 1);
    CHECK(*b == 2);
}

TEST_CASE("ScratchArena conflict avoidance") {
    fate::Arena persistent(1024 * 1024);
    fate::Arena* conflicts[] = { &persistent };
    auto scratch = fate::GetScratch(conflicts, 1);
    CHECK(scratch.arena != &persistent);
}

TEST_CASE("ScratchArena no conflicts returns first") {
    auto s1 = fate::GetScratch(nullptr, 0);
    auto s2 = fate::GetScratch(nullptr, 0);
    CHECK(s1.arena == s2.arena);
}

TEST_CASE("ScratchScope RAII reset") {
    auto scratch = fate::GetScratch();
    size_t posBefore = scratch.arena->position();
    {
        fate::ScratchScope scope(scratch);
        scope.pushType<int>(42);
        CHECK(scratch.arena->position() > posBefore);
    }
    CHECK(scratch.arena->position() == posBefore);
}
