# vlcrlib architecture

Status: technical foundation for an AA/AAA-oriented engine.  
Last audit: 2026-10-25.

## 1. Goals and non-goals

**Goals**

- raylib-style C API for fast prototyping;
- Vulkan-first renderer with PBR, IBL, shadows, post-processing;
- one asset path for glTF, FBX, OBJ and (offline) USD;
- deterministic math and coordinate conventions;
- validation-layer-clean runtime;
- measurable frame time and memory.

**Non-goals**

- embedding a full game editor in the runtime;
- reading every DCC format in the shipping runtime;
- promising AAA production scale without a team and content pipeline.

## 2. Module map

```text
src/
  vcore.c/.h          window, frame, input, timing
  vmath.h             vectors, matrices, quaternions, coordinate contract
  vgraphics.c         renderer front-end, resource tables, draw submission
  vgraphics_device.c  instance/device/swapchain/render passes
  vgraphics_pipeline.c pipelines, descriptor sets, shaders
  vgraphics_material.c PBR material tables/descriptors
  vgraphics_shadow.c  shadow matrices
  vgraphics_rayquery.c hardware ray-query demo
  vrender_graph.c/.h  CPU render-graph planner + Vulkan image execution
  vtextures.c         image decode/upload
  vshapes.c/.h        2D batch
  vtext.c/.h          font/text
  vgui.c/.h           immediate-mode GUI
  vgltf.cpp           tinygltf glTF/GLB + skinned animations
  vfbx.cpp            Assimp FBX/OBJ + skinned clips
  vusd.cpp            optional OpenUSD static meshes
  vloaders.c/.h       extension dispatch
  vcooked.c           .vmesh v2/v3 loader (multi-mesh + PBR + KTX2 refs)
  vaudio3d.c/.h       miniaudio positional mixer (listener, attenuation,
                      occlusion low-pass, FIR convolution reverb, streamed music)
  vscene.c/.h         JSON scene serialization + model runtime
  example/scene_editor_lite.c  node list + transform editor
  tools/vdiff.cpp     golden-image PNG differ
  tests/golden_skinned.ps1  skinned + TAA golden capture/diff
  example/perf_probe.c deterministic frame-time probe
  vanimator.c/.h      states + root motion + transitions + events + additive overlay
  BonePose API         local TRS pose + two-bone IK on skeleton chains
  tools/vlcrc.cpp     asset validator/manifest + .vmesh/KTX2 cook tool
  vphysics.cpp        Bullet 3D
  vphysics2d.cpp      Box2D
  vjolt.cpp           optional Jolt backend
  vaudio.c            VLC-backed sound/music
  vnetwork.c          sockets/HTTP
  vlight.c            CPU light helpers
  vibl.c              HDR → cubemap/irradiance/prefilter/BRDF
  vparticles.c        2D particles
```

## 3. Public API boundary

- Public headers use a C ABI (`extern "C"`).
- Handles are small integer structs (`Model`, `Mesh`, `Texture2D`, `Material`,
  `SkinnedModel`, `Animation`, `Animator`), not raw Vulkan objects.
- Vulkan types stay out of the public API except in dedicated interop headers.
- `vrender_graph.h` is an internal/desktop header: it exposes Vulkan planner
  types but is not included by `vlcrlib.h`.

## 4. Frame flow

```text
InitWindow
  └─ SDL window
  └─ InitGraphicsDevice
       ├─ instance / surface / physical device / device
       ├─ swapchain / depth / render pass / framebuffers
       ├─ pipelines (2D, 3D, PBR, skin, shadow, skybox, post)
       ├─ SSAO / bloom / scene offscreen / composite
       └─ timestamp query pool

while (!WindowShouldClose)
  BeginDrawing
    BeginVulkanFrame
      ├─ wait frame fence
      ├─ read previous GPU timestamps
      ├─ reset per-frame stats
      ├─ record shadow + SSAO prepasses
      ├─ begin scene render pass
    user draws (DrawModel / DrawSkinnedModel / DrawText / GUI)
  EndDrawing
    EndVulkanFrame
      ├─ end scene pass
      ├─ bloom + composite
      ├─ timestamp t3
      └─ submit / present
```

### Frame statistics

`FrameStats` exposes:

- CPU/GPU frame time;
- shadow/scene/post GPU split;
- draw calls, shadow draws, transparent draws, culled draws;
- triangles;
- texture/mesh counts and estimated VRAM.

`vgraphics_CaptureScreenshot(path)` copies the last presented swapchain image
to PNG for golden-image tests and visual regression checks.

## 5. Coordinate and depth contract

- Right-handed, Y-up, metres.
- Directional shadows use four cascades packed into a 2x2 atlas.
- Composite pass supports FXAA; it requires the offscreen post-process path.
- TAA uses jittered projection, a per-frame velocity buffer for model and
  skinned objects (per-model current/previous bone UBOs), depth reprojection
  fallback, a clamped history image and per-frame TAA uniform slots.
