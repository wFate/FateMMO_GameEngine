#pragma once
#include "engine/render/gfx/types.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include <mutex>
#include <vector>

namespace fate {

class Texture {
public:
    Texture() = default;
    ~Texture();

    bool loadFromFile(const std::string& path);
    bool reloadFromFile(const std::string& path);
    bool loadFromMemory(const unsigned char* data, int width, int height, int channels);

    void bind(unsigned int slot = 0) const;
    void unbind() const;

    unsigned int id() const { return textureId_; }
    int width() const { return width_; }
    int height() const { return height_; }
    const std::string& path() const { return path_; }
    gfx::TextureHandle gfxHandle() const { return gfxHandle_; }

private:
    unsigned int textureId_ = 0;
    gfx::TextureHandle gfxHandle_{};
    int width_ = 0;
    int height_ = 0;
    std::string path_;
};

// Centralized texture cache - avoids loading the same image twice
class TextureCache {
public:
    static TextureCache& instance() {
        static TextureCache s_instance;
        return s_instance;
    }

    std::shared_ptr<Texture> load(const std::string& path);
    std::shared_ptr<Texture> get(const std::string& path) const;
    void clear();

    // LRU eviction
    void setVRAMBudget(size_t bytes) { vramBudget_ = bytes; }
    size_t vramBudget() const { return vramBudget_; }
    size_t estimatedVRAM() const { return estimatedVRAM_; }
    void touch(const std::string& path);
    void evictIfOverBudget();
    size_t entryCount() const { return cache_.size(); }
    void advanceFrame() { ++frameCounter_; }

    // Async loading: decode on worker, upload on main thread
    struct PendingUpload {
        std::string path;
        std::vector<unsigned char> pixelData;
        int width = 0;
        int height = 0;
        int channels = 0;
    };

    void requestAsyncLoad(const std::string& path);
    void processUploads(int maxPerFrame = 2);
    bool hasPendingLoads() const;

private:
    struct CacheEntry {
        std::shared_ptr<Texture> texture;
        uint64_t lastAccessFrame = 0;
        size_t estimatedBytes = 0;
    };

    std::unordered_map<std::string, CacheEntry> cache_;
    size_t vramBudget_ = 512 * 1024 * 1024; // 512MB default
    size_t estimatedVRAM_ = 0;
    uint64_t frameCounter_ = 0;

    std::mutex uploadMutex_;
    std::vector<PendingUpload> pendingUploads_;
    std::shared_ptr<Texture> placeholderTexture_;

    void ensurePlaceholder();
};

} // namespace fate
