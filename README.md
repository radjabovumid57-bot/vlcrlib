# vlcrlib

A C game and graphics library with a stable C ABI: SDL3 platform layer plus
Vulkan, WebGPU and a GPU-free CPU rasterizer backend, and idiomatic bindings for
C++, Python, C# and Java.

```c
#include <vlcrlib.h>

int main(void) {
    if (!InitWindow(1280, 720, "Hello vlcrlib")) return 1;
    SetTargetFPS(60);

    Camera3D camera = {
        .position = { 6, 4, 8 }, .target = { 0, 1, 0 }, .up = { 0, 1, 0 },
        .fovy = 55.0f, .nearPlane = 0.1f, .farPlane = 200.0f,
    };

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(RAYWHITE);
        BeginMode3D(camera);
            DrawGrid(20, 1.0f);
            DrawCube((Vector3){ 0, 0.5f, 0 }, (Vector3){ 1, 1, 1 }, RED);
        EndMode3D();
        DrawText("Hello from C", 20, 20, 24, DARKGRAY);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
```

## Features

- **Platform / core** — window, input (keyboard, mouse, gamepad, touch), timing,
  file helpers, camera modes (`vcore`, `vinput`, `vfiles`, `vnetwork`).
- **Renderer** — Vulkan backend (PBR, shadow maps, IBL, SSAO, bloom, hardware ray
  query, skybox), a WebGPU backend for the web, and a CPU rasterizer used for
  headless/GPU-less builds. Shaders are GLSL compiled to SPIR-V by `glslc`.
- **2D** — immediate-mode shapes, batched sprite/texture drawing, bitmap text,
  and a small GUI toolkit (`vgui`).
- **3D content** — glTF/GLB, FBX and USD loaders, meshes/materials, skinned
  models with animation and IK, procedural shapes, particles, Bézier curves.
- **Physics** — Jolt (3D), Bullet and Box2D (2D) backends with a shared body API.
- **Audio** — libVLC/miniaudio backends, sounds, music streams, 3D positional audio.
- **Bindings** — C++, Python, C#, Java (see [docs/BINDINGS.md](docs/BINDINGS.md)).

## Requirements

| | |
| --- | --- |
| CMake | 3.20+ |
| Compiler | C17 + C++17 (MSVC 2022/2026, GCC, Clang) |
| Dependencies | vcpkg manifest ([vcpkg.json](vcpkg.json)): SDL3, Vulkan, Jolt, Bullet, Box2D, fastgltf/tinygltf |
| Shaders | `glslc` from the Vulkan SDK (desktop Vulkan builds) |

## Build

Windows (Visual Studio 2026, vcpkg):

```powershell
build_windows_vs2026.bat
```

Or with CMake presets:

```powershell
cmake --preset default              # VS 2026 x64, Vulkan, vcpkg
cmake --build --preset release

cmake --preset cpu                  # GPU-free CPU backend, no Vulkan needed
cmake --build --preset cpu-release
```

Linux / macOS / web:

```bash
./build_linux.sh
./build_macos.sh
./build_web.sh
```

Useful options: `-DVLCRLIB_GRAPHICS_CPU=ON` (CPU renderer only),
`-DVLCRLIB_GRAPHICS_WEBGPU=ON`, `-DVLCRLIB_ENABLE_JOLT=OFF`,
`-DVLCRLIB_ENABLE_PHYSICS=OFF`, `-DVLCRLIB_ENABLE_VLC=OFF`,
`-DVLCRLIB_BUILD_BINDINGS=ON`.

## Examples

`example/` contains runnable programs, from a minimal window to glTF viewers,
physics, particles, GUI and the CPU backend. Build them with
`-DVLCRLIB_BUILD_EXAMPLES=ON` (on by default) and run e.g. `basic_window`.

## Bindings

C++ (header-only), Python (`ctypes`), C# (P/Invoke) and Java (JNI) packages live
in `bindings/`. They share one stable C ABI (`bindings/ffi/vlcr_ffi.h`) built as
`vlcr_ffi`, so a single native build serves every language:

```powershell
scripts/build_bindings.ps1          # builds the libraries, stages bindings/dist, runs smoke tests
```

Start with [bindings/README.md](bindings/README.md).

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — module layout
- [docs/BINDINGS.md](docs/BINDINGS.md) — language bindings
- [docs/PORTABILITY.md](docs/PORTABILITY.md) — platform notes
- [docs/CPU_BACKEND.md](docs/CPU_BACKEND.md), [docs/WEBGPU_PARITY.md](docs/WEBGPU_PARITY.md) — backends
- [docs/ASSET_PIPELINE.md](docs/ASSET_PIPELINE.md), [docs/MILESTONE_RENDERING.md](docs/MILESTONE_RENDERING.md)
- [AAA_ROADMAP.md](AAA_ROADMAP.md), [CHEATSHEET.html](CHEATSHEET.html) — API cheat sheet

## License

No license file has been added yet; until one is chosen, the code is
“all rights reserved”. Third-party components keep their own licenses
(see `external/`).
