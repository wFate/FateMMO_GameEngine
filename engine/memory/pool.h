#pragma once
#include "engine/memory/arena.h"
#include <cstdint>
#include <cstddef>
#include <cassert>

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
        assert(memory_ && "PoolAllocator: arena allocation failed");

        // Build free list — each free block stores pointer to next free block
        freeList_ = nullptr;
        for (size_t i = blockCount_; i > 0; --i) {
            void* block = memory_ + (i - 1) * blockSize_;
            *static_cast<void**>(block) = freeList_;
            freeList_ = block;
        }
    }

    void* alloc() {
        if (!freeList_) return nullptr;
        void* block = freeList_;
        freeList_ = *static_cast<void**>(freeList_);
        ++activeCount_;
        return block;
    }

    void free(void* block) {
        if (!block) return;
        assert(block >= memory_ && block < memory_ + blockSize_ * blockCount_);
        *static_cast<void**>(block) = freeList_;
        freeList_ = block;
        --activeCount_;
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
};

} // namespace fate
