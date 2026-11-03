# Cross-Platform Build & Runtime Audit

No support claim is made for a platform that was not actually built/run.

| Platform / toolchain | Build | Runtime/tests | Status |
|---|---|---|---|
| Windows x64, MSVC 19.x Release | PASS | 11/11 ctest; PBR/glTF/shadow validation 0; resize 0 | **PASS** |
| Windows x64, MSVC Debug | PASS | ctest 10/11; `api_contracts` exit `0xc0000135` (debug runtime dependency unresolved even after copying VC145 DebugCRT/ucrtbased) | **BUILD PASS, RUNTIME BLOCKED** |
| Windows x64, Clang | NOT TESTED | no clang compiler (only clang-format/tidy installed) | **NOT TESTED** |
| Windows ARM64, MSVC | CONFIGURE FAILED | VS generator `VCTargetsPath`/ARM64 output-path error; no artifact | **NOT TESTED / BLOCKED** |
| Linux x86_64 GCC | NOT TESTED | no WSL distro, no Docker/Linux host | **NOT TESTED** |
| Linux x86_64 Clang | NOT TESTED | no compiler/host | **NOT TESTED** |
| Linux ARM64 | NOT TESTED | no host/toolchain | **NOT TESTED** |
| macOS Apple Silicon / Intel | NOT TESTED | Windows host, no Apple SDK | **NOT TESTED** |
| iOS Simulator / iPhone / iPad | NOT TESTED | no Xcode/Apple SDK | **NOT TESTED** |
| Android arm64-v8a Debug | **PASS** | Gradle + NDK 26.1 + JDK21; APK arm64 | **BUILD PASS, RUNTIME NOT TESTED** |
| Android x86_64 Debug | **PASS** | temporary `abiFilters=x86_64`; APK x86_64; config restored | **BUILD PASS, RUNTIME NOT TESTED** |
| Android runtime | BLOCKED | emulator: "x86_64 emulation currently requires hardware acceleration"; no hypervisor driver | **NOT TESTED** |
| WebAssembly / Emscripten WebGPU | **PASS** | 150 frames, 0 errors, self-golden mean=0; cross mean=1.5520 | **PASS** |
| WebGL2 fallback | **NOT IMPLEMENTED** | no WebGL/GLES backend in repo | **NOT IMPLEMENTED** |

## Commands/evidence

- Windows: `cmake --build build-check --config Release`; `ctest -C Release` →
  `TEST_RESULTS/windows_ctest_release.log`.
- Android arm64: `gradlew :app:assembleDebug` with JDK21 and NDK 26.1 →
  `TEST_RESULTS/android_arm64_build.log`.
- Android x86_64: temporary `abiFilters += listOf("x86_64")`, build, restore →
  `TEST_RESULTS/android_x86_64_build.log`.
- Android emulator: `emulator -avd ... -no-window -gpu swiftshader_indirect` →
  `TEST_RESULTS/android_emulator_launch.log`.
- Web: `cmake --build build-web --target web_validate_example`; WebGPU golden →
  `TEST_RESULTS/web_webgpu_golden.log`.
- Cross-backend: CDP capture → `TEST_RESULTS/cross_backend.log`.

## Platform-specific notes

- **Windows Debug ctest** fails with `0xc0000135` even after copying
  VC145 DebugCRT and `ucrtbased.dll`; another Debug-only dependency is
  unresolved. The Debug build itself succeeds.
- **Android** APK is produced; emulator cannot start because Android Emulator
  hypervisor driver is not installed. Real device/emulator runtime is unverified.
- **macOS/iOS** cannot be built on a Windows host without Xcode and Apple SDK.
  MoltenVK is not present in this workspace, so no SDL3/Vulkan macOS/iOS claim.
- **Linux** cannot be built here without a Linux environment; no cross-build
  claim is made.
- **Web** has no WebGL2 fallback; only the WebGPU backend exists. This conflicts
  with the requested fallback but complies with the prohibition on OpenGL/GLES.

## Cross-platform API behavior

Only Windows Vulkan and WebAssembly WebGPU were executed. No claim is made that
SDL3 input, fullscreen, swapchain behavior, asset paths or threading are
equivalent on macOS/iOS/Linux/Android runtime.

## Runtime scenario matrix (executed subset)

| Scenario | Windows Vulkan | Web WebGPU | Android |
|---|---|---|---|
| API initialization / device creation | PASS | PASS | BUILD ONLY |
| Window creation | PASS | PASS | BUILD ONLY |
| Triangle rendering | NOT TESTED standalone | NOT TESTED standalone | NOT TESTED |
| Texture rendering | PASS (glTF/KTX2) | PASS (KTX2 BC5) | NOT TESTED |
| Shader compilation | PASS static SPIR-V; LoadShader stub | WGSL runtime PASS; LoadShader stub | BUILD ONLY |
| PBR rendering | PASS | PASS | NOT TESTED |
| Multiple objects | PASS (1k/10k/100k) | PASS (validation scene) | NOT TESTED |
| Resource destruction | PASS (validation shutdown) | PASS (page shutdown) | NOT TESTED |
| Resize | PASS (validation 0) | PASS (CDP viewport) | NOT TESTED |
| Fullscreen | NOT TESTED | NOT TESTED | NOT TESTED |
| Input | NOT TESTED (automation) | NOT TESTED | NOT TESTED |
| Multithreading | NOT IMPLEMENTED | NOT IMPLEMENTED | NOT IMPLEMENTED |
| Memory stress | 100k objects PASS | NOT TESTED | NOT TESTED |
| Shutdown/restart | PASS (5x smoke) | PASS (reload runs) | NOT TESTED |

## Android black-screen report

User-provided screenshot shows a black Android app surface with system nav.
This cannot be reproduced here: `adb devices` has no device, and the emulator
fails with "x86_64 emulation currently requires hardware acceleration".

APK evidence:

- `vlcrlib-android-arm64-debug.apk` (arm64-v8a)
- `vlcrlib-android-x86_64-debug.apk` (x86_64, emulator only)
- APK contents: `libmain.so`, `libSDL3.so`, `libc++_shared.so`,
  `assets/DamagedHelmet.glb`; SPIR-V embedded into `libmain.so`.

Needed to diagnose: `adb logcat -s SDL:V vlcrlib:V DEBUG:*` while launching.
Potential classes of failure: Vulkan surface/device init, SDL Activity startup,
asset extraction, or embedded pipeline creation. Do not assume a fix until the
log is captured.
