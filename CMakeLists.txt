cmake_minimum_required(VERSION 3.20)

# vcpkg integration
if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
elseif(EXISTS "C:/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "C:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

project(FateEngine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# Platform Detection
# =============================================================================
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_compile_definitions(FATEMMO_PLATFORM_IOS FATEMMO_MOBILE)
elseif(ANDROID)
    add_compile_definitions(FATEMMO_PLATFORM_ANDROID FATEMMO_MOBILE)
elseif(WIN32)
    add_compile_definitions(FATEMMO_PLATFORM_WINDOWS)
elseif(APPLE)
    add_compile_definitions(FATEMMO_PLATFORM_MACOS)
elseif(UNIX)
    add_compile_definitions(FATEMMO_PLATFORM_LINUX)
endif()

option(FATEMMO_BUILD_MOBILE "Build for mobile (iOS/Android)" OFF)

# Metal backend on all Apple platforms
if(APPLE)
    add_compile_definitions(FATEMMO_METAL)
    find_library(METAL_FRAMEWORK Metal)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore)
    set(METAL_LIBS ${METAL_FRAMEWORK} ${QUARTZCORE_FRAMEWORK})

    # SDL must be built with Metal support
    set(SDL_VIDEO_METAL ON CACHE BOOL "" FORCE)
endif()

# =============================================================================
# Dependencies via FetchContent (zero external installs required)
# =============================================================================
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
# Allow older FetchContent packages with cmake_minimum_required < 3.5
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

# SDL2
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-2.30.10
    GIT_SHALLOW    TRUE
)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2)

# nlohmann/json (header-only)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(json)

# spdlog (fast logging with per-subsystem loggers)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.1
    GIT_SHALLOW    TRUE
)
set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

# stb (for stb_image.h)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(stb)

add_library(stb_image STATIC
    third_party/stb/stb_image_impl.cpp
    third_party/stb/stb_truetype_impl.cpp
    third_party/stb/stb_image_write_impl.cpp
)
target_include_directories(stb_image PUBLIC ${stb_SOURCE_DIR})

# Dear ImGui (docking branch for dockable editor panels)
# Pinned to v1.91.9b-docking: last docking release compatible with ImPlot v0.16.
# The docking tip (1.92+) refactored ImFont and removed FontSize/FindGlyph fields
# that ImPlot v0.16 depends on.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.9b-docking
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
    implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG        v0.16
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(implot)

FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imguizmo)

FetchContent_Declare(
    imnodes
    GIT_REPOSITORY https://github.com/Nelarius/imnodes.git
    GIT_TAG        v0.5
)
FetchContent_MakeAvailable(imnodes)

# doctest (header-only test framework)
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(doctest)

# Tracy Profiler
FetchContent_Declare(
    tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG        v0.11.1
    GIT_SHALLOW    TRUE
)
option(TRACY_ENABLE "Enable Tracy profiler" ON)
option(TRACY_ON_DEMAND "Enable Tracy on-demand mode" ON)
option(ENGINE_MEMORY_DEBUG "Enable allocator debug visualization panels" ON)
option(FATE_SHIPPING "Strip editor UI for production/shipping builds" OFF)
FetchContent_MakeAvailable(tracy)

