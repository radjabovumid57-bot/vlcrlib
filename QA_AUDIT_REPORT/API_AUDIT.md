# Public API Audit

## Contract tested

`tests/test_api_contracts.c` (49 checks) covers NULL/empty/invalid handles:

- models, skinned models, textures, KTX2, render textures, materials;
- mesh registration, model creation, screenshot paths;
- scene add/load/save and invalid parent/default scale;
- animator NULL paths;
- audio reverb NULL/missing;
- render graph invalid image/pass.

`test_cpp_header.cpp` compiles the public headers as C++.
Desktop ctest: **11/11 passed**; CPU-only: **9/9 passed**.

## Parity findings

### API-001 (P2, FIXED) — WebGPU missing public validation error count

`vgraphics.h` declares `vgraphics_GetValidationErrorCount()`. Only Vulkan
implemented it; WebGPU exposes `vgraphics_WebGPUErrorCount()` instead, so a
header-only caller linking WebGPU would fail. Added an alias in
`src/vgraphics_webgpu.c` returning the WebGPU error counter.

### API-002 (P2, documented limitation) — WebGPU no-op shadow APIs

These public functions are no-ops on WebGPU:

- `SetShadowQuality`
- `vgraphics_ComputeDirectionalShadowMatrix`
- `vgraphics_ComputeDirectionalShadowCascades`
- `vgraphics_ComputeSpotShadowMatrix`
- `vgraphics_UpdateShadowMatrix`
- `vgraphics_SetSpotShadows`
- `vgraphics_SetPointShadows`

Vulkan implements them. The public API does not expose a capability query, so
callers cannot detect this difference.

### API-003 (P3) — `LoadShader` stub

Vulkan and WebGPU both return `(Shader){0}` and `UnloadShader` is a no-op. This
is documented as an ABI placeholder, but it is an unimplemented public API.

### API-004 (P2) — Backend feature differences are implicit

Known differences:

- Vulkan has ray-query path; WebGPU does not.
- WebGPU has 4 spot/4 point PCSS paths; Vulkan shadow API is legacy/one-slot
  oriented.
- KTX2 compression support is feature-gated on WebGPU (`bc`, `etc2`, `astc`);
  the audited D3D11 adapter exposes BC only.
- GPU timestamps: Vulkan works; WebGPU reports `gpuFrameMs=-1`/`gpu=n/a` in
  headless because the map callback never resolves.
- Vulkan validation error count is meaningful; WebGPU returns WebGPU error
  count, not Vulkan validation.

## Lifecycle semantics

`LoadMaterialDefault()` intentionally allocates CPU-side material records before
a Vulkan device for the CPU/glTF path. The API contract test was corrected to
reflect this; the engine behavior was not changed.

## Verdict

The C API is stable and NULL-contract-tested, with generation-checked handles.
It is **not fully backend-equivalent**: no capability API and several WebGPU
no-ops/stubs must be documented for consumers.
