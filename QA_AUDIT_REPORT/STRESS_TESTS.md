# Stress & Regression Tests

## Executed

| Test | Setup | Result |
|---|---|---|
| 1,000 objects | 60 frames, 640x360, C17 | no crash; `validation=0` in 10-frame validation run |
| 10,000 objects | 10 frames | no crash; `validation=0` |
| 100,000 objects | 2 frames | no crash; 390 ms CPU/frame; `validation=0` |
| Empty frame | no draws/text/post/SSAO | `validation=0` |
| PBR/glTF/shadow smoke | 90 frames, 800x600 | fails=0, validation=0, resize=0 |
| Resize + shadow resize | frame 20 | validation=0 |
| Repeated create/destroy | 5 separate `api_smoke` processes | exits `0,0,0,0,0` |
| Android arm64 build | Gradle Debug | BUILD SUCCESSFUL |
| Android x86_64 build | temporary abiFilters | BUILD SUCCESSFUL, APK x86_64 |
| Android runtime | emulator | **NOT TESTED** — hypervisor driver absent |
| WebGPU 150-frame runtime | headless Chrome | 0 errors |

## Evidence

- `TEST_RESULTS/windows_ctest_release.log` — 11/11.
- `TEST_RESULTS/windows_ctest_debug.log` — Debug ctest 10/11, missing Debug CRT.
- `TEST_RESULTS/cpu_only_ctest.log` — 9/9.
- `TEST_RESULTS/windows_vulkan_validation_smoke.log` — 0 validation errors.
- `TEST_RESULTS/android_arm64_build.log` / `android_x86_64_build.log`.
- `BENCHMARKS/windows_stress_*.csv`.

## Not tested

- Device loss/OOM injection.
- GPU resource exhaustion.
- Multithreaded stress (no MT recording API).
- Minimize/restore, fullscreen.
- Android/iOS/Linux/macOS runtime stress.
- 10k-frame/24h soak (only 1000-frame long run from the prior audit, not rerun
  after C17 changes).
