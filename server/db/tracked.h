#pragma once
#include <cstdint>

namespace fate {

template<typename T>
class Tracked {
public:
    explicit Tracked(const T& val = T{}) : data_(val) {}

    const T& get() const { return data_; }
    operator const T&() const { return data_; }

    // Mutable access — automatically marks dirty and increments version
    T& mutate() {
        dirty_ = true;
        ++version_;
        return data_;
    }

    bool dirty() const { return dirty_; }
    void clearDirty() { dirty_ = false; }
    uint32_t version() const { return version_; }

private:
    T data_;
    bool dirty_ = false;
    uint32_t version_ = 0;
};

} // namespace fate
