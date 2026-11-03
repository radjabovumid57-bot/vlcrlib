# WebGPU parity milestone

The WebGPU backend is a separate renderer. Full parity is not a port of a few
functions; the current backend submits directly to the swapchain and keeps one
forward PBR pipeline, while Vulkan has a multi-target graph.

## Current WebGPU surface

- 2D textured batching;
- forward PBR (single directional + ambient);
- 4-cascade shadow atlas (2048², 2×2 viewports, PCSS blocker search + variable PCF);
- up to four spot lights, all with 1024² PCSS shadow maps;
- up to four point lights, all with 512² cube PCSS shadows from one 24-layer cube array;
- IBL: irradiance + prefiltered specular cubemaps, BRDF LUT, split-sum in WGSL;
- glTF meshes/materials;
- sRGB texture formats + CPU-generated mip chains, 256-byte aligned uploads,
  per-texture filter/wrap samplers;
- compressed KTX2 BC7/BC5, ETC2 RGB8 and ASTC 4x4 uploads (feature-gated on
  `texture-compression-bc` / `texture-compression-etc2` /
  `texture-compression-astc`),
  a 4×4 BC5 fixture validates KTX2 load and is sampled as AO and normal maps
  on validation quads; BC5 normals reconstruct Z with `sqrt(1-x²-y²)`;
- split bind groups: `group0` UBO, `group1` IBL/shadow resources, `group2`
  material sampler and albedo/MR/normal/AO/emissive maps, `group3` skin bones;
- normal mapping with glTF TANGENT (static and skinned) and derivative-TBN
  fallback, AO and emissive map sampling in PBR;
- skinned glTF meshes/animation (bone palette, skinned PBR/velocity/shadow pipelines);
- depth buffer;
- offscreen scene color + depth;
- FXAA composite pass (runtime-validated, toggleable);
- TAA: jittered projection, velocity reprojection, ping-pong history (runtime-
  validated, toggleable);
- half-res bloom: bright/blur pass + additive composite (runtime-validated);
- half-res SSAO from the scene depth buffer (runtime-validated, toggleable);
- `SetGraphicsExposure` applied in PBR, ACES + explicit sRGB composite;
- Fresnel `kD=(1-F)(1-metallic)` scaling for IBL diffuse and ambient, plus
  Lagarde specular occlusion (Vulkan parity);
- frame stats: draws/triangles/cpu-time/texture/mesh counts;
- PNG screenshot capture from the final composite texture;
- fullscreen composite pass to the canvas.

The offscreen scene/composite pair is the foundation for the graph, but there is
still no multi-pass graph.

## P0 correctness update (2026-10-28)

- Per-draw data no longer overwrites a single UBO/vertex buffer:
  - a 512-slot uniform ring (512-byte stride) is bound with a dynamic offset per
    draw;
  - a per-frame vertex arena (`MAX_FRAME_VERTS`) is addressed by vertex-buffer
    offset per draw. This makes multi-object scenes use their own transform,
    material and geometry instead of the last draw's.
- An uncaptured-error callback prints WebGPU validation errors instead of
  failing silently.
- The main feature entry points used by the examples are link-compatible with
  Vulkan. `SetShadowQuality` remains a no-op; FXAA, TAA, bloom, SSAO, shadows,
  IBL, frame stats and screenshot capture are implemented below. The
  shadow-matrix helpers remain fallbacks/no-ops.
- FXAA runs as a second composite pipeline selected by `SetFXAA`; it uses
  `textureDimensions` for the texel size and has no extra buffers.
- TAA applies a subpixel projection jitter, renders a full-res RGBA16Float
  velocity buffer from current/previous model matrices and view-projection,
  reprojects history by velocity, clamps it to the current 3×3 neighborhood and
  blends 90% history. `SetTAA` toggles it; history is ping-ponged full-res.
  Runtime-validated in the 150-frame smoke. Previous transforms are tracked per
  draw command (the scene draws two instances of the same mesh); skinned
  velocity has its own current/previous bone buffers. TAA currently takes
  precedence over the SSAO/bloom composite paths.
- Bloom renders a half-resolution 3×3 bright/blur pass (threshold 0.65) and
  adds it in the composite (`base + glow * 0.9`). `SetBloom` toggles the path;
  the threshold/intensity arguments are currently fixed in the shader. Runtime-
  validated in the 150-frame smoke with bloom on/off.
