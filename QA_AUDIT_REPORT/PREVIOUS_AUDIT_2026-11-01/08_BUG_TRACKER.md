# 08 — Bug Tracker

Severity: P0 critical, P1 high, P2 medium, P3 low, P4 info.

---

## QA-001 — P1 — Sampled image in UNDEFINED layout (FIXED)

- **File/function:** SSAO depth/color setup in `src/vgraphics.c`
  (`createSSAOResources`, `createSSAODepthPrepass`, first-frame prepass).
- **Description:** Vulkan Validation reports sampled depth/color images in
  `VK_IMAGE_LAYOUT_UNDEFINED`.
- **Reproduction (initial, now fixed):**
  ```
  VLCRLIB_VALIDATION=1 qa_stress.exe 1 2
  QA_NO_DRAW=1 QA_NO_TEXT=1 QA_NO_SSAO=1 QA_NO_POST=1
  ```
- **Expected:** 0 validation errors.
- **Actual before fix:** depth image read before first transition; 1–4 errors.
- **Root cause:** two independent lifecycle bugs:
  1. `ssaoDepthImage` created `UNDEFINED`, sampled by the first prepass, then
     left in `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` while the descriptor declared
     `DEPTH_STENCIL_READ_ONLY_OPTIMAL`;
  2. `velocityImage` created `UNDEFINED` but bound in the composite/TAA
     descriptor and sampled before its first write.
- **Fix applied:**
  - SSAO depth init `DEPTH_STENCIL_READ_ONLY_OPTIMAL`, render pass
    `initial=UNDEFINED`, `final=DEPTH_STENCIL_READ_ONLY_OPTIMAL`;
  - SSAO color created with `TRANSFER_DST`, white-cleared, transitioned to
    `SHADER_READ_ONLY_OPTIMAL`, render pass final read-only;
  - velocity color initialized to `SHADER_READ_ONLY_OPTIMAL`;
  - broken SSAO render-graph path disabled in favor of the explicit
    prepass/SSAO pair until its barrier plan tracks attachment final layouts.
- **Verified:** `qa_stress` empty `validation=0`; 1000 objects `validation=0`;
  `api_smoke` `validation=0`; resize `validation=0`.
- **Status:** **FIXED.**
- **Regression test:** `qa_stress` empty and full draw under validation = 0.

---

## QA-002 — P2 — CPU ctest registers Vulkan-only API test (Not Run)

- **File:** `CMakeLists.txt` (`test_api_contracts` registration).
- **Description:** CPU build excludes `vrender_graph.c`/`vscene`/`vaudio`, but
  `test_api_contracts` was still added; CTest reported `Not Run`.
- **Reproduction:** `ctest --test-dir build-cpu -C Release` (before fix).
- **Expected:** CPU suite passes cleanly.
- **Actual:** 9/10 passed, `api_contracts` Not Run; direct target link failed
  with 18 unresolved symbols (`vrgCreate`, `SceneInit`, `SetReverbWet`, ...).
- **Root cause:** unconditional test registration in CPU configuration.
- **Fix:** register `test_api_contracts` only under
  `if(VLCRLIB_USE_VULKAN_VALUE)`.
- **Regression test:** CPU ctest is 9/9 (`TEST_RESULTS/cpu_ctest.log`).

---

## QA-003 — P2 — API contract test contradicted CPU material lifecycle

- **File:** `tests/test_api_contracts.c`.
- **Description:** asserted `LoadMaterialDefault().id == 0` before a device, but
  CPU/glTF loading intentionally allocates CPU-side material records before a
  device (`test_cpu_gltf` depends on it).
- **Reproduction:** desktop ctest before fix: `api_contracts` 1 failure.
- **Expected:** test reflects supported CPU path.
- **Actual:** `[FAIL] LoadMaterialDefault before device`.
- **Root cause:** stale test expectation, not engine defect.
- **Fix:** test now asserts `id != 0` and documents CPU-path semantics.
- **Regression test:** `api_contracts` passes; `cpu_gltf` still passes.

---

## QA-004 — P2 — api_smoke culling test placed mesh in front of camera

- **File:** `example/api_smoke.c`.
- **Description:** "behind camera" mesh was at z=-20 with camera at z=4.5
  looking toward -Z, i.e. in front; test expected culling.
- **Reproduction:** `api_smoke` before fix: `[FAIL] frustum culling rejects
  behind-camera mesh`, `culled=0`.
