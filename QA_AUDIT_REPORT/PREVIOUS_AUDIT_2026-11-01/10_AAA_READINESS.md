# 10 — AAA Readiness Assessment

Scoring is against a production AAA engine, not a hobby/indie baseline.

| Area | Score /10 | Evidence / gaps |
|---|---:|---|
| Vulkan core correctness | 8 | validation-clean main smoke, resize and empty/1000-object stress after QA-001 |
| PBR / glTF materials | 8 | DamagedHelmet, KTX2, tangent TBN, IBL; cross golden PASS |
| Lighting / shadows | 8 | 4-cascade CSM, 4 spot PCSS, 4 point cube PCSS (WebGPU) |
| Render graph | 4 | post/SSAO graph exists; shadow/scene not graph-driven |
| Resource management | 5 | deferred release + generations; no allocator/suballocation |
| Threading / scalability | 2 | single-threaded recording, one graphics+present queue |
| Device loss / OOM | 1 | no recovery path (QA-008) |
| Asset pipeline | 6 | glTF/FBX/USD/KTX2 paths; optional-dependency stubs; no streaming |
| Animation | 6 | skinning, animator, IK; validated Walk clip on WebGPU |
| Instancing / batching | 2 | no instancing path found; immediate-mode CPU-bound |
| Compute | 1 | no compute pipeline evidenced in audited path |
| Profiling / telemetry | 5 | CPU/GPU timestamps, draw/tri/VRAM estimates; no GPU capture/automation |
| Test/CI maturity | 5 | 11 unit/API tests + external WebGPU/cross goldens; GPU tests opt-in |
| Platform abstraction | 4 | Vulkan/WebGPU/CPU backends; backend no-op APIs |
| Large worlds | 3 | no streaming/LOD/virtual geometry; 100k immediate draws ~340 ms CPU |

## Overall

**AAA-ready: NO.**  
**Production-ready for small/mid-scope games: CONDITIONAL.**

Strong points:

- real Vulkan + WebGPU + CPU backends;
- deterministic goldens on both;
- PBR/glTF/shadows/IBL/skinning implemented;
- validation catches real bugs (this audit found/fixed four memory paths and one
  test/build issue).

Blocking work for AAA:

1. Device-lost/OOM recovery.
2. Multithreaded command recording and transfer queue.
3. Render graph execution for shadow/scene/post.
4. Instancing/batching and large-world streaming/LOD.
5. Backend capability API and removal/documentation of stubs.
6. Sanitizer/GPU-capture CI (ASan/UBSan/RenderDoc/GPU-assisted validation).

## Missing subsystems (not bugs)

- Virtualized geometry / Nanite-class pipeline.
- World partition / streaming.
- Job system / parallel scene traversal.
- Compute-based culling or GI.
- Temporal upscaler/DLSS/FSR integration.
- Multi-window and HDR swapchain path.
- Production asset cooker validation at scale.
- Automated frame-pacing/regression dashboards.
