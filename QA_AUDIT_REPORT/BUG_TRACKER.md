# Bug Tracker

Severity: P0 critical, P1 high, P2 medium, P3 low, P4 info.

## QA-001 — P1 — SSAO/velocity images sampled in UNDEFINED layout — FIXED

- **File:** `src/vgraphics.c` (`createSSAOResources`, `createSSAODepthPrepass`,
  velocity resources, SSAO graph path).
- **Reproduction:** `VLCRLIB_VALIDATION=1 qa_stress.exe 1000 10` before fix.
- **Expected:** 0 validation errors.
- **Actual:** color/depth image sampled with `VK_IMAGE_LAYOUT_UNDEFINED`.
- **Root cause:** SSAO depth/color and velocity targets created without the
  layout/usage required by their first sample; SSAO depth render pass left
  `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` while descriptor declared read-only.
- **Fix:** initialize depth/color/velocity; SSAO color `TRANSFER_DST` + white
  clear; render-pass final read-only; disable broken SSAO graph path in favor of
  explicit prepass/SSAO pair.
- **Regression:** empty + 1000-object stress `validation=0`;
  `TEST_RESULTS/qa_stress_*_validation.log`.

## QA-002 — P2 — C standard was C11, not required C17 — FIXED

- **File:** `CMakeLists.txt`.
- **Reproduction:** inspect `CMAKE_C_STANDARD`.
- **Expected:** C17.
- **Actual:** C11.
- **Root cause:** build configuration did not match fixed architecture.
- **Fix:** `set(CMAKE_C_STANDARD 17)`.
- **Regression:** Windows Release 11/11, CPU 9/9, Android arm64/x86_64, Web all
  rebuilt successfully.

## QA-003 — P2 — WebGPU missing public `vgraphics_GetValidationErrorCount` — FIXED

- **File:** `src/vgraphics_webgpu.c`.
- **Reproduction:** link a WebGPU build calling the public header function.
- **Expected:** symbol defined.
- **Actual:** only Vulkan implementation; WebGPU had private
  `vgraphics_WebGPUErrorCount`.
- **Root cause:** backend API parity gap.
- **Fix:** alias returns WebGPU error counter.
- **Regression:** WebGPU build links; 150 frames 0 errors.

## QA-004 — P2 — Five unchecked `vkMapMemory` upload paths — FIXED

- **File:** `src/vgraphics.c` (texture, mesh VB/IB, RT readback, line buffer).
- **Reproduction:** force map failure/OOM.
- **Expected:** graceful failure.
- **Actual:** `memcpy` into NULL on map failure.
- **Root cause:** ignored `VkResult`.
- **Fix:** check result, destroy local staging, return failure.
- **Regression:** 11/11 ctest, 0 validation errors.

## QA-005 — P2 — CPU ctest registered Vulkan-only API test — FIXED

- **File:** `CMakeLists.txt`.
- **Reproduction:** `ctest build-cpu` before fix.
- **Expected:** clean CPU suite.
- **Actual:** `api_contracts` Not Run / link errors (`vrgCreate`, `SceneInit`).
- **Root cause:** unconditional test registration in CPU config.
- **Fix:** register only when `VLCRLIB_USE_VULKAN_VALUE`.
- **Regression:** CPU 9/9.

## QA-006 — P2 — API test contradicted CPU/glTF material lifecycle — FIXED

- **File:** `tests/test_api_contracts.c`.
- **Reproduction:** desktop ctest before fix.
- **Expected:** CPU path may allocate material before device.
- **Actual:** test expected id 0.
- **Root cause:** stale test expectation.
- **Fix:** assert valid CPU-side material.
- **Regression:** `api_contracts` and `cpu_gltf` pass.

## QA-007 — P2 — api_smoke culling test placed mesh in front of camera — FIXED

- **File:** `example/api_smoke.c`.
- **Reproduction:** `api_smoke` before fix.
- **Expected:** `culled=1`.
- **Actual:** `culled=0`; z=-20 is in front of camera at z=4.5.
- **Root cause:** test scene geometry.
- **Fix:** move to z=+10.
- **Regression:** `culled=1`, smoke 0 failures.

