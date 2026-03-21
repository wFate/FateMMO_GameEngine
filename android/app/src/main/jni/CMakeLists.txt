cmake_minimum_required(VERSION 3.20)

# This is the entry point for Android Gradle's CMake integration.
# It delegates to the project root CMakeLists.txt.
set(FATEMMO_BUILD_MOBILE ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../.. ${CMAKE_CURRENT_BINARY_DIR}/fatemmo)
