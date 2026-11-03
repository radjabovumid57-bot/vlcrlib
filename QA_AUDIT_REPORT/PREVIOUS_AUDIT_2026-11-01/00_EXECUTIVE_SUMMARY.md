# vlcrlib — Executive QA Summary

Date: 2026-11-01  
Auditor mode: independent engine QA / Vulkan / code audit  
Workspace: `C:\Users\Admin\Documents\vlcrlib`

## Verdict

**Not AAA-ready yet; usable for mid-scale projects and technically strong for a solo/small-team engine.**

The API passes its own unit/API/regression suite, the WebGPU golden, and the
Vulkan-vs-WebGPU golden. The audit found and fixed a **P1 image-layout defect**
(SSAO/velocity resources in `UNDEFINED`), plus five OOM-unsafe upload paths and
two test/build defects. Validation is now clean for the empty, 1000-object,
PBR/glTF/shadow, and resize paths. Several AAA-scope subsystems (device-loss
recovery, multithreaded recording, full render graph) are not present.

## Environment actually used

| Item | Value |
|---|---|
| GPU | NVIDIA GeForce RTX 5050, discrete, driver 617.42 |
| Vulkan device API | 1.4.351 |
| Vulkan instance | 1.4.357 |
| Validation layer | `VK_LAYER_KHRONOS_validation` 1.4.357 |
| SPIR-V validator | Vulkan SDK 1.4.357 `spirv-val` |
| Browser WebGPU | headless Chrome, D3D11/ANGLE |
| CPU build | `VLCRLIB_GRAPHICS_CPU=ON` |

## Executed evidence

| Check | Result | Evidence |
|---|---|---|
| Desktop unit/API/regression | **11/11 passed** | `TEST_RESULTS/vulkan_ctest.log` |
| CPU-only tests | **9/9 passed** | `TEST_RESULTS/cpu_ctest.log` |
| Vulkan validation smoke (PBR + glTF + shadows, 90 frames) | **0 validation errors**, 0 app failures | `TEST_RESULTS/api_smoke_validation.log` |
| Resize + shadow resize under validation | **0 validation errors** | `TEST_RESULTS/resize_validation.log` |
| SPIR-V validation | **26/26 valid** | `TEST_RESULTS/spirv_validation.log` |
| WebGPU runtime/golden | 150 frames, 0 errors, `mean=0` | `TEST_RESULTS/webgpu_golden.log` |
| Vulkan vs WebGPU golden | `mean=1.5520`, PASS | `TEST_RESULTS/cross_backend.log` |
| Stress 1k / 10k / 100k objects | no crash; perf measured | `BENCHMARKS/summary.csv` |
| Long run 1000 frames | no crash/hang | `BENCHMARKS/stress_long_1000frames.csv` |
| Repeated create/destroy (5x smoke) | exits `0,0,0,0,0` | this report |
| Validation stress harness | **0 errors** (empty + 1000 objects) after fixes | `TEST_RESULTS/qa_stress_*_validation.log` |
| ASan/UBSan | **not run — runtime absent** (`-lasan/-lubsan` missing) | this report |
| RenderDoc / GPU-assisted validation / sync validation | **not run** | this report |

## Defects found and fixed in this audit

- `test_api_contracts` contradicted the CPU/glTF material lifecycle; test corrected.
- CPU ctest registered a Vulkan-only API test; CMake target now Vulkan-only.
- `api_smoke` placed the "behind camera" mesh in front of the camera; test scene corrected.
- Five unchecked `vkMapMemory` upload paths could `memcpy` into NULL on OOM; checks and staging cleanup added.

## Fixed in this audit

- **QA-001 P1**: SSAO depth/color, velocity and graph initial layouts corrected;
  empty and 1000-object stress are validation-clean.

## Open defects / blockers

- **QA-007 P2**: WebGPU `SetShadowQuality`, spot/point shadow setup functions are no-ops.
- **QA-008 P2**: no device-lost / out-of-memory recovery path.
- **QA-006 P3**: `LoadShader` is a documented public stub returning `{0}`.
- **QA-009 P3**: GPU golden tests are opt-in, not part of default CI ctest.
- AAA gaps: full render graph execution, multithreaded command recording, asset streaming,
  virtualized geometry, production profiling hooks.

## Bottom line

The engine is **not AAA-ready**, but the core Vulkan/WebGPU PBR path is functional, deterministic
enough for golden testing, and clean under validation in the main scene. The highest-priority
engineering item is **device-loss/OOM recovery**, followed by multithreaded
recording and WebGPU shadow API parity. The image-layout P1 found during the
audit is fixed and regression-tested.
