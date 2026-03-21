#pragma once
#include "engine/render/gfx/types.h"
#include <string>
#include <unordered_map>
#include <memory>
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
    std::unordered_map<std::string, std::shared_ptr<Texture>> cache_;

    std::mutex uploadMutex_;
    std::vector<PendingUpload> pendingUploads_;
    std::shared_ptr<Texture> placeholderTexture_;

    void ensurePlaceholder();
};

} // namespace fate
