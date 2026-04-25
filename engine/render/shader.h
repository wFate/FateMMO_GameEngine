#pragma once
#include "engine/core/types.h"
#include "engine/render/gfx/types.h"
#include <string>
#include <unordered_map>

namespace fate {

class Shader {
public:
    Shader() = default;
    ~Shader();

    bool loadFromFile(const std::string& vertPath, const std::string& fragPath);
    bool reloadFromFile(const std::string& vertPath, const std::string& fragPath);
    const std::string& vertPath() const { return vertPath_; }
    const std::string& fragPath() const { return fragPath_; }
    bool loadFromSource(const std::string& vertSrc, const std::string& fragSrc);

    // Set the asset keys associated with this shader. Used by VFS-aware
    // loaders that read shader sources via AssetRegistry::readText() and
    // call loadFromSource directly — the keys are stored only for hot-
    // reload bookkeeping.
    void setPaths(const std::string& vertPath, const std::string& fragPath) {
        vertPath_ = vertPath;
        fragPath_ = fragPath;
    }

    void bind() const;
    void unbind() const;

    void setInt(const std::string& name, int value);
    void setFloat(const std::string& name, float value);
    void setVec2(const std::string& name, const Vec2& value);
    void setVec3(const std::string& name, const Vec3& value);
    void setVec4(const std::string& name, float x, float y, float z, float w);
    void setMat4(const std::string& name, const Mat4& value);

#ifndef FATEMMO_METAL
    unsigned int id() const { return programId_; }
#else
    unsigned int id() const { return 0; }
#endif
    gfx::ShaderHandle gfxHandle() const { return gfxHandle_; }

private:
#ifndef FATEMMO_METAL
    unsigned int programId_ = 0;
#endif
    gfx::ShaderHandle gfxHandle_{};
#ifndef FATEMMO_METAL
    std::unordered_map<std::string, int> uniformCache_;
#endif

#ifndef FATEMMO_METAL
    int getUniformLocation(const std::string& name);
#endif
    std::string vertPath_;
    std::string fragPath_;
};

} // namespace fate
