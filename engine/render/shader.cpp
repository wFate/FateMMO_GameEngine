#include "engine/render/shader.h"
#include "engine/render/gfx/device.h"
#include "engine/render/gfx/backend/gl/gl_loader.h"
#include "engine/core/logger.h"
#include <fstream>
#include <sstream>

namespace fate {

Shader::~Shader() {
    if (gfxHandle_.valid()) {
        gfx::Device::instance().destroy(gfxHandle_);
    }
}

bool Shader::loadFromFile(const std::string& vertPath, const std::string& fragPath) {
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

    vertPath_ = vertPath;
    fragPath_ = fragPath;
    return loadFromSource(vertStream.str(), fragStream.str());
}

bool Shader::reloadFromFile(const std::string& vertPath, const std::string& fragPath) {
    std::ifstream vertFile(vertPath);
    std::ifstream fragFile(fragPath);
    if (!vertFile.is_open() || !fragFile.is_open()) {
        LOG_ERROR("Shader", "Reload: cannot open shader files");
        return false;
    }

    std::stringstream vertStream, fragStream;
    vertStream << vertFile.rdbuf();
    fragStream << fragFile.rdbuf();

    // Save old handle in case loadFromSource fails
    gfx::ShaderHandle oldHandle = gfxHandle_;
    unsigned int oldProgram = programId_;
    gfxHandle_ = {};
    programId_ = 0;

    if (!loadFromSource(vertStream.str(), fragStream.str())) {
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
}

bool Shader::loadFromSource(const std::string& vertSrc, const std::string& fragSrc) {
    auto& device = gfx::Device::instance();
    gfx::ShaderHandle handle = device.createShader(vertSrc, fragSrc);
    if (!handle.valid()) {
        LOG_ERROR("Shader", "Device::createShader failed");
        return false;
    }

    gfxHandle_ = handle;
    programId_ = device.resolveGLShader(handle);

    LOG_INFO("Shader", "Shader program %u linked successfully", programId_);
    return true;
}

void Shader::bind() const {
    glUseProgram(programId_);
}

void Shader::unbind() const {
    glUseProgram(0);
}

void Shader::setInt(const std::string& name, int value) {
    glUniform1i(getUniformLocation(name), value);
}

void Shader::setFloat(const std::string& name, float value) {
    glUniform1f(getUniformLocation(name), value);
}

void Shader::setVec2(const std::string& name, const Vec2& value) {
    glUniform2f(getUniformLocation(name), value.x, value.y);
}

void Shader::setVec3(const std::string& name, const Vec3& value) {
    glUniform3f(getUniformLocation(name), value.x, value.y, value.z);
}

void Shader::setVec4(const std::string& name, float x, float y, float z, float w) {
    glUniform4f(getUniformLocation(name), x, y, z, w);
}

void Shader::setMat4(const std::string& name, const Mat4& value) {
    glUniformMatrix4fv(getUniformLocation(name), 1, GL_FALSE, value.data());
}

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

} // namespace fate
