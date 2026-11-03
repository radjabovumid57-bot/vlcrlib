# ==============================================================================
#  vlcrlib — CMake build
# ==============================================================================
cmake_minimum_required(VERSION 3.20)
project(vlcrlib VERSION 0.1.0 LANGUAGES C CXX)

# ==============================================================================
#  Platform
# ==============================================================================
if(EMSCRIPTEN)
    set(VLCRLIB_PLATFORM "web")
elseif(WIN32)
    set(VLCRLIB_PLATFORM "windows")
elseif(APPLE)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(VLCRLIB_PLATFORM "ios")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "tvOS")
        set(VLCRLIB_PLATFORM "tvos")
    else()
        set(VLCRLIB_PLATFORM "macos")
    endif()
elseif(ANDROID)
    set(VLCRLIB_PLATFORM "android")
else()
    set(VLCRLIB_PLATFORM "linux")
endif()
message(STATUS "vlcrlib platform: ${VLCRLIB_PLATFORM}")

# ==============================================================================
#  vcpkg toolchain (optional on Linux/macOS/system packages; skipped for web)
# ==============================================================================
option(VLCRLIB_SKIP_VCPKG "Do not require vcpkg (system packages / emscripten)" OFF)
if(EMSCRIPTEN)
    set(VLCRLIB_SKIP_VCPKG ON)
endif()

