# Test Results Index

| File | Meaning |
|---|---|
| windows_ctest_release.log | Windows MSVC x64 Release, 11/11 |
| windows_ctest_debug.log | Windows Debug, 10/11; api_contracts missing Debug CRT |
| cpu_only_ctest.log | CPU backend, 9/9 |
| windows_vulkan_validation_smoke.log | 90-frame PBR/glTF/shadow, validation 0 |
| windows_smoke.png | visual smoke (800x600) |
| spirv_validation.log | spirv-val, 26/26 |
| web_webgpu_golden.log | Emscripten WebGPU 150 frames, golden mean 0 |
| cross_backend.log | Vulkan vs WebGPU, mean 1.5520 PASS |
| android_arm64_build.log | Gradle arm64-v8a BUILD SUCCESSFUL |
| android_x86_64_build.log | Gradle x86_64 BUILD SUCCESSFUL |
| android_emulator_launch.log | emulator blocked: hypervisor driver absent |
| windows_arm64_configure.log | ARM64 configure failure (VS generator) |

ASan/UBSan/RenderDoc/GPU-assisted validation were not available.

| android_arm64.apk | arm64-v8a debug APK (5859087 bytes) |
| android_x86_64.apk | x86_64 debug APK (5968274 bytes) |
