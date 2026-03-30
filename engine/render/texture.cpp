#include "engine/render/texture.h"
#include "engine/render/gfx/device.h"
#ifndef FATEMMO_METAL
#include "engine/render/gfx/backend/gl/gl_loader.h"
#endif
#include "engine/core/logger.h"
#include "engine/asset/asset_registry.h"
#include "stb_image.h"
#include <fstream>
#include <cstring>
#include <filesystem>

namespace fate {

// ============================================================================
// GPU compressed texture format detection
// ============================================================================
GPUCompressedFormats& GPUCompressedFormats::instance() {
    static GPUCompressedFormats s;
    return s;
}

void GPUCompressedFormats::detect() {
#ifdef FATEMMO_METAL
    // All Apple Silicon supports ETC2 and ASTC natively
    etc2 = true;
    astc = true;
    LOG_INFO("GPUCaps", "Compressed textures: ETC2=yes ASTC=yes (Metal/Apple Silicon)");
#else
    // ETC2 is mandatory in GLES 3.0, optional on desktop GL 4.3+
    // ASTC is an extension (GL_KHR_texture_compression_astc_ldr)
    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));

    // On GLES 3.0+, ETC2 is always available (core)
#ifdef FATEMMO_GLES
    etc2 = true;
#else
    // On desktop, check GL version >= 4.3 or extension
    if (extensions && strstr(extensions, "GL_ARB_ES3_compatibility")) {
        etc2 = true;
    }
#endif

    if (extensions && strstr(extensions, "GL_KHR_texture_compression_astc_ldr")) {
        astc = true;
    }

    LOG_INFO("GPUCaps", "Compressed textures: ETC2=%s ASTC=%s (GL: %s)",
             etc2 ? "yes" : "no", astc ? "yes" : "no", version ? version : "?");
#endif
}

// ============================================================================
// KTX1 file format constants
// ============================================================================
static constexpr uint8_t KTX1_IDENTIFIER[12] = {
    0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
};

struct KTXHeader {
    uint8_t  identifier[12];
    uint32_t endianness;
    uint32_t glType;
    uint32_t glTypeSize;
    uint32_t glFormat;
    uint32_t glInternalFormat;
    uint32_t glBaseInternalFormat;
    uint32_t pixelWidth;
    uint32_t pixelHeight;
    uint32_t pixelDepth;
    uint32_t numberOfArrayElements;
    uint32_t numberOfFaces;
    uint32_t numberOfMipmapLevels;
    uint32_t bytesOfKeyValueData;
};

static gfx::TextureFormat glInternalFormatToTextureFormat(uint32_t glFmt) {
    switch (glFmt) {
        case 0x9278: return gfx::TextureFormat::ETC2_RGBA8;     // GL_COMPRESSED_RGBA8_ETC2_EAC
        case 0x93B0: return gfx::TextureFormat::ASTC_4x4_RGBA;  // GL_COMPRESSED_RGBA_ASTC_4x4_KHR
        case 0x93B7: return gfx::TextureFormat::ASTC_8x8_RGBA;  // GL_COMPRESSED_RGBA_ASTC_8x8_KHR
        default:     return gfx::TextureFormat::RGBA8;           // unknown — caller should check
    }
}

Texture::~Texture() {
    if (gfxHandle_.valid()) {
        gfx::Device::instance().destroy(gfxHandle_);
    }
}

bool Texture::loadFromFile(const std::string& path) {
    // On mobile, try loading a .ktx compressed version first
#ifdef FATEMMO_MOBILE
    {
        namespace fs = std::filesystem;
        auto ktxPath = (fs::path(path).parent_path() / fs::path(path).stem()).string() + ".ktx";
        if (fs::exists(ktxPath) && loadFromKTX(ktxPath)) {
            return true;
        }
    }
#endif
    // If the path itself is a .ktx file, load it directly
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".ktx") {
        return loadFromKTX(path);
    }

#ifdef FATEMMO_METAL
    stbi_set_flip_vertically_on_load(false);  // Metal: top-left origin
#else
    stbi_set_flip_vertically_on_load(true);   // GL: bottom-left origin
#endif
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