- KTX2 textures use BC7/BC5 raw payloads; no KTX runtime dependency.
- Floor is XZ at `y = 0`.
- `MatrixLookAt` looks down camera `-Z`.
- Vulkan depth: `z ∈ [0,1]` (ZO), not OpenGL `[-1,1]`.
- 3D projection negates Y once; viewport has positive height.
- 2D uses `MatrixOrtho(0,w,h,0)` and negative-height viewport.
- glTF CCW + `frontFace = COUNTER_CLOCKWISE`; do not add a second Y flip.

## 6. Resource ownership

| Resource | Owner | Release |
|---|---|---|
| `Texture2D` | texture table | `UnloadTexture`, deferred during frame |
| `Mesh` | mesh table | `UnloadMesh`, deferred |
| `Model` | model table | `UnloadModel`; may own textures via `vgraphics_TrackModelTexture` |
| `Material` | material table | `UnloadMaterial` |
| `SkinnedModel` | skinned table | `UnloadSkinnedModel` |
| `Animator` | caller | `DestroyAnimator` |
| `VrgGraph` | caller | `vrgFreeVulkan` then `vrgDestroy` |

- Deferred destruction uses frame-slot fences (`DeferredTextureRelease`, etc.).
- `CloseGraphicsDevice` waits idle and destroys post-process, meshes, textures,
  models, skinned models, sync objects and device.
- Validation-layer shutdown check must report zero leaked objects.

## 7. Render graph

`vrender_graph.h` provides:

- image declarations (`VrgImageDesc`);
- pass declarations;
- read/write uses with layout/stage/access;
- topological pass ordering;
- hazard detection and barrier plan;
- final-layout transitions;
- optional transient `VkImage` allocation and `vrgExecute` callbacks;
- external images (`vrgAddExternalImage`) whose barriers are graph-planned
  while the renderer keeps ownership.

Current status: final composite, the three bloom passes (bright / blur H /
blur V), the SSAO depth prepass and the SSAO resolve are planned and executed
through the graph via external images.

## 8. Animation

- Skeleton: parent indices, inverse bind, rest TRS, bone names.
- Bone limit: **256** (u8 joint indices; 16 KB bone UBO).
- Clips: linear position/rotation/scale keys.
- `DrawSkinnedModel` evaluates one clip.
- `DrawSkinnedModelBlend` linearly blends bone matrices of two clips.
- `Animator`:
  - named states;
  - timed crossfade;
  - loop/non-loop state time;
  - explicit `AnimatorSetTime`;
  - explicit `AnimatorUpdate`.
- `assets/skinned_anim.glb` is a two-bone regression asset.
- Not yet: root motion, IK, layered/additive, state conditions, morph targets.

## 9. Asset loaders

| Format | Runtime path | Notes |
|---|---|---|
| glTF/GLB | tinygltf | static + skinned + animations; TEXCOORD_0 only |
| FBX | Assimp optional | static + first skinned mesh + clips |
| OBJ | Assimp optional / built-in | MTL via Assimp |
| USD/USDZ | OpenUSD optional | static geometry; material stub |
| `.vmesh` | `LoadCookedModel` | single-mesh cooked binary from `vlcrc --mesh` |
| OBJ/FBX/USD | cooked binary (planned) | shipping runtime should not parse DCC files |

Known limitations:

- no KTX2/BC7/ASTC runtime compression yet;
- no Draco/meshopt/LOD/streaming;
- only one UV set;
- USD shading network and animation are incomplete;
- FBX duplicate-unit meshes are normalized by a heuristic in `vfbx.cpp`.

## 10. Threading model

Current runtime is effectively single-threaded for rendering and asset upload.
Deferred GPU release is fence-based. The next architectural step is a job
system for:

- asset decode/upload;
- animation sampling;
- culling;
- physics step;
- audio streaming.

## 11. Testing

- Unit tests: math, UBO layout, flags, coordinates, animator, render graph.
- GPU smoke: `api_smoke` with validation.
- `VLCRLIB_VALIDATION=1` enables validation in release builds.
- Strict build: `-DVLCRLIB_STRICT=ON`.
- CI: `.github/workflows/ci.yml`.
- Missing: golden-image tests, fuzzing, performance regression gate.

## 12. Known architectural debt

1. `vgraphics.c` is ~5.5k lines and still contains device, pipelines, passes and
   draw submission.
2. Global `VK` state limits multiple devices/renderers.
3. Descriptor sets are still partly per-frame global instead of graph-owned.
4. `vfastgltf_lib.cpp` is an unused/legacy fastgltf wrapper.
5. Public API mixes raylib-style drawing with lower-level Vulkan concepts.
6. No unified scene/world object: models are handles, not ECS entities.
7. No editor, serialization, asset database or cooker.

## 13. Next milestones

1. Migrate SSAO/bloom/composite to `vrgExecute`.
2. Cascaded shadow maps (4 cascades).
3. GPU instancing + indirect draw + occlusion culling.
4. KTX2/BC7/ASTC asset cooker.
5. Animation graph, root motion, IK.
6. Scene serialization + editor-lite.
