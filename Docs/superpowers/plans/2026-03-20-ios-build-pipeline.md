# iOS Build Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and deploy FateMMO to an iPhone via Xcode, using CMake's Xcode generator, free Apple ID signing, and GLES 3.0 rendering.

**Architecture:** CMake generates an Xcode project targeting iOS arm64. The GL loader conditionally uses GLES 3.0 headers on iOS (instead of the custom desktop loader). SDL2 provides the iOS app lifecycle bridge. Assets are bundled into the .app via CMake resource properties. An `ios/` directory holds Info.plist, launch screen, and build scripts.

**Tech Stack:** CMake Xcode generator, SDL2 iOS backend, OpenGL ES 3.0, Xcode 16+

**IMPORTANT:** The iOS build runs on macOS only. Steps marked [MAC] must execute on the MacBook. Steps marked [WIN] can run on the Windows dev machine.

---

## File Map

| Action | File | Responsibility |
|--------|------|----------------|
| Create | `ios/Info.plist.in` | iOS app metadata (CMake-configured) |
| Create | `ios/LaunchScreen.storyboard` | Required launch screen for iOS |
| Create | `ios/build-ios.sh` | One-command build + deploy script |
| Modify | `CMakeLists.txt` | iOS target configuration, GLES linking, asset bundling |
| Modify | `engine/render/gfx/backend/gl/gl_loader.h` | Conditional GLES 3.0 headers on iOS |
| Modify | `engine/render/gfx/backend/gl/gl_loader.cpp` | Skip desktop GL loading on iOS (linked statically) |
| Modify | `engine/app.cpp` | GLES context attributes on iOS, SDL_WINDOW_ALLOW_HIGHDPI |

---

### Task 1: Create iOS support files [WIN]

**Files:**
- Create: `ios/Info.plist.in`
- Create: `ios/LaunchScreen.storyboard`
- Create: `ios/build-ios.sh`

- [ ] **Step 1: Create ios/ directory and Info.plist.in**

Create `ios/Info.plist.in`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleExecutable</key>
    <string>$(EXECUTABLE_NAME)</string>
    <key>CFBundleIdentifier</key>
    <string>$(PRODUCT_BUNDLE_IDENTIFIER)</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>FateMMO</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>${PROJECT_VERSION}</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>LSRequiresIPhoneOS</key>
    <true/>
    <key>UILaunchStoryboardName</key>
    <string>LaunchScreen</string>
    <key>UIRequiresFullScreen</key>
    <true/>
    <key>UISupportedInterfaceOrientations</key>
    <array>
        <string>UIInterfaceOrientationLandscapeLeft</string>
        <string>UIInterfaceOrientationLandscapeRight</string>
    </array>
    <key>UIStatusBarHidden</key>
    <true/>
    <key>ITSAppUsesNonExemptEncryption</key>
    <false/>
    <key>UIRequiredDeviceCapabilities</key>
    <array>
        <string>arm64</string>
        <string>opengles-3</string>
    </array>
</dict>
</plist>
```

- [ ] **Step 2: Create LaunchScreen.storyboard**

Create `ios/LaunchScreen.storyboard`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<document type="com.apple.InterfaceBuilder3.CocoaTouch.Storyboard.XIB" version="3.0" toolsVersion="21701" targetRuntime="AppleSDK" propertyAccessControl="none" useAutolayout="YES" launchScreen="YES" useTraitCollections="YES" useSafeAreas="YES" colorMatched="YES" initialViewController="01J-lp-oVM">
    <scenes>
        <scene sceneID="EHf-IW-A2E">
            <objects>
                <viewController id="01J-lp-oVM" sceneMemberID="viewController">
                    <view key="view" contentMode="scaleToFill" id="Ze5-6b-2t3">
                        <rect key="frame" x="0.0" y="0.0" width="896" height="414"/>
                        <autoresizingMask key="autoresizingMask" widthSizable="YES" heightSizable="YES"/>
                        <color key="backgroundColor" red="0.0" green="0.0" blue="0.0" alpha="1" colorSpace="custom" customColorSpace="sRGB"/>
                    </view>
                </viewController>
            </objects>
        </scene>
    </scenes>
</document>
```

