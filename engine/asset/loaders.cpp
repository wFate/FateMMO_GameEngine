#include "engine/asset/loaders.h"
#include "engine/asset/asset_source.h"
#include "engine/render/texture.h"
#include "engine/render/gfx/types.h"
#include "engine/render/shader.h"
#include "engine/core/logger.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <cstring>
#include "stb_image.h"

namespace fate {

// ============================================================================
// Texture Loader  (Phase 2 of VFS migration: all IO via AssetRegistry::readBytes)
// ============================================================================

// KTX1 magic + header (kept here so decode is self-contained on fiber threads).
static constexpr uint8_t KTX1_ID[12] = {
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

static gfx::TextureFormat glFmtToTexFmt(uint32_t glFmt) {
    switch (glFmt) {
        case 0x9278: return gfx::TextureFormat::ETC2_RGBA8;
        case 0x93B0: return gfx::TextureFormat::ASTC_4x4_RGBA;
        case 0x93B7: return gfx::TextureFormat::ASTC_8x8_RGBA;
        default:     return gfx::TextureFormat::RGBA8;
    }
}

// Intermediate decoded data shipped from fiber decode → main-thread upload.
// Kept identical across sync/async so they share the upload step.
struct DecodedTexture {
    std::vector<unsigned char> data;
    int width = 0, height = 0;
    int channels = 0;
    bool compressed = false;
    gfx::TextureFormat format = gfx::TextureFormat::RGBA8;
};

static bool isKtxKey(const std::string& key) {
    return key.size() >= 4 && key.substr(key.size() - 4) == ".ktx";
}

// On mobile, look for a sibling ".ktx" alongside any image asset key. Returns
// the original key if no compressed variant exists.
static std::string preferKtxOnMobile(const std::string& key) {
#ifdef FATEMMO_MOBILE
    if (isKtxKey(key)) return key;
    namespace fs = std::filesystem;
    auto ktxKey = (fs::path(key).parent_path() / fs::path(key).stem()).string() + ".ktx";
    std::replace(ktxKey.begin(), ktxKey.end(), '\\', '/');
    auto* src = AssetRegistry::instance().source();
    if (src && src->exists(ktxKey)) return ktxKey;
#endif
    return key;
}

// Decode a KTX1 blob (header already in `bytes`) into a DecodedTexture.
// Returns nullptr on malformed/unsupported payload or missing GPU support.
static DecodedTexture* decodeKtxBlob(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < sizeof(KTXHeader) + 4) return nullptr;
    KTXHeader hdr;
    std::memcpy(&hdr, bytes.data(), sizeof(KTXHeader));
    if (std::memcmp(hdr.identifier, KTX1_ID, 12) != 0) return nullptr;
    if (hdr.endianness != 0x04030201) return nullptr;
    if (hdr.glType != 0 || hdr.glFormat != 0) return nullptr;

    gfx::TextureFormat fmt = glFmtToTexFmt(hdr.glInternalFormat);
    if (!gfx::isCompressedFormat(fmt)) return nullptr;

    auto& caps = GPUCompressedFormats::instance();
    if (fmt == gfx::TextureFormat::ETC2_RGBA8 && !caps.etc2) return nullptr;
    if ((fmt == gfx::TextureFormat::ASTC_4x4_RGBA ||
         fmt == gfx::TextureFormat::ASTC_8x8_RGBA) && !caps.astc) return nullptr;

    size_t cursor = sizeof(KTXHeader) + hdr.bytesOfKeyValueData;
    if (cursor + 4 > bytes.size()) return nullptr;
    uint32_t imageSize = 0;
    std::memcpy(&imageSize, bytes.data() + cursor, 4);
    cursor += 4;
    if (imageSize == 0 || imageSize > 64 * 1024 * 1024) return nullptr;
    if (cursor + imageSize > bytes.size()) return nullptr;

    auto* d = new DecodedTexture();
    d->data.assign(bytes.begin() + cursor, bytes.begin() + cursor + imageSize);
    d->width = static_cast<int>(hdr.pixelWidth);
    d->height = static_cast<int>(hdr.pixelHeight);
    d->compressed = true;
    d->format = fmt;
    return d;
}

// Decode a stbi-supported image blob (PNG/JPG/BMP) into RGBA pixels.
static DecodedTexture* decodeStbiBlob(const std::vector<uint8_t>& bytes) {
#ifdef FATEMMO_METAL
    stbi_set_flip_vertically_on_load_thread(0);
#else
    stbi_set_flip_vertically_on_load_thread(1);
#endif
    int w, h, ch;
    unsigned char* pixels = stbi_load_from_memory(bytes.data(),
                                                   static_cast<int>(bytes.size()),
                                                   &w, &h, &ch, 4);
    if (!pixels) return nullptr;
    auto* d = new DecodedTexture();
    d->width = w;
    d->height = h;
    d->channels = 4;
    d->data.assign(pixels, pixels + (w * h * 4));
    stbi_image_free(pixels);
    return d;
}

static DecodedTexture* decodeTextureFromKey(const std::string& path) {
    std::string key = preferKtxOnMobile(path);
    auto bytes = AssetRegistry::readBytes(key);
    if (!bytes) {
        LOG_ERROR("TextureLoader", "readBytes failed for %s", key.c_str());
        return nullptr;
    }
    return isKtxKey(key) ? decodeKtxBlob(*bytes) : decodeStbiBlob(*bytes);
}

static void* textureLoad(const std::string& path) {
    auto* decoded = decodeTextureFromKey(path);
    if (!decoded) return nullptr;

    auto* tex = new Texture();
    bool ok = decoded->compressed
        ? tex->loadFromMemoryCompressed(decoded->data.data(), decoded->data.size(),
                                        decoded->width, decoded->height, decoded->format)
        : tex->loadFromMemory(decoded->data.data(),
                              decoded->width, decoded->height, decoded->channels);
    delete decoded;
    if (!ok) { delete tex; return nullptr; }
    return tex;
}

static bool textureReload(void* existing, const std::string& path) {
    auto* decoded = decodeTextureFromKey(path);
    if (!decoded) return false;

    auto* tex = static_cast<Texture*>(existing);
    bool ok = decoded->compressed
        ? tex->reloadFromCompressedMemory(decoded->data.data(), decoded->data.size(),
                                          decoded->width, decoded->height, decoded->format)
        : tex->reloadFromDecodedMemory(decoded->data.data(),
                                       decoded->width, decoded->height, decoded->channels);
    delete decoded;
    return ok;
}

static bool textureValidate(const std::string& path) {
    std::string key = preferKtxOnMobile(path);
    auto bytes = AssetRegistry::readBytes(key);
    if (!bytes) return false;

    if (isKtxKey(key)) {
        return bytes->size() >= 12 && std::memcmp(bytes->data(), KTX1_ID, 12) == 0;
    }
    int w, h, c;
    return stbi_info_from_memory(bytes->data(),
                                 static_cast<int>(bytes->size()),
                                 &w, &h, &c) != 0;
}

static void textureDestroy(void* data) {
    delete static_cast<Texture*>(data);
}

static void* textureDecodeAsync(const std::string& path) {
    return decodeTextureFromKey(path);
}

static void* textureUploadToGPU(void* raw) {
    auto* decoded = static_cast<DecodedTexture*>(raw);
    auto* tex = new Texture();
    bool ok = decoded->compressed
        ? tex->loadFromMemoryCompressed(decoded->data.data(), decoded->data.size(),
                                        decoded->width, decoded->height, decoded->format)
        : tex->loadFromMemory(decoded->data.data(),
                              decoded->width, decoded->height, decoded->channels);
    delete decoded;
    if (!ok) { delete tex; return nullptr; }
    return tex;
}

static void textureDestroyDecoded(void* raw) {
    delete static_cast<DecodedTexture*>(raw);
}

AssetLoader makeTextureLoader() {
    return {
        .kind = AssetKind::Texture,
        .load = textureLoad,
        .reload = textureReload,
        .validate = textureValidate,
        .destroy = textureDestroy,
        .extensions = {".png", ".jpg", ".bmp", ".ktx"},
        .decode = textureDecodeAsync,
        .upload = textureUploadToGPU,
        .destroyDecoded = textureDestroyDecoded,
    };
}

// ============================================================================
// JSON Loader
// ============================================================================

static void* jsonLoad(const std::string& path) {
    auto text = AssetRegistry::readText(path);
    if (!text) return nullptr;
    try {
        auto* j = new nlohmann::json();
        *j = nlohmann::json::parse(*text);
        return j;
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR("JsonLoader", "Parse failed: %s -- %s", path.c_str(), e.what());
        return nullptr;
    }
}

static bool jsonReload(void* existing, const std::string& path) {
    auto text = AssetRegistry::readText(path);
    if (!text) return false;
    try {
        auto temp = nlohmann::json::parse(*text);
        *static_cast<nlohmann::json*>(existing) = std::move(temp);
        return true;
    } catch (...) {
        return false;
    }
}

static bool jsonValidate(const std::string& path) {
    auto text = AssetRegistry::readText(path);
    if (!text) return false;
    try {
        (void)nlohmann::json::parse(*text);
        return true;
    } catch (...) {
        return false;
    }
}

static void jsonDestroy(void* data) {
    delete static_cast<nlohmann::json*>(data);
}

AssetLoader makeJsonLoader() {
    return {
        .kind = AssetKind::Json,
        .load = jsonLoad,
        .reload = jsonReload,
        .validate = jsonValidate,
        .destroy = jsonDestroy,
        .extensions = {".json"},
        .decode = jsonLoad,
    };
}

// ============================================================================
// Shader Loader
// ============================================================================

static std::string inferPartnerKey(const std::string& key) {
    namespace fs = std::filesystem;
    auto ext = fs::path(key).extension().string();
    auto stem = fs::path(key).parent_path() / fs::path(key).stem();
    std::string partner;
    if (ext == ".vert") partner = stem.string() + ".frag";
    else if (ext == ".frag") partner = stem.string() + ".vert";
    if (!partner.empty()) std::replace(partner.begin(), partner.end(), '\\', '/');
    return partner;
}

static void resolveShaderPair(const std::string& key,
                              std::string& vertKey, std::string& fragKey) {
    namespace fs = std::filesystem;
    auto ext = fs::path(key).extension().string();
    if (ext == ".vert") {
        vertKey = key;
        fragKey = inferPartnerKey(key);
    } else if (ext == ".frag") {
        fragKey = key;
        vertKey = inferPartnerKey(key);
    } else {
        auto stem = fs::path(key).parent_path() / fs::path(key).stem();
        vertKey = stem.string() + ".vert";
        fragKey = stem.string() + ".frag";
        std::replace(vertKey.begin(), vertKey.end(), '\\', '/');
        std::replace(fragKey.begin(), fragKey.end(), '\\', '/');
    }
}

static void* shaderLoad(const std::string& path) {
    std::string vertKey, fragKey;
    resolveShaderPair(path, vertKey, fragKey);

    auto* shader = new Shader();
#ifdef FATEMMO_METAL
    // Metal: createShaderFromFiles only derives a basename ("sprite.vert" →
    // "sprite") to look up pre-compiled functions in the shared MTLLibrary
    // already populated at App::init via loadMetalShaderLibraryFromBytes /
    // compileMetalShaderSource. No disk read happens in this path, so it is
    // VFS-safe. The library load itself is now bytes-routed (see app.cpp).
    if (!shader->loadFromFile(vertKey, fragKey)) { delete shader; return nullptr; }
#else
    auto vertSrc = AssetRegistry::readText(vertKey);
    auto fragSrc = AssetRegistry::readText(fragKey);
    if (!vertSrc || !fragSrc) {
        LOG_ERROR("ShaderLoader", "readText failed for %s / %s",
                  vertKey.c_str(), fragKey.c_str());
        delete shader;
        return nullptr;
    }
    if (!shader->loadFromSource(*vertSrc, *fragSrc)) { delete shader; return nullptr; }
    shader->setPaths(vertKey, fragKey);
#endif
    return shader;
}

static bool shaderReload(void* existing, const std::string& /*path*/) {
    auto* shader = static_cast<Shader*>(existing);
    const std::string& vertKey = shader->vertPath();
    const std::string& fragKey = shader->fragPath();
#ifdef FATEMMO_METAL
    return shader->reloadFromFile(vertKey, fragKey);
#else
    auto vertSrc = AssetRegistry::readText(vertKey);
    auto fragSrc = AssetRegistry::readText(fragKey);
    if (!vertSrc || !fragSrc) return false;
    if (!shader->loadFromSource(*vertSrc, *fragSrc)) return false;
    shader->setPaths(vertKey, fragKey);
    return true;
#endif
}

static bool shaderValidate(const std::string& path) {
    std::string vertKey, fragKey;
    resolveShaderPair(path, vertKey, fragKey);
    auto* src = AssetRegistry::instance().source();
    if (!src) return false;
    return src->exists(vertKey) && src->exists(fragKey);
}

static void shaderDestroy(void* data) {
    delete static_cast<Shader*>(data);
}

AssetLoader makeShaderLoader() {
    return {
        .kind = AssetKind::Shader,
        .load = shaderLoad,
        .reload = shaderReload,
        .validate = shaderValidate,
        .destroy = shaderDestroy,
        .extensions = {".vert", ".frag", ".glsl"}
    };
}

} // namespace fate
