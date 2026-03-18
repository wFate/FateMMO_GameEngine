cmake_minimum_required(VERSION 3.20)
project(FateEngine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
FetchContent_MakeAvailable(tracy)

# Find OpenGL (cross-platform)
if(WIN32)
    set(OPENGL_LIB opengl32)
else()
    find_library(OPENGL_LIB GL PATHS /usr/lib/x86_64-linux-gnu /usr/lib)
    if(NOT OPENGL_LIB)
        set(OPENGL_LIB GL)
    endif()
endif()

# ImGui is not a cmake project - build it as a static library
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
    ${imguizmo_SOURCE_DIR}/ImGuizmo.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${implot_SOURCE_DIR}
    ${imguizmo_SOURCE_DIR}
)
target_link_libraries(imgui_lib PUBLIC SDL2::SDL2-static ${OPENGL_LIB})
# No loader define = ImGui uses its own minimal GL loader

# =============================================================================
# Engine Library
# =============================================================================
file(GLOB_RECURSE ENGINE_SOURCES
    engine/*.cpp
    engine/*.h
)

add_library(fate_engine STATIC ${ENGINE_SOURCES})
target_include_directories(fate_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(fate_engine PUBLIC
    SDL2::SDL2-static
    nlohmann_json::nlohmann_json
    stb_image
    imgui_lib
    ${OPENGL_LIB}
    TracyClient
)

if(WIN32)
    target_link_libraries(fate_engine PUBLIC ws2_32)
endif()

if(ENGINE_MEMORY_DEBUG)
    target_compile_definitions(fate_engine PUBLIC ENGINE_MEMORY_DEBUG)
endif()

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

# Pass source directory to code so saves go to the right place
target_compile_definitions(FateEngine PRIVATE
    FATE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

# Copy assets to build directory on every build
add_custom_command(TARGET FateEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:FateEngine>/assets
)

# =============================================================================
# Test Executable
# =============================================================================
file(GLOB_RECURSE TEST_SOURCES tests/*.cpp)
# Some tests reference game/shared code (gitignored); include if present
file(GLOB_RECURSE GAME_SHARED_SOURCES game/shared/*.cpp)
if(TEST_SOURCES)
    add_executable(fate_tests ${TEST_SOURCES} ${GAME_SHARED_SOURCES})
    target_link_libraries(fate_tests PRIVATE fate_engine doctest::doctest)
    target_include_directories(fate_tests PRIVATE ${CMAKE_SOURCE_DIR})
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
    target_compile_definitions(imgui_lib PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(fate_engine PRIVATE /W4 /wd4100 /wd4201 /wd4458)
    target_compile_options(FateEngine PRIVATE /W4 /wd4100 /wd4201 /wd4458)
    target_compile_options(imgui_lib PRIVATE /W4 /wd4100 /wd4201 /wd4458 /wd4996)
    target_compile_options(fate_engine PRIVATE /MP)
    target_compile_options(FateEngine PRIVATE /MP)
    target_compile_options(imgui_lib PRIVATE /MP)
else()
    target_compile_options(fate_engine PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(FateEngine PRIVATE -Wall -Wextra -Wpedantic)
endif()