# PhysicsFS (virtual filesystem with ZIP/archive mount support)
FetchContent_Declare(
    physfs
    GIT_REPOSITORY https://github.com/icculus/physfs.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
set(PHYSFS_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(physfs)

# SoLoud audio library
FetchContent_Declare(
    soloud
    GIT_REPOSITORY https://github.com/jarikomppa/soloud.git
    GIT_TAG        RELEASE_20200207
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(soloud)

set(SOLOUD_SRC_DIR ${soloud_SOURCE_DIR}/src)
add_library(soloud_lib STATIC
    ${SOLOUD_SRC_DIR}/core/soloud.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_audiosource.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_bus.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_core_3d.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_core_basicops.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_core_faderops.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_fader.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_core_filterops.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_core_getters.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_core_setters.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_core_voicegroup.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_core_voiceops.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_fft.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_fft_lut.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_file.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_filter.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_misc.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_queue.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_thread.cpp
    ${SOLOUD_SRC_DIR}/audiosource/wav/soloud_wav.cpp
    ${SOLOUD_SRC_DIR}/audiosource/wav/soloud_wavstream.cpp
    ${SOLOUD_SRC_DIR}/audiosource/wav/stb_vorbis.c
    ${SOLOUD_SRC_DIR}/audiosource/wav/dr_impl.cpp
    ${SOLOUD_SRC_DIR}/backend/sdl2_static/soloud_sdl2_static.cpp
    ${SOLOUD_SRC_DIR}/backend/null/soloud_null.cpp
)
target_include_directories(soloud_lib PUBLIC ${soloud_SOURCE_DIR}/include)
target_compile_definitions(soloud_lib PUBLIC WITH_SDL2_STATIC WITH_NULL)
target_link_libraries(soloud_lib PRIVATE SDL2::SDL2-static)
if(MSVC)
    target_compile_options(soloud_lib PRIVATE /W0)
else()
    target_compile_options(soloud_lib PRIVATE -w)
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT ANDROID)
    # OpenSSL (TLS for auth) — installed via vcpkg
    find_package(OpenSSL REQUIRED)

    # PostgreSQL client (libpq — required by libpqxx) — installed via vcpkg
    find_package(PostgreSQL REQUIRED)

    # FreeType (font rendering for ImGui) — installed via vcpkg
    find_package(Freetype REQUIRED)

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

# Find OpenGL / OpenGL ES (cross-platform)
# Apple platforms use Metal; OpenGL detection is skipped there.
if(APPLE)
    set(OPENGL_LIB "")
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # iOS uses OpenGL ES 3.0 framework
    find_library(OPENGLES_LIB OpenGLES)
    set(OPENGL_LIB ${OPENGLES_LIB})
    add_compile_definitions(FATEMMO_GLES)
elseif(WIN32)
    set(OPENGL_LIB opengl32)
else()
    find_library(OPENGL_LIB GL PATHS /usr/lib/x86_64-linux-gnu /usr/lib)
    if(NOT OPENGL_LIB)
        set(OPENGL_LIB GL)
    endif()
endif()

# ImGui is not a cmake project - build it as a static library
# Shipping builds have no ImGui dependency — skip the entire lib.
if(NOT FATE_SHIPPING)
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
    ${imguizmo_SOURCE_DIR}/ImGuizmo.cpp
    ${imnodes_SOURCE_DIR}/imnodes.cpp
    ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/freetype
    ${implot_SOURCE_DIR}
    ${imguizmo_SOURCE_DIR}
    ${imnodes_SOURCE_DIR}
)
if(APPLE)
    # Metal backend: swap OpenGL backend for Metal + SDL2
    target_sources(imgui_lib PRIVATE
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_metal.mm
    )
    target_link_libraries(imgui_lib PUBLIC SDL2::SDL2-static Freetype::Freetype ${METAL_LIBS})
else()
    # OpenGL backend (Windows / Linux)
    target_sources(imgui_lib PRIVATE
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_link_libraries(imgui_lib PUBLIC SDL2::SDL2-static ${OPENGL_LIB} Freetype::Freetype)
endif()
# No loader define = ImGui uses its own minimal GL loader
endif() # NOT FATE_SHIPPING

# =============================================================================
# Engine Library
# =============================================================================
file(GLOB_RECURSE ENGINE_SOURCES
    engine/*.cpp
    engine/*.h
)

if(FATE_SHIPPING)
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX "engine/editor/.*")
endif()

# Exclude platform-specific fiber backends that don't apply to the current target
if(WIN32 AND NOT FATEMMO_BUILD_MOBILE)
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX "fiber_minicoro\.cpp$")
else()
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX "fiber_win32\.cpp$")
endif()

# Platform-specific socket implementation
if(WIN32)
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX "socket_posix\\.cpp$")
else()
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX "socket_win32\\.cpp$")
endif()

# GFX backend: Metal (.mm) on Apple, GL (.cpp) elsewhere.
# GLOB_RECURSE above only captures *.cpp so .mm files must be added explicitly.
# On Apple we also exclude the GL backend sources from ENGINE_SOURCES to avoid
# linking two competing implementations.
if(APPLE)
    set(GFX_BACKEND_SOURCES
        engine/render/gfx/backend/metal/metal_device.mm
        engine/render/gfx/backend/metal/metal_command_list.mm
        engine/render/gfx/backend/metal/metal_shader_lib.mm
    )
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX "engine/render/gfx/backend/gl/.*")
else()
    set(GFX_BACKEND_SOURCES
        engine/render/gfx/backend/gl/gl_device.cpp
        engine/render/gfx/backend/gl/gl_loader.cpp
        engine/render/gfx/backend/gl/gl_command_list.cpp
    )
    # Remove any GL files already captured by GLOB so we don't double-add them
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX "engine/render/gfx/backend/gl/.*")
endif()

# Files with #ifdef FATEMMO_METAL ObjC syntax blocks must compile as ObjC++
if(APPLE)
    set_source_files_properties(
        engine/render/sprite_batch.cpp
        engine/tilemap/chunk_renderer.cpp
        engine/render/fullscreen_quad.cpp
        engine/render/sdf_text.cpp
        engine/render/tile_texture_array.cpp
        engine/app.cpp
        engine/editor/editor.cpp
        PROPERTIES LANGUAGE OBJCXX
    )
endif()

add_library(fate_engine STATIC ${ENGINE_SOURCES} ${GFX_BACKEND_SOURCES})
target_include_directories(fate_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(fate_engine PUBLIC
    SDL2::SDL2-static
    nlohmann_json::nlohmann_json
    stb_image
    ${OPENGL_LIB}
    TracyClient
    spdlog::spdlog
    physfs-static
    soloud_lib
)
if(NOT FATE_SHIPPING)
    target_link_libraries(fate_engine PUBLIC imgui_lib)
endif()
if(APPLE)
    target_link_libraries(fate_engine PUBLIC ${METAL_LIBS})
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT ANDROID)
    target_link_libraries(fate_engine PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

# libsodium (AEAD packet encryption) — optional, via vcpkg
find_package(unofficial-sodium CONFIG QUIET)
if(unofficial-sodium_FOUND)
    target_link_libraries(fate_engine PUBLIC unofficial-sodium::sodium)
    target_compile_definitions(fate_engine PUBLIC FATE_HAS_SODIUM=1)
    message(STATUS "libsodium found — AEAD packet encryption enabled")
else()
    message(STATUS "libsodium NOT found — AEAD encryption disabled (plaintext fallback)")
endif()

if(WIN32)
    target_link_libraries(fate_engine PUBLIC ws2_32)
endif()

if(ENGINE_MEMORY_DEBUG)
    target_compile_definitions(fate_engine PUBLIC ENGINE_MEMORY_DEBUG)
endif()

if(FATE_SHIPPING)
    target_compile_definitions(fate_engine PUBLIC FATE_SHIPPING=1)
endif()

# Precompiled headers — stable, frequently-included headers
target_precompile_headers(fate_engine PRIVATE
    <vector>
    <string>
    <unordered_map>
    <memory>
    <functional>
    <optional>
    <variant>
    <cstdint>
    <algorithm>
    <array>
    <cmath>
)

# =============================================================================
# Game Executable
# =============================================================================
file(GLOB_RECURSE GAME_SOURCES
    game/*.cpp
    game/*.h
)

add_executable(FateEngine ${GAME_SOURCES})
target_link_libraries(FateEngine PRIVATE
    fate_engine
    SDL2::SDL2main
)
if(APPLE)
    target_link_libraries(FateEngine PRIVATE ${METAL_LIBS})
endif()

# Pass source directory to code so saves go to the right place
target_compile_definitions(FateEngine PRIVATE
    FATE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    FATE_DEV_TLS
    EDITOR_BUILD
)

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
else()
    # Copy assets to build directory on every build (desktop only)
    add_custom_command(TARGET FateEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:FateEngine>/assets
    )
endif()

# =============================================================================
# Shipping Metallib Build
# =============================================================================
if(APPLE AND FATE_SHIPPING)
    file(GLOB METAL_SHADERS ${CMAKE_SOURCE_DIR}/assets/shaders/metal/*.metal)
    if(METAL_SHADERS)
        set(METAL_AIR_FILES "")
        foreach(SHADER ${METAL_SHADERS})
            get_filename_component(SHADER_NAME ${SHADER} NAME_WE)
            set(AIR_FILE ${CMAKE_BINARY_DIR}/metal_air/${SHADER_NAME}.air)
            add_custom_command(
                OUTPUT ${AIR_FILE}
                COMMAND xcrun metal -c ${SHADER} -o ${AIR_FILE}
                DEPENDS ${SHADER}
                COMMENT "Compiling Metal shader: ${SHADER_NAME}"
            )
            list(APPEND METAL_AIR_FILES ${AIR_FILE})
        endforeach()
        set(METALLIB_OUTPUT ${CMAKE_BINARY_DIR}/default.metallib)
        add_custom_command(
            OUTPUT ${METALLIB_OUTPUT}
            COMMAND xcrun metallib ${METAL_AIR_FILES} -o ${METALLIB_OUTPUT}
            DEPENDS ${METAL_AIR_FILES}
            COMMENT "Linking Metal shaders into default.metallib"
        )
        add_custom_target(metal_shaders ALL DEPENDS ${METALLIB_OUTPUT})
        add_dependencies(FateEngine metal_shaders)
    endif()
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT ANDROID)
    # =============================================================================
    # Server Executable (headless, no SDL window)
    # =============================================================================
    file(GLOB_RECURSE SERVER_SOURCES server/*.cpp server/*.h)
    file(GLOB_RECURSE SERVER_SHARED_SOURCES game/shared/*.cpp)
    if(SERVER_SOURCES)
        add_executable(FateServer ${SERVER_SOURCES} ${SERVER_SHARED_SOURCES})
        target_link_libraries(FateServer PRIVATE fate_engine pqxx PostgreSQL::PostgreSQL bcrypt_lib OpenSSL::SSL OpenSSL::Crypto spdlog::spdlog)
        target_include_directories(FateServer PRIVATE ${CMAKE_SOURCE_DIR})
        if(WIN32)
            target_link_libraries(FateServer PRIVATE winmm)
        endif()
        if(MSVC)
            target_compile_definitions(FateServer PRIVATE _CRT_SECURE_NO_WARNINGS)
            target_compile_options(FateServer PRIVATE /W4 /wd4100 /wd4201 /wd4458 /MP)
        endif()
    endif()

    # =============================================================================
    # Test Executable
    # =============================================================================
    file(GLOB_RECURSE TEST_SOURCES tests/*.cpp)
    list(FILTER TEST_SOURCES EXCLUDE REGEX "tests/scenarios/")
    # Some tests reference game/shared code (gitignored); include if present
    file(GLOB_RECURSE GAME_SHARED_SOURCES game/shared/*.cpp)
    # Game animation loader (needed by animation tests)
    set(GAME_ANIM_TEST_SOURCES game/animation_loader.cpp)
    # Server sources needed by tests (WAL, persistence queue, etc.)
    file(GLOB_RECURSE SERVER_TEST_SOURCES server/wal/*.cpp)
    list(APPEND SERVER_TEST_SOURCES server/db/persistence_priority.cpp)
    if(TEST_SOURCES)
        add_executable(fate_tests ${TEST_SOURCES} ${GAME_SHARED_SOURCES} ${GAME_ANIM_TEST_SOURCES} ${SERVER_TEST_SOURCES})
        target_link_libraries(fate_tests PRIVATE fate_engine doctest::doctest spdlog::spdlog)
        target_include_directories(fate_tests PRIVATE ${CMAKE_SOURCE_DIR})
        target_compile_definitions(fate_tests PRIVATE
            FATE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
            EDITOR_BUILD
        )
        if(MSVC)
            target_compile_options(fate_tests PRIVATE /EHsc /W4 /wd4100 /wd4201 /wd4458 /MP)
        endif()
    endif()

    # =============================================================================
    # Scenario Tests (require running FateServer + database)
    # =============================================================================
    file(GLOB SCENARIO_TEST_SOURCES tests/scenarios/*.cpp)
    if(SCENARIO_TEST_SOURCES)
        add_executable(fate_scenario_tests ${SCENARIO_TEST_SOURCES})
        target_link_libraries(fate_scenario_tests PRIVATE fate_engine doctest::doctest spdlog::spdlog)
        target_include_directories(fate_scenario_tests PRIVATE ${CMAKE_SOURCE_DIR})
        if(MSVC)
            target_compile_definitions(fate_scenario_tests PRIVATE _CRT_SECURE_NO_WARNINGS FATE_DEV_TLS)
            target_compile_options(fate_scenario_tests PRIVATE /EHsc /W4 /wd4100 /wd4201 /wd4458 /MP)
        endif()
    endif()
endif()

# =============================================================================
# Compiler settings
# =============================================================================
# Sanitizer presets (use -DCMAKE_BUILD_TYPE=ASan)
if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set(CMAKE_CXX_FLAGS_ASAN "/fsanitize=address /Zi /Od" CACHE STRING "")
    set(CMAKE_EXE_LINKER_FLAGS_ASAN "/DEBUG" CACHE STRING "")
    # Note: MSVC does not support UBSan. Available via Clang-cl only.
endif()

if(MSVC)
    target_compile_definitions(fate_engine PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(FateEngine PRIVATE _CRT_SECURE_NO_WARNINGS)
    if(NOT FATE_SHIPPING)
        target_compile_definitions(imgui_lib PRIVATE _CRT_SECURE_NO_WARNINGS IMGUI_DEFINE_MATH_OPERATORS IMGUI_ENABLE_FREETYPE)
    endif()
    target_compile_options(fate_engine PRIVATE /W4 /wd4100 /wd4201 /wd4458)
    target_compile_options(FateEngine PRIVATE /W4 /wd4100 /wd4201 /wd4458 /bigobj)
    if(NOT FATE_SHIPPING)
        target_compile_options(imgui_lib PRIVATE /W4 /wd4100 /wd4201 /wd4458 /wd4996)
    endif()
    target_compile_options(fate_engine PRIVATE /MP /bigobj)
    target_compile_options(FateEngine PRIVATE /MP)
    if(NOT FATE_SHIPPING)
        target_compile_options(imgui_lib PRIVATE /MP)
    endif()
else()
    target_compile_options(fate_engine PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(FateEngine PRIVATE -Wall -Wextra -Wpedantic)
endif()
