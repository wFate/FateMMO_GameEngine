#include "engine/render/shader.h"
#include "engine/render/gfx/device.h"
#ifndef FATEMMO_METAL
#include "engine/render/gfx/backend/gl/gl_loader.h"
#endif
#include "engine/core/logger.h"
#include <fstream>
#include <sstream>

namespace fate {

#ifndef FATEMMO_METAL
namespace {
    const char* getShaderPreamble(bool isFragment) {
#ifdef FATEMMO_GLES
        if (isFragment) {
            return "#version 300 es\nprecision highp float;\nprecision highp sampler2D;\nprecision highp sampler2DArray;\n";
        } else {
            return "#version 300 es\n";
        }
#else
        (void)isFragment;
        return "#version 330 core\n";
#endif
    }

    bool hasVersionDirective(const std::string& src) {
        // Skip leading whitespace/newlines
        size_t pos = src.find_first_not_of(" \t\r\n");
        if (pos == std::string::npos) return false;
        return src.compare(pos, 8, "#version") == 0;
    }

    std::string prependPreamble(const std::string& src, bool isFragment) {
        if (hasVersionDirective(src)) return src;
        return std::string(getShaderPreamble(isFragment)) + src;
    }
} // anonymous namespace
#endif // !FATEMMO_METAL

Shader::~Shader() {
    if (gfxHandle_.valid() && gfx::Device::instance().isAlive()) {
        gfx::Device::instance().destroy(gfxHandle_);
    }
}

bool Shader::loadFromFile(const std::string& vertPath, const std::string& fragPath) {
    vertPath_ = vertPath;
    fragPath_ = fragPath;

#ifdef FATEMMO_METAL
    auto& device = gfx::Device::instance();
    gfxHandle_ = device.createShaderFromFiles(vertPath, fragPath);
    if (!gfxHandle_.valid()) {
        LOG_ERROR("Shader", "Device::createShaderFromFiles failed: %s / %s",
                  vertPath.c_str(), fragPath.c_str());
        return false;
    }
    return true;
#else
    std::ifstream vertFile(vertPath);
    std::ifstream fragFile(fragPath);

    if (!vertFile.is_open()) {
        LOG_ERROR("Shader", "Cannot open vertex shader: %s", vertPath.c_str());
        return false;
    }
    if (!fragFile.is_open()) {
        LOG_ERROR("Shader", "Cannot open fragment shader: %s", fragPath.c_str());
        return false;
    }

    std::stringstream vertStream, fragStream;
    vertStream << vertFile.rdbuf();
    fragStream << fragFile.rdbuf();

    std::string vertSrc = prependPreamble(vertStream.str(), false);
    std::string fragSrc = prependPreamble(fragStream.str(), true);

    return loadFromSource(vertSrc, fragSrc);
#endif
}

bool Shader::reloadFromFile(const std::string& vertPath, const std::string& fragPath) {
#ifdef FATEMMO_METAL
    gfx::ShaderHandle oldHandle = gfxHandle_;
    gfxHandle_ = {};

    gfxHandle_ = gfx::Device::instance().createShaderFromFiles(vertPath, fragPath);
    if (!gfxHandle_.valid()) {
        gfxHandle_ = oldHandle;
        LOG_WARN("Shader", "Reload failed — keeping old Metal shader");
        return false;
    }

    if (oldHandle.valid()) {
        gfx::Device::instance().destroy(oldHandle);
    }
    vertPath_ = vertPath;
    fragPath_ = fragPath;
    LOG_INFO("Shader", "Reloaded Metal shader");
    return true;
#else
    std::ifstream vertFile(vertPath);
    std::ifstream fragFile(fragPath);
    if (!vertFile.is_open() || !fragFile.is_open()) {
        LOG_ERROR("Shader", "Reload: cannot open shader files");
        return false;
    }

    std::stringstream vertStream, fragStream;
    vertStream << vertFile.rdbuf();
    fragStream << fragFile.rdbuf();

    std::string vertSrc = prependPreamble(vertStream.str(), false);
    std::string fragSrc = prependPreamble(fragStream.str(), true);

    // Save old handle in case loadFromSource fails
    gfx::ShaderHandle oldHandle = gfxHandle_;
    unsigned int oldProgram = programId_;
    gfxHandle_ = {};
    programId_ = 0;

    if (!loadFromSource(vertSrc, fragSrc)) {
        // Restore old handle — loadFromSource already logged the error
        gfxHandle_ = oldHandle;
        programId_ = oldProgram;
        LOG_WARN("Shader", "Reload failed — keeping old program %u", programId_);
        return false;
    }

    // Success: destroy old handle, clear uniform cache (locations may differ)
    if (oldHandle.valid()) {
        gfx::Device::instance().destroy(oldHandle);
    }
    uniformCache_.clear();
    vertPath_ = vertPath;
    fragPath_ = fragPath;
    LOG_INFO("Shader", "Reloaded shader program %u", programId_);
    return true;
#endif
}

bool Shader::loadFromSource(const std::string& vertSrc, const std::string& fragSrc) {
    auto& device = gfx::Device::instance();

#ifdef FATEMMO_METAL
    gfx::ShaderHandle handle = device.createShader(vertSrc, fragSrc);
    if (!handle.valid()) {
        LOG_ERROR("Shader", "Device::createShader failed");
        return false;
    }
    gfxHandle_ = handle;
    LOG_INFO("Shader", "Metal shader library compiled successfully");
    return true;
#else
    // Ensure both sources have a version preamble (skip if already present)
    std::string vert = prependPreamble(vertSrc, false);
    std::string frag = prependPreamble(fragSrc, true);

    gfx::ShaderHandle handle = device.createShader(vert, frag);
    if (!handle.valid()) {
        LOG_ERROR("Shader", "Device::createShader failed");
        return false;
    }

    gfxHandle_ = handle;
    programId_ = device.resolveGLShader(handle);

    LOG_INFO("Shader", "Shader program %u linked successfully", programId_);
    return true;
#endif
}

void Shader::bind() const {
#ifndef FATEMMO_METAL
    glUseProgram(programId_);
#endif
}

void Shader::unbind() const {
#ifndef FATEMMO_METAL
    glUseProgram(0);
#endif
}

void Shader::setInt(const std::string& name, int value) {
#ifndef FATEMMO_METAL
    glUniform1i(getUniformLocation(name), value);
#else
    (void)name; (void)value;
#endif
}

void Shader::setFloat(const std::string& name, float value) {
#ifndef FATEMMO_METAL
    glUniform1f(getUniformLocation(name), value);
#else
    (void)name; (void)value;
#endif
}

void Shader::setVec2(const std::string& name, const Vec2& value) {
#ifndef FATEMMO_METAL
    glUniform2f(getUniformLocation(name), value.x, value.y);
#else
    (void)name; (void)value;
#endif
}

void Shader::setVec3(const std::string& name, const Vec3& value) {
#ifndef FATEMMO_METAL
    glUniform3f(getUniformLocation(name), value.x, value.y, value.z);
#else
    (void)name; (void)value;
#endif
}

void Shader::setVec4(const std::string& name, float x, float y, float z, float w) {
#ifndef FATEMMO_METAL
    glUniform4f(getUniformLocation(name), x, y, z, w);
#else
    (void)name; (void)x; (void)y; (void)z; (void)w;
#endif
}

void Shader::setMat4(const std::string& name, const Mat4& value) {
#ifndef FATEMMO_METAL
    glUniformMatrix4fv(getUniformLocation(name), 1, GL_FALSE, value.data());
#else
    (void)name; (void)value;
#endif
}

#ifndef FATEMMO_METAL
int Shader::getUniformLocation(const std::string& name) {
    auto it = uniformCache_.find(name);
    if (it != uniformCache_.end()) return it->second;

    int loc = glGetUniformLocation(programId_, name.c_str());
    if (loc == -1) {
        LOG_WARN("Shader", "Uniform '%s' not found in shader %u", name.c_str(), programId_);
    }
    uniformCache_[name] = loc;
    return loc;
}
#endif // !FATEMMO_METAL

} // namespace fate
