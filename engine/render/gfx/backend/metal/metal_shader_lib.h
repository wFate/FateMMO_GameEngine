#pragma once
#include <string>
#include <unordered_map>

#ifdef __OBJC__
#import <Metal/Metal.h>
#endif

namespace gfx {
namespace metal {

class MetalShaderLib {
public:
    void init(void* mtlDevice);  // id<MTLDevice>
    void shutdown();

    // Compile .metal source string into the library
    bool compileSource(const std::string& source, const std::string& label = "");

    // Load pre-compiled .metallib file (FATE_SHIPPING)
    bool loadMetallib(const std::string& path);

    // Get a named function from the library
    void* getFunction(const std::string& name);  // returns id<MTLFunction>

private:
#ifdef __OBJC__
    id<MTLDevice> device_ = nil;
    id<MTLLibrary> library_ = nil;
#else
    void* device_ = nullptr;
    void* library_ = nullptr;
#endif
    std::unordered_map<std::string, void*> cache_;
};

} // namespace metal
} // namespace gfx
