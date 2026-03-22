#include "engine/render/gfx/device.h"
#include "engine/render/gfx/backend/gl/gl_loader.h"
#include "engine/core/logger.h"

#include <fstream>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <functional>
#include <cstring>

namespace gfx {

// ============================================================================
// Impl
// ============================================================================
struct Device::Impl {
    uint32_t nextId = 1;

    std::unordered_map<uint32_t, GLuint>                shaders;        // id -> GL program
    std::unordered_map<uint32_t, GLuint>                textures;       // id -> GL texture
    std::unordered_map<uint32_t, GLuint>                buffers;        // id -> GL buffer
    std::unordered_map<uint32_t, BufferType>            bufferTypes;    // id -> type
    std::unordered_map<uint32_t, GLuint>                framebuffers;   // id -> GL FBO
    std::unordered_map<uint32_t, GLuint>                fbTextures;     // id -> GL FBO color tex
    std::unordered_map<uint32_t, GLuint>                fbRenderbuffers;// id -> GL RBO
    std::unordered_map<uint32_t, std::pair<int,int>>    fbSizes;        // id -> (w,h)
    std::unordered_map<uint32_t, uint32_t>              fbTextureHandles; // fbo id -> tex handle id
    std::unordered_map<uint32_t, PipelineDesc>          pipelines;      // id -> desc
    std::unordered_map<uint32_t, GLuint>                pipelineVAOs;   // id -> GL VAO
    std::unordered_map<uint64_t, int>                   uniformCache;   // key -> location

