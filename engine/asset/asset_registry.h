#pragma once
#include "engine/asset/asset_handle.h"
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <memory>
#include <optional>
#include <cstdint>

namespace fate {

class IAssetSource;

enum class AssetKind : uint8_t { Texture, Json, Shader };
enum class AssetState : uint8_t { Empty, Loading, Ready, Failed };

struct AssetLoader {
    AssetKind kind;
    void* (*load)(const std::string& path);
    bool (*reload)(void* existing, const std::string& path);
    bool (*validate)(const std::string& path);
    void (*destroy)(void* data);
    std::vector<std::string> extensions;
    // Async pipeline (optional — set decode to enable fiber-based loading)
    void* (*decode)(const std::string& path) = nullptr;   // fiber-safe: deserialize from disk
    void* (*upload)(void* decoded) = nullptr;              // main thread: GPU finalize, consumes decoded
    void (*destroyDecoded)(void* decoded) = nullptr;       // cleanup decoded data on cancel
};

struct AssetSlot {
    std::string path;
    uint32_t generation = 0;
    AssetKind kind{};
    void* data = nullptr;
    AssetState state = AssetState::Empty;
};

// Thread safety: queueReload() is safe to call from any thread.
// All other methods (load, loadAsync, get, find, clear, processReloads,
// processAsyncLoads) must be called from the main thread only.
class AssetRegistry {
public:
    static AssetRegistry& instance();

    void registerLoader(AssetLoader loader);

    // Synchronous load (blocks until complete)
    AssetHandle load(const std::string& path);

    // Async load: submits decode to fiber job, returns handle immediately.
    // Asset not ready until processAsyncLoads() finalizes it on the main thread.
    // Falls back to sync load() if the loader has no decode callback.
    AssetHandle loadAsync(const std::string& path);

    // Call each frame from the main loop: finalizes decoded assets (GPU upload)
    void processAsyncLoads(int maxPerFrame = 4);

    // True once an async-loaded asset has been finalized
    bool isReady(AssetHandle handle) const;

    // Number of async loads currently in flight
    size_t pendingAsyncCount() const;

    template<typename T>
    T* get(AssetHandle handle) {
        if (!handle.valid()) return nullptr;
        uint32_t idx = handle.index();
        if (idx >= slots_.size()) return nullptr;
        auto& slot = slots_[idx];
        if (slot.state != AssetState::Ready || slot.generation != handle.generation())
            return nullptr;
        return static_cast<T*>(slot.data);
    }

    void queueReload(const std::string& path);
    void processReloads(float currentTime);

    AssetHandle find(const std::string& path) const;
    void clear();

    // Debug info
    size_t assetCount() const;

    // ---- VFS integration (Phase 1: plumbing only) --------------------------
    // Install the active asset source. Takes ownership. Pass nullptr to clear.
    // Phase 1 stores the pointer but does not yet route loader IO through it;
    // that migration is Phase 2 (loaders) + Phase 3 (scene loader) + Phase 4
    // (audio). When source() returns non-null and FATE_USE_VFS is enabled at
    // build time, future phases will prefer source->readBytes() over fopen.
    void setSource(std::unique_ptr<IAssetSource> source);
    IAssetSource* source();
    const IAssetSource* source() const;

    // Logical asset key: forward-slash, no filesystem resolution. Phase 2
    // flipped pathToIndex_ to use this — keys look like "assets/foo.png",
    // not "C:/.../assets/foo.png". FileWatcher callback updated together to
    // produce matching keys.
    static std::string toAssetKey(std::string_view path);

    // Read raw bytes for an asset key via the installed IAssetSource.
    // Loaders MUST call this instead of fopen so packaged .pak builds work
    // with FATE_USE_VFS=ON. Thread-safe (sources promise read methods are).
    // Returns nullopt on miss / IO error / no source installed.
    static std::optional<std::vector<uint8_t>> readBytes(std::string_view assetKey);
    static std::optional<std::string> readText(std::string_view assetKey);

    // Recent-reads ring (latest first). Used by the `/vfs_status` GM command
    // to help answer "why is this texture not loading." Thread-safe.
    struct ReadLogEntry {
        std::string key;
        size_t bytes = 0;  // 0 on miss/error
        bool ok = false;
    };
    static std::vector<ReadLogEntry> recentReads();

private:
    AssetRegistry();

    struct AsyncDecodeRequest {
        uint32_t slotIndex = 0;
        std::string path;
        void* (*decode)(const std::string&) = nullptr;
        void* (*upload)(void*) = nullptr;
        void (*destroyDecoded)(void*) = nullptr;
        void* result = nullptr;
        std::atomic<bool> complete{false};
        bool failed = false;
    };

    static void asyncDecodeJobFunc(void* param);

    std::vector<AssetSlot> slots_;       // slot 0 reserved
    std::vector<uint32_t> freeList_;
    std::unordered_map<std::string, uint32_t> pathToIndex_;
    std::unordered_set<std::string> failedPaths_;  // prevents re-attempting known-bad loads
    std::vector<AssetLoader> loaders_;

    std::mutex reloadQueueMutex_;
    struct PendingReload {
        std::string path;
        float lastEventTime = 0.0f;
    };
    std::vector<PendingReload> pendingReloads_;
    static constexpr float kDebounceDelay = 0.3f;

    std::vector<std::unique_ptr<AsyncDecodeRequest>> activeDecodes_;

    std::unique_ptr<IAssetSource> source_;

    const AssetLoader* findLoader(const std::string& path) const;
    uint32_t allocSlot();
};

} // namespace fate