- **Expected:** culled > 0.
- **Actual:** culled 0; draws included the second model.
- **Root cause:** test scene geometry, not culling code.
- **Fix:** move to z=+10 (behind camera), comment corrected.
- **Regression test:** `culled=1`, draws=3, smoke passes.

---

## QA-005 — P2 — Unchecked `vkMapMemory` in upload paths

- **File:** `src/vgraphics.c` (texture upload, mesh VB/IB upload, render-target
  readback, line-buffer upload).
- **Description:** `vkMapMemory` result ignored, then `memcpy(mapped, ...)`.
- **Reproduction:** force `VK_ERROR_MEMORY_MAP_FAILED`/OOM; not injected here.
- **Expected:** fail gracefully with cleaned staging resources.
- **Actual:** NULL dereference/corruption on map failure.
- **Root cause:** unchecked Vulkan result on host-visible upload paths.
- **Fix:** check each map; destroy staging buffer/memory and return failure.
- **Regression test:** full ctest 11/11; validation smoke 0 errors.

---

## QA-006 — P3 — `LoadShader` is a public stub

- **File:** `src/vgraphics.c`, `src/vgraphics_webgpu.c`, `src/vgraphics.h`.
- **Description:** `LoadShader` returns `(Shader){0}`, `UnloadShader` no-op.
- **Reproduction:** call `LoadShader("a.vert","a.frag")`.
- **Expected/actual:** documented "not implemented"; returns empty handle.
- **Root cause:** intentionally left for ABI stability.
- **Fix:** not fixed. Expose capability or remove from public surface in a major
  version.
- **Regression test:** none (documented behavior).

---

## QA-007 — P2 — WebGPU shadow-tuning APIs are no-ops

- **File:** `src/vgraphics_webgpu.c` (`SetShadowQuality`,
  `vgraphics_ComputeDirectionalShadowMatrix`,
  `vgraphics_ComputeDirectionalShadowCascades`,
  `vgraphics_ComputeSpotShadowMatrix`, `vgraphics_UpdateShadowMatrix`,
  `vgraphics_SetSpotShadows`, `vgraphics_SetPointShadows`).
- **Description:** functions exist for link compatibility but do nothing.
- **Reproduction:** call on WebGPU; no effect.
- **Expected/actual:** documented backend gap; no runtime error.
- **Root cause:** not ported to WebGPU.
- **Fix:** implement or mark as unsupported in a capability API.
- **Regression test:** WebGPU runtime remains 0 errors.

---

## QA-008 — P2 — No device-lost / OOM recovery

- **File:** `src/vgraphics_device.c`, `src/vgraphics.c`.
- **Description:** `VK_ERROR_DEVICE_LOST`/`VK_ERROR_OUT_OF_DEVICE_MEMORY` are only
  stringified; frame failure sets `VK.frameFailed` and stops, with no recreation.
- **Reproduction:** device loss/OOM injection not performed.
- **Expected:** recoverable error path for AAA.
- **Actual:** no recovery implementation.
- **Root cause:** architectural gap.
- **Fix:** OPEN. Add device-lost state machine and resource recreation.
- **Regression test:** none yet.

---

## QA-009 — P3 — GPU golden tests opt-in only

- **File:** `CMakeLists.txt` (`VLCRLIB_GPU_TESTS` default OFF).
- **Description:** `golden_skinned` is not part of default ctest; WebGPU/cross
  goldens are external PowerShell scripts.
- **Reproduction:** `ctest -N` shows 11 tests, no golden_skinned.
- **Expected:** CI regression coverage.
- **Actual:** manual/opt-in.
- **Fix:** OPEN (CI concern, not renderer bug).

---

## QA-010 — P2 — Single-threaded command recording / single queue

- **File:** `src/vgraphics.c` frame path.
- **Description:** one graphics+present queue, one command buffer recorded on the
  main thread; no worker command pools.
- **Reproduction:** code inspection.
- **Expected for AAA:** parallel recording, transfer queue.
- **Actual:** no MT path.
- **Fix:** architectural; not a regression.

---

## QA-011 — P4 — Missing capability reporting for backend differences

- **Description:** callers cannot query whether `LoadShader`, point shadows,
  compute, etc. are supported.
- **Fix:** add `vgraphics_GetCapabilities()` in a future API revision.
- **Status:** OPEN (recommendation).

---

## QA-012 — P3 — Validation runs not automated in CTest

- **Description:** validation requires `VLCRLIB_VALIDATION=1` and manual example.
- **Fix:** add a CTest target with environment and `VLCRLIB_VALIDATION=1`.
- **Status:** OPEN (recommendation).