- SSAO samples the scene depth buffer in 8 screen-space directions and darkens
  depth discontinuities (objects against the ground/silhouettes), then composites
  `color * mix(1, ao, 0.8)`. `SetSSAO` toggles radius/intensity. Runtime-validated
  with SSAO on/off; the composite applies AO to the whole scene (direct light
  too), and there is no blur/normal-aware pass yet.
- Directional shadows: a 2048² atlas with four 1024² cascades in a 2×2 layout.
  Splits use a practical logarithmic/uniform blend from the actual near/far
  planes; each cascade is fitted to its camera frustum slice in light space with
  texel snapping, uses PCSS blocker search plus a variable-radius 5×5 PCF; the
  PBR shader selects by camera distance, applies normal/constant bias and fades
  at cascade boundaries. `vgraphics_SetShadows`/`SetShadowSoftness` drive it. A
  all four spot lights are shadowed: perspective light matrices, 1024² depth
  maps, PCSS, cone attenuation and PBR spot terms. `vgraphics_AddLight` fills the
  three extra spots.
  Four point lights are evaluated in the PBR fragment loop with distance
  attenuation (`SetPointLight` fills the first slot). All four point lights
  render 512² depth cubes (six face passes) into one 24-layer cube array and use
  cube PCSS with raw-array blocker search. Runtime-validated in the 150-frame
  smoke (`errors=0`, `shadows=256`).
- IBL is implemented from the same `VIBL_Bundle` bake used by the CPU/Vulkan
  paths: irradiance and prefiltered cubemaps (and the BRDF LUT) use
  `RGBA32Float` when `float32-filterable` is available, otherwise
  `RGBA16Float`, sampled with a linear-filtering sampler and
  combined with the split-sum approximation in WGSL. Uploads use 256-byte
  aligned rows; 1×1 fallbacks keep the bind group valid before `LoadIBL`.
  `vgraphics_LoadIBL`, `IsIBLLoaded`, `UnloadIBL` and `SetIBLIntensity` are
  implemented. Runtime-validated in the same 150-frame smoke (`errors=0`).
- `vgraphics_GetFrameStats` reports frame index, size, draw calls, shadow
  draws, triangles, texture/mesh counts, estimated GPU bytes, CPU frame time and
  timestamp-query GPU frame time (feature-gated; negative when no sample is
  available). The validation report uses it (`draws=10 shadows=256 tris=15926
  gpu=n/a` when readback is unavailable).
- `vgraphics_CaptureScreenshot` copies the final composite texture (after
  FXAA/TAA/bloom/SSAO) to a MapRead/CopyDst buffer, maps it asynchronously,
  converts BGRA→RGBA and writes PNG with `stbi_write_png`. Browser download is
  opt-in via `window.vlcrDownloadScreenshots`; the validation report prints
  `shot=1`.
- `example/web_validate_example.c` + `tools/web_validate.ps1` provide a headless
  runtime smoke test: multi-object scene, 150 frames, uncaptured-error count and
  a screenshot. The page embeds JS+wasm (`SINGLE_FILE`) so an external `.js`
  cannot be blocked by endpoint security. The
  `.github/workflows/webgpu-runtime.yml` job runs it on Ubuntu with SwiftShader
  WebGPU and fails on `errors != 0`. See
  [WEBGPU_RUNTIME.md](WEBGPU_RUNTIME.md).
- Vulkan-vs-WebGPU diff passes through CDP viewport capture:
  `mean=1.5561` against a `6.0` threshold (max-over 2.90%). The harness forces a
  960x540 DevTools viewport, because headless Chrome otherwise opens the canvas
  at 942x388 and creates a false aspect difference.
- Still missing: render graph. The deterministic
  WebGPU self-golden passes.
  The 150-frame smoke passed on Chrome 156 headless
  with `--use-angle=d3d11` (`frames=150 errors=0`); see
  `docs/webgpu_validation.png`.

## Skinning + orientation fix (2026-10-30)

- Bone palettes are converted with the same column-major helper used for the
  per-draw UBO. `Matrix` keeps row-major memory order while WGSL `mat4x4f`
  arrays are column-major, so the previous raw upload transposed every bone
  matrix (visible as collapsed/projective skinned vertices).
