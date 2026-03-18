#include "engine/render/texture.h"
#include "engine/render/gfx/device.h"
#include "engine/render/gfx/backend/gl/gl_loader.h"
#include "engine/core/logger.h"
#include "engine/asset/asset_registry.h"
#include "stb_image.h"

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
    if (it != cache_.end()) return it->second;

    // Delegate to AssetRegistry for actual loading
    AssetHandle h = AssetRegistry::instance().load(path);
    Texture* raw = AssetRegistry::instance().get<Texture>(h);
    if (!raw) return nullptr;

    // Non-owning shared_ptr — AssetRegistry owns the lifetime
    auto tex = std::shared_ptr<Texture>(raw, [](Texture*){});
    cache_[path] = tex;
    return tex;
}

std::shared_ptr<Texture> TextureCache::get(const std::string& path) const {
    auto it = cache_.find(path);
    return (it != cache_.end()) ? it->second : nullptr;
}

void TextureCache::clear() {
    cache_.clear();
}

} // namespace fate
