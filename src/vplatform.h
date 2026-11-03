#ifndef VLCRLIB_VPLATFORM_H
#define VLCRLIB_VPLATFORM_H

#include <stdbool.h>

#if defined(__EMSCRIPTEN__)
#  define VLCRLIB_PLATFORM_WEB     1
#  define VLCRLIB_PLATFORM_NAME    "Web"
#elif defined(_WIN32) || defined(_WIN64)
#  define VLCRLIB_PLATFORM_WINDOWS 1
#  define VLCRLIB_PLATFORM_NAME    "Windows"
#elif defined(__APPLE__)
#  include <TargetConditionals.h>
#  if TARGET_OS_IPHONE
#    define VLCRLIB_PLATFORM_IOS   1
#    define VLCRLIB_PLATFORM_NAME  "iOS"
#  else
#    define VLCRLIB_PLATFORM_MACOS 1
#    define VLCRLIB_PLATFORM_NAME  "macOS"
#  endif
#elif defined(__ANDROID__)
#  define VLCRLIB_PLATFORM_ANDROID 1
#  define VLCRLIB_PLATFORM_NAME    "Android"
#elif defined(__linux__)
#  define VLCRLIB_PLATFORM_LINUX   1
#  define VLCRLIB_PLATFORM_NAME    "Linux"
#else
#  define VLCRLIB_PLATFORM_UNKNOWN 1
#  define VLCRLIB_PLATFORM_NAME    "Unknown"
#endif

typedef enum VRenderBackend {
    VRENDER_BACKEND_AUTO = 0,
    VRENDER_BACKEND_GPU,
    VRENDER_BACKEND_CPU
} VRenderBackend;

bool           VRenderBackendSelect(VRenderBackend backend);
VRenderBackend VRenderBackendGet(void);
const char    *VRenderBackendName(void);
bool           VRenderBackendAvailable(VRenderBackend backend);

#if defined(VLCRLIB_PLATFORM_ANDROID) || defined(VLCRLIB_PLATFORM_IOS)
#  define VLCRLIB_PLATFORM_MOBILE 1
#endif
#if defined(VLCRLIB_PLATFORM_WINDOWS) || defined(VLCRLIB_PLATFORM_MACOS) || \
    defined(VLCRLIB_PLATFORM_LINUX)
#  define VLCRLIB_PLATFORM_DESKTOP 1
#endif

/* CPU-only target → CPU | Desktop → Vulkan | Web → WebGPU */
#if defined(VLCRLIB_GRAPHICS_CPU) && VLCRLIB_GRAPHICS_CPU
#  define VLCRLIB_USE_VULKAN 0
#  define VLCRLIB_USE_WEBGPU 0
#elif defined(VLCRLIB_GRAPHICS_WEBGPU) && VLCRLIB_GRAPHICS_WEBGPU
#  define VLCRLIB_USE_VULKAN 0
#  define VLCRLIB_USE_WEBGPU 1
#elif defined(__EMSCRIPTEN__)
#  define VLCRLIB_USE_VULKAN 0
#  define VLCRLIB_USE_WEBGPU 1
#else
#  ifndef VLCRLIB_USE_VULKAN
#    define VLCRLIB_USE_VULKAN 1
#  endif
#  ifndef VLCRLIB_USE_WEBGPU
#    define VLCRLIB_USE_WEBGPU 0
#  endif
#endif

#if VLCRLIB_USE_VULKAN
#  define VLCRLIB_GRAPHICS_NAME "Vulkan"
#elif VLCRLIB_USE_WEBGPU
#  define VLCRLIB_GRAPHICS_NAME "WebGPU"
#elif defined(VLCRLIB_GRAPHICS_CPU) && VLCRLIB_GRAPHICS_CPU
#  define VLCRLIB_GRAPHICS_NAME "CPU"
#else
#  define VLCRLIB_GRAPHICS_NAME "None"
#endif

#endif