- [ ] **Step 3: Create build script**

Create `ios/build-ios.sh`:

```bash
#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "=== FateMMO iOS Build ==="
echo "Project: $PROJECT_DIR"

# Generate Xcode project
cmake -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_CXX_STANDARD=23 \
    -DFATEMMO_BUILD_MOBILE=ON \
    -S "$PROJECT_DIR" -B "$PROJECT_DIR/build-ios"

echo ""
echo "=== Xcode project generated at build-ios/ ==="
echo ""
echo "Next steps:"
echo "  1. Open build-ios/FateEngine.xcodeproj in Xcode"
echo "  2. Select your Apple ID in Signing & Capabilities"
echo "  3. Select your iPhone as the run destination"
echo "  4. Press Cmd+R to build and run"
echo ""
echo "Or build from command line:"
echo "  cmake --build build-ios --config Debug -- -allowProvisioningUpdates"
```

Make it executable: `chmod +x ios/build-ios.sh`

- [ ] **Step 4: Commit**

```bash
git add ios/
git commit -m "feat: add iOS support files (Info.plist, LaunchScreen, build script)"
```

---

### Task 2: Add iOS configuration to CMakeLists.txt [WIN]

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add iOS build option near platform detection**

After the platform detection block (around line 28), add:

```cmake
option(FATEMMO_BUILD_MOBILE "Build for mobile (iOS/Android)" OFF)
```

- [ ] **Step 2: Conditionally skip server-only dependencies on iOS**

The iOS build doesn't need PostgreSQL, libpqxx, bcrypt, or OpenSSL (server-only). Wrap those dependencies:

Find the OpenSSL/PostgreSQL/libpqxx/bcrypt blocks (lines 136-160) and wrap them:

```cmake
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT ANDROID)
    # OpenSSL (TLS for auth) — installed via vcpkg
    find_package(OpenSSL REQUIRED)

    # PostgreSQL client (libpq — required by libpqxx) — installed via vcpkg
    find_package(PostgreSQL REQUIRED)

    # libpqxx (C++ PostgreSQL client, wraps libpq)
    FetchContent_Declare(
        libpqxx
        GIT_REPOSITORY https://github.com/jtv/libpqxx.git
        GIT_TAG        7.9.2
        GIT_SHALLOW    TRUE
    )
    set(SKIP_BUILD_TEST ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(libpqxx)

    # bcrypt (password hashing — OpenBSD implementation)
    file(GLOB BCRYPT_SOURCES third_party/bcrypt/*.c)
    if(BCRYPT_SOURCES)
        add_library(bcrypt_lib STATIC ${BCRYPT_SOURCES})
        target_include_directories(bcrypt_lib PUBLIC third_party/bcrypt)
        if(MSVC)
            target_compile_options(bcrypt_lib PRIVATE /wd4100 /wd4244 /wd4267 /wd4996)
        endif()
    endif()
endif()
```

- [ ] **Step 3: Add GLES linking for iOS**

Replace the OpenGL finding block (lines 162-170):

```cmake
# Find OpenGL / OpenGL ES (cross-platform)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # iOS uses OpenGL ES 3.0 framework
    find_library(OPENGLES_LIB OpenGLES)
    set(OPENGL_LIB ${OPENGLES_LIB})
    add_compile_definitions(FATEMMO_GLES)
elseif(WIN32)
    set(OPENGL_LIB opengl32)
elseif(APPLE)
    find_library(OPENGL_LIB OpenGL)
else()
    find_library(OPENGL_LIB GL PATHS /usr/lib/x86_64-linux-gnu /usr/lib)
    if(NOT OPENGL_LIB)
        set(OPENGL_LIB GL)
    endif()
endif()
```

