# 02 — Vulkan Implementation Audit

## Device facts (measured)

```
vulkaninfo --summary
GPU0: NVIDIA GeForce RTX 5050
apiVersion    = 1.4.351
driverVersion = 617.42.0.0
deviceType    = DISCRETE_GPU
```

Instance validation layer used: `VK_LAYER_KHRONOS_validation` 1.4.357.
`VLCRLIB_VALIDATION=1` enables it in release builds.

## Area-by-area

| Area | Status | Evidence / notes |
|---|---|---|
| Instance creation | PASS | loader version queried, capped to 1.3; debug-utils added only when validation available |
| Validation layer selection | PASS | enumerates layers, falls back with log; no hard dependency |
| Physical device selection | PASS | requires graphics+present queue; scores by device type; logs selected GPU |
| Queue families | PARTIAL | one graphics+present queue; no dedicated transfer/compute queue |
| Device creation | PASS | 1.3 features chained only when advertised; ray-query gated by 3 extensions + features |
| Swapchain | PASS | caps/formats/modes checked; recreate on OUT_OF_DATE/SUBOPTIMAL/resize |
| Presentation | PASS | acquire semaphore + render-finished semaphore per frame; image fences |
| Render passes | PASS | legacy render pass + dynamic rendering feature flag; no validation errors in smoke |
| Command buffers/pools | PASS | per-swapchain-image command buffers, reset each frame; no multithreaded recording |
| Fences/semaphores | PASS | 2 frames in flight; image-in-flight fence; resize smoke clean |
| Barriers | PASS | QA-001 fixed: SSAO depth/color, velocity and graph layouts; empty + 1000-object stress validation 0 |
| Pipeline/pipeline layout | PASS | checked creation; PBR/skin/shadow/post layouts; shader modules validated |
| Descriptor sets/pools | PASS | per-material PBR/skin sets, update-on-dirty; generations checked |
| Buffers | PASS | staging + device-local path; five unchecked map paths fixed (QA-005) |
| Images | PASS | createImage/layout helpers; shadow/IBL/render targets; QA-001 layout bugs fixed |
| Memory allocation | PARTIAL | dedicated allocations; no allocator/suballocation, no OOM recovery |
| Texture/sampler | PASS | sRGB, mips, per-texture sampler, max-anisotropy clamp, KTX2 upload |
| Shader compilation / SPIR-V | PASS | 26 `.spv` files validated with `spirv-val`, 0 failures |
| Depth/stencil | PASS | D32/D24 paths; scene depth and shadow depth |
| MSAA | INFO | pipelines use sample count 1; no MSAA path exercised |
| Multithreaded recording | **NOT IMPLEMENTED** | single command buffer recorded on main thread |
| Device lost | **NOT IMPLEMENTED** | only `VK_ERROR_DEVICE_LOST` string; no recovery (QA-008) |
| Resource lifetime | PARTIAL | deferred release tables; some GPU resources freed at close with `vkDeviceWaitIdle` |

## Validation runs

### Main PBR/glTF/shadow smoke

Command: `VLCRLIB_VALIDATION=1 api_smoke.exe`, 90 frames, 800x600.

```
[stats] draws=3 tris=170000 culled=1 cpu=0.936ms gpu=0.246ms vram=113.1MB
[stats] gpu pass ms: shadow=0.049 scene=0.122 post=0.075
[stats] validation errors=0
[stats] validation errors after shutdown=0
```

### Resize + shadow-resize

`VLCR_RESIZE=1 VLCR_SHADOW_RESIZE=1`, 90 frames: `validation errors=0`, exit 0.

### Stress harness (P1, fixed)

Two independent layout defects were found and fixed:

1. SSAO depth target was created in `UNDEFINED` and read by the first depth
   prepass; its render pass also left the image in
   `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` while the descriptor declared
   `DEPTH_STENCIL_READ_ONLY_OPTIMAL`.
2. Velocity image was created in `UNDEFINED` but bound in the composite/TAA
   descriptor set. Debug names (`ssao_depth`, `ssao_color`, `velocity_color`)
   identified the resources under Validation.

Fixes:

- SSAO depth initialized to `DEPTH_STENCIL_READ_ONLY_OPTIMAL`, render pass
  `initial=UNDEFINED`, `final=DEPTH_STENCIL_READ_ONLY_OPTIMAL`;
- SSAO color created with `TRANSFER_DST`, white-cleared, transitioned to
  `SHADER_READ_ONLY_OPTIMAL`; render pass final is `SHADER_READ_ONLY_OPTIMAL`;
- velocity color transitioned to `SHADER_READ_ONLY_OPTIMAL` at creation;
- SSAO render-graph path disabled until its barrier plan can track the
  attachment final layout; the explicit prepass/SSAO pair is used.

After fixes:

```
qa_stress 1 object, empty:  validation=0
qa_stress 1000 objects:     validation=0
api_smoke 90 frames:        validation=0
```

QA-001 is **FIXED** and regression-tested.

## Shader/SPIR-V validation

```
26 files, 0 failures  (spirv-val 1.4.357)
```

## Vulkan verdict

The main rendering path is validation-clean and uses reasonable modern Vulkan
(dynamic rendering feature, synchronization2 query, ray-query gating). It is not
production-ready until device-loss/OOM recovery and multithreaded recording are
addressed. QA-001 is fixed and regression-tested.
