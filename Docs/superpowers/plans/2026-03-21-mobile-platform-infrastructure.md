# Mobile Platform Infrastructure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enable iOS and Android builds with proper lifecycle handling, memory management, thermal throttling, and GLES shader compatibility.

**Architecture:** SDL2 already abstracts iOS/Android window and GL context creation. The work is: (1) shader preamble injection for GLES 3.0 compatibility, (2) SDL lifecycle event handling for background/foreground, (3) runtime RAM detection for texture cache budgeting, (4) thermal state polling for frame rate adaptation, (5) iOS Xcode build pipeline, (6) Android Gradle project with NDK/CMake integration.

**Tech Stack:** SDL2, OpenGL ES 3.0, CMake, Xcode, Gradle/NDK

---

## File Structure

| Action | Path | Responsibility |
|--------|------|---------------|
| Modify | `engine/render/shader.cpp` | GLES preamble injection in loadFromSource() |
| Create | `engine/platform/lifecycle.h` | SDL lifecycle event handler |
| Create | `engine/platform/lifecycle.cpp` | Background/foreground/low-memory response |
| Create | `engine/platform/device_info.h` | RAM detection, thermal state, device tier |
| Create | `engine/platform/device_info.cpp` | Platform-specific implementations |
| Modify | `engine/render/texture.h` | Accept dynamic VRAM budget from device tier |
| Create | `ios/Info.plist.in` | iOS app metadata template |
| Create | `ios/LaunchScreen.storyboard` | Required iOS launch screen |
| Create | `ios/build.sh` | One-command iOS build+deploy script |
| Create | `android/app/build.gradle.kts` | Android build config with CMake integration |
| Create | `android/app/src/main/java/com/fatemmo/game/FateMMOActivity.java` | SDLActivity subclass |
| Create | `android/app/src/main/AndroidManifest.xml` | Android manifest with permissions |
| Create | `android/settings.gradle.kts` | Gradle project settings |
| Create | `android/build.gradle.kts` | Root Gradle config |
| Create | `android/gradle.properties` | Gradle configuration |
| Modify | `CMakeLists.txt` | iOS bundle assets, Android shared library target |
| Create | `tests/test_device_info.cpp` | Device tier detection tests |

---

### Task 1: GLES 3.0 shader preamble injection

**Files:**
- Modify: `engine/render/shader.cpp` (or `shader.h` if `loadFromSource` is there)
- Modify: `assets/shaders/sprite.vert`
- Modify: `assets/shaders/sprite.frag`

The current shaders have NO `#version` directive — they rely on the OpenGL default. For GLES 3.0 compatibility, the engine must inject the correct version and precision qualifiers at load time.

- [ ] **Step 1: Add preamble injection to shader loading**

In `shader.cpp`, modify `loadFromSource()` to prepend a platform-specific preamble:

```cpp
bool Shader::loadFromSource(const std::string& vertSrc, const std::string& fragSrc) {
    std::string preamble;
#if defined(FATEMMO_GLES)
    preamble = "#version 300 es\nprecision highp float;\nprecision highp sampler2D;\n";
#else
    preamble = "#version 330 core\n";
#endif

    std::string fullVert = preamble + vertSrc;
    std::string fullFrag = preamble + fragSrc;

    // ... compile fullVert and fullFrag instead of raw vertSrc/fragSrc
}
```

- [ ] **Step 2: Remove any existing #version from shader files if present**

Verify `sprite.vert` and `sprite.frag` do NOT start with `#version`. If they do, remove it (the engine injects it now). Based on the audit, they currently have no version directive — no changes needed.

- [ ] **Step 3: Verify GL_QUADS and glMapBuffer are not used**

Search for `GL_QUADS` and `glMapBuffer` (without Range) in engine code. These don't exist in GLES 3.0. The SpriteBatch uses `GL_TRIANGLES` with index buffers, so this should be safe.

- [ ] **Step 4: Commit**

```bash
git add engine/render/shader.cpp
git commit -m "feat: GLES 3.0 shader preamble injection for mobile rendering"
```

---

