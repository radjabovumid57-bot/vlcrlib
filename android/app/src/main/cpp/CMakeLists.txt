cmake_minimum_required(VERSION 3.22)
project(vlcrlib_mobile LANGUAGES C CXX)

set(VLCRLIB_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../..")
set(SDL3_SOURCE_DIR "${VLCRLIB_ROOT}/external/SDL3")

set(VLCRLIB_ENABLE_PHYSICS OFF CACHE BOOL "" FORCE)
set(VLCRLIB_ENABLE_JOLT OFF CACHE BOOL "" FORCE)
set(VLCRLIB_ENABLE_VLC OFF CACHE BOOL "" FORCE)
set(VLCRLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(VLCRLIB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(VLCRLIB_BUILD_TOOLS OFF CACHE BOOL "" FORCE)

# NDK ships glslc under shader-tools/<host>; the root CMake finds it by name.
if(ANDROID_NDK)
    file(GLOB _ndk_shader_dirs "${ANDROID_NDK}/shader-tools/*")
    foreach(_dir ${_ndk_shader_dirs})
        list(APPEND CMAKE_PROGRAM_PATH "${_dir}")
    endforeach()
endif()

if(EXISTS "${SDL3_SOURCE_DIR}/CMakeLists.txt")
    add_subdirectory("${SDL3_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/SDL3")
else()
    find_package(SDL3 CONFIG REQUIRED)
endif()

add_subdirectory("${VLCRLIB_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/vlcrlib")

# SDL's Android activity loads libmain.so and calls SDL_main.
# mobile_gltf.c renders assets/DamagedHelmet.glb from the APK;
# override with -DMOBILE_ENTRY=.../mobile_hello.c to build the minimal demo.
set(MOBILE_ENTRY "${VLCRLIB_ROOT}/example/mobile_gltf.c"
    CACHE FILEPATH "Entry point source compiled into libmain.so")
add_library(main SHARED "${MOBILE_ENTRY}")
target_link_libraries(main PRIVATE vlcrlib SDL3::SDL3)
if(TARGET SDL3::SDL3main)
    target_link_libraries(main PRIVATE SDL3::SDL3main)
endif()
set_target_properties(main PROPERTIES OUTPUT_NAME "main")
