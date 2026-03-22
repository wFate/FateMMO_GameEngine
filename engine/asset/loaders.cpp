#include "engine/asset/loaders.h"
#include "engine/render/texture.h"
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

AssetLoader makeTextureLoader() {
    return {
        .kind = AssetKind::Texture,
        .load = textureLoad,
        .reload = textureReload,
        .validate = textureValidate,
        .destroy = textureDestroy,
        .extensions = {".png", ".jpg", ".bmp", ".ktx"}
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
        LOG_ERROR("JsonLoader", "Parse failed: %s — %s", path.c_str(), e.what());
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
        .extensions = {".json"}
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
