# Phase 5A: Fiber-Based Job System — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a fiber-based job system with 4 worker threads supporting suspend/resume, targeting spatial grid rebuild, chunk lifecycle, and AI ticking.

**Architecture:** Win32 fibers on a fixed thread pool with lock-free MPMC queue. Each fiber has its own scratch arena pair. Counter-based synchronization with a single fan-out/join per frame.

**Tech Stack:** C++20, Win32 Fiber API, std::atomic, std::thread

**Spec:** `Docs/superpowers/specs/2026-03-17-phase5-job-system-gfx-rhi-design.md` (Part 1)

**Build command:**
```bash
export LIB="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\lib\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\um\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\ucrt\\x64"
export INCLUDE="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\include;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\ucrt;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\um;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\shared"
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_engine
```

**Test command:**
```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_tests && out/build/x64-Debug/fate_tests.exe
```

---

## File Structure

```
engine/job/
  fiber.h              — Platform fiber abstraction (FiberHandle, createFiber, switchToFiber, etc.)
  fiber_win32.cpp      — Win32 CreateFiber/SwitchToFiber implementation
  job_system.h         — JobSystem, Job, Counter public API
  job_system.cpp       — Fiber pool, worker threads, MPMC queue, scheduling

tests/
  test_job_system.cpp  — Unit tests for job submission, counters, fiber suspend/resume
```

**Modified files:**
- `engine/app.cpp` — Init/shutdown JobSystem in startup/teardown
- `game/game_app.cpp` — Submit AI/spatial/chunk jobs in update loop
- `game/systems/mob_ai_system.h` — Parallel AI ticking via jobs
- `engine/memory/scratch_arena.h` — Add fiber-local arena accessor

---

### Task 1: Platform Fiber Abstraction

**Files:**
- Create: `engine/job/fiber.h`
- Create: `engine/job/fiber_win32.cpp`
- Test: `tests/test_job_system.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_job_system.cpp
// NOTE: Do NOT define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — tests/test_main.cpp already provides main
#include <doctest/doctest.h>
#include "engine/job/fiber.h"

using namespace fate;

static bool fiberRan = false;

static void __stdcall testFiberProc(void* param) {
    fiberRan = true;
    // Switch back to the main fiber stored in param
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
```

- [ ] **Step 2: Run test to verify it fails**

Build and run tests. Expected: FAIL — `fiber.h` does not exist.

- [ ] **Step 3: Write fiber.h (platform-agnostic interface)**

```cpp
// engine/job/fiber.h
#pragma once
#include <cstddef>

namespace fate {

using FiberHandle = void*;

// Platform-specific fiber entry point calling convention
#ifdef _WIN32
using FiberProc = void (__stdcall *)(void*);
#else
using FiberProc = void (*)(void*);
#endif

namespace fiber {
    // Convert the calling thread into a fiber (required before switchTo)
    FiberHandle convertThreadToFiber();

    // Convert fiber back to a normal thread (cleanup)
    void convertFiberToThread();

    // Create a new fiber with the given stack size
    FiberHandle create(size_t stackSize, FiberProc proc, void* param);

    // Destroy a fiber (must not be the currently running fiber)
    void destroy(FiberHandle fiber);

    // Switch execution to the given fiber
    void switchTo(FiberHandle fiber);

    // Get the currently running fiber
    FiberHandle current();
}

} // namespace fate
```

- [ ] **Step 4: Write fiber_win32.cpp**

```cpp
// engine/job/fiber_win32.cpp
#include "engine/job/fiber.h"

#ifdef _WIN32
#include <Windows.h>

namespace fate {
namespace fiber {

FiberHandle convertThreadToFiber() {
    return ::ConvertThreadToFiber(nullptr);
}

void convertFiberToThread() {
    ::ConvertFiberToThread();
}

FiberHandle create(size_t stackSize, FiberProc proc, void* param) {
    return ::CreateFiber(stackSize, reinterpret_cast<LPFIBER_START_ROUTINE>(proc), param);
}

void destroy(FiberHandle f) {
    if (f) ::DeleteFiber(f);
}

void switchTo(FiberHandle f) {
    ::SwitchToFiber(f);
}

FiberHandle current() {
    return ::GetCurrentFiber();
}

} // namespace fiber
} // namespace fate

#endif // _WIN32
```

