#pragma once
#include "engine/core/types.h"
#include <string>
#include <unordered_map>

namespace fate {

class Shader {
public:
    Shader() = default;
    ~Shader();

    bool loadFromFile(const std::string& vertPath, const std::string& fragPath);
    bool loadFromSource(const std::string& vertSrc, const std::string& fragSrc);

    void bind() const;
    void unbind() const;

    void setInt(const std::string& name, int value);
    void setFloat(const std::string& name, float value);
    void setVec2(const std::string& name, const Vec2& value);
    void setVec3(const std::string& name, const Vec3& value);
    void setVec4(const std::string& name, float x, float y, float z, float w);
    void setMat4(const std::string& name, const Mat4& value);

    unsigned int id() const { return programId_; }

private:
    unsigned int programId_ = 0;
    std::unordered_map<std::string, int> uniformCache_;

    int getUniformLocation(const std::string& name);
    bool compileShader(unsigned int shader, const std::string& source, const char* type);
};

} // namespace fate
