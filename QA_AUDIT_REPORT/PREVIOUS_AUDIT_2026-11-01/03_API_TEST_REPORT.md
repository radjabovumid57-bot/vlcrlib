# 03 — API Functional Test Report

## Test suites actually executed

| Suite | Result | Log |
|---|---|---|
| `ctest` desktop (Vulkan, Release) | **11/11 passed** | `TEST_RESULTS/vulkan_ctest.log` |
| `ctest` CPU-only (Release) | **9/9 passed** | `TEST_RESULTS/cpu_ctest.log` |
| `api_smoke` with validation | **0 app failures**, 0 validation errors | `TEST_RESULTS/api_smoke_validation.log` |
| Resize smoke | pass, 0 validation errors | `TEST_RESULTS/resize_validation.log` |
| SPIR-V validation | 26/26 valid | `TEST_RESULTS/spirv_validation.log` |

## Contract coverage

`tests/test_api_contracts.c` covers:

- `LoadModel(NULL/""/missing)`, `LoadSkinnedModel(NULL)`;
- `LoadTextureKTX2(NULL)`, `LoadTextureFromImage({0})`;
- `vgraphics_RegisterMesh(NULL)`, `LoadRenderTexture(0,0)`;
- `vgraphics_CreateModelFromMeshesEx(NULL)`;
- file API NULL paths;
- `vgraphics_CaptureScreenshot(NULL/"")`;
- `vgraphics_GetFrameStats(NULL)`, `vgraphics_GetRenderTargetSize` before device;
- scene add/save/load NULL/invalid parent/default scale;
- animator NULL paths;
- audio reverb NULL/missing;
- render graph invalid image/pass.

## Defect found: material lifecycle contract (fixed)

`LoadMaterialDefault()` before a Vulkan device was expected by the test to return
id 0, but the CPU/glTF path intentionally allocates CPU-side material records
before device creation (`test_cpu_gltf` verifies this). The engine behavior is
correct for the CPU path; the **test expectation was wrong**.

Fix: test now asserts `id != 0` and documents the CPU-path semantics. No engine
API change. See QA-003.

## Defect found: CPU ctest Not Run (fixed)

In the CPU configuration `test_api_contracts` was registered but cannot link
(`vrg*`/`vscene`/`vaudio` not in the CPU source set). CTest reported
`Not Run` instead of a build error.

Fix: CMake now registers `test_api_contracts` only when
`VLCRLIB_USE_VULKAN_VALUE` is true. CPU ctest is 9/9. See QA-002.

## Public stubs / no-ops

| API | Backend | Behavior | Classification |
|---|---|---|---|
| `LoadShader`/`UnloadShader` | Vulkan, WebGPU | returns `{0}`, no-op | documented stub, QA-006 |
| `SetShadowQuality` | WebGPU | no-op | QA-007 |
| `vgraphics_ComputeDirectionalShadowMatrix` | WebGPU | no-op | QA-007 |
| `vgraphics_ComputeDirectionalShadowCascades` | WebGPU | no-op | QA-007 |
| `vgraphics_ComputeSpotShadowMatrix` | WebGPU | no-op | QA-007 |
| `vgraphics_UpdateShadowMatrix` | WebGPU | no-op | QA-007 |
| `vgraphics_SetSpotShadows` | WebGPU | no-op | QA-007 |
| `vgraphics_SetPointShadows` | WebGPU | no-op | QA-007 |

These are backend-fidelity gaps, not crashes. Callers need capability reporting.

## Not covered by automated tests

- Invalid Vulkan handles after device shutdown (beyond NULL checks).
- Concurrent calls / thread safety (not claimed by API).
- Device loss during frame.
- Out-of-memory injection.
- 100k-object API behavior through unit tests (covered by `qa_stress` process).

## API test verdict

**Core handle/NULL contract is solid and green.** Backend no-ops must be
documented or exposed through capabilities before the API is AAA-consumable.
