# Vulkan Renderer Audit

## Environment

```
GPU: NVIDIA GeForce RTX 5050 (discrete), driver 617.42
Device API: 1.4.351
Instance: 1.4.357
Validation: VK_LAYER_KHRONOS_validation 1.4.357
```

## Result summary

| Area | Status | Evidence |
|---|---|---|
| Instance / validation layer | PASS | layer enabled via `VLCRLIB_VALIDATION=1`; debug utils messenger |
| Physical/logical device | PASS | graphics+present queue selected; Vulkan 1.3 features gated |
| Swapchain/presentation | PASS | 800x600, 3 images; resize validation 0 |
| Render passes / dynamic rendering | PASS | PBR, shadow, SSAO, bloom, composite |
| Command buffers | PASS | per-swapchain-image reset; single-threaded recording |
| Synchronization | PASS | fences/semaphores; empty + 1000-object stress validation 0 |
| Pipeline/pipeline layout | PASS | SPIR-V valid; main smoke 0 errors |
| Descriptors | PASS | material sets, generation checks |
| Buffers/images/memory | PASS (limited allocator) | dedicated allocations; OOM-unsafe maps fixed |
| Shader compilation | PASS | 26/26 `spirv-val` |
| Depth/stencil | PASS | scene + shadow depth |
| MSAA | NOT IMPLEMENTED/TESTED | sample count 1 in audited pipelines |
| Multithreaded recording | NOT IMPLEMENTED | main-thread command buffer |
| Device lost/OOM | NOT IMPLEMENTED | no recovery path |
| Compute pipelines | NOT IMPLEMENTED | none found in audited draw path |

## Validation evidence

Main PBR/glTF/shadow smoke, 90 frames:

```
draws=3 tris=170000 culled=1 cpu=1.075ms gpu=0.240ms tex=7 mesh=2 vram=113.1MB
gpu pass ms: shadow=0.050 scene=0.121 post=0.069
validation errors=0
validation errors after shutdown=0
```

Empty and 1000-object audit stress:

```
RESULT objects=1    frames=2  validation=0
RESULT objects=1000 frames=10 validation=0
```

## Defects found/fixed

- **QA-001 (P1)**: SSAO depth/color and velocity images were sampled in
  `UNDEFINED`; debug names (`ssao_depth`, `ssao_color`, `velocity_color`)
  identified them. Initial transitions, SSAO white clear, render-pass final
  layouts and velocity init were fixed. The broken SSAO graph path is disabled
  in favor of the explicit prepass/SSAO pair.
- **QA-004 (P2)**: five unchecked `vkMapMemory` upload paths could memcpy into
  NULL on failure; checks and staging cleanup added.

## Remaining risks

- No dedicated allocator/suballocation; every image/buffer is a dedicated
  allocation.
- No device-lost/OOM recovery.
- No multithreaded command recording or transfer queue.
- MSAA, compute, ray-tracing path not exercised in this audit.
- Render graph covers post/SSAO, not shadow/scene.

## Verdict

Vulkan core PBR path is validation-clean on Windows and visually deterministic.
It is **production-usable for small/mid scopes**, not AAA-ready without
device-loss/OOM, allocator, MT recording and compute/RT verification.