### Task 2: SDL lifecycle event handling

**Files:**
- Create: `engine/platform/lifecycle.h`
- Create: `engine/platform/lifecycle.cpp`
- Modify: `game/game_app.cpp` (or main entry point)

- [ ] **Step 1: Create lifecycle handler**

```cpp
// engine/platform/lifecycle.h
#pragma once
#include <functional>
#include <SDL.h>

class AppLifecycle {
public:
    // Callbacks for app lifecycle events
    std::function<void()> onEnterBackground;   // save state, pause game loop
    std::function<void()> onEnterForeground;   // resume, reconnect network
    std::function<void()> onLowMemory;         // flush caches, reduce budgets

    // Call once before SDL event loop starts
    void install();

    bool isPaused() const { return paused_; }

private:
    bool paused_ = false;

    // SDL event filter callback (fires even before main event loop)
    static int eventFilter(void* userdata, SDL_Event* event);
};
```

- [ ] **Step 2: Implement lifecycle handler**

```cpp
// engine/platform/lifecycle.cpp
#include "engine/platform/lifecycle.h"
#include <spdlog/spdlog.h>

void AppLifecycle::install() {
    SDL_SetEventFilter(eventFilter, this);
}

int AppLifecycle::eventFilter(void* userdata, SDL_Event* event) {
    auto* self = static_cast<AppLifecycle*>(userdata);

    switch (event->type) {
    case SDL_APP_WILLENTERBACKGROUND:
        spdlog::info("[Lifecycle] Entering background");
        self->paused_ = true;
        if (self->onEnterBackground) self->onEnterBackground();
        return 0; // handled

    case SDL_APP_DIDENTERFOREGROUND:
        spdlog::info("[Lifecycle] Entering foreground");
        self->paused_ = false;
        if (self->onEnterForeground) self->onEnterForeground();
        return 0;

    case SDL_APP_LOWMEMORY:
        spdlog::warn("[Lifecycle] Low memory warning");
        if (self->onLowMemory) self->onLowMemory();
        return 0;

    default:
        return 1; // pass to main event queue
    }
}
```

- [ ] **Step 3: Wire into game app**

In `game_app.cpp` initialization:

```cpp
AppLifecycle lifecycle;
lifecycle.onEnterBackground = [&]() {
    // Flush network (send "going away" message)
    if (netClient_.isConnected()) {
        netClient_.sendHeartbeat(); // ensure server knows we're alive
    }
    // Save any unsaved state
    // Pause audio (when implemented)
};

lifecycle.onEnterForeground = [&]() {
    // Reconnect if disconnected
    if (!netClient_.isConnected() && !netClient_.isReconnecting()) {
        netClient_.onConnectionLost();
    }
    // Resume audio
};

lifecycle.onLowMemory = [&]() {
    // Cut texture cache budget by 50%
    // textureCache.setBudget(textureCache.budget() / 2);
    // Flush unused textures
    // textureCache.evictUnused();
};

lifecycle.install();

// In main loop, skip rendering when paused:
if (!lifecycle.isPaused()) {
    // ... render frame
}
```

- [ ] **Step 4: Commit**

```bash
git add engine/platform/lifecycle.h engine/platform/lifecycle.cpp game/game_app.cpp
git commit -m "feat: SDL lifecycle event handling for background/foreground/low-memory"
```

---

### Task 3: Mobile memory tiers and thermal throttling

**Files:**
- Create: `engine/platform/device_info.h`
- Create: `engine/platform/device_info.cpp`
- Create: `tests/test_device_info.cpp`

- [ ] **Step 1: Write tests**

