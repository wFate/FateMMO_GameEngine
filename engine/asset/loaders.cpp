#include "engine/asset/loaders.h"
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
// Texture Loader
// ============================================================================

static void* textureLoad(const std::string& path) {
    auto* tex = new Texture();
    if (!tex->loadFromFile(path)) {
        delete tex;
        return nullptr;
    }
    return tex;
}

static bool textureReload(void* existing, const std::string& path) {
    return static_cast<Texture*>(existing)->reloadFromFile(path);
}

static bool textureValidate(const std::string& path) {
    // KTX files: check for valid identifier (first 12 bytes)
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".ktx") {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) return false;
        uint8_t id[12];
        f.read(reinterpret_cast<char*>(id), 12);
        static constexpr uint8_t KTX_ID[12] = {
            0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
        };
        return f.good() && std::memcmp(id, KTX_ID, 12) == 0;
    }
    int w, h, c;
    return stbi_info(path.c_str(), &w, &h, &c) != 0;
}

static void textureDestroy(void* data) {
    delete static_cast<Texture*>(data);
}

// ---- Async decode/upload pipeline for textures ----------------------------

// Intermediate data produced by fiber decode, consumed by main-thread upload.
struct DecodedTexture {
    std::vector<unsigned char> data;
    int width = 0, height = 0;
    int channels = 0;           // 0 for compressed formats
    bool compressed = false;
    gfx::TextureFormat format = gfx::TextureFormat::RGBA8;
};

