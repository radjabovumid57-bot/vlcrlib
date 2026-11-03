# Test Results Index

| File | What it is |
|---|---|
| `vulkan_ctest.log` | initial desktop ctest (before fixes) |
| `vulkan_ctest_after_fixes.log` | final desktop ctest, 11/11 |
| `cpu_ctest.log` | initial CPU ctest (before CMake fix) |
| `cpu_ctest_after_fixes.log` | final CPU ctest, 9/9 |
| `api_smoke_validation.log` | PBR/glTF/shadow smoke, 90 frames, validation 0 |
| `resize_validation.log` | resize + shadow-resize smoke, validation 0 |
| `spirv_validation.log` | spirv-val over 26 compiled shaders |
| `qa_stress_empty_validation.log` | empty frame path, validation 0 |
| `qa_stress_full_validation.log` | 1000-object path, validation 0 after QA-001 fix |
| `webgpu_golden.log` | WebGPU 150-frame golden, mean 0 |
| `cross_backend.log` | Vulkan vs WebGPU, mean 1.5520 PASS |
| `qa_smoke.png` | visual smoke screenshot (800x600, SMOKE OK) |

Unavailable tools: ASan/UBSan runtime (`-lasan`/`-lubsan` missing), RenderDoc,
GPU-assisted and synchronization validation.
