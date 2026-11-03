# AAA readiness status

Last updated: 2026-10-29.

## Verdict

vlcrlib is now a **credible AA-oriented technology foundation**, not an AAA
engine. The foundation is healthy: Vulkan validation is clean, frame metrics
exist, a render-graph planner is in place, animation has crossfades and a state
machine, and there is a real skinned regression asset.

AAA still requires a content pipeline, tools, a team and substantial renderer
work that cannot be produced by a single library alone.

## Scorecard after the audit

| Area | Before | Now | Notes |
|---|---:|---:|---|
| Math/core | 4 | 4 | tested |
| Rendering | 3 | 3.2 | culling, profiling; no CSM/TAA/upscaling |
| Assets | 2 | 2.2 | loaders + test asset; no cooker/compression |
| Animation | 2 | 2.8 | crossfade, animator; no graph/IK/root motion |
| Physics | 3 | 3 | wrappers unchanged |
| Audio | 1 | 1 | no 3D mixer |
| GUI | 3 | 3 | viewer integration |
| Tools | 1 | 1.3 | docs/architecture, no editor |
| Performance | 2 | 2.7 | frame stats, culling; no instancing/GPU-driven |
| Testing | 1 | 2.5 | 11 unit tests, CI, validation; no golden images |
| Documentation | 3 | 3.8 | architecture, assets, roadmap, cheat sheet |
| Platforms | 2 | 2 | desktop Vulkan; WebGPU partial, smoke-validated |

Approximate aggregate: **2.3 → 2.7 / 5**.

## Evidence

```text
CTest:                    11/11 passed
VLCRLIB_VALIDATION=1:     0 errors / 0 warnings
validation after shutdown: 0 errors
VLCRLIB_STRICT:           OK
api_smoke:                fails=0, frames=90
WebGPU runtime:           frames=150, errors=0 (Chrome 156, D3D11, TAA/SSAO/CSM/spot/point-cube/skin/GPU-time/stats/screenshot)
animator:                 clips=3 bones=2
frustum culling:          behind-camera mesh rejected
```

## What is production-usable today

- glTF/GLB static + skinned loading, animations;
- FBX/OBJ through Assimp (optional);
- PBR materials, IBL, shadows, SSAO, bloom, exposure, fog, atmosphere;
- skinned animation with crossfade and named states;
- Framebuffer stats (CPU/GPU/pass split/VRAM/draws/culling);
- validation-clean Vulkan lifecycle;
- render-graph planner with transient images and barrier emission;
- CPU frustum culling for static meshes;
- 256-bone skeleton support;
- named states + crossfade + root motion + parameter transitions;
- JSON scene serialization + runtime model loading;
- scene editor-lite (nodes, transforms, parent hierarchy, save/load, undo/redo);
- golden-image differ (`vdiff`), `perf_probe`, skinned+TAA golden test;
- miniaudio mixer: occlusion low-pass, FIR convolution reverb, streamed music/seek;
- animation crossfade + additive overlay + named clip events;
- pose evaluation API + two-bone IK on skeleton chains;
- `vlcrc` asset validator/manifest tool for glTF/FBX/OBJ;
- `.vmesh` mesh cooking + runtime `LoadCookedModel`;
- 4-cascade directional shadows in a 2x2 atlas;
- WebGPU sRGB textures, mip chains, split UBO/global/material/skin bind groups and normal/AO/emissive maps;
- PNG screenshot capture for visual regression checks;
- FXAA + TAA (jitter, model + skinned velocity, depth reprojection, history clamp);
- KTX2 BC7/BC5/ETC2/ASTC runtime upload (Vulkan + WebGPU), BC5 normal-Z reconstruction;
- `.vmesh` v2/v3 with PBR materials + texture references;
- multi-mesh / multi-material cooked models;
- KTX2 mip chains for BC7/BC5.

Next rendering/audio milestone: [MILESTONE_RENDERING.md](MILESTONE_RENDERING.md).

## What blocks AAA

1. Cascaded shadow map visual tuning (splits/bias/PCF).
2. TAA + motion vectors + FSR/DLSS.
3. GPU instancing / indirect / occlusion culling.
4. Asset cooker: KTX2, BC7/ASTC, meshopt, LOD, streaming.
5. Migrate SSAO/bloom/composite to the render graph (all are graph-planned).
6. Animation graph, root motion, IK, events, morph targets.
7. Scene serialization + editor/asset browser.
8. 3D audio mixer.
9. Golden-image and performance-regression tests.
10. Platform/console certification and a production team.

## Recommended next three sprints

### Sprint A — shadows and graph

- CSM 4 cascades, four spot maps and four point-cube shadows;
- graph-owned shadow/bloom/scene resources;
- migrate SSAO/bloom/composite to `vrgExecute`;
- per-pass graph timings.

### Sprint B — content pipeline

- `vlcrc` importer for glTF/FBX/OBJ;
- KTX2 + BC7/BC5/BC4;
- meshoptimizer vertex cache/overdraw;
- LOD generation;
- validation report.

### Sprint C — animation and tools

- animation graph (1D/2D blend, layers);
- root motion and events;
- scene serialization;
- editor-lite hierarchy/inspector.

## Release gate for an “AAA-ready” claim

Do not claim AAA until:

- a complete vertical slice runs at 1080p60 on target hardware;
- assets are cooked and streamed;
- CSM/TAA/upscaling are stable;
- an editor and content pipeline exist;
- golden-image and performance CI pass;
- the team and budget match AAA production.