Note: `add_compile_definitions(FATEMMO_GLES)` activates the shader preamble injection we already built.

- [ ] **Step 4: Conditionally remove OpenSSL from engine on iOS**

In the `fate_engine` target_link_libraries (line 206-215), make OpenSSL conditional:

```cmake
target_link_libraries(fate_engine PUBLIC
    SDL2::SDL2-static
    nlohmann_json::nlohmann_json
    stb_image
    imgui_lib
    ${OPENGL_LIB}
    TracyClient
    spdlog::spdlog
)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT ANDROID)
    target_link_libraries(fate_engine PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()
```

- [ ] **Step 5: Add iOS app properties to FateEngine target**

After the FateEngine executable definition (around line 233-237), add iOS-specific properties:

```cmake
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set_target_properties(FateEngine PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.fatemmo.game"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/ios/Info.plist.in"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "iPhone Developer"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ""
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "15.0"
        RESOURCE "${CMAKE_SOURCE_DIR}/ios/LaunchScreen.storyboard"
    )

    # Bundle assets into the .app
    file(GLOB_RECURSE IOS_ASSETS "${CMAKE_SOURCE_DIR}/assets/*")
    set_source_files_properties(${IOS_ASSETS} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources/assets"
    )
    target_sources(FateEngine PRIVATE ${IOS_ASSETS})
endif()
```

- [ ] **Step 6: Skip server build on iOS**

Wrap the server executable block (lines 255-268):

```cmake
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT ANDROID)
    # ... existing server block ...
endif()
```

Similarly wrap the test executable block for now (tests need desktop GL context).

- [ ] **Step 7: Touch, build on Windows to verify no regressions**

```bash
touch CMakeLists.txt
```
Build on Windows. Expected: compiles cleanly (all conditions evaluate to the existing desktop path).

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt
git commit -m "feat: add iOS build configuration to CMakeLists.txt"
```

---

### Task 3: Adapt GL loader for GLES on iOS [WIN]

**Files:**
- Modify: `engine/render/gfx/backend/gl/gl_loader.h`
- Modify: `engine/render/gfx/backend/gl/gl_loader.cpp`
- Modify: `engine/app.cpp`

- [ ] **Step 1: Add GLES conditional headers to gl_loader.h**

At the top of `engine/render/gfx/backend/gl/gl_loader.h`, before any existing GL includes, add:

```cpp
#ifdef FATEMMO_GLES
    #include <OpenGLES/ES3/gl.h>
    #include <OpenGLES/ES3/glext.h>
    // On iOS, all GL ES functions are linked statically — no runtime loading needed
    #define FATEMMO_GL_STATIC_LINK
#else
    // ... existing desktop GL includes and function pointer declarations ...
#endif
```

The implementer should read the full gl_loader.h to understand the structure. The key change: when `FATEMMO_GLES` is defined, include the GLES3 headers directly and skip all the `PFNGL*` function pointer typedefs and `extern` declarations. The `loadGLFunctions()` function becomes a no-op on GLES.

- [ ] **Step 2: Make loadGLFunctions() a no-op on GLES**

In `engine/render/gfx/backend/gl/gl_loader.cpp`, wrap the body:

```cpp
bool loadGLFunctions() {
#ifdef FATEMMO_GL_STATIC_LINK
    return true; // All functions linked statically on iOS
#else
    // ... existing desktop loading code ...
#endif
}
```

- [ ] **Step 3: Set GLES context attributes in App::init()**

In `engine/app.cpp`, in `App::init()`, make the GL context setup conditional:

```cpp
#ifdef FATEMMO_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
```

Also add `SDL_WINDOW_ALLOW_HIGHDPI` to the window flags:

```cpp
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
```

And on iOS, use `SDL_GL_GetDrawableSize()` instead of window size for the actual pixel dimensions:

After creating the GL context, add:

```cpp
#ifdef FATEMMO_GLES
    int drawW, drawH;
    SDL_GL_GetDrawableSize(window_, &drawW, &drawH);
    config_.windowWidth = drawW;
    config_.windowHeight = drawH;
    LOG_INFO("App", "Drawable size: %dx%d", drawW, drawH);