bool Texture::loadFromKTX(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("Texture", "KTX: cannot open %s", path.c_str());
        return false;
    }

    // Read and validate header
    KTXHeader hdr{};
    file.read(reinterpret_cast<char*>(&hdr), sizeof(KTXHeader));
    if (!file || std::memcmp(hdr.identifier, KTX1_IDENTIFIER, 12) != 0) {
        LOG_ERROR("Texture", "KTX: invalid header in %s", path.c_str());
        return false;
    }

    // Only support little-endian KTX files (0x04030201)
    if (hdr.endianness != 0x04030201) {
        LOG_ERROR("Texture", "KTX: big-endian not supported in %s", path.c_str());
        return false;
    }

    // Must be a compressed format (glType == 0 and glFormat == 0 for compressed)
    if (hdr.glType != 0 || hdr.glFormat != 0) {
        LOG_ERROR("Texture", "KTX: not a compressed texture (%s)", path.c_str());
        return false;
    }

    // Map GL internal format to our enum
    gfx::TextureFormat fmt = glInternalFormatToTextureFormat(hdr.glInternalFormat);
    if (!gfx::isCompressedFormat(fmt)) {
        LOG_ERROR("Texture", "KTX: unsupported compressed format 0x%X in %s",
                  hdr.glInternalFormat, path.c_str());
        return false;
    }

    // Check GPU support
    auto& caps = GPUCompressedFormats::instance();
    if (fmt == gfx::TextureFormat::ETC2_RGBA8 && !caps.etc2) {
        LOG_WARN("Texture", "KTX: ETC2 not supported on this GPU, skipping %s", path.c_str());
        return false;
    }
    if ((fmt == gfx::TextureFormat::ASTC_4x4_RGBA || fmt == gfx::TextureFormat::ASTC_8x8_RGBA) && !caps.astc) {
        LOG_WARN("Texture", "KTX: ASTC not supported on this GPU, skipping %s", path.c_str());
        return false;
    }

    // Skip key-value data
    file.seekg(hdr.bytesOfKeyValueData, std::ios::cur);

    // Read first mipmap level (we only use level 0)
    uint32_t imageSize = 0;
    file.read(reinterpret_cast<char*>(&imageSize), 4);
    if (!file || imageSize == 0 || imageSize > 64 * 1024 * 1024) {
        LOG_ERROR("Texture", "KTX: invalid image size %u in %s", imageSize, path.c_str());
        return false;
    }

    std::vector<uint8_t> compressedData(imageSize);
    file.read(reinterpret_cast<char*>(compressedData.data()), imageSize);
    if (!file) {
        LOG_ERROR("Texture", "KTX: truncated data in %s", path.c_str());
        return false;
    }

    // Upload to GPU
    width_ = static_cast<int>(hdr.pixelWidth);
    height_ = static_cast<int>(hdr.pixelHeight);
    format_ = fmt;

    auto& device = gfx::Device::instance();
    gfxHandle_ = device.createCompressedTexture(width_, height_, fmt,
                                                 compressedData.data(), imageSize);
    if (!gfxHandle_.valid()) {
        LOG_ERROR("Texture", "KTX: GPU upload failed for %s", path.c_str());
        return false;
    }

#ifndef FATEMMO_METAL
    textureId_ = device.resolveGLTexture(gfxHandle_);
#endif
    path_ = path;

    LOG_INFO("Texture", "KTX loaded %s (%dx%d, fmt=0x%X, %zu bytes)",
             path.c_str(), width_, height_, hdr.glInternalFormat, imageSize);
    return true;
}

bool Texture::reloadFromFile(const std::string& path) {
#ifdef FATEMMO_METAL
    stbi_set_flip_vertically_on_load(false);  // Metal: top-left origin
#else
    stbi_set_flip_vertically_on_load(true);   // GL: bottom-left origin
#endif
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
#ifndef FATEMMO_METAL
        textureId_ = 0;
#endif
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

#ifndef FATEMMO_METAL
    textureId_ = device.resolveGLTexture(gfxHandle_);
#endif
    return true;
}

bool Texture::loadFromMemoryCompressed(const unsigned char* data, size_t dataSize,
                                       int width, int height, gfx::TextureFormat fmt) {
    width_ = width;
    height_ = height;
    format_ = fmt;

    auto& device = gfx::Device::instance();
    gfxHandle_ = device.createCompressedTexture(width, height, fmt, data, dataSize);
    if (!gfxHandle_.valid()) {
        LOG_ERROR("Texture", "createCompressedTexture failed");
        return false;
    }

#ifndef FATEMMO_METAL
    textureId_ = device.resolveGLTexture(gfxHandle_);
#endif
    return true;
}

void Texture::bind(unsigned int slot) const {
#ifndef FATEMMO_METAL
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, textureId_);
#endif
}

void Texture::unbind() const {
#ifndef FATEMMO_METAL
    glBindTexture(GL_TEXTURE_2D, 0);
#endif
}

void Texture::setFilter(bool linear) {
#ifndef FATEMMO_METAL
    if (!textureId_) return;
    GLenum filter = linear ? GL_LINEAR : GL_NEAREST;
    glBindTexture(GL_TEXTURE_2D, textureId_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glBindTexture(GL_TEXTURE_2D, 0);
#endif
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
    size_t bytes = gfx::estimateTextureBytes(tex->width(), tex->height(), tex->format());
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
    cache_[path] = CacheEntry{placeholderTexture_};

    // Submit decode to fiber job via AssetRegistry; GPU upload happens in processUploads
    AssetHandle h = AssetRegistry::instance().loadAsync(path);
    if (h.valid()) {
        pendingHandles_.push_back({path, h});
    }
}

void TextureCache::processUploads(int maxPerFrame) {
    // Finalize any completed async decodes (GPU upload on main thread)
    AssetRegistry::instance().processAsyncLoads(maxPerFrame);

    // Check pending handles for newly-ready textures
    auto it = pendingHandles_.begin();
    while (it != pendingHandles_.end()) {
        auto& reg = AssetRegistry::instance();
        if (reg.isReady(it->second)) {
            Texture* raw = reg.get<Texture>(it->second);
            if (raw) {
                // Non-owning shared_ptr — AssetRegistry owns the lifetime
                auto tex = std::shared_ptr<Texture>(raw, [](Texture*){});
                size_t bytes = gfx::estimateTextureBytes(tex->width(), tex->height(), tex->format());
                cache_[it->first] = {tex, frameCounter_, bytes};
                estimatedVRAM_ += bytes;
                LOG_DEBUG("Texture", "Async uploaded %s (%dx%d)",
                          it->first.c_str(), raw->width(), raw->height());
            }
            it = pendingHandles_.erase(it);
        } else {
            ++it;
        }
    }

    evictIfOverBudget();
}

bool TextureCache::hasPendingLoads() const {
    return !pendingHandles_.empty();
}

} // namespace fate
