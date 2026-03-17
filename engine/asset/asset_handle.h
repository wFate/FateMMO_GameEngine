#pragma once
#include <cstdint>
#include <functional>

namespace fate {

struct AssetHandle {
    uint32_t bits = 0;

    uint32_t index() const { return bits & 0xFFFFF; }
    uint32_t generation() const { return bits >> 20; }
    bool valid() const { return bits != 0; }

    static AssetHandle make(uint32_t index, uint32_t gen) {
        return { (gen << 20) | (index & 0xFFFFF) };
    }

    bool operator==(AssetHandle o) const { return bits == o.bits; }
    bool operator!=(AssetHandle o) const { return bits != o.bits; }
};

} // namespace fate

template<>
struct std::hash<fate::AssetHandle> {
    size_t operator()(fate::AssetHandle h) const noexcept {
        return std::hash<uint32_t>{}(h.bits);
    }
};