- The glTF loader now transposes `inverseBindMatrices` (and `matrix`-style node
  transforms) after reading: glTF stores MAT4 accessors column-major, and a
  direct `memcpy` into `Matrix` transposed them. Without this, the skinned tip
  bone produced a projective palette (w != 1) and half of each skinned mesh
  vanished.
- WebGPU clip space is y-up while the engine's `MatrixPerspective` negates Y for
  Vulkan's y-down NDC. `vgraphics_Set3DMatrices` mirrors clip Y for the WebGPU
  backend and the culled PBR pipeline uses `frontFace = CW`, so glTF CCW meshes
  stay front-facing. Before this the whole WebGPU 3D scene rendered vertically
  mirrored (the skinned mesh made it obvious).
- The loader now maps hierarchy parents from node indices to bone indices; the
  previous node-index parents were masked on the two-bone test asset by the
  self-parent guard but would have produced wrong animation on deeper rigs.
- Skinned PBR/velocity/shadow pipelines use `cullMode = none` to match the
  Vulkan backend's `VK_CULL_MODE_NONE`.
- Runtime smoke: one animated skinned draw with the Walk clip, TAA enabled for
  the last 30 frames; `frames=150 errors=0`. The skinned mesh deforms and casts
  into all shadow cascades, the spot map and the point cube map.

## Required work

1. **Offscreen frame graph**
   - scene color/depth textures;
   - ping-pong bloom targets;
   - half-res SSAO targets;
   - pass ordering and barriers (`wgpuCommandEncoder` resource transitions).
2. **TAA** — done: jittered projection, per-command and skinned velocity,
   neighborhood-clamped ping-pong history.
3. **SSAO** — done for the smoke path: half-res depth-discontinuity AO with 8
   screen-space samples. A depth prepass/noise texture, normal-aware sampling
   and a blur pass remain quality work.
4. **Bloom** — done for the smoke path: half-res bright/blur + additive
   composite. A separable ping-pong blur is optional future quality work.
5. **Shadows** — 4-cascade frustum-fit atlas with adaptive splits, texel
   snapping and boundary fade done; all four spot lights + 1024² shadow maps
   and four point-light 512² cube PCSS maps in one 24-layer array.
6. **Skinning** — done: bone matrix storage, skinned vertex format/WGSL,
   skinned PBR, velocity and shadow pipelines; one animated skinned draw (Walk
   clip) is part of the runtime validation scene.
7. **Render graph**
   - a WebGPU-specific graph API with imported/transient textures;
   - resource aliasing/barriers.
8. **Limits/features**
   - `maxStorageBuffers`, `maxColorAttachments`, timestamp-query fallback;
   - no bindless assumptions.

## Acceptance

- same feature toggles as Vulkan (`SetTAA`, `SetSSAO`, `SetBloom`, shadows);
- no validation errors in Chrome/Dawn;
- golden frame within `vdiff` threshold of Vulkan;
- 60 fps at 960x540 on an integrated GPU.

## Toolchain status

Emscripten 3.1.58 is installed at `C:\emsdk`; the vendored SDL3 is built with
`emcc`, and the legacy `-sUSE_WEBGPU=1` Dawn port is enabled. All six web
examples now compile and link to `.html`/`.js`/`.wasm`:

```text
web_hello, web_3d_example, web_2d_example,
web_gltf_example, scene3d_example, cgltf_min_example
```

Runtime validation passed in Chrome 156 headless (`--use-angle=d3d11`):
`[webgpu-validation] frames=150 errors=0 backend=WebGPU`. The backend was adapted
from the newer surface API to the legacy swapchain API exposed by this emsdk
(`wgpuDeviceCreateSwapChain`/`wgpuSwapChainGetCurrentTextureView`).
`wgpuSwapChainPresent` is intentionally not called: Emscripten's legacy port
aborts on it and presents from the requestAnimationFrame callback instead.
Emscripten 6.x ships `emdawnwebgpu`, whose C API is incompatible with this
source tree. The validation target embeds JS+wasm (`SINGLE_FILE`) so endpoint
security cannot lock a separate `.js` artifact.
