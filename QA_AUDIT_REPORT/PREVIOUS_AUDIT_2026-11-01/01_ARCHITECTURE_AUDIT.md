# 01 — Architecture & Source Audit

## Inventory

- C/C++ sources: 67 files under `src/`, ~20k lines.
- Main backends:
  - `vgraphics.c` (7.7k lines) + `vgraphics_device.c`, `_pipeline.c`, `_material.c`, `_shadow.c`, `_rayquery.c` — Vulkan.
  - `vgraphics_webgpu.c` (2.7k lines) — Emscripten/WebGPU.
  - `vgraphics_cpu.c`, `vcpu.c` — GPU-free raster backend.
- Shared modules: `vfastgltf.c`, `vgltf.cpp`, `vscene.c`, `vanimator.c`, `vibl.c`,
  `vrender_graph.c`, physics, audio, GUI, input, network.
- Public umbrella: `src/vlcrlib.h`; graphics API `src/vgraphics.h`; RHI `src/vlcr_rhi.h`.
- Tests: 11 CTest entries, 13 source files under `tests/`.
- Examples: 60+ under `example/`.

## Module dependencies (observed)

```
application
  -> vlcrlib.h / vgraphics.h (public API)
     -> vgraphics.c (Vulkan device/pipeline/draw)
        -> vgraphics_device/pipeline/material/shadow/rayquery
        -> vrender_graph.c (post/SSAO graph)
        -> vgltf.cpp / vfastgltf.c (asset load)
     -> vgraphics_webgpu.c (mutually exclusive backend)
     -> vgraphics_cpu.c -> vcpu.c (GPU-free)
  -> vscene/vanimator/vibl/vgui/vphysics/...
```

CMake selects exactly one graphics implementation (`vgraphics.c` *or*
`vgraphics_webgpu.c` *or* `vgraphics_cpu.c`). Shared modules are linked into all
backends; CPU config intentionally omits Vulkan-only sources.

## Findings

### A-01 — Monolithic Vulkan file (P3)

`src/vgraphics.c` is 7.7k lines and mixes:

- instance/device adjacent helpers;
- swapchain presentation;
- resource lifetime and deferred deletion;
- PBR/skin/shadow recording;
- 2D/3D state;
- public API.

It is split partially into `vgraphics_*` files, but the main translation unit still
has low cohesion. This is a maintainability risk, not a correctness bug.

### A-02 — Backend API duplication (P3)

`vgraphics.h` is implemented three times: Vulkan, WebGPU, CPU. Several functions
have different fidelity:

- WebGPU `SetShadowQuality`, `vgraphics_ComputeDirectionalShadow*`,
  `vgraphics_UpdateShadowMatrix`, `vgraphics_SetSpotShadows`,
  `vgraphics_SetPointShadows` are no-ops (QA-007).
- Vulkan `LoadShader`/`UnloadShader` are documented stubs (QA-006).

This is an API-contract trap: callers cannot tell backend support without
documentation.

### A-03 — Stubs / TODO-like surface (P3)

- `LoadShader()` returns `(Shader){0}` in Vulkan and WebGPU.
- `vfastgltf_lib_stub.c`, `vfbx.cpp`, `vusd.cpp`, `vjolt.cpp` contain intentional
  optional-dependency stubs. They fail safely, but AAA asset pipelines need a
  capability query before use.
- No `TODO/FIXME` markers remain in core `.c` files; stubs are documented.

### A-04 — ABI / headers (P3)

- Static library; no DLL export macros. ABI is C for the public C API.
- `test_cpp_header.cpp` compiles the public headers as C++ and passes.
- Public handles are POD structs with `id`/`generation`; generation checks exist
  in texture/mesh/material table lookups.
- No packing attributes observed on public structs; default ABI used consistently
  between C and C++.

### A-05 — Duplication / SOLID (P3, informational)

- Vulkan and WebGPU material/descriptor logic is duplicated.
- Shadow matrix/cascade logic exists in both backends with different APIs.
- Immediate-mode draw API is intentional; this is not a SOLID violation per se,
  but limits batching and threading.

### A-06 — Error handling and lifecycle (P2)

Strengths:

- Most Vulkan creation calls are checked.
- Deferred mesh/texture/material release exists.
- Descriptor/texture generations guard stale handles.

Weaknesses:

- Five unchecked `vkMapMemory` paths found and fixed (QA-005).
- No device-lost/OOM recovery (QA-008).
- Some public functions allocate CPU-side material records before a device; this is
  required by the CPU/glTF path but was not documented in the API header (test was
  corrected, not the lifecycle).

## Architecture verdict

**Adequate for a compact engine; not AAA architecture yet.** The backends are
separated and testable, but the Vulkan implementation needs decomposition,
backend capability reporting, and device-loss/threading infrastructure for
production use.