```cpp
// tests/test_device_info.cpp
#include <doctest/doctest.h>
#include "engine/platform/device_info.h"

TEST_SUITE("Device Info") {

TEST_CASE("device tier is Low, Medium, or High") {
    auto tier = DeviceInfo::getDeviceTier();
    CHECK((tier == DeviceTier::Low || tier == DeviceTier::Medium || tier == DeviceTier::High));
}

TEST_CASE("VRAM budget is positive") {
    auto budget = DeviceInfo::recommendedVRAMBudget();
    CHECK(budget > 0);
    CHECK(budget <= 1024); // max 1GB
}

TEST_CASE("thermal state is in valid range") {
    auto state = DeviceInfo::getThermalState();
    CHECK(state >= 0);
    CHECK(state <= 3); // 0=nominal, 1=fair, 2=serious, 3=critical
}

TEST_CASE("physical RAM is detected") {
    auto ram = DeviceInfo::getPhysicalRAM_MB();
    CHECK(ram > 0);
}

} // TEST_SUITE
```

- [ ] **Step 2: Create DeviceInfo header**

```cpp
// engine/platform/device_info.h
#pragma once
#include <cstdint>

enum class DeviceTier : uint8_t {
    Low    = 0,  // ≤3GB RAM — budget 150-250MB VRAM
    Medium = 1,  // 4-6GB RAM — budget 250-400MB VRAM
    High   = 2   // 8GB+ RAM — budget 400-600MB VRAM
};

struct DeviceInfo {
    static uint64_t getPhysicalRAM_MB();
    static DeviceTier getDeviceTier();
    static int recommendedVRAMBudget(); // MB

    // Thermal state: 0=nominal, 1=fair, 2=serious, 3=critical
    static int getThermalState();

    // Recommended max FPS based on thermal state
    static int recommendedFPS();
};
```

- [ ] **Step 3: Implement per-platform**

```cpp
// engine/platform/device_info.cpp
#include "engine/platform/device_info.h"

#if defined(_WIN32)
#include <windows.h>
uint64_t DeviceInfo::getPhysicalRAM_MB() {
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    return status.ullTotalPhys / (1024 * 1024);
}
int DeviceInfo::getThermalState() { return 0; } // desktop: always nominal

#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IOS
#include <sys/sysctl.h>
uint64_t DeviceInfo::getPhysicalRAM_MB() {
    int64_t memsize;
    size_t len = sizeof(memsize);
    sysctlbyname("hw.memsize", &memsize, &len, nullptr, 0);
    return static_cast<uint64_t>(memsize) / (1024 * 1024);
}
int DeviceInfo::getThermalState() {
    // iOS thermal state requires Objective-C bridge
    // NSProcessInfo.processInfo.thermalState
    // 0=NSProcessInfoThermalStateNominal, 1=Fair, 2=Serious, 3=Critical
    return 0; // placeholder — needs ObjC++ bridge
}
#else // macOS
#include <sys/sysctl.h>
uint64_t DeviceInfo::getPhysicalRAM_MB() {
    int64_t memsize;
    size_t len = sizeof(memsize);
    sysctlbyname("hw.memsize", &memsize, &len, nullptr, 0);
    return static_cast<uint64_t>(memsize) / (1024 * 1024);
}
int DeviceInfo::getThermalState() { return 0; }
#endif

#elif defined(__ANDROID__)
#include <sys/sysinfo.h>
uint64_t DeviceInfo::getPhysicalRAM_MB() {
    struct sysinfo info;
    sysinfo(&info);
    return (info.totalram * info.mem_unit) / (1024 * 1024);
}
int DeviceInfo::getThermalState() {
    // Android: AThermal_getCurrentThermalStatus (API 30+)
    return 0; // placeholder — needs JNI bridge
}

#else // Linux
#include <sys/sysinfo.h>
uint64_t DeviceInfo::getPhysicalRAM_MB() {
    struct sysinfo info;
    sysinfo(&info);
    return (info.totalram * info.mem_unit) / (1024 * 1024);
}
int DeviceInfo::getThermalState() { return 0; }
#endif

DeviceTier DeviceInfo::getDeviceTier() {
    auto ram = getPhysicalRAM_MB();
    if (ram <= 3072) return DeviceTier::Low;
    if (ram <= 6144) return DeviceTier::Medium;
    return DeviceTier::High;
}

int DeviceInfo::recommendedVRAMBudget() {
    switch (getDeviceTier()) {
        case DeviceTier::Low:    return 200;
        case DeviceTier::Medium: return 350;
        case DeviceTier::High:   return 512;
    }
    return 256;
}

int DeviceInfo::recommendedFPS() {
    switch (getThermalState()) {
        case 0: return 60;  // nominal
        case 1: return 60;  // fair
        case 2: return 30;  // serious: drop to 30fps
        case 3: return 30;  // critical: 30fps + reduce particles
    }
    return 60;
}
```