    uint32_t allocId() { return nextId++; }
};

// ============================================================================
// Helpers (file-local)
// ============================================================================
static GLenum toGLBufferTarget(BufferType t) {
    switch (t) {
        case BufferType::Vertex:  return GL_ARRAY_BUFFER;
        case BufferType::Index:   return GL_ELEMENT_ARRAY_BUFFER;
        case BufferType::Uniform: return GL_ARRAY_BUFFER; // GL 3.3 UBOs handled separately later
    }
    return GL_ARRAY_BUFFER;
}

static GLenum toGLBufferUsage(BufferUsage u) {
    switch (u) {
        case BufferUsage::Static:  return GL_STATIC_DRAW;
        case BufferUsage::Dynamic: return GL_DYNAMIC_DRAW;
        case BufferUsage::Stream:  return GL_STREAM_DRAW;
    }
    return GL_STATIC_DRAW;
}

static void textureFormatToGL(TextureFormat fmt, GLenum& internalFmt, GLenum& pixelFmt, GLenum& pixelType) {
    switch (fmt) {
        case TextureFormat::RGBA8:
            internalFmt = GL_RGBA8;
            pixelFmt    = GL_RGBA;
            pixelType   = GL_UNSIGNED_BYTE;
            break;
        case TextureFormat::RGB8:
            internalFmt = GL_RGB8;
            pixelFmt    = GL_RGB;
            pixelType   = GL_UNSIGNED_BYTE;
            break;
        case TextureFormat::R8:
            internalFmt = GL_R8;
            pixelFmt    = GL_RED;
            pixelType   = GL_UNSIGNED_BYTE;
            break;
        case TextureFormat::Depth24Stencil8:
            internalFmt = GL_DEPTH24_STENCIL8;
            pixelFmt    = GL_DEPTH_STENCIL;
            pixelType   = GL_UNSIGNED_INT_24_8;
            break;
    }
}

static std::string readFileToString(const std::string& path) {
    std::ifstream f(path, std::ios::in);
    if (!f.is_open()) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ============================================================================
// Singleton
// ============================================================================
Device& Device::instance() {
    static Device s;
    return s;
}

// ============================================================================
// init / shutdown
// ============================================================================
bool Device::init() {
    if (impl_) return true; // already initialised
    impl_ = new Impl();
    LOG_INFO("gfx", "Device initialised");
    return true;
}

void Device::shutdown() {
    if (!impl_) return;

    // Delete all GL resources
    for (auto& [id, prog] : impl_->shaders)        glDeleteProgram(prog);
    for (auto& [id, tex]  : impl_->textures)        glDeleteTextures(1, &tex);
    for (auto& [id, buf]  : impl_->buffers)         glDeleteBuffers(1, &buf);
    for (auto& [id, rbo]  : impl_->fbRenderbuffers) glDeleteRenderbuffers(1, &rbo);
    for (auto& [id, tex]  : impl_->fbTextures)      glDeleteTextures(1, &tex);
    for (auto& [id, fbo]  : impl_->framebuffers)    glDeleteFramebuffers(1, &fbo);
    for (auto& [id, vao]  : impl_->pipelineVAOs)    glDeleteVertexArrays(1, &vao);

    delete impl_;
    impl_ = nullptr;
    LOG_INFO("gfx", "Device shut down");
}

// ============================================================================
// Shader
// ============================================================================
static GLuint compileStage(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        LOG_ERROR("gfx", "Shader compile error: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

ShaderHandle Device::createShader(const std::string& vertSrc, const std::string& fragSrc) {
    GLuint vs = compileStage(GL_VERTEX_SHADER, vertSrc.c_str());
    if (!vs) return {};

    GLuint fs = compileStage(GL_FRAGMENT_SHADER, fragSrc.c_str());
    if (!fs) { glDeleteShader(vs); return {}; }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        LOG_ERROR("gfx", "Shader link error: %s", log);
        glDeleteProgram(prog);
        return {};
    }

    uint32_t id = impl_->allocId();
    impl_->shaders[id] = prog;
    return { id };
}

ShaderHandle Device::createShaderFromFiles(const std::string& vertPath, const std::string& fragPath) {
    std::string vs = readFileToString(vertPath);
    std::string fs = readFileToString(fragPath);
    if (vs.empty()) { LOG_ERROR("gfx", "Failed to read vertex shader: %s", vertPath.c_str()); return {}; }
    if (fs.empty()) { LOG_ERROR("gfx", "Failed to read fragment shader: %s", fragPath.c_str()); return {}; }
    return createShader(vs, fs);
}

// ============================================================================
// Texture
// ============================================================================
TextureHandle Device::createTexture(int width, int height, TextureFormat format, const void* data) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    GLenum internalFmt, pixelFmt, pixelType;
    textureFormatToGL(format, internalFmt, pixelFmt, pixelType);

    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, width, height, 0, pixelFmt, pixelType, data);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    uint32_t id = impl_->allocId();
    impl_->textures[id] = tex;
    return { id };
}

TextureHandle Device::createCompressedTexture(int width, int height, TextureFormat format,
                                               const void* data, size_t dataSize) {
    // Map compressed TextureFormat to GL internal format
    GLenum internalFmt = 0;
    switch (format) {
        case TextureFormat::ETC2_RGBA8:    internalFmt = 0x9278; break; // GL_COMPRESSED_RGBA8_ETC2_EAC
        case TextureFormat::ASTC_4x4_RGBA: internalFmt = 0x93B0; break; // GL_COMPRESSED_RGBA_ASTC_4x4_KHR
        case TextureFormat::ASTC_8x8_RGBA: internalFmt = 0x93B7; break; // GL_COMPRESSED_RGBA_ASTC_8x8_KHR
        default:
            LOG_ERROR("gfx", "createCompressedTexture called with non-compressed format");
            return {};
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glCompressedTexImage2D(GL_TEXTURE_2D, 0, internalFmt, width, height, 0,
                           static_cast<GLsizei>(dataSize), data);

    // Check for GL errors (unsupported format on this GPU)
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOG_ERROR("gfx", "glCompressedTexImage2D failed (0x%X) — format 0x%X may not be supported", err, internalFmt);
        glDeleteTextures(1, &tex);
        return {};
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    uint32_t id = impl_->allocId();
    impl_->textures[id] = tex;
    return { id };
}

// ============================================================================
// Buffer
// ============================================================================
BufferHandle Device::createBuffer(BufferType type, BufferUsage usage, size_t size, const void* data) {
    GLuint buf = 0;
    glGenBuffers(1, &buf);

    GLenum target = toGLBufferTarget(type);
    glBindBuffer(target, buf);
    glBufferData(target, static_cast<GLsizeiptr>(size), data, toGLBufferUsage(usage));
    glBindBuffer(target, 0);

    uint32_t id = impl_->allocId();
    impl_->buffers[id]     = buf;
    impl_->bufferTypes[id] = type;
    return { id };
}

// ============================================================================
// Pipeline
// ============================================================================
PipelineHandle Device::createPipeline(const PipelineDesc& desc) {
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    // VAO is created empty — vertex attributes are configured at draw time

    uint32_t id = impl_->allocId();
    impl_->pipelines[id]    = desc;
    impl_->pipelineVAOs[id] = vao;
    return { id };
}

// ============================================================================
// Framebuffer
// ============================================================================
FramebufferHandle Device::createFramebuffer(int width, int height,
                                            TextureFormat colorFormat,
                                            bool withDepthStencil) {
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    // Color attachment texture
    GLuint colorTex = 0;
    glGenTextures(1, &colorTex);
    glBindTexture(GL_TEXTURE_2D, colorTex);

    GLenum internalFmt, pixelFmt, pixelType;
    textureFormatToGL(colorFormat, internalFmt, pixelFmt, pixelType);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, width, height, 0, pixelFmt, pixelType, nullptr);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);

