#include "engine/render/texture.h"
#include "engine/render/gl_loader.h"
#include "engine/core/logger.h"
#include "stb_image.h"

namespace fate {

Texture::~Texture() {
    if (textureId_) {
        glDeleteTextures(1, &textureId_);
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

    width_ = w;
    height_ = h;
    path_ = path;

    // Reuse existing GL texture name — glTexImage2D respecifies storage
    glBindTexture(GL_TEXTURE_2D, textureId_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);
    LOG_INFO("Texture", "Reloaded %s (%dx%d)", path.c_str(), w, h);
    return true;
}

bool Texture::loadFromMemory(const unsigned char* data, int width, int height, int channels) {
    width_ = width;
    height_ = height;

    glGenTextures(1, &textureId_);
    glBindTexture(GL_TEXTURE_2D, textureId_);

    // Pixel art settings: nearest-neighbor filtering, no blurring
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLenum format = (channels == 4) ? GL_RGBA : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, format, GL_UNSIGNED_BYTE, data);

    glBindTexture(GL_TEXTURE_2D, 0);
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

    auto tex = std::make_shared<Texture>();
    if (!tex->loadFromFile(path)) return nullptr;

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
