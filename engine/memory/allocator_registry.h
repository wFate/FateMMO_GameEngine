#pragma once

#if defined(ENGINE_MEMORY_DEBUG)

#include <vector>
#include <string>
#include <functional>
#include <cstring>
#include <algorithm>

namespace fate {

enum class AllocatorType : uint8_t { Arena, FrameArena, Pool };

struct AllocatorInfo {
    const char* name = "";
    AllocatorType type = AllocatorType::Arena;
    std::function<size_t()> getUsed;
    std::function<size_t()> getCommitted;
    std::function<size_t()> getReserved;
    // Pool-specific (empty for arenas)
    std::function<size_t()> getActiveBlocks;
    std::function<size_t()> getTotalBlocks;
    std::function<const uint8_t*()> getOccupancyBitmap;
};

class AllocatorRegistry {
public:
    static AllocatorRegistry& instance() {
        static AllocatorRegistry s_instance;
        return s_instance;
    }

    void add(AllocatorInfo info) {
        allocators_.push_back(std::move(info));
    }

    void remove(const char* name) {
        allocators_.erase(
            std::remove_if(allocators_.begin(), allocators_.end(),
                [name](const AllocatorInfo& a) { return std::strcmp(a.name, name) == 0; }),
            allocators_.end()
        );
    }

    const std::vector<AllocatorInfo>& all() const { return allocators_; }

private:
    AllocatorRegistry() = default;
    std::vector<AllocatorInfo> allocators_;
};

} // namespace fate

#endif // ENGINE_MEMORY_DEBUG