    // Optional depth/stencil renderbuffer
    GLuint rbo = 0;
    if (withDepthStencil) {
        glGenRenderbuffers(1, &rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR("gfx", "Framebuffer incomplete: 0x%X", status);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    uint32_t id = impl_->allocId();
    impl_->framebuffers[id]    = fbo;
    impl_->fbTextures[id]      = colorTex;
    impl_->fbSizes[id]         = { width, height };
    if (rbo) impl_->fbRenderbuffers[id] = rbo;

    return { id };
}

// ============================================================================
// Resource destruction
// ============================================================================
void Device::destroy(ShaderHandle h) {
    if (!h.valid() || !impl_) return;
    auto it = impl_->shaders.find(h.id);
    if (it != impl_->shaders.end()) {
        glDeleteProgram(it->second);
        impl_->shaders.erase(it);
    }
}

void Device::destroy(TextureHandle h) {
    if (!h.valid() || !impl_) return;
    auto it = impl_->textures.find(h.id);
    if (it != impl_->textures.end()) {
        glDeleteTextures(1, &it->second);
        impl_->textures.erase(it);
    }
}

void Device::destroy(BufferHandle h) {
    if (!h.valid() || !impl_) return;
    auto it = impl_->buffers.find(h.id);
    if (it != impl_->buffers.end()) {
        glDeleteBuffers(1, &it->second);
        impl_->buffers.erase(it);
    }
    impl_->bufferTypes.erase(h.id);
}

void Device::destroy(PipelineHandle h) {
    if (!h.valid() || !impl_) return;
    auto vit = impl_->pipelineVAOs.find(h.id);
    if (vit != impl_->pipelineVAOs.end()) {
        glDeleteVertexArrays(1, &vit->second);
        impl_->pipelineVAOs.erase(vit);
    }
    impl_->pipelines.erase(h.id);
}

void Device::destroy(FramebufferHandle h) {
    if (!h.valid() || !impl_) return;

    // Delete color texture
    auto tit = impl_->fbTextures.find(h.id);
    if (tit != impl_->fbTextures.end()) {
        glDeleteTextures(1, &tit->second);
        impl_->fbTextures.erase(tit);
    }

    // Delete renderbuffer if present
    auto rit = impl_->fbRenderbuffers.find(h.id);
    if (rit != impl_->fbRenderbuffers.end()) {
        glDeleteRenderbuffers(1, &rit->second);
        impl_->fbRenderbuffers.erase(rit);
    }

    // Delete FBO
    auto fit = impl_->framebuffers.find(h.id);
    if (fit != impl_->framebuffers.end()) {
        glDeleteFramebuffers(1, &fit->second);
        impl_->framebuffers.erase(fit);
    }

    // Clean up cached texture handle and remove from textures map
    auto thit = impl_->fbTextureHandles.find(h.id);
    if (thit != impl_->fbTextureHandles.end()) {
        impl_->textures.erase(thit->second);
        impl_->fbTextureHandles.erase(thit);
    }

    impl_->fbSizes.erase(h.id);
}

// ============================================================================
// Resource updates
// ============================================================================
void Device::updateBuffer(BufferHandle h, const void* data, size_t size, size_t offset) {
    if (!h.valid() || !impl_) return;
    auto bit = impl_->buffers.find(h.id);
    if (bit == impl_->buffers.end()) return;

    GLenum target = toGLBufferTarget(impl_->bufferTypes[h.id]);
    glBindBuffer(target, bit->second);
    glBufferSubData(target, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), data);
    glBindBuffer(target, 0);
}

void Device::updateTexture(TextureHandle h, const void* data, int width, int height) {
    if (!h.valid() || !impl_) return;
    auto it = impl_->textures.find(h.id);
    if (it == impl_->textures.end()) return;

    glBindTexture(GL_TEXTURE_2D, it->second);
    // Assume RGBA8 for sub-image updates (most common case)
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ============================================================================
// Queries
// ============================================================================
TextureHandle Device::getFramebufferTexture(FramebufferHandle h) {
    if (!h.valid() || !impl_) return {};

    // Return cached handle if we already created one
    auto cached = impl_->fbTextureHandles.find(h.id);
    if (cached != impl_->fbTextureHandles.end()) {
        return { cached->second };
    }

    auto tit = impl_->fbTextures.find(h.id);
    if (tit == impl_->fbTextures.end()) return {};

    // Register the FBO's color texture in the textures map so resolveGLTexture works
    uint32_t texId = impl_->allocId();
    impl_->textures[texId] = tit->second;
    impl_->fbTextureHandles[h.id] = texId;
    return { texId };
}

void Device::getFramebufferSize(FramebufferHandle h, int& outW, int& outH) {
    if (!h.valid() || !impl_) { outW = 0; outH = 0; return; }
    auto it = impl_->fbSizes.find(h.id);
    if (it == impl_->fbSizes.end()) { outW = 0; outH = 0; return; }
    outW = it->second.first;
    outH = it->second.second;
}

// ============================================================================
// GL resolve helpers
// ============================================================================
unsigned int Device::resolveGLShader(ShaderHandle h) const {
    if (!h.valid() || !impl_) return 0;
    auto it = impl_->shaders.find(h.id);
    return (it != impl_->shaders.end()) ? it->second : 0;
}

unsigned int Device::resolveGLTexture(TextureHandle h) const {
    if (!h.valid() || !impl_) return 0;
    auto it = impl_->textures.find(h.id);
    return (it != impl_->textures.end()) ? it->second : 0;
}

unsigned int Device::resolveGLBuffer(BufferHandle h) const {
    if (!h.valid() || !impl_) return 0;
    auto it = impl_->buffers.find(h.id);
    return (it != impl_->buffers.end()) ? it->second : 0;
}

unsigned int Device::resolveGLFramebuffer(FramebufferHandle h) const {
    if (!h.valid() || !impl_) return 0;
    auto it = impl_->framebuffers.find(h.id);
    return (it != impl_->framebuffers.end()) ? it->second : 0;
}

unsigned int Device::resolveGLPipelineVAO(PipelineHandle h) const {
    if (!h.valid() || !impl_) return 0;
    auto it = impl_->pipelineVAOs.find(h.id);
    return (it != impl_->pipelineVAOs.end()) ? it->second : 0;
}

const PipelineDesc* Device::resolvePipelineDesc(PipelineHandle h) const {
    if (!h.valid() || !impl_) return nullptr;
    auto it = impl_->pipelines.find(h.id);
    return (it != impl_->pipelines.end()) ? &it->second : nullptr;
}

BufferType Device::getBufferType(BufferHandle h) const {
    if (!h.valid() || !impl_) return BufferType::Vertex;
    auto it = impl_->bufferTypes.find(h.id);
    return (it != impl_->bufferTypes.end()) ? it->second : BufferType::Vertex;
}

// ============================================================================
// Uniform location cache
// ============================================================================
int Device::getUniformLocation(ShaderHandle shader, const char* name) {
    if (!shader.valid() || !impl_) return -1;

    GLuint prog = resolveGLShader(shader);
    if (!prog) return -1;

    // Build cache key: upper 32 bits = program, lower 32 bits = name hash
    uint64_t nameHash = std::hash<std::string>{}(name);
    uint64_t key = (static_cast<uint64_t>(prog) << 32) | (nameHash & 0xFFFFFFFF);

    auto it = impl_->uniformCache.find(key);
    if (it != impl_->uniformCache.end()) return it->second;

    int loc = glGetUniformLocation(prog, name);
    impl_->uniformCache[key] = loc;
    return loc;
}

} // namespace gfx
