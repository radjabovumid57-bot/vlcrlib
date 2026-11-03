# AAA Readiness

## Scores (production AAA baseline)

| Area | Score /10 | Notes |
|---|---:|---|
| C17 core / portability | 7 | C17 fixed; compact but monolithic Vulkan unit |
| Public API | 6 | handle/NULL contracts passed; no capability API; stubs |
| SDL3 integration | 7 | Windows/Web/Android build paths; input/fullscreen untested |
| Vulkan renderer | 7 | validation-clean PBR path; no device-loss/OOM/MT |
| WebGPU renderer | 6 | 150-frame runtime/golden; no-op APIs; headless timestamp gap |
| Shader system | 7 | SPIR-V 26/26; runtime shader loading is a stub |
| Resource management | 5 | generations/deferred release; dedicated allocations only |
| Memory safety | 6 | map OOM paths fixed; no ASan/UBSan/RenderDoc evidence |
| Cross-platform | 4 | Windows + Web + Android build; Linux/macOS/iOS/ARM64 unverified |
| Rendering correctness | 8 | main golden exact, cross golden 1.552 PASS |
| Performance | 4 | 1k ~9.8ms, 10k ~42.8ms, 100k ~390ms CPU; no MT/instancing |
| Stress/stability | 5 | 1k–100k no crash; device loss/OOM/MT not tested |
| Testing/CI | 5 | 11 ctest + external WebGPU/cross; GPU tests opt-in |

## Overall

**AAA-ready: NO.**  
**Small/mid-scope production: CONDITIONAL.**

Strengths: real Vulkan/WebGPU backends, validation-clean main path, deterministic
goldens, C17 + SDL3, PBR/glTF/shadows/IBL/skinning, Android arm64/x86_64 builds.

Blocking AAA work:

1. Device-lost/OOM recovery.
2. Multithreaded command recording + transfer queue.
3. WebGPU public API parity/capability reporting.
4. Cross-platform CI: Linux, macOS/iOS/MoltenVK, Windows ARM64/Clang.
5. Instancing/batching and large-world streaming.
6. Allocator/suballocation and memory telemetry.
7. ASan/UBSan/RenderDoc/GPU-assisted validation in CI.

## Missing subsystems (not bugs)

Virtual geometry, world partition/streaming, job system, compute culling/GI,
temporal upscalers, multi-window/HDR swapchain, production asset cooker.
