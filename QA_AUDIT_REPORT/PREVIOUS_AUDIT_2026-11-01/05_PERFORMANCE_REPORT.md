# 05 — Performance Report

## Methodology

- Real Vulkan window on RTX 5050 (driver 617.42), Release build.
- `example/qa_stress.c`: N `DrawModel` calls per frame, one cube model, 1000
  objects arranged in a grid, no shadows, TAA/FXAA off, post enabled.
- CPU time: `FrameStats.cpuFrameMs`.
- GPU time: Vulkan timestamp queries (`FrameStats.gpuFrameMs`), where available.
- Warmup: first 2 frames excluded from averages.
- Each run uses `SetTargetFPS(0)`; numbers are throughput measurements, not
  vsync-locked FPS.

Raw data: `BENCHMARKS/summary.csv`, per-frame CSV files in the same directory.

## Object-count scaling (640x360)

| Objects | Frames | Avg CPU ms | Min CPU | Max CPU | Draws | Triangles | VRAM estimate |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1,000 | 60 | 5.58 | 4.13 | 21.33 | 1,001 | 12,786 | 5.33 MB |
| 10,000 | 20 | 37.05 | 35.33 | 42.70 | 5,687 | 69,018 | 5.33 MB |
| 100,000 | 3 | 339.73 | 339.73 | 339.73 | 30,525 | 367,074 | 5.33 MB |

Derived throughput (1 / avg CPU): ~179 FPS, ~27 FPS, ~2.9 FPS. These are CPU
submission/record times, not presented FPS.

## Resolution scaling (1,000 objects)

| Resolution | Avg CPU ms | Avg GPU ms | Draws |
|---|---:|---:|---:|
| 640x360 | 5.58 | 0.403 | 1,001 |
| 1280x720 | 6.29 | 0.623 | 1,001 |
| 1920x1080 | 9.08 | 0.561 | 1,001 |

GPU timings are timestamp-query totals for the frame (shadow+scene+post) and are
sub-millisecond for this simple scene. CPU cost grows with resolution because of
post/UI and command recording.

## Main PBR scene (api_smoke, 800x600)

```
draws=3 tris=170000 culled=1 cpu=0.936ms gpu=0.246ms vram=113.1MB
gpu pass ms: shadow=0.049 scene=0.122 post=0.075
```

The 170k triangles and 113 MB VRAM come from the DamagedHelmet glTF + floor +
shadow/IBL/offscreen targets.

## Long run

10 objects, 1000 frames:

```
avg_cpu_ms=7.1825 min=0.1160 max=51.0719 draws=11 tris=258
```

Large min/max spread indicates scheduling/window/present jitter rather than a
stable frame-pacing implementation. No per-frame pacing histogram was captured.

## Observations

- Draw submission is immediate-mode; 100k objects is CPU-bound (~340 ms).
- AABB frustum culling reduced 100k objects to 30,525 draws.
- No instancing/batching path was found.
- GPU frame time is sub-millisecond at 1k objects, so the tested scene is
  CPU/driver-submission bound, not GPU-bound.
- `estimatedGpuBytes` reports 5.33 MB for the cube stress scene and 113.1 MB for
  the PBR smoke scene; it is an estimate, not an allocator measurement.

## Not measured

- Frame pacing percentiles under vsync.
- Pipeline switch count / descriptor update count (no counters exposed).
- Multithreaded scalability (no MT recording).
- Memory allocator overhead (dedicated allocations only).
- GPU time with validation disabled vs enabled (validation runs are slower by design).
