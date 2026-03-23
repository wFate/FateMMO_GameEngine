#pragma once
#include "engine/memory/arena.h"
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include "engine/core/logger.h"

namespace fate {

// ==========================================================================
// PoolAllocator — fixed-size block pool built on top of an Arena
//
// Provides O(1) alloc/dealloc via embedded free list.
// When the backing arena resets, all pool state is implicitly freed.
// ==========================================================================
class PoolAllocator {
public:
    PoolAllocator() = default;

    void init(Arena& arena, size_t blockSize, size_t blockCount) {
        blockSize_ = blockSize < sizeof(void*) ? sizeof(void*) : blockSize;
        blockCount_ = blockCount;
        activeCount_ = 0;

        memory_ = static_cast<uint8_t*>(arena.push(blockSize_ * blockCount_, 16));
        if (!memory_) {
            LOG_FATAL("Pool", "Arena allocation failed (%zu x %zu bytes)", blockCount, blockSize_);
            std::abort();
        }

        // Build free list — each free block stores pointer to next free block
        freeList_ = nullptr;
        for (size_t i = blockCount_; i > 0; --i) {
            void* block = memory_ + (i - 1) * blockSize_;
            *static_cast<void**>(block) = freeList_;
            freeList_ = block;
        }
#if defined(ENGINE_MEMORY_DEBUG)
        size_t bitmapBytes = (blockCount_ + 7) / 8;
        occupancyBitmap_ = static_cast<uint8_t*>(arena.push(bitmapBytes, 1));
        std::memset(occupancyBitmap_, 0, bitmapBytes);
#endif
    }

    void* alloc() {
        if (!freeList_) return nullptr;
        void* block = freeList_;
        freeList_ = *static_cast<void**>(freeList_);
        ++activeCount_;
#if defined(ENGINE_MEMORY_DEBUG)
        size_t idx = (static_cast<uint8_t*>(block) - memory_) / blockSize_;
        occupancyBitmap_[idx / 8] |= (1 << (idx % 8));
#endif
        return block;
    }

    void free(void* block) {
        if (!block) return;
        assert(block >= memory_ && block < memory_ + blockSize_ * blockCount_);
        *static_cast<void**>(block) = freeList_;
        freeList_ = block;
        --activeCount_;
#if defined(ENGINE_MEMORY_DEBUG)
        size_t idx = (static_cast<uint8_t*>(block) - memory_) / blockSize_;
        occupancyBitmap_[idx / 8] &= ~(1 << (idx % 8));
#endif
    }

    size_t activeCount() const { return activeCount_; }
    size_t blockCount() const { return blockCount_; }
    size_t blockSize() const { return blockSize_; }
    bool isFull() const { return freeList_ == nullptr; }

private:
    uint8_t* memory_ = nullptr;
    void* freeList_ = nullptr;
    size_t blockSize_ = 0;
    size_t blockCount_ = 0;
    size_t activeCount_ = 0;
#if defined(ENGINE_MEMORY_DEBUG)
    uint8_t* occupancyBitmap_ = nullptr;
public:
    const uint8_t* occupancyBitmap() const { return occupancyBitmap_; }
private:
#endif
};

} // namespace fate
