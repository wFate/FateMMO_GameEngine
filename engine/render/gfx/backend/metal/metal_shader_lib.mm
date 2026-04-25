#import "engine/render/gfx/backend/metal/metal_shader_lib.h"
#import "engine/core/logger.h"
#import <Metal/Metal.h>
#import <dispatch/dispatch.h>

namespace gfx {
namespace metal {

void MetalShaderLib::init(void* mtlDevice) {
    device_ = (__bridge id<MTLDevice>)mtlDevice;
}

void MetalShaderLib::shutdown() {
    for (auto& [name, ptr] : cache_) {
        if (ptr) CFRelease(ptr);
    }
    cache_.clear();
    library_ = nil;
    device_ = nil;
}

bool MetalShaderLib::compileSource(const std::string& source, const std::string& label) {
    @autoreleasepool {
        NSString* src = [NSString stringWithUTF8String:source.c_str()];
        MTLCompileOptions* opts = [[MTLCompileOptions alloc] init];
        opts.languageVersion = MTLLanguageVersion2_4;

        NSError* error = nil;
        id<MTLLibrary> lib = [device_ newLibraryWithSource:src options:opts error:&error];
        if (!lib) {
            LOG_ERROR("metal", "Shader compile failed: %s", [[error localizedDescription] UTF8String]);
            return false;
        }

        // Merge functions into cache
        for (NSString* name in [lib functionNames]) {
            id<MTLFunction> func = [lib newFunctionWithName:name];
            if (func) {
                cache_[std::string([name UTF8String])] = (__bridge_retained void*)func;
            }
        }

        // Keep last compiled library as the active one
        library_ = lib;
        if (!label.empty()) {
            LOG_INFO("metal", "Compiled shader library: %s (%lu functions)",
                     label.c_str(), (unsigned long)[lib functionNames].count);
        }
        return true;
    }
}

bool MetalShaderLib::loadMetallib(const std::string& path) {
    @autoreleasepool {
        NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
        NSURL* url = [NSURL fileURLWithPath:nsPath];
        NSError* error = nil;
        id<MTLLibrary> lib = [device_ newLibraryWithURL:url error:&error];
        if (!lib) {
            LOG_ERROR("metal", "Failed to load metallib: %s", [[error localizedDescription] UTF8String]);
            return false;
        }

        library_ = lib;
        // Pre-cache all functions
        for (NSString* name in [lib functionNames]) {
            id<MTLFunction> func = [lib newFunctionWithName:name];
            if (func) {
                cache_[std::string([name UTF8String])] = (__bridge_retained void*)func;
            }
        }
        LOG_INFO("metal", "Loaded metallib: %s (%lu functions)",
                 path.c_str(), (unsigned long)[lib functionNames].count);
        return true;
    }
}

bool MetalShaderLib::loadMetallibFromBytes(const void* data, size_t size, const std::string& label) {
    if (!data || size == 0) {
        LOG_ERROR("metal", "loadMetallibFromBytes: empty payload");
        return false;
    }
    @autoreleasepool {
        // dispatch_data_create copies the buffer (DISPATCH_DATA_DESTRUCTOR_DEFAULT),
        // so the caller's std::vector<uint8_t> can free immediately after this call.
        dispatch_data_t dd = dispatch_data_create(data, size, dispatch_get_main_queue(),
                                                  DISPATCH_DATA_DESTRUCTOR_DEFAULT);
        NSError* error = nil;
        id<MTLLibrary> lib = [device_ newLibraryWithData:dd error:&error];
        if (!lib) {
            LOG_ERROR("metal", "Failed to load metallib from bytes (%s): %s",
                      label.empty() ? "<unlabeled>" : label.c_str(),
                      [[error localizedDescription] UTF8String]);
            return false;
        }

        library_ = lib;
        for (NSString* name in [lib functionNames]) {
            id<MTLFunction> func = [lib newFunctionWithName:name];
            if (func) {
                cache_[std::string([name UTF8String])] = (__bridge_retained void*)func;
            }
        }
        LOG_INFO("metal", "Loaded metallib from bytes: %s (%lu functions, %zu bytes)",
                 label.empty() ? "<unlabeled>" : label.c_str(),
                 (unsigned long)[lib functionNames].count, size);
        return true;
    }
}

void* MetalShaderLib::getFunction(const std::string& name) {
    auto it = cache_.find(name);
    if (it != cache_.end()) return it->second;

    // Try library lookup as fallback
    if (library_) {
        NSString* nsName = [NSString stringWithUTF8String:name.c_str()];
        id<MTLFunction> func = [library_ newFunctionWithName:nsName];
        if (func) {
            void* ptr = (__bridge_retained void*)func;
            cache_[name] = ptr;
            return ptr;
        }
    }
    LOG_ERROR("metal", "Shader function not found: %s", name.c_str());
    return nullptr;
}

} // namespace metal
} // namespace gfx
