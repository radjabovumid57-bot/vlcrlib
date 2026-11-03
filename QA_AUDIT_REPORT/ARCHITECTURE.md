# Architecture Audit — C17 + SDL3 + Vulkan + WebGPU

## Fixed stack compliance

| Requirement | Status | Evidence |
|---|---|---|
| C17 | **FIXED** | CMake had `CMAKE_C_STANDARD 11`; changed to `17`; Windows/Android/Web builds pass |
| C++17 for C++ sources | PASS | `CMAKE_CXX_STANDARD 17` |
| SDL3 | PASS | `external/SDL3`; Windows DLL, Emscripten SDL port, Android Gradle SDL3 target |
| Vulkan native | PASS | Windows MSVC x64 runtime/validation |
| WebGPU web | PASS | Emscripten `-sUSE_WEBGPU=1`, 150-frame runtime/golden |
| CMake | PASS | all tested builds via CMake/Gradle+CMake |
| No DirectX/OpenGL/GLES backend | PASS | no such backend sources; WebGL2 fallback absent (see BUG-013) |

## Module layout

```
vlcrlib.h
 ├─ vcore.c            SDL3 window/events/time/input
 ├─ vgraphics.c        Vulkan device/swapchain/passes/draw API
 ├─ vgraphics_*        device, pipeline, material, shadow, ray query
 ├─ vgraphics_webgpu.c WebGPU implementation of the same public API
 ├─ vgraphics_cpu.c    GPU-free raster (not one of the two GPU backends)
 ├─ vrender_graph.c    Vulkan post/SSAO graph
 ├─ vrender_graph_webgpu.c WebGPU graph wrapper
 ├─ vscene/vanimator/vibl/vgui/vphysics/...
 └─ vfastgltf/vgltf/vfbx/vusd loaders
```

Backend selection is compile-time (`VLCRLIB_USE_VULKAN`, `VLCRLIB_USE_WEBGPU`,
`VLCRLIB_GRAPHICS_CPU`). SDL3 is the only window/input layer in all builds.

## Findings

### A-01 — Language standard mismatch (FIXED)

`CMakeLists.txt` declared C11 although the required stack is C17. Changed to
`C_STANDARD 17`; all tested builds compile and tests pass.

### A-02 — Backend API parity is not complete

The public header is shared, but WebGPU has documented no-ops and different
feature limits. See `API_AUDIT.md`; a capability query API is missing.

### A-03 — Monolithic Vulkan translation unit

`src/vgraphics.c` is ~8.5k lines and mixes device-adjacent, resource, draw and
public API code. `vgraphics_*` modules split some subsystems, but the main unit
has low cohesion. Maintainability risk, not a correctness failure.

### A-04 — Thin abstraction / render graph

The public API is immediate-mode; Vulkan/WebGPU share signatures but not a
backend-neutral RHI abstraction. `VrgGraph` is used for post/SSAO; the full
shadow→scene→post graph and multithreaded recording do not exist.

### A-05 — No WebGL2 fallback

The repo contains no WebGL/GLES backend. The fixed requirements forbid OpenGL
backends, so WebGL2 fallback is **NOT IMPLEMENTED** by design. Web support is
WebGPU-only and browser-dependent.

## Verdict

Architecture matches the required stack after the C17 fix, with SDL3 cleanly
isolated. It is a compact engine architecture, not an AAA-scale ECS/render-graph
architecture; backend parity and device-loss/threading infrastructure are the
main structural gaps.
