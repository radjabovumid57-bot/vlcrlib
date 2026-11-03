# Benchmark Artifacts

`summary.csv` aggregates real runs from `example/qa_stress.c` on:

- NVIDIA RTX 5050, driver 617.42, Vulkan 1.4.351;
- Windows, Release build;
- 640x360 window unless noted;
- first 2 frames excluded from averages;
- CPU time from `FrameStats.cpuFrameMs`, GPU from Vulkan timestamp queries.

Per-frame files:

- `stress_1k.csv` — 1000 objects, 60 frames, 640x360
- `stress_10k.csv` — 10000 objects, 20 frames, 640x360
- `stress_100k.csv` — 100000 objects, 3 frames, 640x360
- `stress_long_1000frames.csv` — 10 objects, 1000 frames

These are throughput measurements with `SetTargetFPS(0)`, not vsync-locked FPS.
The 100k run is CPU-submission-bound and not a playable workload.
