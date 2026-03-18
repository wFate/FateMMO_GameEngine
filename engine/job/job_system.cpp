#include "engine/job/job_system.h"
#include "engine/job/fiber.h"
#include "engine/core/logger.h"
#include <thread>
#include <cassert>

namespace fate {

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

    for (int i = 0; i < MAX_FIBERS; ++i) {
        fiberPool_[i] = nullptr;
        fiberInUse_[i].store(false, std::memory_order_relaxed);
    }
    waitCount_.store(0, std::memory_order_relaxed);
    waitLock_.clear();

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
        j.counter = counter;
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
    while (waitLock_.test_and_set(std::memory_order_acquire)) {}
    int idx = waitCount_.fetch_add(1, std::memory_order_relaxed);
    assert(idx < MAX_FIBERS && "Wait list full");
    waitList_[idx].fiber = fiber::current();
    waitList_[idx].counter = counter;
    waitList_[idx].target = target;
    waitLock_.clear(std::memory_order_release);

    fiber::switchTo(t_schedulerFiber);
}

void JobSystem::workerMain(int workerIndex) {
    t_workerIndex = workerIndex;

    FiberHandle schedulerFiber = fiber::convertThreadToFiber();
    t_schedulerFiber = schedulerFiber;

    while (running_.load(std::memory_order_relaxed)) {
        checkWaitList();

        auto maybeJob = jobQueue_.tryPop();
        if (!maybeJob.has_value()) {
            std::this_thread::yield();
            continue;
        }

        int fiberIdx = -1;
        FiberHandle f = acquireFiber(fiberIdx);
        if (!f) {
            jobQueue_.push(maybeJob.value());
            std::this_thread::yield();
            continue;
        }

        fiberContexts_[fiberIdx].currentJob = maybeJob.value();
        fiberContexts_[fiberIdx].workerIndex = workerIndex;
        t_currentFiberIndex = fiberIdx;

        fiber::switchTo(f);

        // Fiber returned — release it
        releaseFiber(fiberIdx);
    }

    fiber::convertFiberToThread();
}

void __stdcall JobSystem::fiberEntry(void* param) {
    (void)param;
    auto& js = JobSystem::instance();

    for (;;) {
        int idx = t_currentFiberIndex;
        auto& ctx = js.fiberContexts_[idx];

        if (ctx.currentJob.function) {
            ctx.currentJob.function(ctx.currentJob.param);
        }

        // Decrement counter — this is what unblocks waitForCounter
        if (ctx.currentJob.counter) {
            ctx.currentJob.counter->value.fetch_sub(1, std::memory_order_release);
        }

        // Switch back to scheduler
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
                --i;

                counterPool_.release(c);
                waitLock_.clear(std::memory_order_release);

                // Resume the waiting fiber
                t_currentFiberIndex = -1;
                fiber::switchTo(f);

                // Re-acquire lock to continue
                while (waitLock_.test_and_set(std::memory_order_acquire)) {}
                count = waitCount_.load(std::memory_order_relaxed);
                i = -1;
            }
        }
    }

    waitLock_.clear(std::memory_order_release);
}

} // namespace fate