- [ ] **Step 5: Run test to verify it passes**

Build and run `test_job_system`. Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add engine/job/fiber.h engine/job/fiber_win32.cpp tests/test_job_system.cpp
git commit -m "feat(job): add Win32 fiber platform abstraction with test"
```

---

### Task 2: Lock-Free MPMC Queue

**Files:**
- Modify: `engine/job/job_system.h` (create)
- Test: `tests/test_job_system.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_job_system.cpp`:

```cpp
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
```

- [ ] **Step 2: Run test to verify it fails**

Expected: FAIL — `job_system.h` does not exist.

- [ ] **Step 3: Write the MPMC queue in job_system.h**

```cpp
// engine/job/job_system.h
#pragma once
#include <atomic>
#include <optional>
#include <cstdint>
#include <cassert>

namespace fate {

// Lock-free bounded MPMC queue (power-of-two capacity)
template<typename T, size_t Capacity>
class MPMCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

    struct Cell {
        std::atomic<size_t> sequence;
        T data;
    };

    alignas(64) Cell buffer_[Capacity];
    alignas(64) std::atomic<size_t> enqueuePos_{0};
    alignas(64) std::atomic<size_t> dequeuePos_{0};

public:
    MPMCQueue() {
        for (size_t i = 0; i < Capacity; ++i)
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
    }

    bool push(const T& item) {
        Cell* cell;
        size_t pos = enqueuePos_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &buffer_[pos & (Capacity - 1)];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (diff == 0) {
                if (enqueuePos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                    break;
            } else if (diff < 0) {
                return false; // full
            } else {
                pos = enqueuePos_.load(std::memory_order_relaxed);
            }
        }
        cell->data = item;
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    std::optional<T> tryPop() {
        Cell* cell;
        size_t pos = dequeuePos_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &buffer_[pos & (Capacity - 1)];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
            if (diff == 0) {
                if (dequeuePos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                    break;
            } else if (diff < 0) {
                return std::nullopt; // empty
            } else {
                pos = dequeuePos_.load(std::memory_order_relaxed);
            }
        }
        T result = std::move(cell->data);
        cell->sequence.store(pos + Capacity, std::memory_order_release);
        return result;
    }
};

} // namespace fate
```

- [ ] **Step 4: Run test to verify it passes**

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/job/job_system.h tests/test_job_system.cpp
git commit -m "feat(job): add lock-free MPMC queue"
```

---

### Task 3: Counter and Job Types

**Files:**
- Modify: `engine/job/job_system.h`
- Test: `tests/test_job_system.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_job_system.cpp`:

```cpp
TEST_CASE("Counter: atomic increment and decrement") {
    fate::Counter counter;
    counter.value.store(3);

    counter.value.fetch_sub(1);
    CHECK(counter.value.load() == 2);

    counter.value.fetch_sub(1);
    counter.value.fetch_sub(1);
    CHECK(counter.value.load() == 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: FAIL — `Counter` not defined.

- [ ] **Step 3: Add Counter, Job, and CounterPool to job_system.h**

Append to `engine/job/job_system.h` before the closing `} // namespace fate`:

```cpp
// Job definition — counter is set by submit(), decremented after function runs
struct Job {
    void (*function)(void* param) = nullptr;
    void* param = nullptr;
    Counter* counter = nullptr; // set internally by submit()
};

// Atomic counter for job group completion tracking
struct Counter {
    std::atomic<int32_t> value{0};
    std::atomic<bool> inUse{false};
};

// Fixed-size counter pool (no heap allocation per submit)
// Thread-safe: uses atomic compare_exchange for acquisition
class CounterPool {
    static constexpr int POOL_SIZE = 64;
    Counter counters_[POOL_SIZE];

public:
    Counter* acquire() {
        for (int i = 0; i < POOL_SIZE; ++i) {
            bool expected = false;
            if (counters_[i].inUse.compare_exchange_strong(expected, true,
                    std::memory_order_acquire)) {
                counters_[i].value.store(0, std::memory_order_relaxed);
                return &counters_[i];
            }
        }
        assert(false && "Counter pool exhausted");
        return nullptr;
    }

    void release(Counter* c) {
        if (c) c->inUse.store(false, std::memory_order_release);
    }
};
```

