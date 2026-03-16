#include "engine/render/shader.h"
#include "engine/render/gl_loader.h"
#include "engine/core/logger.h"
#include <fstream>
#include <sstream>

namespace fate {

Shader::~Shader() {
    if (programId_) {
        glDeleteProgram(programId_);
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

    return loadFromSource(vertStream.str(), fragStream.str());
}

bool Shader::loadFromSource(const std::string& vertSrc, const std::string& fragSrc) {
    unsigned int vert = glCreateShader(GL_VERTEX_SHADER);
    unsigned int frag = glCreateShader(GL_FRAGMENT_SHADER);

    if (!compileShader(vert, vertSrc, "VERTEX")) {
        glDeleteShader(vert);
        glDeleteShader(frag);
        return false;
    }
    if (!compileShader(frag, fragSrc, "FRAGMENT")) {
        glDeleteShader(vert);
        glDeleteShader(frag);
        return false;
    }

    programId_ = glCreateProgram();
    glAttachShader(programId_, vert);
    glAttachShader(programId_, frag);
    glLinkProgram(programId_);

    GLint success;
    glGetProgramiv(programId_, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        glGetProgramInfoLog(programId_, 1024, nullptr, infoLog);
        LOG_ERROR("Shader", "Program link error:\n%s", infoLog);
        glDeleteProgram(programId_);
        programId_ = 0;
        glDeleteShader(vert);
        glDeleteShader(frag);
        return false;
    }

    glDeleteShader(vert);
    glDeleteShader(frag);

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

bool Shader::compileShader(unsigned int shader, const std::string& source, const char* type) {
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        glGetShaderInfoLog(shader, 1024, nullptr, infoLog);
        LOG_ERROR("Shader", "%s shader compile error:\n%s", type, infoLog);
        return false;
    }
    return true;
}

} // namespace fate
