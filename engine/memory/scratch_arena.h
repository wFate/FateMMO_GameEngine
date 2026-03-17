#pragma once
#include "engine/memory/arena.h"

namespace fate {

struct ScratchArena {
    Arena* arena = nullptr;
};

// Thread-local scratch arenas (2 per thread, 256 MB reserved each).
// GetScratch returns whichever scratch arena does NOT conflict with
// any arena in the conflicts array.
inline ScratchArena GetScratch(Arena** conflicts, int conflictCount) {
    static constexpr size_t SCRATCH_RESERVE = 256 * 1024 * 1024;
    thread_local Arena s_scratch[2] = { Arena(SCRATCH_RESERVE), Arena(SCRATCH_RESERVE) };

    for (int i = 0; i < 2; ++i) {
        bool conflict = false;
        for (int c = 0; c < conflictCount; ++c) {
            if (conflicts && conflicts[c] == &s_scratch[i]) {
                conflict = true;
                break;
            }
        }
        if (!conflict) return { &s_scratch[i] };
    }
    return { &s_scratch[0] };
}

inline ScratchArena GetScratch() {
    return GetScratch(nullptr, 0);
}

// RAII guard — saves arena position on construction, resets on destruction.
struct ScratchScope {
    Arena* arena;
    size_t savedPos;

    explicit ScratchScope(ScratchArena s) : arena(s.arena), savedPos(s.arena->position()) {}
    ~ScratchScope() { arena->resetTo(savedPos); }
    ScratchScope(const ScratchScope&) = delete;
    ScratchScope& operator=(const ScratchScope&) = delete;

    void* push(size_t size, size_t alignment = 16) { return arena->push(size, alignment); }
    template<typename T, typename... Args>
    T* pushType(Args&&... args) { return arena->pushType<T>(std::forward<Args>(args)...); }
};

} // namespace fate