## QA-008 — P2 — Windows Debug ctest blocked by debug runtime dependency — OPEN (ENV)

- **File:** build/test environment.
- **Reproduction:** `ctest -C Debug`; `api_contracts` exit `0xc0000135`.
- **Expected:** Debug ctest passes.
- **Actual:** 10/11; failure persists after copying VC145 DebugCRT and
  `ucrtbased.dll`, so another Debug-only dependency is unresolved.
- **Root cause:** environment/runtime resolution, not demonstrated engine code.
- **Fix:** inspect with Dependencies/ProcMon or install complete Debug runtime;
  test again.
- **Regression:** pending environment.

## QA-009 — P2 — WebGPU shadow tuning APIs are no-ops — OPEN

- **File:** `src/vgraphics_webgpu.c`.
- **Functions:** `SetShadowQuality`, shadow matrix compute/update, spot/point
  shadow setup.
- **Expected/actual:** Vulkan has effects; WebGPU ignores.
- **Root cause:** not ported.
- **Fix:** implement or expose capability query.
- **Regression:** none.

## QA-010 — P2 — No device-lost/OOM recovery — OPEN

- **File:** Vulkan frame/device path.
- **Expected:** recoverable error state.
- **Actual:** only logging/frame failure.
- **Fix:** device-lost state machine and recreation.
- **Regression:** no test yet.

## QA-011 — P2 — WebGL2 fallback absent — NOT IMPLEMENTED

- **File:** Web backend.
- **Expected by request:** WebGL2 fallback.
- **Actual:** only WebGPU; no GL/GLES backend (also forbidden by fixed stack).
- **Fix:** impossible without violating no-OpenGL rule; document WebGPU-only
  browser compatibility.
- **Regression:** WebGPU PASS.

## QA-012 — P2 — Linux/macOS/iOS/Windows ARM64/Clang not verified — NOT TESTED

- **File:** build/platform.
- **Reason:** no WSL distro/Docker, no Apple SDK, no clang compiler, VS ARM64
  configure failure. See `CROSS_PLATFORM.md`.
- **Fix:** real platform CI/agents.

## QA-013 — P2 — Android runtime not verified — NOT TESTED

- **File:** Android package/emulator.
- **Build:** arm64/x86_64 PASS.
- **Runtime:** emulator blocked: Android Emulator hypervisor driver absent.
- **Fix:** install hypervisor/real device and run APK.

## QA-014 — P3 — `LoadShader` public stub — OPEN

- **File:** `vgraphics.c`, `vgraphics_webgpu.c`.
- **Expected:** compile shader.
- **Actual:** returns `{0}`.
- **Fix:** implement or capability-flag/major-version removal.

## QA-015 — P3 — No instancing/compute path — NOT IMPLEMENTED

- No instanced draw or compute pipeline found in audited path. AAA gap, not a
  regression.

## QA-016 — P3 — Single-threaded command recording — OPEN

- One graphics+present queue; main-thread command buffer. AAA scalability gap.

## QA-017 � P1 � Android black screen after launch (USER-REPORTED, NOT REPRODUCED)

- **File:** `example/mobile_gltf.c`, `android/app/src/main/AndroidManifest.xml`,
  Android Gradle/CMake packaging.
- **Reproduction:** install `vlcrlib-android-arm64-debug.apk` on an Android phone
  and launch; user screenshot shows a black app surface with system navigation.
- **Expected:** SDL3 window with glTF viewer, grid and HUD.
- **Actual:** black screen; no app UI visible.
- **Root cause:** NOT DETERMINED. No device is attached to this host
  (`adb devices` empty); emulator cannot start because the Android Emulator
  hypervisor driver is absent. APK contains `libmain.so`, `libSDL3.so`,
  `libc++_shared.so`, embedded SPIR-V, and `assets/DamagedHelmet.glb`.
- **Fix:** pending `adb logcat` from the device. First filter:
  `adb logcat -s SDL:V vlcrlib:V DEBUG:*` and look for
  `InitGraphicsDevice failed`, `cannot open shader`, `SDL_CreateWindow failed`,
  or an Activity/native crash.
- **Regression:** not executed; runtime NOT TESTED.