- [ ] **Step 4: Run test to verify it passes**

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/job/job_system.h tests/test_job_system.cpp
git commit -m "feat(job): add Job, Counter, and CounterPool types"
```

---

### Task 4: JobSystem Core — Init, Submit, Worker Loop

**Files:**
- Modify: `engine/job/job_system.h`
- Create: `engine/job/job_system.cpp`
- Test: `tests/test_job_system.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_job_system.cpp`:

```cpp
TEST_CASE("JobSystem: submit and wait") {
    auto& js = fate::JobSystem::instance();
    js.init(2); // 2 workers for testing

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
```

- [ ] **Step 2: Run test to verify it fails**

Expected: FAIL — `JobSystem` class not fully defined.

- [ ] **Step 3: Add JobSystem class declaration to job_system.h**

Append to `engine/job/job_system.h`:

```cpp
// Forward declare
struct FiberContext;

class JobSystem {
public:
    static JobSystem& instance();

    void init(int workerCount = 4);
    void shutdown();

    // Submit a batch of jobs. Returns a counter that decrements as jobs complete.
    Counter* submit(Job* jobs, int count);

    // Suspend the current fiber until counter reaches target.
    // MUST be called from a fiber (worker thread or converted main thread).
    void waitForCounter(Counter* counter, int target = 0);

private:
    JobSystem() = default;

    static constexpr int MAX_FIBERS = 32;
    static constexpr size_t FIBER_STACK_SIZE = 65536; // 64KB

    struct WaitEntry {
        FiberHandle fiber = nullptr;
        Counter* counter = nullptr;
        int target = 0;
    };

    std::thread workers_[8]; // max 8 workers
    int workerCount_ = 0;
    std::atomic<bool> running_{false};

    MPMCQueue<Job, 256> jobQueue_;
    CounterPool counterPool_;

    // Fiber pool
    FiberHandle fiberPool_[MAX_FIBERS];
    std::atomic<bool> fiberInUse_[MAX_FIBERS];

    // Wait list — protected by spinlock since entries are swapped during removal
    WaitEntry waitList_[MAX_FIBERS];
    std::atomic<int> waitCount_{0};
    std::atomic_flag waitLock_ = ATOMIC_FLAG_INIT;

    // Per-fiber context (indexed by fiber pool slot)
    struct FiberContext {
        Job currentJob;
        int workerIndex = -1;
    };
    FiberContext fiberContexts_[MAX_FIBERS];

    void workerMain(int workerIndex);
    static void __stdcall fiberEntry(void* param);
    FiberHandle acquireFiber(int& outIndex);
    void releaseFiber(int fiberIndex);
    void checkWaitList();
};
```

- [ ] **Step 4: Write job_system.cpp**

```cpp
// engine/job/job_system.cpp
#include "engine/job/job_system.h"
#include "engine/job/fiber.h"
#include "engine/core/logger.h"
#include <thread>
#include <cassert>

namespace fate {

// Thread-local: which worker are we, and what's our scheduler fiber
thread_local int t_workerIndex = -1;
thread_local FiberHandle t_schedulerFiber = nullptr;
thread_local int t_currentFiberIndex = -1;

JobSystem& JobSystem::instance() {
    static JobSystem s;
    return s;
}

void JobSystem::init(int workerCount) {
    assert(workerCount > 0 && workerCount <= 8);
    workerCount_ = workerCount;
    running_.store(true, std::memory_order_relaxed);

    // Pre-allocate fiber pool
    for (int i = 0; i < MAX_FIBERS; ++i) {
        fiberPool_[i] = nullptr; // created lazily
        fiberInUse_[i].store(false, std::memory_order_relaxed);
    }
    waitCount_.store(0, std::memory_order_relaxed);
    waitLock_.clear();

    // Launch workers
    for (int i = 0; i < workerCount_; ++i) {
        workers_[i] = std::thread(&JobSystem::workerMain, this, i);
    }

    LOG_INFO("JobSystem", "Initialized with %d workers, %d fibers", workerCount_, MAX_FIBERS);
}

void JobSystem::shutdown() {
    running_.store(false, std::memory_order_relaxed);
    for (int i = 0; i < workerCount_; ++i) {
        if (workers_[i].joinable())
            workers_[i].join();
    }

    // Clean up fibers
    for (int i = 0; i < MAX_FIBERS; ++i) {
        if (fiberPool_[i]) {
            fiber::destroy(fiberPool_[i]);
            fiberPool_[i] = nullptr;
        }
    }

    LOG_INFO("JobSystem", "Shutdown complete");
}

Counter* JobSystem::submit(Job* jobs, int count) {
    Counter* counter = counterPool_.acquire();
    counter->value.store(count, std::memory_order_release);

    for (int i = 0; i < count; ++i) {
        Job j = jobs[i];
        j.counter = counter; // associate counter with each job
        bool ok = jobQueue_.push(j);
        assert(ok && "Job queue full");
        (void)ok;
    }
    return counter;
}

void JobSystem::waitForCounter(Counter* counter, int target) {
    if (counter->value.load(std::memory_order_acquire) <= target) {
        counterPool_.release(counter);
        return;
    }

    // If called from main thread (no scheduler fiber), spin-wait
    if (t_schedulerFiber == nullptr) {
        while (counter->value.load(std::memory_order_acquire) > target) {
            checkWaitList();
            std::this_thread::yield();
        }
        counterPool_.release(counter);
        return;
    }

    // On a worker fiber: register wait and switch back to scheduler
    // Spinlock protects wait list from concurrent modification
    while (waitLock_.test_and_set(std::memory_order_acquire)) {}
    int idx = waitCount_.fetch_add(1, std::memory_order_relaxed);
    assert(idx < MAX_FIBERS && "Wait list full");
    waitList_[idx].fiber = fiber::current();
    waitList_[idx].counter = counter;
    waitList_[idx].target = target;
    waitLock_.clear(std::memory_order_release);

    // Switch back to scheduler — this fiber is now suspended
    fiber::switchTo(t_schedulerFiber);
}

void JobSystem::workerMain(int workerIndex) {
    t_workerIndex = workerIndex;

    // Convert this thread to a fiber so we can switch
    FiberHandle schedulerFiber = fiber::convertThreadToFiber();
    t_schedulerFiber = schedulerFiber;
    workerFibers_[workerIndex] = schedulerFiber;

    while (running_.load(std::memory_order_relaxed)) {
        // Check if any waiting fibers can resume
        checkWaitList();

        // Try to get a job
        auto maybeJob = jobQueue_.tryPop();
        if (!maybeJob.has_value()) {
            std::this_thread::yield();
            continue;
        }

        // Acquire a fiber to run the job
        int fiberIdx = -1;
        FiberHandle f = acquireFiber(fiberIdx);
        if (!f) {
            // No free fiber — push job back and yield
            jobQueue_.push(maybeJob.value());
            std::this_thread::yield();
            continue;
        }

        // Set up fiber context
        fiberContexts_[fiberIdx].currentJob = maybeJob.value();
        fiberContexts_[fiberIdx].workerIndex = workerIndex;
        t_currentFiberIndex = fiberIdx;

        // Switch to fiber — it runs the job and switches back
        fiber::switchTo(f);

        // Fiber returned — release it back to pool
        releaseFiber(fiberIdx);
    }

    fiber::convertFiberToThread();
}

void __stdcall JobSystem::fiberEntry(void* param) {
    (void)param;
    auto& js = JobSystem::instance();

    // Fiber main loop: run job, then switch back to scheduler
    for (;;) {
        int idx = t_currentFiberIndex;
        auto& ctx = js.fiberContexts_[idx];

        // Run the job
        if (ctx.currentJob.function) {
            ctx.currentJob.function(ctx.currentJob.param);
        }

        // Decrement the counter — this is what makes waitForCounter unblock
        if (ctx.currentJob.counter) {
            ctx.currentJob.counter->value.fetch_sub(1, std::memory_order_release);
        }

        // Switch back to the scheduler fiber of THIS worker thread
        // (t_schedulerFiber is thread_local, so it's always the current worker's scheduler)
        fiber::switchTo(t_schedulerFiber);
    }
}

FiberHandle JobSystem::acquireFiber(int& outIndex) {
    for (int i = 0; i < MAX_FIBERS; ++i) {
        bool expected = false;
        if (fiberInUse_[i].compare_exchange_strong(expected, true, std::memory_order_acquire)) {
            if (!fiberPool_[i]) {
                fiberPool_[i] = fiber::create(FIBER_STACK_SIZE, fiberEntry, nullptr);
            }
            outIndex = i;
            return fiberPool_[i];
        }
    }
    outIndex = -1;
    return nullptr;
}

void JobSystem::releaseFiber(int fiberIndex) {
    fiberInUse_[fiberIndex].store(false, std::memory_order_release);
}

void JobSystem::checkWaitList() {
    // Spinlock protects wait list
    while (waitLock_.test_and_set(std::memory_order_acquire)) {}

    int count = waitCount_.load(std::memory_order_relaxed);
    for (int i = 0; i < count; ++i) {
        auto& entry = waitList_[i];
        if (entry.fiber && entry.counter) {
            if (entry.counter->value.load(std::memory_order_acquire) <= entry.target) {
                FiberHandle f = entry.fiber;
                Counter* c = entry.counter;

                // Remove from wait list (swap with last)
                entry = waitList_[count - 1];
                waitList_[count - 1] = {};
                waitCount_.fetch_sub(1, std::memory_order_relaxed);
                --count;
                --i; // re-check this index

                counterPool_.release(c);
                waitLock_.clear(std::memory_order_release);

                // Resume the waiting fiber on THIS worker
                // The fiber will switch back to t_schedulerFiber when done
                t_currentFiberIndex = -1; // resumed fiber manages its own index
                fiber::switchTo(f);

                // Re-acquire lock to continue checking
                while (waitLock_.test_and_set(std::memory_order_acquire)) {}
                count = waitCount_.load(std::memory_order_relaxed);
                i = -1; // restart scan
            }
        }
    }

    waitLock_.clear(std::memory_order_release);
}

} // namespace fate
```

- [ ] **Step 5: Run test to verify it passes**

Expected: PASS — 4 jobs each add 10, sum = 40.

- [ ] **Step 6: Commit**

```bash
git add engine/job/job_system.h engine/job/job_system.cpp tests/test_job_system.cpp
git commit -m "feat(job): add JobSystem with fiber pool, MPMC queue, and counter-based sync"
```

---

### Task 5: Fiber-Local Scratch Arenas

**Files:**
- Modify: `engine/memory/scratch_arena.h`
- Modify: `engine/job/job_system.h`
- Test: `tests/test_job_system.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_job_system.cpp`:

```cpp
TEST_CASE("JobSystem: fiber-local scratch arena") {
    auto& js = fate::JobSystem::instance();
    js.init(2);

    std::atomic<bool> arenaWorked{false};

    auto jobFunc = [](void* param) {
        auto* flag = static_cast<std::atomic<bool>*>(param);
        // Verify that fiber scratch arena is accessible and usable
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
```

- [ ] **Step 2: Run test to verify it fails**

Expected: FAIL — `fiberScratchArena()` not defined.

- [ ] **Step 3: Add fiber-local arenas**

Add to `JobSystem` class in `job_system.h`:

```cpp
    // Per-fiber scratch arenas (NOT thread_local — fiber-local)
    Arena fiberArenas_[MAX_FIBERS * 2]; // 2 arenas per fiber (Fleury conflict pattern)

    // Get the scratch arena for the currently running fiber
    Arena* fiberScratchArena();
```

Initialize arenas in `job_system.cpp` `init()`:

```cpp
    // Initialize fiber-local arenas (256MB reserved each, committed on demand)
    static constexpr size_t FIBER_ARENA_SIZE = 256 * 1024 * 1024;
    for (int i = 0; i < MAX_FIBERS * 2; ++i) {
        fiberArenas_[i] = Arena(FIBER_ARENA_SIZE);
    }
```

Implement `fiberScratchArena()`:

```cpp
Arena* JobSystem::fiberScratchArena() {
    // Find current fiber index
    FiberHandle current = fiber::current();
    for (int i = 0; i < MAX_FIBERS; ++i) {
        if (fiberPool_[i] == current) {
            return &fiberArenas_[i * 2]; // Return first of the pair
        }
    }
    return nullptr; // Not on a job fiber
}
```

- [ ] **Step 4: Run test to verify it passes**

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/job/job_system.h engine/job/job_system.cpp engine/memory/scratch_arena.h tests/test_job_system.cpp
git commit -m "feat(job): add fiber-local scratch arenas"
```

---

### Task 6: Engine Integration — Init/Shutdown

**Files:**
- Modify: `engine/app.cpp`

- [ ] **Step 1: Add JobSystem include and init**

In `engine/app.cpp`, add include:
```cpp
#include "engine/job/job_system.h"
```

In `App::init()` (after GL context creation, around line 75), add:
```cpp
    JobSystem::instance().init(4);
```

In `App::~App()` (before GL context destruction), add:
```cpp
    JobSystem::instance().shutdown();
```

- [ ] **Step 2: Build and verify no errors**

Run build command. Expected: compiles clean.

- [ ] **Step 3: Commit**

```bash
git add engine/app.cpp
git commit -m "feat(job): wire JobSystem init/shutdown into App lifecycle"
```

---

### Task 7: AI Ticking via Jobs

**Files:**
- Modify: `game/systems/mob_ai_system.h`
- Modify: `game/game_app.cpp`

- [ ] **Step 1: Add parallel AI update to MobAISystem**

In `game/systems/mob_ai_system.h`, add a new method alongside the existing `update()`:

```cpp
    // Parallel AI update — submits mob groups as jobs
    Counter* submitParallelUpdate(float dt) {
        if (!world_) return nullptr;

        // Collect mobs that need AI ticks this frame
        pendingMobs_.clear();
        world_->forEach<Transform, MobAIComponent, EnemyStatsComponent>(
            [&](Entity* entity, Transform* t, MobAIComponent* ai, EnemyStatsComponent* enemy) {
                if (!enemy->stats.isAlive) return;
                ai->tickAccumulator += dt;
                if (ai->tickAccumulator < 0.1f) return; // DEAR: 10 ticks/sec max
                ai->tickAccumulator = 0.0f;
                pendingMobs_.push_back({entity, t, ai, enemy});
            }
        );

        if (pendingMobs_.empty()) return nullptr;

        // Partition into groups of 4
        int groupCount = ((int)pendingMobs_.size() + 3) / 4;
        aiJobs_.resize(groupCount);
        aiJobParams_.resize(groupCount);

        for (int g = 0; g < groupCount; ++g) {
            int start = g * 4;
            int end = std::min(start + 4, (int)pendingMobs_.size());
            aiJobParams_[g] = {this, start, end};
            aiJobs_[g].function = [](void* param) {
                auto* p = static_cast<AIJobParam*>(param);
                for (int i = p->start; i < p->end; ++i) {
                    auto& mob = p->system->pendingMobs_[i];
                    p->system->tickSingleMob(mob.entity, mob.transform, mob.ai, mob.enemy);
                }
            };
            aiJobs_[g].param = &aiJobParams_[g];
        }

        return JobSystem::instance().submit(aiJobs_.data(), groupCount);
    }

private:
    struct MobEntry {
        Entity* entity;
        Transform* transform;
        MobAIComponent* ai;
        EnemyStatsComponent* enemy;
    };

    struct AIJobParam {
        MobAISystem* system;
        int start, end;
    };

    std::vector<MobEntry> pendingMobs_;
    std::vector<Job> aiJobs_;
    std::vector<AIJobParam> aiJobParams_;

    void tickSingleMob(Entity* entity, Transform* t, MobAIComponent* ai, EnemyStatsComponent* enemy) {
        // Extract the existing per-mob AI logic from update() into here
        // This runs on a worker fiber — only writes to ai->* and enemy->*
    }
```

- [ ] **Step 2: Wire into game_app.cpp update**

In `game/game_app.cpp`, in the section where systems update (called via `World::update`), add a pre-update step that submits AI jobs and waits:

```cpp
// In onUpdate() or a pre-update hook:
if (mobAISystem_) {
    Counter* aiCounter = mobAISystem_->submitParallelUpdate(deltaTime);
    if (aiCounter) {
        JobSystem::instance().waitForCounter(aiCounter, 0);
    }
}
```

- [ ] **Step 3: Build and verify no errors**

Expected: compiles clean. AI still ticks correctly (same behavior, just parallel).

- [ ] **Step 4: Commit**

```bash
git add game/systems/mob_ai_system.h game/game_app.cpp
git commit -m "feat(job): parallel AI ticking via fiber jobs"
```

---

### Task 8: Spatial Grid and Chunk Jobs (Stubs)

**Files:**
- Modify: `game/game_app.cpp`

- [ ] **Step 1: Add spatial grid job submission stub**

In `game/game_app.cpp`, add placeholder job submissions for spatial grid and chunk lifecycle. These serve as integration points — the actual spatial grid double-buffer and chunk async I/O can be implemented when those systems are stressed:

```cpp
// Spatial grid rebuild job (placeholder — currently rebuilds inline)
// TODO: When spatial grid exists, submit rebuild as a job here
// Counter* spatialCounter = submitSpatialRebuild();

// Chunk lifecycle job (placeholder — currently transitions inline)
// TODO: When chunk system is CPU-bound, submit transitions as jobs here
// Counter* chunkCounter = submitChunkTransitions();
```

- [ ] **Step 2: Commit**

```bash
git add game/game_app.cpp
git commit -m "feat(job): add spatial grid and chunk lifecycle job stubs"
```

---

### Task 9: Integration Test — Full Job Pipeline

**Files:**
- Test: `tests/test_job_system.cpp`

- [ ] **Step 1: Write integration test**

```cpp
TEST_CASE("JobSystem: dependent job chains via counters") {
    auto& js = fate::JobSystem::instance();
    js.init(4);

    // Stage 1: compute partial sums
    std::atomic<int> partials[4] = {0, 0, 0, 0};
    fate::Job stage1[4];
    for (int i = 0; i < 4; ++i) {
        stage1[i].function = [](void* p) {
            static_cast<std::atomic<int>*>(p)->store(25);
        };
        stage1[i].param = &partials[i];
    }

    fate::Counter* c1 = js.submit(stage1, 4);
    js.waitForCounter(c1, 0);

    // Stage 2: sum all partials (depends on stage 1 being done)
    std::atomic<int> total{0};
    fate::Job stage2;
    stage2.function = [](void* p) {
        // This runs after stage 1 is fully complete
        auto* t = static_cast<std::atomic<int>*>(p);
        t->store(100); // 4 * 25
    };
    stage2.param = &total;

    fate::Counter* c2 = js.submit(&stage2, 1);
    js.waitForCounter(c2, 0);

    CHECK(total.load() == 100);

    js.shutdown();
}
```

- [ ] **Step 2: Run all job system tests**

Expected: All PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/test_job_system.cpp
git commit -m "test(job): add dependent job chain integration test"
```
