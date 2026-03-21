#include "engine/render/texture.h"
#include "engine/render/gfx/device.h"
#include "engine/render/gfx/backend/gl/gl_loader.h"
#include "engine/core/logger.h"
#include "engine/asset/asset_registry.h"
#include "stb_image.h"
#include <thread>

namespace fate {

Texture::~Texture() {
    if (gfxHandle_.valid()) {
        gfx::Device::instance().destroy(gfxHandle_);
    }
}

bool Texture::loadFromFile(const std::string& path) {
    stbi_set_flip_vertically_on_load(true);
    int channels;
    unsigned char* data = stbi_load(path.c_str(), &width_, &height_, &channels, 4);
    if (!data) {
        LOG_ERROR("Texture", "Failed to load: %s (%s)", path.c_str(), stbi_failure_reason());
        return false;
    }

    bool result = loadFromMemory(data, width_, height_, 4);
    stbi_image_free(data);
    path_ = path;

    if (result) {
        LOG_DEBUG("Texture", "Loaded %s (%dx%d)", path.c_str(), width_, height_);
    }
    return result;
}

bool Texture::reloadFromFile(const std::string& path) {
    stbi_set_flip_vertically_on_load(true);
    int w, h, channels;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!data) {
        LOG_ERROR("Texture", "Reload failed: %s (%s)", path.c_str(), stbi_failure_reason());
        return false;
    }

    // Destroy old handle and create a new one
    if (gfxHandle_.valid()) {
        gfx::Device::instance().destroy(gfxHandle_);
        gfxHandle_ = {};
        textureId_ = 0;
    }

    width_ = w;
    height_ = h;
    path_ = path;

    bool result = loadFromMemory(data, w, h, 4);
    stbi_image_free(data);

    if (result) {
        LOG_INFO("Texture", "Reloaded %s (%dx%d)", path.c_str(), w, h);
    }
    return result;
}

bool Texture::loadFromMemory(const unsigned char* data, int width, int height, int channels) {
    width_ = width;
    height_ = height;

    auto& device = gfx::Device::instance();
    gfx::TextureFormat fmt = (channels == 4) ? gfx::TextureFormat::RGBA8 : gfx::TextureFormat::RGB8;
    gfxHandle_ = device.createTexture(width, height, fmt, data);
    if (!gfxHandle_.valid()) {
        LOG_ERROR("Texture", "Device::createTexture failed");
        return false;
    }

    textureId_ = device.resolveGLTexture(gfxHandle_);
    return true;
}

void Texture::bind(unsigned int slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, textureId_);
}

void Texture::unbind() const {
    glBindTexture(GL_TEXTURE_2D, 0);
}

// TextureCache
std::shared_ptr<Texture> TextureCache::load(const std::string& path) {
    auto it = cache_.find(path);
    if (it != cache_.end()) {
        it->second.lastAccessFrame = frameCounter_;
        return it->second.texture;
    }

    // Delegate to AssetRegistry for actual loading
    AssetHandle h = AssetRegistry::instance().load(path);
    Texture* raw = AssetRegistry::instance().get<Texture>(h);
    if (!raw) return nullptr;

    // Non-owning shared_ptr — AssetRegistry owns the lifetime
    auto tex = std::shared_ptr<Texture>(raw, [](Texture*){});
    size_t bytes = static_cast<size_t>(tex->width()) * tex->height() * 4;
    cache_[path] = {tex, frameCounter_, bytes};
    estimatedVRAM_ += bytes;
    evictIfOverBudget();
    return tex;
}

std::shared_ptr<Texture> TextureCache::get(const std::string& path) const {
    auto it = cache_.find(path);
    return (it != cache_.end()) ? it->second.texture : nullptr;
}

void TextureCache::clear() {
    cache_.clear();
    estimatedVRAM_ = 0;
}

void TextureCache::touch(const std::string& path) {
    auto it = cache_.find(path);
    if (it != cache_.end()) {
        it->second.lastAccessFrame = frameCounter_;
    }
}

void TextureCache::evictIfOverBudget() {
    size_t target = static_cast<size_t>(vramBudget_ * 0.85); // evict to 85%
    while (estimatedVRAM_ > vramBudget_ && !cache_.empty()) {
        // Find oldest entry with refcount == 1 (only cache holds it)
        auto oldest = cache_.end();
        uint64_t oldestFrame = UINT64_MAX;
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->second.texture.use_count() <= 1 &&
                it->second.lastAccessFrame < oldestFrame) {
                oldest = it;
                oldestFrame = it->second.lastAccessFrame;
            }
        }
        if (oldest == cache_.end()) break; // everything is in use
        estimatedVRAM_ -= oldest->second.estimatedBytes;
        cache_.erase(oldest);
        if (estimatedVRAM_ <= target) break;
    }
}

void TextureCache::ensurePlaceholder() {
    if (placeholderTexture_) return;
    placeholderTexture_ = std::make_shared<Texture>();
    // Create 1x1 magenta pixel as placeholder
    unsigned char magenta[] = {255, 0, 255, 255};
    placeholderTexture_->loadFromMemory(magenta, 1, 1, 4);
}

void TextureCache::requestAsyncLoad(const std::string& path) {
    // If already cached, nothing to do
    auto it = cache_.find(path);
    if (it != cache_.end()) return;

    // Insert placeholder immediately so subsequent requests don't re-queue
    ensurePlaceholder();
    cache_[path] = placeholderTexture_;

    // Spawn a detached thread to decode the image
    // (In production, this would use the fiber job system instead)
    std::thread([this, path]() {
        int w, h, ch;
        stbi_set_flip_vertically_on_load(true);
        // stbi_load is thread-safe for different files
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4); // force RGBA
        if (!data) return;

        PendingUpload upload;
        upload.path = path;
        upload.width = w;
        upload.height = h;
        upload.channels = 4;
        upload.pixelData.assign(data, data + (w * h * 4));
        stbi_image_free(data);

        std::lock_guard<std::mutex> lock(uploadMutex_);
        pendingUploads_.push_back(std::move(upload));
    }).detach();
}

void TextureCache::processUploads(int maxPerFrame) {
    std::vector<PendingUpload> batch;
    {
        std::lock_guard<std::mutex> lock(uploadMutex_);
        int count = std::min(maxPerFrame, static_cast<int>(pendingUploads_.size()));
        if (count == 0) return;
        batch.assign(
            std::make_move_iterator(pendingUploads_.begin()),
            std::make_move_iterator(pendingUploads_.begin() + count)
        );
        pendingUploads_.erase(pendingUploads_.begin(), pendingUploads_.begin() + count);
    }

    for (auto& upload : batch) {
        auto tex = std::make_shared<Texture>();
        if (tex->loadFromMemory(upload.pixelData.data(), upload.width, upload.height, upload.channels)) {
            // Replace placeholder entry
            cache_[upload.path] = tex;
            LOG_DEBUG("Texture", "Async uploaded %s (%dx%d)", upload.path.c_str(), upload.width, upload.height);
        } else {
            LOG_ERROR("Texture", "Async upload failed for %s", upload.path.c_str());
        }
    }
}

bool TextureCache::hasPendingLoads() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(uploadMutex_));
    return !pendingUploads_.empty();
}

} // namespace fate
