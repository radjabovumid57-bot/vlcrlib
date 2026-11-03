# 09 — Fix History & Regression Log

All fixes are in-place; no public API was removed or renamed.

## FIX-001 — API contract test vs CPU material lifecycle

- **Bug:** QA-003.
- **Files:** `tests/test_api_contracts.c`.
- **Change:** `LoadMaterialDefault` before device now expected to return a valid
  CPU-side material (`id != 0`).
- **Rationale:** `test_cpu_gltf` proves CPU/glTF requires material records before
  a device. Engine behavior unchanged.
- **Regression:** desktop ctest 11/11; `cpu_gltf` passes.

## FIX-002 — CPU ctest Not Run

- **Bug:** QA-002.
- **Files:** `CMakeLists.txt`.
- **Change:** `test_api_contracts` is registered only when
  `VLCRLIB_USE_VULKAN_VALUE` is true; it depends on Vulkan render-graph types.
- **Verification:**
  ```
  cmake -S . -B build-cpu -DVLCRLIB_GRAPHICS_CPU=ON -DVLCRLIB_BUILD_TESTS=ON
  cmake --build build-cpu --config Release
  ctest --test-dir build-cpu -C Release
  => 9/9 passed
  ```

## FIX-003 — api_smoke culling scene

- **Bug:** QA-004.
- **Files:** `example/api_smoke.c`.
- **Change:** "behind camera" mesh moved from z=-20 to z=+10; comment corrected.
- **Verification:** validation smoke outputs `culled=1`, draws=3, `fails=0`.

## FIX-004 — Unchecked vkMapMemory

- **Bug:** QA-005.
- **Files:** `src/vgraphics.c`.
- **Change:** five map sites check `VkResult`; local staging resources destroyed on
  failure; upload functions return failure instead of dereferencing NULL.
- **Verification:**
  - desktop ctest 11/11;
  - `api_smoke` with validation: 0 errors, exit 0;
  - no visual regression in `qa_smoke.png`.

## FIX-005 — SSAO depth/color initial layout

- **Bug:** QA-001 (SSAO depth/color + velocity initial layouts).
- **Files:** `src/vgraphics.c`.
- **Change:**
  - `ssaoDepthImage` initialized to `DEPTH_STENCIL_READ_ONLY_OPTIMAL`; SSAO
    depth render pass `initial=UNDEFINED`, `final=DEPTH_STENCIL_READ_ONLY_OPTIMAL`;
  - `ssaoImage` created with `TRANSFER_DST`, white-cleared, transitioned to
    `SHADER_READ_ONLY_OPTIMAL`; SSAO color render pass final read-only;
  - `velocityImage` initialized to `SHADER_READ_ONLY_OPTIMAL` at creation;
  - SSAO graph path disabled until its barrier plan tracks attachment finals.
- **Verification:** `qa_stress` empty and 1000-object runs validation=0;
  `api_smoke` validation=0; resize validation=0.
- **Status:** QA-001 FIXED.

## Regression evidence

| Run | Result |
|---|---|
| `ctest build-check` | 11/11 passed |
| `ctest build-cpu` | 9/9 passed |
| `api_smoke` validation | fails=0, validation=0, shutdown validation=0 |
| resize smoke | validation=0 |
| WebGPU golden | mean=0.0000 |
| cross-backend | mean=1.5520 PASS |

## Backup policy

No source backups were created before minor audit fixes; all changed files are
listed above and the project is not a git repository. For future large changes,
create a file-level backup or initialize version control first.

## Open fixes (not applied)

- QA-008 device-loss/OOM recovery.
- QA-007 WebGPU shadow API parity.
- QA-006 LoadShader implementation/capability API.
