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

#include "engine/job/job_system.h"

TEST_CASE("MPMCQueue: push and pop") {
    fate::MPMCQueue<int, 64> queue;

    CHECK(queue.tryPop() == std::nullopt);

    queue.push(42);
    queue.push(99);

    auto v1 = queue.tryPop();
    auto v2 = queue.tryPop();
    auto v3 = queue.tryPop();

    REQUIRE(v1.has_value());
    CHECK(v1.value() == 42);
    REQUIRE(v2.has_value());
    CHECK(v2.value() == 99);
    CHECK(!v3.has_value());
}

TEST_CASE("Counter: atomic increment and decrement") {
    fate::Counter counter;
    counter.value.store(3);

    counter.value.fetch_sub(1);
    CHECK(counter.value.load() == 2);

    counter.value.fetch_sub(1);
    counter.value.fetch_sub(1);
    CHECK(counter.value.load() == 0);
}

TEST_CASE("CounterPool: acquire and release") {
    fate::CounterPool pool;

    fate::Counter* c1 = pool.acquire();
    REQUIRE(c1 != nullptr);
    CHECK(c1->value.load() == 0);

    c1->value.store(5);
    pool.release(c1);

    // After release, can re-acquire
    fate::Counter* c2 = pool.acquire();
    REQUIRE(c2 != nullptr);
    CHECK(c2->value.load() == 0); // reset on acquire
}

TEST_CASE("JobSystem: submit and wait") {
    auto& js = fate::JobSystem::instance();
    js.init(2);

    std::atomic<int> sum{0};

    auto jobFunc = [](void* param) {
        auto* s = static_cast<std::atomic<int>*>(param);
        s->fetch_add(10);
    };

    fate::Job jobs[4];
    for (auto& j : jobs) {
        j.function = jobFunc;
        j.param = &sum;
    }

    fate::Counter* counter = js.submit(jobs, 4);
    js.waitForCounter(counter, 0);

    CHECK(sum.load() == 40);

    js.shutdown();
}

TEST_CASE("JobSystem: fiber-local scratch arena") {
    auto& js = fate::JobSystem::instance();
    js.init(2);

    std::atomic<bool> arenaWorked{false};

    auto jobFunc = [](void* param) {
        auto* flag = static_cast<std::atomic<bool>*>(param);
        auto* arena = fate::JobSystem::instance().fiberScratchArena();
        if (arena) {
            size_t saved = arena->position();
            void* mem = arena->push(128, 16);
            if (mem) flag->store(true);
            arena->resetTo(saved);
        }
    };

    fate::Job job;
    job.function = jobFunc;
    job.param = &arenaWorked;

    fate::Counter* c = js.submit(&job, 1);
    js.waitForCounter(c, 0);

    CHECK(arenaWorked.load());

    js.shutdown();
}

TEST_CASE("JobSystem: dependent job chains via counters") {
    auto& js = fate::JobSystem::instance();
    js.init(4);

    std::atomic<int> partials[4];
    for (auto& p : partials) p.store(0);

    fate::Job stage1[4];
    for (int i = 0; i < 4; ++i) {
        stage1[i].function = [](void* p) {
            static_cast<std::atomic<int>*>(p)->store(25);
        };
        stage1[i].param = &partials[i];
    }

    fate::Counter* c1 = js.submit(stage1, 4);
    js.waitForCounter(c1, 0);

    // Verify stage 1 completed
    int sum = 0;
    for (auto& p : partials) sum += p.load();
    CHECK(sum == 100);

    // Stage 2 depends on stage 1
    std::atomic<int> total{0};
    fate::Job stage2;
    stage2.function = [](void* p) {
        static_cast<std::atomic<int>*>(p)->store(100);
    };
    stage2.param = &total;

    fate::Counter* c2 = js.submit(&stage2, 1);
    js.waitForCounter(c2, 0);

    CHECK(total.load() == 100);

    js.shutdown();
}
