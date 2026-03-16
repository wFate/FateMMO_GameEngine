#pragma once
#include <string>
#include <unordered_map>
#include <memory>

namespace fate {

class Texture {
public:
    Texture() = default;
    ~Texture();

    bool loadFromFile(const std::string& path);
    bool loadFromMemory(const unsigned char* data, int width, int height, int channels);

    void bind(unsigned int slot = 0) const;
    void unbind() const;

    unsigned int id() const { return textureId_; }
    int width() const { return width_; }
    int height() const { return height_; }
    const std::string& path() const { return path_; }

private:
    unsigned int textureId_ = 0;
    int width_ = 0;
    int height_ = 0;
    std::string path_;
};

// Centralized texture cache - avoids loading the same image twice
class TextureCache {
public:
    static TextureCache& instance() {
        static TextureCache s_instance;
        return s_instance;
    }

    std::shared_ptr<Texture> load(const std::string& path);
    std::shared_ptr<Texture> get(const std::string& path) const;
    void clear();

private:
    std::unordered_map<std::string, std::shared_ptr<Texture>> cache_;
};

} // namespace fate