if(NOT VLCRLIB_SKIP_VCPKG AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(_vcpkg_roots
        "$ENV{VCPKG_ROOT}"
        "C:/vcpkg"
        "C:/dev/vcpkg"
        "$ENV{USERPROFILE}/vcpkg"
        "$ENV{USERPROFILE}/source/vcpkg"
        "$ENV{HOME}/vcpkg"
    )
    foreach(_root ${_vcpkg_roots})
        if(_root AND EXISTS "${_root}/scripts/buildsystems/vcpkg.cmake")
            set(CMAKE_TOOLCHAIN_FILE
                "${_root}/scripts/buildsystems/vcpkg.cmake"
                CACHE STRING "vcpkg toolchain file" FORCE)
            message(STATUS "vlcrlib: vcpkg found at ${_root}")
            break()
        endif()
    endforeach()
endif()

if(NOT VLCRLIB_SKIP_VCPKG AND NOT DEFINED CMAKE_TOOLCHAIN_FILE AND WIN32)
    message(FATAL_ERROR
        "vcpkg не найден.\n"
        "  Установите vcpkg или -DVLCRLIB_SKIP_VCPKG=ON с системными пакетами.")
endif()

# ==============================================================================
#  Options
# ==============================================================================
option(VLCRLIB_BUILD_EXAMPLES "Build example executable" ON)
# VLC is the only audio backend. The SDK ships in external/vlc-sdk, so default
# to ON when it is present; otherwise the audio API compiles to no-op stubs and
# every audio example exits immediately with "audio unavailable".
set(VLCRLIB_ENABLE_VLC_DEFAULT OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/vlc-sdk/include/vlc/vlc.h")
    set(VLCRLIB_ENABLE_VLC_DEFAULT ON)
endif()
option(VLCRLIB_ENABLE_VLC "Enable VLC audio backend" ${VLCRLIB_ENABLE_VLC_DEFAULT})
option(VLCRLIB_ENABLE_JOLT    "Enable Jolt Physics backend" ON)
option(VLCRLIB_ENABLE_PHYSICS "Enable Bullet/Box2D physics" ON)
option(VLCRLIB_STRICT         "Treat warnings as errors"  OFF)
option(VLCRLIB_BUILD_TOOLS    "Build asset tools (vlcrc)" ON)
option(VLCRLIB_GRAPHICS_WEBGPU "Force WebGPU backend instead of Vulkan" OFF)
option(VLCRLIB_GRAPHICS_CPU "Build the GPU-free CPU backend (no Vulkan/WebGPU)" OFF)
option(VLCRLIB_GPU_TESTS "Enable GPU golden-image tests (requires Vulkan)" OFF)

if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # The checked-in VLC SDK is desktop-shaped; mobile uses SDL/miniaudio.
    set(VLCRLIB_ENABLE_VLC OFF CACHE BOOL "Enable VLC audio backend" FORCE)
endif()

if(EMSCRIPTEN)
    set(VLCRLIB_GRAPHICS_WEBGPU ON)
    set(VLCRLIB_ENABLE_JOLT OFF)
    set(VLCRLIB_ENABLE_VLC OFF)
endif()

if(VLCRLIB_GRAPHICS_CPU)
    set(VLCRLIB_GRAPHICS_WEBGPU OFF)
    set(VLCRLIB_ENABLE_JOLT OFF)
    set(VLCRLIB_ENABLE_VLC OFF)
    set(VLCRLIB_ENABLE_PHYSICS OFF)
    set(VLCRLIB_BUILD_TOOLS OFF)
endif()

# ==============================================================================
#  Standards
# ==============================================================================
set(CMAKE_C_STANDARD          17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD        17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS      OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# ==============================================================================
#  Compiler flags
# ==============================================================================
if(MSVC)
    add_compile_options(/W3 /MP /permissive- /wd4244 /wd4267 /wd4996 /wd4100)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    if(VLCRLIB_STRICT)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(-Wall -Wextra)
    if(VLCRLIB_STRICT)
        add_compile_options(-Werror)
    endif()
endif()

# ==============================================================================
#  Dependencies
# ==============================================================================

# --- SDL3 ---
if(EMSCRIPTEN)
    # Emscripten's SDL3 port is newer than the available 3.1.x emsdk; build
    # the vendored SDL3 source with emcc instead.
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/SDL3"
                     "${CMAKE_CURRENT_BINARY_DIR}/SDL3")
elseif(NOT TARGET SDL3::SDL3)
    find_package(SDL3 CONFIG REQUIRED)
endif()

# --- tinygltf (header-only; used by src/vgltf.cpp) ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/tinygltf/include/tiny_gltf.h")
    set(TINYGLTF_INCLUDE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/external/tinygltf/include"
        CACHE PATH "tinygltf include directory" FORCE)
else()
    find_path(TINYGLTF_INCLUDE_DIR NAMES tiny_gltf.h
        HINTS ${CMAKE_PREFIX_PATH}
        PATH_SUFFIXES include)
endif()
if(NOT TINYGLTF_INCLUDE_DIR)
    message(FATAL_ERROR "tiny_gltf.h not found (install tinygltf or add its include dir)")
endif()
message(STATUS "vlcrlib: tinygltf = ${TINYGLTF_INCLUDE_DIR}")

find_path(MINIAUDIO_INCLUDE_DIR NAMES miniaudio.h
    HINTS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES include)
if(MINIAUDIO_INCLUDE_DIR)
    message(STATUS "vlcrlib: miniaudio = ${MINIAUDIO_INCLUDE_DIR}")
else()
    message(STATUS "vlcrlib: miniaudio = stub (3D audio disabled)")
endif()

set(VLCRLIB_USE_VULKAN_VALUE 1)
if(VLCRLIB_GRAPHICS_WEBGPU OR EMSCRIPTEN OR VLCRLIB_GRAPHICS_CPU)
    set(VLCRLIB_USE_VULKAN_VALUE 0)
endif()

# --- Vulkan + glslc (desktop only) ---
if(VLCRLIB_USE_VULKAN_VALUE)
    find_package(Vulkan QUIET)
    if(NOT TARGET Vulkan::Vulkan)
        # Android NDK and some iOS/MoltenVK packages expose a raw library
        # rather than CMake's Vulkan::Vulkan target.
        find_library(VLCRLIB_VULKAN_LIBRARY NAMES vulkan MoltenVK
            HINTS ${Vulkan_LIBRARY} ${CMAKE_PREFIX_PATH}
            PATH_SUFFIXES lib lib64)
        find_path(VLCRLIB_VULKAN_INCLUDE_DIR NAMES vulkan/vulkan.h
            HINTS ${Vulkan_INCLUDE_DIRS} ${CMAKE_PREFIX_PATH}
            PATH_SUFFIXES include)
        if(NOT VLCRLIB_VULKAN_LIBRARY OR NOT VLCRLIB_VULKAN_INCLUDE_DIR)
            message(FATAL_ERROR "Vulkan не найден (нужен Vulkan/MoltenVK SDK).")
        endif()
        add_library(Vulkan::Vulkan UNKNOWN IMPORTED)
        set_target_properties(Vulkan::Vulkan PROPERTIES
            IMPORTED_LOCATION "${VLCRLIB_VULKAN_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${VLCRLIB_VULKAN_INCLUDE_DIR}")
    endif()
endif()

# --- Bullet / Box2D (optional on mobile/web) ---
set(VLCRLIB_HAS_PHYSICS 1)
if(EMSCRIPTEN OR NOT VLCRLIB_ENABLE_PHYSICS)
    set(VLCRLIB_HAS_PHYSICS 0)
else()
    find_package(Bullet CONFIG REQUIRED)
    find_package(box2d CONFIG QUIET)
    if(NOT box2d_FOUND)
        find_package(Box2D CONFIG QUIET)
    endif()
    if(NOT box2d_FOUND AND NOT Box2D_FOUND)
        message(FATAL_ERROR "Box2D не найден.")
    endif()
endif()

# --- Optional model loaders ---
find_package(assimp CONFIG QUIET)
find_package(usd CONFIG QUIET)

# --- Jolt Physics (optional, desktop) ---
set(VLCRLIB_HAS_JOLT_VALUE 0)
if(VLCRLIB_ENABLE_JOLT AND NOT EMSCRIPTEN)
    find_package(unofficial-joltphysics CONFIG QUIET)
    if(unofficial-joltphysics_FOUND)
        set(VLCRLIB_HAS_JOLT_VALUE 1)
        message(STATUS "vlcrlib: Jolt Physics found")
    else()
        find_package(Jolt CONFIG QUIET)
        if(Jolt_FOUND OR TARGET Jolt::Jolt)
            set(VLCRLIB_HAS_JOLT_VALUE 1)
            message(STATUS "vlcrlib: Jolt Physics found (Jolt::Jolt)")
        else()
            message(WARNING "Jolt not found — stubs enabled.")
        endif()
    endif()
endif()

# ==============================================================================
#  Sources
# ==============================================================================
set(VLCRLIB_SOURCES
    src/vcore.c
    src/vshapes.c
    src/vcpu.c
    src/vtext.c
    src/vtextures.c
    src/vibl.c
    src/vgui.c
    src/vinput.c
    src/vlight.c
    src/vparticles.c
    src/vbezier.c
    src/vfiles.c
    src/vnetwork.c
    src/vaudio.c
    src/vmodels.cpp
    src/vanimator.c
    src/vloaders.c
    src/vcooked.c
    src/vscene.c
    src/vaudio3d.c
    src/vfbx.cpp
    src/vusd.cpp
)

if(VLCRLIB_USE_VULKAN_VALUE)
    list(APPEND VLCRLIB_SOURCES src/vgraphics.c src/vgraphics_shadow.c src/vgraphics_material.c src/vgraphics_pipeline.c src/vgraphics_device.c src/vgraphics_rayquery.c src/vrender_graph.c src/vgltf.cpp)
else()
    # WebGPU + shared glTF loader (tinygltf)
    list(APPEND VLCRLIB_SOURCES src/vgraphics_webgpu.c src/vgltf.cpp src/vrender_graph_webgpu.c)
endif()

# Dependency-free glTF/GLB loader (hand-written parser, no third-party code).
# Declared in vfastgltf.h and documented for use by applications, but it was
# missing from this list, so FastLoadGltf()/FastLoadGltfEx() did not exist in
# the library and every caller failed at link time.
list(APPEND VLCRLIB_SOURCES src/vfastgltf.c)

if(VLCRLIB_HAS_PHYSICS)
    list(APPEND VLCRLIB_SOURCES src/vphysics.cpp src/vphysics2d.cpp)
endif()
list(APPEND VLCRLIB_SOURCES src/vjolt.cpp)

if(VLCRLIB_GRAPHICS_CPU)
    # Minimal GPU-free library: no Vulkan/WebGPU, physics, audio or tools.
    set(VLCRLIB_SOURCES
        src/vcore.c
        src/vshapes.c
        src/vcpu.c
        src/vtext.c
        src/vtextures.c
        src/vibl.c
        src/vfiles.c
        src/vgraphics_cpu.c
        src/vgltf.cpp
    )
endif()

foreach(_f ${VLCRLIB_SOURCES})
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_f}")
        message(FATAL_ERROR "vlcrlib: missing source file '${_f}'")
    endif()
endforeach()

add_library(vlcrlib STATIC ${VLCRLIB_SOURCES})
add_library(vlcrlib::vlcrlib ALIAS vlcrlib)

target_include_directories(vlcrlib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external
    ${TINYGLTF_INCLUDE_DIR})
if(MINIAUDIO_INCLUDE_DIR)
    target_include_directories(vlcrlib PRIVATE ${MINIAUDIO_INCLUDE_DIR})
    target_compile_definitions(vlcrlib PRIVATE VLCRLIB_HAS_MINIAUDIO=1)
    if(WIN32)
        target_link_libraries(vlcrlib PRIVATE ole32 winmm)
    elseif(APPLE)
        find_library(COREAUDIO_LIBRARY CoreAudio REQUIRED)
        find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox REQUIRED)
        find_library(COREFOUNDATION_LIBRARY CoreFoundation REQUIRED)
        target_link_libraries(vlcrlib PRIVATE
            ${COREAUDIO_LIBRARY} ${AUDIOTOOLBOX_LIBRARY}
            ${COREFOUNDATION_LIBRARY})
    elseif(UNIX)
        find_package(Threads REQUIRED)
        target_link_libraries(vlcrlib PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
    endif()
else()
    target_compile_definitions(vlcrlib PRIVATE VLCRLIB_HAS_MINIAUDIO=0)
endif()

if(assimp_FOUND OR TARGET assimp::assimp)
    target_compile_definitions(vlcrlib PRIVATE VLCRLIB_HAS_ASSIMP=1)
    if(TARGET assimp::assimp)
        target_link_libraries(vlcrlib PRIVATE assimp::assimp)
        if(WIN32)
            add_custom_command(TARGET vlcrlib POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<TARGET_FILE:assimp::assimp>"
                        "$<TARGET_FILE_DIR:vlcrlib>"
                COMMENT "Copying Assimp runtime next to vlcrlib")
        endif()
    endif()
    message(STATUS "vlcrlib: FBX loader = Assimp")
else()
    target_compile_definitions(vlcrlib PRIVATE VLCRLIB_HAS_ASSIMP=0)
    message(STATUS "vlcrlib: FBX loader = stub (assimp not found)")
endif()

if(usd_FOUND OR TARGET usd_ms)
    target_compile_definitions(vlcrlib PRIVATE VLCRLIB_HAS_USD=1)
    if(TARGET usd_ms)
        target_link_libraries(vlcrlib PRIVATE usd_ms)
    endif()
    message(STATUS "vlcrlib: USD loader = OpenUSD")
else()
    target_compile_definitions(vlcrlib PRIVATE VLCRLIB_HAS_USD=0)
    message(STATUS "vlcrlib: USD loader = stub (OpenUSD not found)")
endif()

# ==============================================================================
#  VLC (optional)
# ==============================================================================
if(VLCRLIB_ENABLE_VLC)
    set(_vlc_sdk "${CMAKE_CURRENT_SOURCE_DIR}/external/vlc-sdk")
    if(NOT EXISTS "${_vlc_sdk}/include/vlc/vlc.h")
        message(WARNING "VLCRLIB_ENABLE_VLC=ON, но SDK не найден: ${_vlc_sdk}")
        target_compile_definitions(vlcrlib PUBLIC VLCRLIB_NO_VLC)
    else()
        target_include_directories(vlcrlib PRIVATE "${_vlc_sdk}/include")
        if(EXISTS "${_vlc_sdk}/lib")
            target_link_directories(vlcrlib PUBLIC "${_vlc_sdk}/lib")
        endif()
        # PUBLIC: vlcrlib is a static library, so its link dependencies (and the
        # directory above) must propagate to every executable that links it.
        # Only libvlc is linked: libvlccore.dll is loaded internally by
        # libvlc.dll and the SDK ships no libvlccore.lib import library.
        target_link_libraries(vlcrlib PUBLIC libvlc)
    endif()
else()
    target_compile_definitions(vlcrlib PUBLIC VLCRLIB_NO_VLC)
endif()

# ==============================================================================
#  Link libraries
# ==============================================================================
target_link_libraries(vlcrlib PUBLIC SDL3::SDL3)

if(ANDROID)
    # SDL3 uses the Android native activity; Vulkan and logging are NDK libs.
    target_link_libraries(vlcrlib PUBLIC android log)
    set_target_properties(vlcrlib PROPERTIES POSITION_INDEPENDENT_CODE ON)
elseif(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
    foreach(_fw UIKit Foundation CoreGraphics QuartzCore CoreMotion
                AVFoundation Metal MetalKit CoreAudio AudioToolbox CoreFoundation)
        find_library(VLCRLIB_FW_${_fw} ${_fw} REQUIRED)
        target_link_libraries(vlcrlib PUBLIC ${VLCRLIB_FW_${_fw}})
    endforeach()
    set_target_properties(vlcrlib PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

if(VLCRLIB_GRAPHICS_CPU)
    target_compile_definitions(vlcrlib PUBLIC
        VLCRLIB_USE_VULKAN=0 VLCRLIB_USE_WEBGPU=0 VLCRLIB_GRAPHICS_CPU=1)
elseif(VLCRLIB_USE_VULKAN_VALUE)
    target_compile_definitions(vlcrlib PUBLIC VLCRLIB_USE_VULKAN=1 VLCRLIB_USE_WEBGPU=0)
else()
    target_compile_definitions(vlcrlib PUBLIC VLCRLIB_USE_VULKAN=0 VLCRLIB_USE_WEBGPU=1)
endif()


if(VLCRLIB_USE_VULKAN_VALUE)
    target_link_libraries(vlcrlib PUBLIC Vulkan::Vulkan)
endif()

if(VLCRLIB_HAS_PHYSICS)
    foreach(_bt BulletDynamics BulletCollision LinearMath BulletSoftBody)
        if(TARGET Bullet::${_bt})
            target_link_libraries(vlcrlib PUBLIC Bullet::${_bt})
        elseif(TARGET ${_bt})
            target_link_libraries(vlcrlib PUBLIC ${_bt})
        endif()
    endforeach()
    if(TARGET box2d::box2d)
        target_link_libraries(vlcrlib PUBLIC box2d::box2d)
    elseif(TARGET Box2D::Box2D)
        target_link_libraries(vlcrlib PUBLIC Box2D::Box2D)
    elseif(TARGET box2d)
        target_link_libraries(vlcrlib PUBLIC box2d)
    endif()
endif()

if(VLCRLIB_HAS_JOLT_VALUE)
    target_compile_definitions(vlcrlib PUBLIC VLCRLIB_HAS_JOLT=1)
    if(TARGET unofficial::joltphysics::Jolt)
        target_link_libraries(vlcrlib PUBLIC unofficial::joltphysics::Jolt)
    elseif(TARGET Jolt::Jolt)
        target_link_libraries(vlcrlib PUBLIC Jolt::Jolt)
    elseif(TARGET Jolt)
        target_link_libraries(vlcrlib PUBLIC Jolt)
    endif()
else()
    target_compile_definitions(vlcrlib PUBLIC VLCRLIB_HAS_JOLT=0)
endif()

if(WIN32)
    target_link_libraries(vlcrlib PRIVATE ws2_32)
endif()

if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    target_link_libraries(vlcrlib PRIVATE
        Threads::Threads
        m
    )
endif()

if(EMSCRIPTEN)
    # Standalone CPU rasterizer module for the WebAssembly fallback demo.
    add_custom_command(
        OUTPUT "${CMAKE_SOURCE_DIR}/web/vcpu_web.wasm"
        COMMAND ${CMAKE_C_COMPILER} -O3 -sSTANDALONE_WASM --no-entry
                "-sEXPORTED_FUNCTIONS=_vcpu_init,_vcpu_frame,_vcpu_hash"
                -I "${CMAKE_SOURCE_DIR}/src"
                "${CMAKE_SOURCE_DIR}/web/vcpu_web.c"
                "${CMAKE_SOURCE_DIR}/src/vcpu.c"
                -o "${CMAKE_SOURCE_DIR}/web/vcpu_web.wasm"
        DEPENDS "${CMAKE_SOURCE_DIR}/web/vcpu_web.c"
                "${CMAKE_SOURCE_DIR}/src/vcpu.c"
        VERBATIM)
    add_custom_target(vcpu_wasm ALL
        DEPENDS "${CMAKE_SOURCE_DIR}/web/vcpu_web.wasm")
    target_compile_options(vlcrlib PUBLIC -sUSE_WEBGPU=1)
    target_link_options(vlcrlib PUBLIC
        -sUSE_WEBGPU=1 -sASYNCIFY
        -sALLOW_MEMORY_GROWTH=1
        -sFORCE_FILESYSTEM=1
        --shell-file=${CMAKE_SOURCE_DIR}/web/shell.html
    )
endif()

# ==============================================================================
#  Shaders: GLSL -> SPIR-V (Vulkan desktop only)  [Sprint A point 2]
#  Always rebuild .spv when .vert/.frag change; copy next to lib and examples.
# ==============================================================================
set(GLSLC_CMD "")
set(SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUT_DIR}")

if(VLCRLIB_USE_VULKAN_VALUE)
    set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")

    # --- find glslc (Vulkan SDK / CMake Vulkan package / PATH) ---
    if(TARGET Vulkan::glslc)
        # Generator expression resolved at build time
        set(GLSLC_CMD "$<TARGET_FILE:Vulkan::glslc>")
        message(STATUS "vlcrlib: glslc = Vulkan::glslc")
    elseif(DEFINED Vulkan_GLSLC_EXECUTABLE AND EXISTS "${Vulkan_GLSLC_EXECUTABLE}")
        set(GLSLC_CMD "${Vulkan_GLSLC_EXECUTABLE}")
        message(STATUS "vlcrlib: glslc = ${GLSLC_CMD}")
    else()
        find_program(GLSLC_BIN NAMES glslc glslc.exe
            HINTS
                ENV VULKAN_SDK
                "$ENV{VULKAN_SDK}/Bin"
                "$ENV{VULKAN_SDK}/bin"
                ENV ANDROID_NDK_HOME
                "${ANDROID_NDK}"
                "C:/VulkanSDK"
            PATH_SUFFIXES Bin bin
                shader-tools/windows-x86_64
                shader-tools/linux-x86_64
                shader-tools/darwin-x86_64
        )
        if(GLSLC_BIN)
            set(GLSLC_CMD "${GLSLC_BIN}")
            message(STATUS "vlcrlib: glslc = ${GLSLC_CMD}")
        endif()
    endif()

    if(NOT GLSLC_CMD)
        message(FATAL_ERROR
            "glslc не найден.\n"
            "  1) Установите LunarG Vulkan SDK: https://vulkan.lunarg.com/\n"
            "  2) Или добавьте glslc в PATH\n"
            "  3) Проверьте VULKAN_SDK (например C:/VulkanSDK/1.x.x.x)")
    endif()

    # Explicit list (reliable; CONFIGURE_DEPENDS GLOB can miss IDE edits)
    set(VLCRLIB_SHADER_SOURCES
        "${SHADER_SRC_DIR}/2d.vert"
        "${SHADER_SRC_DIR}/2d.frag"
        "${SHADER_SRC_DIR}/3d.vert"
        "${SHADER_SRC_DIR}/3d.frag"
        "${SHADER_SRC_DIR}/pbr.vert"
        "${SHADER_SRC_DIR}/pbr.frag"
        "${SHADER_SRC_DIR}/skin.vert"
        "${SHADER_SRC_DIR}/shadow.vert"
        "${SHADER_SRC_DIR}/shadow_point.vert"
        "${SHADER_SRC_DIR}/shadow_point.frag"
        "${SHADER_SRC_DIR}/triangle.vert"
        "${SHADER_SRC_DIR}/triangle.frag"
    )
    # Also pick up any extra .vert/.frag/.comp not in the list
    file(GLOB _extra_shaders CONFIGURE_DEPENDS
        "${SHADER_SRC_DIR}/*.vert"
        "${SHADER_SRC_DIR}/*.frag"
        "${SHADER_SRC_DIR}/*.comp"
    )
    list(APPEND VLCRLIB_SHADER_SOURCES ${_extra_shaders})
    list(REMOVE_DUPLICATES VLCRLIB_SHADER_SOURCES)

    set(SHADER_OUTPUTS "")
    foreach(_src ${VLCRLIB_SHADER_SOURCES})
        if(NOT EXISTS "${_src}")
            continue()
        endif()
        get_filename_component(_fname "${_src}" NAME)
        set(_out "${SHADER_OUT_DIR}/${_fname}.spv")
        add_custom_command(
            OUTPUT "${_out}"
            COMMAND ${GLSLC_CMD} "${_src}" -o "${_out}"
            DEPENDS "${_src}"
            COMMENT "GLSL -> SPIR-V: ${_fname}"
            VERBATIM
        )
        list(APPEND SHADER_OUTPUTS "${_out}")
    endforeach()

    if(NOT SHADER_OUTPUTS)
        message(FATAL_ERROR "vlcrlib: no shader sources under ${SHADER_SRC_DIR}")
    endif()

    add_custom_target(vlcrlib_shaders ALL DEPENDS ${SHADER_OUTPUTS})
    add_dependencies(vlcrlib vlcrlib_shaders)

    # Copy entire shaders/ dir next to the library output (and examples depend on this)
    add_custom_command(TARGET vlcrlib_shaders POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUT_DIR}"
        COMMENT "SPIR-V shaders ready in ${SHADER_OUT_DIR}"
    )
    add_custom_command(TARGET vlcrlib POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:vlcrlib>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${SHADER_OUT_DIR}"
                "$<TARGET_FILE_DIR:vlcrlib>/shaders"
        COMMENT "Copying SPIR-V shaders next to vlcrlib"
        VERBATIM
    )

    # Aggregate "all SPIR-V is up to date" target. The Visual Studio generator
    # ignores plain file names in add_custom_command(DEPENDS ...) coming from a
    # different directory, and target_sources() turns them into no-op <None>
    # items, so a real target is the only way to order glslc before the
    # examples' post-build shader copy.
    add_custom_target(vlcrlib_spv ALL DEPENDS ${SHADER_OUTPUTS})

    # Export path for example/CMakeLists.txt
    set(VLCRLIB_SHADER_OUT_DIR "${SHADER_OUT_DIR}" CACHE INTERNAL "Compiled SPIR-V directory")
    set(VLCRLIB_SHADER_TARGET  "vlcrlib_spv"    CACHE INTERNAL "Target that builds all SPIR-V")

    # An APK ships jniLibs + assets only; the shaders/ directory copied next to
    # the library never reaches the device, so every pipeline would fail at
    # createShaderModule(). Embed the SPIR-V blobs in the library on Android.
    if(ANDROID)
        set(_vlcr_embedded_shaders
            "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_shaders.c")
        add_custom_command(
            OUTPUT "${_vlcr_embedded_shaders}"
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "${CMAKE_CURRENT_BINARY_DIR}/generated"
            COMMAND ${CMAKE_COMMAND}
                    -DSPV_DIR=${SHADER_OUT_DIR}
                    -DOUT=${_vlcr_embedded_shaders}
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_shaders.cmake"
            DEPENDS ${SHADER_OUTPUTS}
            COMMENT "Embedding SPIR-V shaders into vlcrlib (Android)"
            VERBATIM)
        target_sources(vlcrlib PRIVATE "${_vlcr_embedded_shaders}")
        target_compile_definitions(vlcrlib PRIVATE VLCRLIB_EMBEDDED_SHADERS=1)
    endif()
else()
    add_custom_target(vlcrlib_shaders ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUT_DIR}"
        COMMENT "WebGPU backend — no SPIR-V shaders")
    set(VLCRLIB_SHADER_OUT_DIR "${SHADER_OUT_DIR}" CACHE INTERNAL "Compiled SPIR-V directory")
    set(VLCRLIB_SHADER_TARGET  "vlcrlib_shaders" CACHE INTERNAL "Target that builds all SPIR-V")
endif()

# ==============================================================================
#  Asset tools
# ==============================================================================
if(VLCRLIB_BUILD_TOOLS AND NOT EMSCRIPTEN)
    add_executable(vlcrc tools/vlcrc.cpp tools/stb_impl.c)
    target_include_directories(vlcrc PRIVATE
        ${TINYGLTF_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/external
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(assimp_FOUND OR TARGET assimp::assimp)
        target_compile_definitions(vlcrc PRIVATE VLCRLIB_HAS_ASSIMP=1)
        if(TARGET assimp::assimp)
            target_link_libraries(vlcrc PRIVATE assimp::assimp)
        endif()
    else()
        target_compile_definitions(vlcrc PRIVATE VLCRLIB_HAS_ASSIMP=0)
    endif()
    find_package(Ktx CONFIG QUIET)
    if(TARGET KTX::ktx)
        target_compile_definitions(vlcrc PRIVATE VLCRLIB_HAS_KTX=1)
        target_link_libraries(vlcrc PRIVATE KTX::ktx)
        message(STATUS "vlcrlib: vlcrc KTX2/BC7/BC5 = ON")
    else()
        target_compile_definitions(vlcrc PRIVATE VLCRLIB_HAS_KTX=0)
        message(STATUS "vlcrlib: vlcrc KTX2/BC7/BC5 = OFF (Ktx not found)")
    endif()
    add_executable(vdiff tools/vdiff.cpp tools/stb_impl.c)
    target_include_directories(vdiff PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external)
    message(STATUS "vlcrlib: vlcrc asset tool = ON")
    message(STATUS "vlcrlib: vdiff golden-image tool = ON")

    if(WIN32 AND VLCRLIB_BUILD_EXAMPLES)
        add_custom_target(golden_skinned
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                    -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden_skinned.ps1"
                    -BuildDir "${CMAKE_BINARY_DIR}"
            DEPENDS golden_skinned_capture vdiff
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "Running skinned TAA golden-image test")
        if(VLCRLIB_GPU_TESTS)
            enable_testing()
            add_test(NAME golden_skinned COMMAND powershell -NoProfile
                     -ExecutionPolicy Bypass
                     -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden_skinned.ps1"
                     -BuildDir "${CMAKE_BINARY_DIR}")
        endif()
    endif()
endif()

# ==============================================================================
#  Examples
# ==============================================================================
if(VLCRLIB_BUILD_EXAMPLES)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/example/CMakeLists.txt")
        add_subdirectory(example)
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
        add_subdirectory(examples)
    else()
        message(STATUS "vlcrlib: no example/CMakeLists.txt found — skipping")
    endif()
endif()

# ==============================================================================
#  Summary
# ==============================================================================
message(STATUS "")
message(STATUS "=== vlcrlib configuration ===")
message(STATUS "  Build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  vcpkg        : ${CMAKE_TOOLCHAIN_FILE}")
message(STATUS "  SDL3         : ${SDL3_DIR}")
message(STATUS "  Vulkan       : ${Vulkan_VERSION}")
message(STATUS "  glslc        : ${GLSLC_CMD}")
message(STATUS "  Bullet       : ${Bullet_DIR}")
message(STATUS "  Jolt         : ${VLCRLIB_HAS_JOLT_VALUE}")
message(STATUS "  VLC          : ${VLCRLIB_ENABLE_VLC}")
message(STATUS "  Examples     : ${VLCRLIB_BUILD_EXAMPLES}")
message(STATUS "===============================")
message(STATUS "")

# ==============================================================================
# Unit Tests
# ==============================================================================

option(VLCRLIB_BUILD_TESTS "Build unit tests" ON)

# Common configuration for standalone test executables.
function(vlcrlib_configure_test target)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    # Linux and other Unix systems require explicit libm linking.
    if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
        target_link_libraries(${target} PRIVATE m)
    endif()
endfunction()

if(VLCRLIB_BUILD_TESTS)

    enable_testing()

    # --------------------------------------------------------------------------
    # Math
    # --------------------------------------------------------------------------

    add_executable(test_math
        tests/test_math.c
    )

    vlcrlib_configure_test(test_math)

    add_test(NAME math COMMAND test_math)

    # --------------------------------------------------------------------------
    # UBO
    # --------------------------------------------------------------------------

    add_executable(test_ubo
        tests/test_ubo.c
    )

    vlcrlib_configure_test(test_ubo)

    add_test(NAME ubo COMMAND test_ubo)

    # --------------------------------------------------------------------------
    # Flags
    # --------------------------------------------------------------------------

    add_executable(test_flags
        tests/test_flags.c
    )

    vlcrlib_configure_test(test_flags)

    add_test(NAME flags COMMAND test_flags)

    # --------------------------------------------------------------------------
    # Coordinates
    # --------------------------------------------------------------------------

    add_executable(test_coords
        tests/test_coords.c
    )

    vlcrlib_configure_test(test_coords)

    add_test(NAME coords COMMAND test_coords)

    # --------------------------------------------------------------------------
    # Animator
    # --------------------------------------------------------------------------

    add_executable(test_animator
        tests/test_animator.c
        src/vanimator.c
    )

    vlcrlib_configure_test(test_animator)

    add_test(NAME animator COMMAND test_animator)

    # --------------------------------------------------------------------------
    # Scene
    # --------------------------------------------------------------------------

    add_executable(test_scene
        tests/test_scene.c
        src/vscene.c
    )

    vlcrlib_configure_test(test_scene)

    add_test(NAME scene COMMAND test_scene)

    # --------------------------------------------------------------------------
    # Vulkan Render Graph
    # --------------------------------------------------------------------------

    if(VLCRLIB_USE_VULKAN_VALUE)

        add_executable(test_render_graph
            tests/test_render_graph.c
            src/vrender_graph.c
        )

        target_include_directories(test_render_graph PRIVATE
            ${Vulkan_INCLUDE_DIRS}
        )

        target_link_libraries(test_render_graph PRIVATE
            Vulkan::Vulkan
        )

        vlcrlib_configure_test(test_render_graph)

        add_test(NAME render_graph COMMAND test_render_graph)

    endif()

    # --------------------------------------------------------------------------
    # CPU Rasterizer
    # --------------------------------------------------------------------------

    add_executable(test_cpu_raster
        tests/test_cpu_raster.c
        tests/vibl_stub.c
        src/vcpu.c
    )

    vlcrlib_configure_test(test_cpu_raster)

    add_test(NAME cpu_raster COMMAND test_cpu_raster)

    # --------------------------------------------------------------------------
    # CPU glTF
    # --------------------------------------------------------------------------

    if(VLCRLIB_USE_VULKAN_VALUE OR VLCRLIB_GRAPHICS_CPU)

        add_executable(test_cpu_gltf
            tests/test_cpu_gltf.c
        )

        target_include_directories(test_cpu_gltf PRIVATE
            ${Vulkan_INCLUDE_DIRS}
        )

        target_link_libraries(test_cpu_gltf PRIVATE
            vlcrlib
        )

        vlcrlib_configure_test(test_cpu_gltf)

        add_test(
            NAME cpu_gltf
            COMMAND test_cpu_gltf
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/DamagedHelmet.glb"
        )

        if(WIN32 AND EXISTS "C:/vcpkg/installed/x64-windows/bin/SDL3.dll")

            set_tests_properties(cpu_gltf PROPERTIES
                ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:C:/vcpkg/installed/x64-windows/bin"
            )

        endif()

    endif()

    # --------------------------------------------------------------------------
    # C++ Header Compatibility
    # --------------------------------------------------------------------------

    add_executable(test_cpp_header
        tests/test_cpp_header.cpp
    )

    target_include_directories(test_cpp_header PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${Vulkan_INCLUDE_DIRS}
    )

    vlcrlib_configure_test(test_cpp_header)

    add_test(NAME cpp_header COMMAND test_cpp_header)

    # --------------------------------------------------------------------------
    # API Contracts
    # --------------------------------------------------------------------------

    if(VLCRLIB_USE_VULKAN_VALUE)

        add_executable(test_api_contracts
            tests/test_api_contracts.c
        )

        target_include_directories(test_api_contracts PRIVATE
            ${Vulkan_INCLUDE_DIRS}
        )

        target_link_libraries(test_api_contracts PRIVATE
            vlcrlib
        )

        vlcrlib_configure_test(test_api_contracts)

        add_test(NAME api_contracts COMMAND test_api_contracts)

        if(WIN32 AND EXISTS "C:/vcpkg/installed/x64-windows/bin/SDL3.dll")

            set_tests_properties(api_contracts PROPERTIES
                ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:C:/vcpkg/installed/x64-windows/bin"
            )

        endif()

    endif()

endif()

# ==============================================================================
#  Language bindings
#  Shared C ABI (bindings/ffi) + optional JNI bridge; see bindings/README.md.
# ==============================================================================
option(VLCRLIB_BUILD_BINDINGS "Build language bindings (vlcr_ffi shared library)" OFF)
if(VLCRLIB_BUILD_BINDINGS)
    add_subdirectory(bindings)
endif()