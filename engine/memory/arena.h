#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <utility>
#include "engine/core/logger.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace fate {

// ==========================================================================
// Arena — bump allocator backed by virtual memory
//
// Reserves a large virtual address range up front, commits pages on demand.
// All allocations freed in O(1) by resetting the position pointer.
// ==========================================================================
class Arena {
public:
    explicit Arena(size_t reserveSize = 256 * 1024 * 1024) {
        reserveSize_ = alignUp(reserveSize, pageSize());
#ifdef _WIN32
        base_ = static_cast<uint8_t*>(
            VirtualAlloc(nullptr, reserveSize_, MEM_RESERVE, PAGE_NOACCESS));
#else
        base_ = static_cast<uint8_t*>(
            mmap(nullptr, reserveSize_, PROT_NONE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (base_ == MAP_FAILED) base_ = nullptr;
#endif
        if (!base_) {
            LOG_FATAL("Arena", "Virtual memory reservation failed (%zu bytes)", reserveSize_);
            std::abort();
        }
    }

    ~Arena() {
        if (base_) {
#ifdef _WIN32
            VirtualFree(base_, 0, MEM_RELEASE);
#else
            munmap(base_, reserveSize_);
#endif
        }
    }

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    Arena(Arena&& other) noexcept
        : base_(other.base_), pos_(other.pos_),
          committed_(other.committed_), reserveSize_(other.reserveSize_) {
        other.base_ = nullptr;
        other.pos_ = 0;
        other.committed_ = 0;
        other.reserveSize_ = 0;
    }

    Arena& operator=(Arena&& other) noexcept {
        if (this != &other) {
            this->~Arena();
            new (this) Arena(std::move(other));
        }
        return *this;
    }

    void* push(size_t size, size_t alignment = 16) {
        size_t aligned = alignUp(pos_, alignment);
        size_t newPos = aligned + size;
        if (newPos > reserveSize_) return nullptr;

        if (newPos > committed_) {
            size_t commitTarget = alignUp(newPos, pageSize());
            if (commitTarget > reserveSize_) commitTarget = reserveSize_;
#ifdef _WIN32
            VirtualAlloc(base_ + committed_, commitTarget - committed_,
                         MEM_COMMIT, PAGE_READWRITE);
#else
            mprotect(base_ + committed_, commitTarget - committed_,
                     PROT_READ | PROT_WRITE);
#endif
            committed_ = commitTarget;
        }

        pos_ = newPos;
        return base_ + aligned;
    }

    template<typename T, typename... Args>
    T* pushType(Args&&... args) {
        void* mem = push(sizeof(T), alignof(T));
        if (!mem) return nullptr;
        return new (mem) T(std::forward<Args>(args)...);
    }

    template<typename T>
    T* pushArray(size_t count) {
        void* mem = push(sizeof(T) * count, alignof(T));
        if (!mem) return nullptr;
        T* arr = static_cast<T*>(mem);
        for (size_t i = 0; i < count; ++i)
            new (&arr[i]) T();
        return arr;
    }

    void reset() { pos_ = 0; }
    void resetTo(size_t pos) { pos_ = pos; }

    void resetAndDecommit(size_t keepBytes = 0) {
        pos_ = 0;
        size_t keep = alignUp(keepBytes, pageSize());
        if (committed_ > keep) {
#ifdef _WIN32
            VirtualFree(base_ + keep, committed_ - keep, MEM_DECOMMIT);
#else
            madvise(base_ + keep, committed_ - keep, MADV_DONTNEED);
            mprotect(base_ + keep, committed_ - keep, PROT_NONE);
#endif
            committed_ = keep;
        }
    }

    size_t position() const { return pos_; }
    size_t committed() const { return committed_; }
    size_t reserved() const { return reserveSize_; }
    uint8_t* base() const { return base_; }

private:
    uint8_t* base_ = nullptr;
    size_t pos_ = 0;
    size_t committed_ = 0;
    size_t reserveSize_ = 0;

    static size_t alignUp(size_t value, size_t alignment) {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    static size_t pageSize() {
        static const size_t cached = []() -> size_t {
#ifdef _WIN32
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            return si.dwPageSize;
#else
            return static_cast<size_t>(sysconf(_SC_PAGESIZE));
#endif
        }();
        return cached;
    }
};

// ==========================================================================
// FrameArena — double-buffered arena for per-frame temporaries
//
// Call swap() at frame start. Current arena is for this frame's allocations;
// previous arena's data is still valid (for referencing last frame's results).
// ==========================================================================
class FrameArena {
public:
    explicit FrameArena(size_t reservePerBuffer = 64 * 1024 * 1024)
        : arenas_{Arena(reservePerBuffer), Arena(reservePerBuffer)} {}

    void swap() {
        current_ ^= 1;
        arenas_[current_].reset();
    }

    Arena& current() { return arenas_[current_]; }
    Arena& previous() { return arenas_[current_ ^ 1]; }

    void* push(size_t size, size_t alignment = 16) {
        return arenas_[current_].push(size, alignment);
    }

    template<typename T, typename... Args>
    T* pushType(Args&&... args) {
        return arenas_[current_].pushType<T>(std::forward<Args>(args)...);
    }

    template<typename T>
    T* pushArray(size_t count) {
        return arenas_[current_].pushArray<T>(count);
    }

private:
    Arena arenas_[2];
    int current_ = 0;
};

} // namespace fate
