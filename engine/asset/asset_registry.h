#pragma once
#include "engine/asset/asset_handle.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace fate {

enum class AssetKind : uint8_t { Texture, Json, Shader };

struct AssetLoader {
    AssetKind kind;
    void* (*load)(const std::string& path);
    bool (*reload)(void* existing, const std::string& path);
    bool (*validate)(const std::string& path);
    void (*destroy)(void* data);
    std::vector<std::string> extensions;
};

struct AssetSlot {
    std::string path;
    uint32_t generation = 0;
    AssetKind kind{};
    void* data = nullptr;
    bool loaded = false;
};

// Thread safety: queueReload() is safe to call from any thread.
// All other methods (load, get, find, clear, processReloads) must be
// called from the main thread only.
class AssetRegistry {
public:
    static AssetRegistry& instance();

    void registerLoader(AssetLoader loader);

    AssetHandle load(const std::string& path);

    template<typename T>
    T* get(AssetHandle handle) {
        if (!handle.valid()) return nullptr;
        uint32_t idx = handle.index();
        if (idx >= slots_.size()) return nullptr;
        auto& slot = slots_[idx];
        if (!slot.loaded || slot.generation != handle.generation())
            return nullptr;
        return static_cast<T*>(slot.data);
    }

    void queueReload(const std::string& path);
    void processReloads(float currentTime);

    AssetHandle find(const std::string& path) const;
    void clear();

    // Debug info
    size_t assetCount() const;

private:
    AssetRegistry();

    std::vector<AssetSlot> slots_;       // slot 0 reserved
    std::vector<uint32_t> freeList_;
    std::unordered_map<std::string, uint32_t> pathToIndex_;
    std::vector<AssetLoader> loaders_;

    std::mutex reloadQueueMutex_;
    struct PendingReload {
        std::string path;
        float lastEventTime = 0.0f;
    };
    std::vector<PendingReload> pendingReloads_;
    static constexpr float kDebounceDelay = 0.3f;

    const AssetLoader* findLoader(const std::string& path) const;
    uint32_t allocSlot();
};

} // namespace fate