// KTX1 header (duplicated here to keep decode self-contained on the fiber)
static constexpr uint8_t ASYNC_KTX1_ID[12] = {
    0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
};
struct KTXHeaderLocal {
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

static gfx::TextureFormat asyncGLFmtToTexFmt(uint32_t glFmt) {
    switch (glFmt) {
        case 0x9278: return gfx::TextureFormat::ETC2_RGBA8;
        case 0x93B0: return gfx::TextureFormat::ASTC_4x4_RGBA;
        case 0x93B7: return gfx::TextureFormat::ASTC_8x8_RGBA;
        default:     return gfx::TextureFormat::RGBA8;
    }
}

// Fiber-safe: decodes image from disk (no GPU calls).
static void* textureDecodeAsync(const std::string& path) {
    namespace fs = std::filesystem;
    std::string actualPath = path;

    // On mobile, prefer .ktx compressed variant
#ifdef FATEMMO_MOBILE
    if (path.size() < 4 || path.substr(path.size() - 4) != ".ktx") {
        auto ktxPath = (fs::path(path).parent_path() / fs::path(path).stem()).string() + ".ktx";
        if (fs::exists(ktxPath)) actualPath = ktxPath;
    }
#endif

    bool isKtx = actualPath.size() >= 4 &&
                 actualPath.substr(actualPath.size() - 4) == ".ktx";

    if (isKtx) {
        // ---- KTX path: read header + compressed blob ----
        std::ifstream file(actualPath, std::ios::binary);
        if (!file.is_open()) return nullptr;

        KTXHeaderLocal hdr{};
        file.read(reinterpret_cast<char*>(&hdr), sizeof(KTXHeaderLocal));
        if (!file || std::memcmp(hdr.identifier, ASYNC_KTX1_ID, 12) != 0) return nullptr;
        if (hdr.endianness != 0x04030201) return nullptr;
        if (hdr.glType != 0 || hdr.glFormat != 0) return nullptr;

        gfx::TextureFormat fmt = asyncGLFmtToTexFmt(hdr.glInternalFormat);
        if (!gfx::isCompressedFormat(fmt)) return nullptr;

        // GPU capability check (read-only singleton, safe from any thread)
        auto& caps = GPUCompressedFormats::instance();
        if (fmt == gfx::TextureFormat::ETC2_RGBA8 && !caps.etc2) return nullptr;
        if ((fmt == gfx::TextureFormat::ASTC_4x4_RGBA ||
             fmt == gfx::TextureFormat::ASTC_8x8_RGBA) && !caps.astc) return nullptr;

        file.seekg(hdr.bytesOfKeyValueData, std::ios::cur);

        uint32_t imageSize = 0;
        file.read(reinterpret_cast<char*>(&imageSize), 4);
        if (!file || imageSize == 0 || imageSize > 64 * 1024 * 1024) return nullptr;

        auto* decoded = new DecodedTexture();
        decoded->data.resize(imageSize);
        file.read(reinterpret_cast<char*>(decoded->data.data()), imageSize);
        if (!file) { delete decoded; return nullptr; }

        decoded->width = static_cast<int>(hdr.pixelWidth);
        decoded->height = static_cast<int>(hdr.pixelHeight);
        decoded->compressed = true;
        decoded->format = fmt;
        return decoded;
    }

    // ---- Regular image path: stbi_load (CPU decode) ----
#ifdef FATEMMO_METAL
    stbi_set_flip_vertically_on_load_thread(0);
#else
    stbi_set_flip_vertically_on_load_thread(1);
#endif
    int w, h, ch;
    unsigned char* pixels = stbi_load(actualPath.c_str(), &w, &h, &ch, 4);
    if (!pixels) return nullptr;

    auto* decoded = new DecodedTexture();
    decoded->width = w;
    decoded->height = h;
    decoded->channels = 4;
    decoded->data.assign(pixels, pixels + (w * h * 4));
    stbi_image_free(pixels);
    return decoded;
}

// Main-thread only: creates GPU texture from decoded data, consumes decoded.
static void* textureUploadToGPU(void* raw) {
    auto* decoded = static_cast<DecodedTexture*>(raw);

    auto* tex = new Texture();
    bool ok = false;
    if (decoded->compressed) {
        ok = tex->loadFromMemoryCompressed(
            decoded->data.data(), decoded->data.size(),
            decoded->width, decoded->height, decoded->format);
    } else {
        ok = tex->loadFromMemory(
            decoded->data.data(), decoded->width, decoded->height, decoded->channels);
    }

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
    try {
        std::ifstream file(path);
        if (!file.is_open()) return nullptr;
        auto* j = new nlohmann::json();
        *j = nlohmann::json::parse(file);
        return j;
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR("JsonLoader", "Parse failed: %s --%s", path.c_str(), e.what());
        return nullptr;
    }
}

static bool jsonReload(void* existing, const std::string& path) {
    try {
        std::ifstream file(path);
        if (!file.is_open()) return false;
        auto temp = nlohmann::json::parse(file);
        *static_cast<nlohmann::json*>(existing) = std::move(temp);
        return true;
    } catch (...) {
        return false;
    }
}

static bool jsonValidate(const std::string& path) {
    try {
        std::ifstream file(path);
        if (!file.is_open()) return false;
        nlohmann::json::parse(file);
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
        .decode = jsonLoad,  // JSON is CPU-only; decode result IS the final asset
    };
}

// ============================================================================
// Shader Loader
// ============================================================================

static std::string inferPartnerPath(const std::string& path) {
    namespace fs = std::filesystem;
    auto ext = fs::path(path).extension().string();
    auto stem = fs::path(path).parent_path() / fs::path(path).stem();
    if (ext == ".vert") return stem.string() + ".frag";
    if (ext == ".frag") return stem.string() + ".vert";
    return "";
}

static void* shaderLoad(const std::string& path) {
    namespace fs = std::filesystem;
    auto ext = fs::path(path).extension().string();
    std::string vertPath, fragPath;
    if (ext == ".vert") {
        vertPath = path;
        fragPath = inferPartnerPath(path);
    } else if (ext == ".frag") {
        fragPath = path;
        vertPath = inferPartnerPath(path);
    } else {
        auto stem = fs::path(path).parent_path() / fs::path(path).stem();
        vertPath = stem.string() + ".vert";
        fragPath = stem.string() + ".frag";
    }

    auto* shader = new Shader();
    if (!shader->loadFromFile(vertPath, fragPath)) {
        delete shader;
        return nullptr;
    }
    return shader;
}

static bool shaderReload(void* existing, const std::string& path) {
    auto* shader = static_cast<Shader*>(existing);
    return shader->reloadFromFile(shader->vertPath(), shader->fragPath());
}

static bool shaderValidate(const std::string& path) {
    std::ifstream file(path);
    return file.is_open() && file.peek() != std::ifstream::traits_type::eof();
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
