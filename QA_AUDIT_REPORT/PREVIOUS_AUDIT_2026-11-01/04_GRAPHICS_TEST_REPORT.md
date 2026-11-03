# 04 — Graphics Scenario Test Report

## Executed scenarios

| # | Scenario | How tested | Result |
|---|---|---|---|
| 1 | Triangle / 2D | not executed standalone in this audit | NOT RUN |
| 2 | Multiple objects | `qa_stress` 1/10/1000/10000/100000 DrawModel | PASS |
| 3 | Vertex/index buffers | `api_smoke`, `qa_stress`, glTF | PASS |
| 4 | Textures | `api_smoke` glTF, WebGPU KTX2 fixture | PASS |
| 5 | PBR materials | `api_smoke` DamagedHelmet, WebGPU validation, cross golden | PASS |
| 6 | Lighting | `api_smoke` directional+ambient; WebGPU 4 spots/4 points | PASS |
| 7 | Shadows | `api_smoke` CSM; WebGPU 4 spot PCSS + 4 point cube PCSS | PASS |
| 8 | HDR / IBL | WebGPU validation IBL; cross example IBL | PASS (WebGPU); Vulkan IBL example not rerun |
| 9 | Post-processing | `api_smoke` FXAA/TAA; WebGPU toggles; SSAO/bloom paths | PASS |
| 10 | Skybox | not executed standalone | NOT RUN |
| 11 | Skeletal animation | WebGPU validation Walk clip; `test_animator` math | PASS |
| 12 | Instancing | no instancing draw path found | NOT SUPPORTED |
| 13 | Compute shaders | no compute pipeline in audited path | NOT TESTED |
| 14 | Offscreen rendering | `api_smoke` offscreen scene/post targets; screenshot | PASS |
| 15 | Resize/fullscreen | `VLCR_RESIZE=1` + shadow resize, validation 0 | PASS |
| 16 | Multiple render passes | shadow → SSAO → scene → composite | PASS, validation=0 |

## Visual evidence

- `docs/qa_smoke.png` — DamagedHelmet PBR + floor + shadow, 800x600, 90 frames,
  `SMOKE OK`.
- `docs/webgpu_validation.png` — WebGPU 150-frame scene with 4 spot PCSS,
  4 point PCSS, IBL, KTX2 BC5 AO/normal quads, skinning.
- `docs/cross_backend_vulkan.png` / `docs/cross_backend_webgpu.png` — direct
  Vulkan vs WebGPU comparison.

## Golden results

```
WebGPU self-golden: mean=0.0000 max=0
Vulkan-vs-WebGPU:   mean=1.5520, PASS (threshold 6, max-over 5%)
```

The cross harness uses CDP `Emulation.setDeviceMetricsOverride(960x540)`; without
it Chrome opens the canvas at 942x388 and produces a false aspect/FOV diff.

## Not proven / limitations

- No compute/instancing test because no such public draw path was found in the
  audited implementation.
- MSAA, fullscreen exclusive, HDR swapchain, and multiple windows were not run.
- `qa_stress` empty and 1000-object paths are now validation-clean (QA-001
  fixed).

## Graphics verdict

PBR/glTF/lighting/shadow/IBL/skinning/offscreen/resize are functional and
visually correct on the tested scene. The renderer is **not** a full AAA feature
set: no instancing, no compute path evidenced, no MSAA verification.
