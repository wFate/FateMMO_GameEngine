cmake_minimum_required(VERSION 3.22)
project(FateMMO_Android)

# Propagate Android/mobile flags
set(FATEMMO_BUILD_MOBILE ON CACHE BOOL "" FORCE)

# Include the main engine CMakeLists.txt
# The path goes up from android/app/src/main/jni/ to the project root
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../.. ${CMAKE_BINARY_DIR}/engine)
