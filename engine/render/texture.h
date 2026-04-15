#pragma once
#include "engine/render/gfx/types.h"
#include "engine/asset/asset_handle.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include <vector>

namespace fate {

// GPU compressed texture format support flags (queried once at startup)
struct GPUCompressedFormats {
    bool etc2   = false;  // GL_COMPRESSED_RGBA8_ETC2_EAC (mandatory in GLES 3.0)
    bool astc   = false;  // GL_COMPRESSED_RGBA_ASTC_*_KHR (most mobile GPUs since ~2015)

    // Call once after GL context creation to probe supported formats.
    static GPUCompressedFormats& instance();
    void detect();
};

class Texture {
public:
    Texture() = default;
    ~Texture();

    bool loadFromFile(const std::string& path);
    bool reloadFromFile(const std::string& path);
    bool loadFromMemory(const unsigned char* data, int width, int height, int channels);
    bool loadFromMemoryCompressed(const unsigned char* data, size_t dataSize,
                                  int width, int height, gfx::TextureFormat fmt);
    bool loadFromKTX(const std::string& path);

    void bind(unsigned int slot = 0) const;
    void unbind() const;
    void setFilter(bool linear);

    unsigned int id() const {
#ifdef FATEMMO_METAL
        return 0;
#else
        return textureId_;
#endif
    }
    int width() const { return width_; }
    int height() const { return height_; }
    const std::string& path() const { return path_; }
    gfx::TextureHandle gfxHandle() const { return gfxHandle_; }
    gfx::TextureFormat format() const { return format_; }

private:
#ifndef FATEMMO_METAL
    unsigned int textureId_ = 0;
#endif
    gfx::TextureHandle gfxHandle_{};
    int width_ = 0;
    int height_ = 0;
    gfx::TextureFormat format_ = gfx::TextureFormat::RGBA8;
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

    // 1x1 magenta fallback for missing textures (created on first use)
    std::shared_ptr<Texture> placeholder();

    // Async loading: decode on fiber job, GPU upload on main thread via AssetRegistry
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

    // Pending async handles awaiting finalization
    std::vector<std::pair<std::string, AssetHandle>> pendingHandles_;
    std::shared_ptr<Texture> placeholderTexture_;

    void ensurePlaceholder();
};

} // namespace fate
