# Milestone: velocity TAA, Render Graph post-process, convolution reverb

This is the next rendering/audio milestone. It is intentionally scoped as a
multi-change refactor; the current tree already ships TAA with depth
reprojection, but the three items below need shared-resource changes rather
than local patches.

## 1. TAA velocity buffer for moving objects

### Design

1. Add a second scene color attachment:
   - `VK_FORMAT_R16G16_SFLOAT`;
   - usage `COLOR_ATTACHMENT | SAMPLED`;
   - written by PBR / skin / 3D pipelines as `outVelocity`.
2. Vertex shaders write:
   ```glsl
   out vec2 vPrevClip;
   out vec2 vCurClip;
   ```
   - current vertex uses the current model/skeleton;
   - previous vertex uses the previous model matrix and previous bone UBO slot.
3. Fragment velocity:
   ```glsl
   vec2 prevNdc = vPrevClip / vPrevClip.w;
   vec2 curNdc  = vCurClip / vCurClip.w;
   outVelocity = vec4((curNdc - prevNdc) * 0.5, 0, 1);
   ```
4. Composite samples velocity + depth; TAA uses velocity directly and only
   falls back to depth reprojection when velocity is zero.
5. Previous state:
   - `ModelEntry.prevTransform`;
   - `SkinnedModelEntry.prevBoneMatrices` per frame slot;
   - swap current/previous on `EndVulkanFrame`.
6. Resize/cleanup: scene/depth/TAA/velocity/SSAO/bloom targets and their
   descriptors are recreated with the new swapchain extent.

### Acceptance

- moving cube has no trailing ghost at 1–4 px/frame;
- static scene unchanged;
- validation 0 errors;
- `api_smoke` with a moving model passes;
- skinned velocity uses per-model current/previous bone buffers (`velocity_skin.vert`);
- golden test: `cmake --build build-check --target golden_skinned`.

## 2. SSAO/Bloom on Render Graph

Status: Bloom bright/blurH/blurV, SSAO depth prepass/resolve and composite are
migrated. Remaining: skinned velocity and convolution reverb.

### Design

1. Add graph images for:
   - half-res SSAO depth;
   - SSAO color;
   - bloom bright;
   - bloom ping-pong.
2. Split recording into pass callbacks:
   - `ssao_depth`;
   - `ssao`;
   - `bloom_bright`;
   - `bloom_blur_h`;
   - `bloom_blur_v`;
   - `composite`.
3. Remove manual layout transitions from pass functions; graph owns:
   - `UNDEFINED -> ATTACHMENT`;
   - `ATTACHMENT -> SHADER_READ`;
   - `SHADER_READ -> ATTACHMENT` for ping-pong.
4. Keep composite external-image graph as the last pass.

### Acceptance

- barrier plan includes every SSAO/bloom transition;
- no manual `vkCmdPipelineBarrier` in post-process code;
- image validation clean;
- visual diff vs current frame within `vdiff --threshold 2`.

## 3. Convolution reverb

### Design

- miniaudio custom `ma_node` with an FIR impulse response:
  - load IR as WAV via miniaudio decoder;
  - ring buffer + partitioned convolution;
  - wet/dry + pre-delay;
  - one global reverb bus fed by zone sends.
- Zone API:
  ```c
  AudioReverbZone zone = {center, radius, ir, wet};
  ```
- Per-sound send level from position inside zone.
- Fallback to the current dual-delay room reverb when no IR is loaded.

### Acceptance

- IR length up to 1 s works without dropouts at 48 kHz;
- tail decays smoothly;
- no allocations in the audio callback;
- fallback remains functional on machines without miniaudio.
