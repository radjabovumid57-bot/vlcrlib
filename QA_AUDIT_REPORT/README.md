# QA Audit Report — C17/SDL3/Vulkan/WebGPU

Date: 2026-11-01. Previous audit archived in `PREVIOUS_AUDIT_2026-11-01/`.

## Reports

- `ARCHITECTURE.md`
- `SDL3_AUDIT.md`
- `VULKAN_AUDIT.md`
- `API_AUDIT.md`
- `CROSS_PLATFORM.md`
- `PERFORMANCE.md`
- `MEMORY_SAFETY.md`
- `STRESS_TESTS.md`
- `BUG_TRACKER.md`
- `AAA_READINESS.md`

## Headline evidence

- Windows MSVC x64 Release: 11/11 ctest; validation smoke 0 errors.
- CPU-only: 9/9 ctest.
- Windows Debug build: PASS; Debug ctest blocked by missing Debug CRT.
- Android arm64-v8a and x86_64 Debug: BUILD SUCCESSFUL (Gradle/NDK); runtime NOT TESTED (emulator hypervisor unavailable).
- Web WebGPU: 150 frames, 0 errors, self-golden mean 0.
- Vulkan vs WebGPU: mean 1.5520, PASS.
- SPIR-V 26/26 valid.
- C17: CMake was C11; fixed and rebuilt Windows/Android/Web.

## Not tested / not implemented

Linux, macOS, iOS, Windows ARM64, Clang builds; WebGL2 fallback; device-loss/OOM;
multithreaded rendering; fullscreen/input automation; ASan/UBSan/RenderDoc.
These are explicitly NOT TESTED / NOT IMPLEMENTED, never PASS.

## Android APK

- vlcrlib-android-arm64-debug.apk
- vlcrlib-android-x86_64-debug.apk

The user-reported black screen is tracked as QA-017; runtime logcat is required.