- [ ] **Step 4: Run tests**

Run: `cmake --build build --target fate_tests && build/Debug/fate_tests.exe -tc="Device Info"`
Expected: All 4 tests PASS

- [ ] **Step 5: Commit**

```bash
git add engine/platform/device_info.h engine/platform/device_info.cpp tests/test_device_info.cpp
git commit -m "feat: device tier detection with RAM-based VRAM budgets and thermal state"
```

---

### Task 4: iOS build pipeline

**Files:**
- Create: `ios/Info.plist.in`
- Create: `ios/LaunchScreen.storyboard`
- Create: `ios/build.sh`
- Modify: `CMakeLists.txt` (iOS bundle section already partially exists)

- [ ] **Step 1: Create Info.plist template**

```xml
<!-- ios/Info.plist.in -->
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>     <string>en</string>
    <key>CFBundleDisplayName</key>           <string>FateMMO</string>
    <key>CFBundleExecutable</key>            <string>${MACOSX_BUNDLE_EXECUTABLE_NAME}</string>
    <key>CFBundleIdentifier</key>            <string>com.fatemmo.game</string>
    <key>CFBundleInfoDictionaryVersion</key> <string>6.0</string>
    <key>CFBundleName</key>                  <string>${MACOSX_BUNDLE_BUNDLE_NAME}</string>
    <key>CFBundlePackageType</key>           <string>APPL</string>
    <key>CFBundleShortVersionString</key>    <string>0.1.0</string>
    <key>CFBundleVersion</key>               <string>1</string>
    <key>LSRequiresIPhoneOS</key>            <true/>
    <key>UIRequiresFullScreen</key>          <true/>
    <key>UIStatusBarHidden</key>             <true/>
    <key>UILaunchStoryboardName</key>        <string>LaunchScreen</string>
    <key>UISupportedInterfaceOrientations</key>
    <array>
        <string>UIInterfaceOrientationLandscapeLeft</string>
        <string>UIInterfaceOrientationLandscapeRight</string>
    </array>
    <key>ITSAppUsesNonExemptEncryption</key> <false/>
</dict>
</plist>
```

- [ ] **Step 2: Create minimal LaunchScreen storyboard**

```xml
<!-- ios/LaunchScreen.storyboard -->
<?xml version="1.0" encoding="UTF-8"?>
<document type="com.apple.InterfaceBuilder3.CocoaTouch.Storyboard.XIB" version="3.0">
    <scenes>
        <scene sceneID="1">
            <objects>
                <viewController id="01J" sceneMemberID="viewController">
                    <view key="view" contentMode="scaleToFill" id="Ze5-6b-2t3">
                        <rect key="frame" x="0" y="0" width="896" height="414"/>
                        <color key="backgroundColor" red="0" green="0" blue="0" alpha="1"/>
                    </view>
                </viewController>
                <placeholder placeholderIdentifier="IBFirstResponder" id="iYj-Kq-Ea1" sceneMemberID="firstResponder"/>
            </objects>
        </scene>
    </scenes>
</document>
```

- [ ] **Step 3: Create iOS build script**

