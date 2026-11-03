# ==============================================================================
#  vlcrlib examples
# ==============================================================================

if(VLCRLIB_GRAPHICS_CPU)
    set(EXAMPLE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/cpu_gltf_example.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/cpu_3d_example.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/cpu_2d_example.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/cpu_raster_example.c")
elseif(EMSCRIPTEN)
    set(EXAMPLE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/web_hello.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/web_3d_example.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/scene3d_example.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/cgltf_min_example.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/web_gltf_example.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/web_2d_example.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/web_validate_example.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/cross_backend_example.c")
elseif(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # One small SDL entry point; desktop-only examples are excluded.
    set(EXAMPLE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/mobile_hello.c")
else()
    file(GLOB EXAMPLE_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    )
    list(REMOVE_ITEM EXAMPLE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/mobile_hello.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/web_validate_example.c")
endif()

if(NOT EXAMPLE_SOURCES)
    message(FATAL_ERROR "example: no sources found")
endif()

if(CMAKE_CONFIGURATION_TYPES)
    set(EXAMPLE_BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/Release")
else()
    set(EXAMPLE_BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}")
endif()
set(EXAMPLE_SHADER_DIR "${EXAMPLE_BIN_DIR}/shaders")
# Always use the top-level build/shaders directory (where glslc writes SPIR-V)
set(_VLCR_SPV_SRC "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${_VLCR_SPV_SRC}")
message(STATUS "example: shader src = ${_VLCR_SPV_SRC}")
message(STATUS "example: shader dst = ${EXAMPLE_SHADER_DIR}")

# Generate the copy script once. It is invoked with -P only.
# IMPORTANT: the Visual Studio generator mangles quoted -D values that contain
# spaces/backslashes, so the source directory is not passed on the command line:
# the script takes it from the environment variable set below (and falls back to
# its relative location next to this build tree).
set(_VLCR_SPV_SRC_ENV "${_VLCR_SPV_SRC}")
file(TO_NATIVE_PATH "${_VLCR_SPV_SRC}" _VLCR_SPV_SRC_NATIVE)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/copy_shaders.cmake" [=[
set(_src "${SRC}")
if(NOT EXISTS "${_src}")
  # 1) environment variable exported for the whole build (see below)
  if(DEFINED ENV{VLCRLIB_SHADER_OUT_DIR} AND EXISTS "$ENV{VLCRLIB_SHADER_OUT_DIR}")
    set(_src "$ENV{VLCRLIB_SHADER_OUT_DIR}")
  endif()
endif()
if(NOT EXISTS "${_src}")
  # 2) relative to this script: <build>/example/../shaders
  get_filename_component(_cand "${CMAKE_CURRENT_LIST_DIR}/../shaders" ABSOLUTE)
  if(EXISTS "${_cand}")
    set(_src "${_cand}")
  endif()
endif()
if(NOT EXISTS "${_src}")
  message(FATAL_ERROR "copy_shaders: SPIR-V directory not found (SRC='${SRC}')")
endif()
# The Visual Studio generator wraps -D values in literal quotes; strip them.
string(REGEX REPLACE "^\"|\"$" "" _dst "${DST}")
if(_dst STREQUAL "")
  message(FATAL_ERROR "copy_shaders: destination not set (DST='${DST}')")
endif()
file(MAKE_DIRECTORY "${_dst}")
file(COPY "${_src}/" DESTINATION "${_dst}")
message(STATUS "copy_shaders: ${_src} -> ${_dst}")
]=])

# Make the shader directory visible to the -P script without relying on -D.
set(ENV{VLCRLIB_SHADER_OUT_DIR} "${_VLCR_SPV_SRC_NATIVE}")

add_custom_target(vlcrlib_example_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Debug/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${_VLCR_SPV_SRC}" "${CMAKE_CURRENT_BINARY_DIR}/Debug/shaders"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Release/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${_VLCR_SPV_SRC}" "${CMAKE_CURRENT_BINARY_DIR}/Release/shaders"
    COMMENT "Copying SPIR-V shaders from ${_VLCR_SPV_SRC} to Debug/Release example shaders"
    VERBATIM)
add_dependencies(vlcrlib_example_shaders vlcrlib_shaders)
# Every example depends on this target. The SPIR-V build target (exported by the
# top-level CMakeLists) is a dependency of it too, so the copy above can never
# run before glslc has produced the .spv files. Building an example therefore
# always leaves ${EXAMPLE_SHADER_DIR} populated, even on a clean tree.
if(VLCRLIB_SHADER_TARGET)
    add_dependencies(vlcrlib_example_shaders ${VLCRLIB_SHADER_TARGET})
endif()
# The Visual Studio generator does not order POST_BUILD steps against other
# targets, so the copy above/here also has an explicit dependency on the target
# that produces the .spv files (exported by the top-level CMakeLists).
if(VLCRLIB_SHADER_TARGET)
    add_dependencies(vlcrlib_example_shaders ${VLCRLIB_SHADER_TARGET})
endif()

# --- VLC runtime next to examples -------------------------------------------
# vlcrlib links libvlc; Windows resolves libvlc.dll / libvlccore.dll and the
# plugins directory at runtime. Stage them for both Debug and Release so
# audio_example works from a clean build without a system VLC install/PATH.
if(WIN32 AND VLCRLIB_ENABLE_VLC)
    set(_vlc_runtime "${CMAKE_SOURCE_DIR}/external/vlc-sdk/lib")
    if(EXISTS "${_vlc_runtime}/libvlc.dll")
        add_custom_target(vlcrlib_vlc_runtime ALL
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Release"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Debug"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_vlc_runtime}/libvlc.dll"
                    "${CMAKE_CURRENT_BINARY_DIR}/Release/libvlc.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_vlc_runtime}/libvlccore.dll"
                    "${CMAKE_CURRENT_BINARY_DIR}/Release/libvlccore.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${_vlc_runtime}/plugins"
                    "${CMAKE_CURRENT_BINARY_DIR}/Release/plugins"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_vlc_runtime}/libvlc.dll"
                    "${CMAKE_CURRENT_BINARY_DIR}/Debug/libvlc.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_vlc_runtime}/libvlccore.dll"
                    "${CMAKE_CURRENT_BINARY_DIR}/Debug/libvlccore.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${_vlc_runtime}/plugins"
                    "${CMAKE_CURRENT_BINARY_DIR}/Debug/plugins"
            COMMENT "Staging VLC runtime (libvlc, libvlccore, plugins)")
    endif()
endif()

# All Visual Studio examples share one configuration output directory. Choose
# one target to stage common runtime DLLs and the shared glTF asset; parallel
# POST_BUILD copies from every executable race on Windows and can fail with
# ERROR_SHARING_VIOLATION. Other examples wait for the staging owner.
list(GET EXAMPLE_SOURCES 0 _VLCR_EXAMPLE_RUNTIME_OWNER_SRC)
get_filename_component(_VLCR_EXAMPLE_RUNTIME_OWNER "${_VLCR_EXAMPLE_RUNTIME_OWNER_SRC}" NAME_WE)
set(_VLCR_PREVIOUS_EXAMPLE "")

foreach(_src ${EXAMPLE_SOURCES})
    get_filename_component(_name "${_src}" NAME_WE)
    add_executable(${_name} "${_src}")
    target_link_libraries(${_name} PRIVATE vlcrlib)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(${_name} PRIVATE SDL3::SDL3main)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set_target_properties(${_name} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST
                "${CMAKE_SOURCE_DIR}/platform/ios/Info.plist")
    endif()
    add_dependencies(${_name} vlcrlib_example_shaders)
    if(TARGET vlcrlib_vlc_runtime)
        add_dependencies(${_name} vlcrlib_vlc_runtime)
    endif()
    # vcpkg also runs an app-local copy after every link. The VS generator
    # does not honor USES_TERMINAL as a serialization lock, so order example
    # targets to keep those writes to the shared Release directory exclusive.
    # The serialization dependency exists only to avoid Windows DLL copy races.
    if(_VLCR_PREVIOUS_EXAMPLE AND NOT EMSCRIPTEN)
        add_dependencies(${_name} ${_VLCR_PREVIOUS_EXAMPLE})
    endif()
    set(_VLCR_PREVIOUS_EXAMPLE "${_name}")

    if(EMSCRIPTEN)
        set_target_properties(${_name} PROPERTIES SUFFIX ".html")
        target_link_options(${_name} PRIVATE
            -sUSE_WEBGPU=1 -sASYNCIFY
            -sALLOW_MEMORY_GROWTH=1
            --preload-file=${CMAKE_SOURCE_DIR}/assets@/assets
            --shell-file=${CMAKE_SOURCE_DIR}/web/shell.html
        )
        if(_name STREQUAL "web_validate_example")
            # Embed JS + wasm in the HTML so endpoint security cannot lock a
            # separate .js file; the preload .data file stays separate.
            target_link_options(${_name} PRIVATE -sSINGLE_FILE=1)
        endif()
    else()
        set_target_properties(${_name} PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${_name}>")
        if("${_name}" STREQUAL "${_VLCR_EXAMPLE_RUNTIME_OWNER}")
            add_custom_command(TARGET ${_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_RUNTIME_DLLS:${_name}>
                        "$<TARGET_FILE_DIR:${_name}>"
                COMMAND_EXPAND_LISTS
                COMMENT "Copying shared runtime DLLs next to examples")
            # The example binaries use the same working/output directory.
            if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/DamagedHelmet.glb")
                add_custom_command(TARGET ${_name} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${CMAKE_CURRENT_SOURCE_DIR}/DamagedHelmet.glb"
                            "$<TARGET_FILE_DIR:${_name}>/DamagedHelmet.glb"
                    COMMENT "Copying shared DamagedHelmet.glb asset")
            endif()
        endif()
        # VLC runtime (libvlc.dll + libvlccore.dll + plugins/) next to the audio
        # example. libvlc locates its codecs in a "plugins" folder relative to
        # libvlccore.dll, so all three must live beside the executable — the
        # generic $<TARGET_RUNTIME_DLLS> copy above does not pull them in.
        if(VLCRLIB_ENABLE_VLC AND _name STREQUAL "audio_example")
            set(_vlc_lib "${CMAKE_CURRENT_SOURCE_DIR}/../external/vlc-sdk/lib")
            add_custom_command(TARGET ${_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_vlc_lib}/libvlc.dll"
                        "${_vlc_lib}/libvlccore.dll"
                        "$<TARGET_FILE_DIR:${_name}>"
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${_vlc_lib}/plugins"
                        "$<TARGET_FILE_DIR:${_name}>/plugins"
                COMMENT "Deploying VLC runtime next to ${_name}"
                VERBATIM)
        endif()
        # SPIR-V next to the exe is handled by the vlcrlib_example_shaders target
        # above, which this example already depends on. A POST_BUILD copy must
        # NOT be used here: the Visual Studio generator ran it before glslc on
        # clean builds, leaving this folder empty and making the example fail at
        # startup with VKINIT_ERR_PIPELINE.
    endif()
endforeach()
