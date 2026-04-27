#pragma once
#include <atomic>
#include <optional>
#include <cstdint>
#include <cstddef>
#include <cassert>
#include <thread>
#include "engine/job/fiber.h"
#include "engine/memory/arena.h"
#include "engine/core/logger.h"

namespace fate {

// Lock-free bounded MPMC queue (Dmitry Vyukov algorithm, power-of-two capacity)
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

// Atomic counter for job group completion tracking
struct Counter {
    std::atomic<int32_t> value{0};
    std::atomic<bool> inUse{false};
};

// Job definition — counter is set by submit(), decremented after function runs
struct Job {
    void (*function)(void* param) = nullptr;
    void* param = nullptr;
    Counter* counter = nullptr; // set internally by submit()
};

// Fixed-size counter pool — thread-safe acquisition via atomic CAS
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
        LOG_ERROR("JobSystem", "Counter pool exhausted (%d counters)", POOL_SIZE);
        return nullptr;
    }

    void release(Counter* c) {
        if (c) c->inUse.store(false, std::memory_order_release);
    }
};

class JobSystem {
public:
    static JobSystem& instance();

    void init(int workerCount = 4);
    void shutdown();

    Counter* submit(Job* jobs, int count);
    void submitFireAndForget(Job* jobs, int count);
    // Non-blocking variant: returns false immediately if the job queue is
    // full, instead of yield-spinning until a slot opens. Callers (typically
    // DbDispatcher) are expected to backlog the job and retry on the next
    // tick. This prevents the game thread from stalling on a full queue
    // when workers are slow (e.g. high-RTT remote DB during mass disconnect).
    bool tryPushFireAndForget(const Job& j);
    void waitForCounter(Counter* counter, int target = 0);

    Arena* fiberScratchArena();

private:
    JobSystem() = default;

    static constexpr int MAX_FIBERS = 32;
    static constexpr size_t FIBER_STACK_SIZE = 65536;

    struct WaitEntry {
        FiberHandle fiber = nullptr;
        Counter* counter = nullptr;
        int target = 0;
        int fiberIndex = -1;
    };

    struct FiberContext {
        Job currentJob;
        int workerIndex = -1;
        bool suspended = false;
    };

    std::thread workers_[8];
    int workerCount_ = 0;
    std::atomic<bool> running_{false};

    MPMCQueue<Job, 256> jobQueue_;
    CounterPool counterPool_;

    FiberHandle fiberPool_[MAX_FIBERS];
    std::atomic<bool> fiberInUse_[MAX_FIBERS];
    FiberContext fiberContexts_[MAX_FIBERS];

    WaitEntry waitList_[MAX_FIBERS];
    std::atomic<int> waitCount_{0};
    std::atomic_flag waitLock_ = ATOMIC_FLAG_INIT;

    Arena* fiberArenas_ = nullptr;

    void workerMain(int workerIndex);
#ifdef _WIN32
    static void __stdcall fiberEntry(void* param);
#else
    static void fiberEntry(void* param);
#endif
    FiberHandle acquireFiber(int& outIndex);
    void releaseFiber(int fiberIndex);
    void checkWaitList();
};

} // namespace fate