```bash
#!/bin/bash
# ios/build.sh — Build, archive, and optionally deploy to device or TestFlight
set -e

CONFIG=${1:-Debug}
ACTION=${2:-build}  # build, device, testflight

echo "=== FateMMO iOS $CONFIG build ==="

# Generate Xcode project
cmake -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_CXX_STANDARD=23 \
  -DFATEMMO_BUILD_MOBILE=ON \
  -S .. -B ../build-ios

# Build
cmake --build ../build-ios --config $CONFIG -- -quiet

if [ "$ACTION" = "device" ]; then
    echo "=== Deploying to connected device ==="
    ios-deploy --bundle ../build-ios/$CONFIG-iphoneos/FateEngine.app
elif [ "$ACTION" = "testflight" ]; then
    echo "=== Archiving for TestFlight ==="
    xcodebuild archive -project ../build-ios/FateEngine.xcodeproj \
      -scheme FateEngine -configuration Release \
      -archivePath ../build-ios/FateMMO.xcarchive

    xcodebuild -exportArchive \
      -archivePath ../build-ios/FateMMO.xcarchive \
      -exportOptionsPlist ExportOptions.plist \
      -exportPath ../build-ios/export

    echo "=== Uploading to TestFlight ==="
    xcrun altool --upload-app -f ../build-ios/export/FateEngine.ipa \
      -t ios --apiKey $APP_STORE_API_KEY --apiIssuer $APP_STORE_ISSUER
fi

echo "=== Done ==="
```

- [ ] **Step 4: Commit**

```bash
git add ios/Info.plist.in ios/LaunchScreen.storyboard ios/build.sh
git commit -m "feat: iOS build pipeline with Info.plist, launch screen, and build script"
```

---

### Task 5: Android Gradle project

**Files:**
- Create: `android/settings.gradle.kts`
- Create: `android/build.gradle.kts`
- Create: `android/gradle.properties`
- Create: `android/app/build.gradle.kts`
- Create: `android/app/src/main/AndroidManifest.xml`
- Create: `android/app/src/main/java/com/fatemmo/game/FateMMOActivity.java`

- [ ] **Step 1: Create Gradle project structure**

```kotlin
// android/settings.gradle.kts
rootProject.name = "FateMMO"
include(":app")
```

```kotlin
// android/build.gradle.kts
plugins {
    id("com.android.application") version "8.7.0" apply false
}
```

```properties
# android/gradle.properties
android.useAndroidX=true
org.gradle.jvmargs=-Xmx4096m
android.nonTransitiveRClass=true
```

```kotlin
// android/app/build.gradle.kts
plugins {
    id("com.android.application")
}

android {
    namespace = "com.fatemmo.game"
    compileSdk = 35
    ndkVersion = "27.1.12297006"

    defaultConfig {
        applicationId = "com.fatemmo.game"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"

        ndk {
            abiFilters += listOf("arm64-v8a")
        }

        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++23"
                arguments += listOf(
                    "-DFATEMMO_BUILD_MOBILE=ON",
                    "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON"
                )
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/jni/CMakeLists.txt")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    sourceSets {
        getByName("main") {
            assets.srcDirs("../../assets")
        }
    }
}
```

- [ ] **Step 2: Create AndroidManifest.xml**

```xml
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">
    <uses-permission android:name="android.permission.INTERNET" />
    <uses-feature android:glEsVersion="0x00030000" android:required="true" />

    <application
        android:allowBackup="true"
        android:label="FateMMO"
        android:icon="@mipmap/ic_launcher"
        android:theme="@android:style/Theme.NoTitleBar.Fullscreen"
        android:hasCode="true">

        <activity
            android:name=".FateMMOActivity"
            android:exported="true"
            android:configChanges="orientation|screenSize|keyboardHidden"
            android:screenOrientation="sensorLandscape">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
```

- [ ] **Step 3: Create SDLActivity subclass**

```java
// android/app/src/main/java/com/fatemmo/game/FateMMOActivity.java
package com.fatemmo.game;

import org.libsdl.app.SDLActivity;

public class FateMMOActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL2",
            "main"  // our game code compiles to libmain.so
        };
    }
}
```

- [ ] **Step 4: Create JNI CMakeLists.txt wrapper**

```cmake
# android/app/src/main/jni/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

# Point to the main engine CMakeLists.txt
# This file is a thin wrapper that Android Gradle uses
set(FATEMMO_BUILD_MOBILE ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../.. ${CMAKE_CURRENT_BINARY_DIR}/fatemmo)
```

- [ ] **Step 5: Commit**

```bash
git add android/
git commit -m "feat: Android Gradle project with NDK/CMake integration and SDLActivity"
```