#endif
```

- [ ] **Step 4: Touch, build on Windows**

```bash
touch engine/render/gfx/backend/gl/gl_loader.h engine/render/gfx/backend/gl/gl_loader.cpp engine/app.cpp
```
Build. Expected: compiles cleanly (FATEMMO_GLES not defined on Windows, all code takes the existing path).

- [ ] **Step 5: Run tests**

`./build/Debug/fate_tests.exe`
Expected: All 388 tests pass.

- [ ] **Step 6: Commit**

```bash
git add engine/render/gfx/backend/gl/gl_loader.h engine/render/gfx/backend/gl/gl_loader.cpp engine/app.cpp
git commit -m "feat: conditional GLES 3.0 context and static GL linking for iOS"
```

---

### Task 4: First iOS build on MacBook [MAC]

This task runs entirely on the MacBook Pro.

- [ ] **Step 1: Clone/sync the repo to the MacBook**

```bash
git clone <your-repo-url> ~/FateMMO_GameEngine
cd ~/FateMMO_GameEngine
```

Or if already cloned, `git pull`.

- [ ] **Step 2: Verify Xcode is installed**

```bash
xcode-select --print-path
# Should show /Applications/Xcode.app/Contents/Developer
xcodebuild -version
# Should show Xcode 16.x+
```

- [ ] **Step 3: Run the build script**

```bash
chmod +x ios/build-ios.sh
./ios/build-ios.sh
```

This generates the Xcode project at `build-ios/FateEngine.xcodeproj`.

- [ ] **Step 4: Open in Xcode and configure signing**

1. Open `build-ios/FateEngine.xcodeproj` in Xcode
2. Select the `FateEngine` target
3. Go to Signing & Capabilities
4. Enable "Automatically manage signing"
5. Select your Apple ID as the Team (add it in Xcode → Settings → Accounts if not already added)
6. Xcode will create a provisioning profile automatically

- [ ] **Step 5: Connect iPhone and build**

1. Connect your iPhone 17 Pro via USB (or use wireless debugging if paired)
2. Select your iPhone as the run destination in the Xcode toolbar
3. Press Cmd+R to build and run
4. On first deploy, you'll need to trust the developer certificate on the iPhone: Settings → General → VPN & Device Management → tap your developer profile → Trust

- [ ] **Step 6: Troubleshoot common issues**

If SDL2 fails to build:
- Ensure CMake detects the iOS SDK: `xcrun --sdk iphoneos --show-sdk-path`
- Check that `CMAKE_OSX_SYSROOT` is set (CMake usually auto-detects)

If ImGui/ImPlot fails:
- The Xcode generator may need all source files explicitly listed. Check for GLOB issues.

If GL fails at runtime:
- Verify `FATEMMO_GLES` is defined: add `LOG_INFO("GL", "GLES mode: %d", 1);` inside the `#ifdef FATEMMO_GLES` block
- Check that `SDL_GL_CONTEXT_PROFILE_ES` is set

If the app crashes on launch:
- Check Xcode console for the crash log
- Most likely cause: a desktop-only code path being hit (check `#ifdef` guards)

- [ ] **Step 7: Verify rendering**

On the iPhone, you should see:
- The game world rendering at 480×270 upscaled to the phone's display
- Touch controls visible (D-pad bottom-left, buttons bottom-right)
- Touch D-pad controls movement
- Tap-to-target works in the game world area

If the editor (ImGui) is showing, add `#ifdef FATEMMO_MOBILE` guards to skip editor initialization on mobile.
