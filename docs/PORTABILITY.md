# Portability

vlcrlib targets Windows, Linux and macOS desktop with the same public C API.
The headers are C11/C++17 clean and do not expose HWND, HANDLE, X11, Cocoa or
platform sockets.

## Supported matrix

| Platform | Window / input | Graphics | Audio | File API |
|---|---|---|---|---|
| Windows 10/11 | SDL3 | Vulkan 1.1+ | miniaudio (WASAPI), SDL3/VLC | stdio / Win32 stat |
| Linux (X11/Wayland) | SDL3 | Vulkan 1.1+ | miniaudio (Pulse/ALSA), SDL3/VLC | stdio / POSIX stat |
| macOS 12+ | SDL3 | MoltenVK | miniaudio (CoreAudio), SDL3/VLC | stdio / POSIX stat |
| Web / Emscripten | SDL3 | WebGPU backend | SDL3 | stdio + Emscripten FS |

## Build requirements

Common: CMake 3.20+, a C11/C++17 compiler, SDL3, tinygltf, and on desktop a
Vulkan loader + `glslc`. Physics additionally needs Bullet and Box2D; Jolt,
Assimp, USD, KTX and miniaudio are optional.

### Linux

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DVLCRLIB_SKIP_VCPKG=ON \
  -DCMAKE_PREFIX_PATH=/usr/local \
  -DVLCRLIB_ENABLE_VLC=OFF
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Install the distro equivalents of `libsdl3-dev`, `libvulkan-dev`,
`vulkan-tools`, `glslc`, `libbullet-dev`, `libbox2d-dev`, `libtinygltf-dev`;
miniaudio is header-only. The code links `dl`, `m`, and pthread for miniaudio.

### macOS

```bash
brew install cmake ninja sdl3 vulkan-sdk bullet box2d tinygltf
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DVLCRLIB_SKIP_VCPKG=ON \
  -DVLCRLIB_ENABLE_VLC=OFF
cmake --build build -j
ctest --test-dir build --output-on-failure
```

MoltenVK requires the portability instance/device extensions. `createInstance`
enables `VK_KHR_portability_enumeration` and
`VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR`; `createDevice` enables
`VK_KHR_portability_subset` when the device advertises it.

### Windows

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 `
  -DVLCRLIB_SKIP_VCPKG=ON `
  -DCMAKE_PREFIX_PATH=C:/vcpkg/installed/x64-windows `
  -DVLCRLIB_ENABLE_VLC=OFF
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## API rules

- **Paths**: UTF-8, `/` accepted on every platform; `\` is accepted on
  Windows. Do not pass UTF-16 paths.
- **Window handle**: `GetWindowHandle()` returns an SDL window pointer, not a
  native HWND/NSWindow/X11 window.
- **Socket handle**: `NetSocket.handle` is an `intptr_t`; on Windows it holds a
  `SOCKET`, on POSIX an `int`.
- **Threads**: graphics and audio calls are main-thread unless documented
  otherwise. Audio node setters currently assume the caller is not racing the
  audio callback.
- **Color/vector types**: plain C structs; no compiler-specific vector types.
- **File I/O**: use `vfiles.h`; it is implemented on stdio + POSIX `stat`
  (Windows uses `_stat`/`_mkdir`) and never exposes a platform file handle.

## Android

Use the NDK toolchain file. SDL3 must be built/installed for the same ABI.

```bash
cmake -S . -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release \
  -DVLCRLIB_SKIP_VCPKG=ON \
  -DVLCRLIB_ENABLE_VLC=OFF \
  -DVLCRLIB_BUILD_EXAMPLES=ON
cmake --build build-android -j
```

The checked-in `android/` Gradle project builds `arm64-v8a` APKs end to end:
Gradle 8.9, NDK 26.1.10909125, SDK 35, CMake 3.22.1. It produces
`libmain.so`, `libSDL3.so` and `libc++_shared.so`; minSdk is 28 because the
Vulkan 1.1/1.2 entry points are resolved at runtime. SDL3's native activity
supplies the Android lifecycle. Use `SDL_IOFromFile`-backed `vfiles` for assets
placed under `assets/`; tinygltf `.bin`/image resources use the same layer.

## iOS

```bash
cmake -S . -B build-ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DVLCRLIB_SKIP_VCPKG=ON \
  -DVLCRLIB_ENABLE_VLC=OFF \
  -DVLCRLIB_BUILD_EXAMPLES=ON
cmake --build build-ios --config Release
```

CMake links UIKit, Foundation, CoreGraphics, QuartzCore, CoreMotion,
AVFoundation, Metal/MetalKit, CoreAudio, AudioToolbox and CoreFoundation.
MoltenVK supplies Vulkan. The example is a `MACOSX_BUNDLE`; add
`assets/Roboto-Medium.ttf` and shader `.spv` files to the bundle resources for
a standalone app. tinygltf filesystem callbacks now use `SDL_IOFromFile`, so
external `.bin`/image resources resolve through the same mobile asset layer as
GLB/embedded resources.

## Web / Emscripten

Install Emscripten **3.1.58** (the legacy `-sUSE_WEBGPU=1` port; 6.x's
`emdawnwebgpu` uses an incompatible C API). The build vendors SDL3 from
`external/SDL3`; no `-sUSE_SDL=3` port is required.

```bash
emcmake cmake -S . -B build-web -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DVLCRLIB_SKIP_VCPKG=ON \
  -DVLCRLIB_GRAPHICS_WEBGPU=ON \
  -DVLCRLIB_BUILD_EXAMPLES=ON
cmake --build build-web -j
```

The build emits `.html` examples with `--preload-file assets@/assets`. All six
web examples link successfully with the legacy swapchain API. Runtime validation
now passes: `web_validate_example` reports `frames=150 errors=0` in Chrome 156
headless with `--use-angle=d3d11` (see `docs/WEBGPU_RUNTIME.md`). The WebGPU
backend is still a smaller feature set than Vulkan: 2D, PBR, glTF, meshes/shapes,
FXAA, TAA, bloom, SSAO, 4-cascade + spot + point-cube shadows, point lights, skinned glTF/animation and IBL; render-graph passes
are not implemented there. `vfiles` uses SDL's Emscripten filesystem, which is
why assets must be preloaded.

## Header-only checks

These compile with just `-I src` and no SDL/Vulkan:

```bash
g++ -std=c++17 -I src -c tests/test_cpp_header.cpp
gcc -std=c11   -I src -c tests/test_api_contracts.c   # link stage needs vlcrlib
```

The CMake targets `test_cpp_header` and `api_contracts` exercise the same
contract on the host platform.

## Known portability gaps

- `vgraphics_Resize()` reallocates the offscreen scene/TAA/velocity/SSAO/bloom
  targets and refreshes their descriptors; it is platform-independent.
- VLC is optional and the checked-in SDK is Windows-shaped. On Linux/macOS use
  `-DVLCRLIB_ENABLE_VLC=OFF` (SDL3 WAV fallback) or provide a matching SDK.
- The golden-image test target is Windows-only (`powershell`); use `vdiff`
  directly on other hosts.
- `LoadTextureKTX2()` uploads the mip chain; Vulkan accepts BC7/BC5 and WebGPU
  requires `texture-compression-bc` (otherwise it returns an empty texture).
- Web builds use the separate WebGPU backend, not Vulkan; that backend is a
  subset (no render-graph passes; FXAA, TAA, bloom, SSAO, 4-cascade shadows,
  point/spot lights and skinned glTF exist).
- Android/iOS ship native CMake targets and a minimal `mobile_hello` example,
  but not a ready Gradle/Xcode application package.
