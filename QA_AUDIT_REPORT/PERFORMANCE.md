# Performance Audit

## Method

- Windows x64 MSVC Release, C17, NVIDIA RTX 5050 / driver 617.42.
- `example/qa_stress.c`: N `DrawModel` cubes/frame, 640x360, no shadows,
  TAA/FXAA off, `SetTargetFPS(0)`.
- CPU time from `FrameStats.cpuFrameMs`; GPU time from Vulkan timestamp queries.
- Per-frame data: `BENCHMARKS/windows_stress_*.csv`.

## Results (actual)

| Objects | Frames | Avg CPU ms | Min CPU | Max CPU | Avg GPU ms | Draws | Tris | VRAM est. |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1,000 | 60 | 9.81 | 3.98 | 49.43 | 0.355 | 1,001 | 12,786 | 5.33 MB |
| 10,000 | 10 | 42.81 | 36.87 | 50.04 | 3.147 | 5,687 | 69,018 | 5.33 MB |
| 100,000 | 2 | 390.25 | 373.62 | 406.89 | n/a (no timestamp sample) | 30,525 | 367,074 | 5.33 MB |

Derived CPU throughput: ~102 FPS, ~23 FPS, ~2.6 FPS. These are submission/record
throughput, not vsync-locked presented FPS.

## Main PBR scene

```
draws=3 tris=170000 culled=1 cpu=1.075ms gpu=0.240ms tex=7 mesh=2 vram=113.1MB
gpu pass ms: shadow=0.050 scene=0.121 post=0.069
```

The 113.1 MB is `FrameStats.estimatedGpuBytes` for the DamagedHelmet scene,
shadow/IBL/offscreen targets; it is an estimate, not allocator accounting.

## Interpretation

- 1k–10k objects are CPU-submission bound; 100k is ~330 ms/frame.
- AABB culling reduced 100k objects to 30,525 draws.
- GPU time is sub-millisecond for the 1k cube scene; 10k shows ~3.3 ms.
- No pipeline-switch, descriptor-update, allocation-overhead or RAM counters are
  exposed, so those numbers are **NOT MEASURED**.
- 100k GPU timestamp did not resolve (`gpuFrameMs=-1`), reported as unavailable,
  not 0 ms.

## Platform performance

- Windows Vulkan: measured above.
- Web WebGPU: runtime 150 frames/0 errors, but headless timestamp map callback
  does not resolve (`gpu=n/a`), so no trustworthy GPU time.
- Android: build-only, no runtime emulator/device; **NOT MEASURED**.
- Linux/macOS/iOS: **NOT TESTED**.

## Missing metrics

FPS/frame pacing under vsync, RAM usage, VRAM allocator usage, pipeline switches,
descriptor updates, synchronization stalls, upload time, multithreaded scaling.
No fabricated values are provided.
