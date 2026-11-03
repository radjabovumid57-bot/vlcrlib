# 06 — Stress & Stability Report

## Executed

| Test | Setup | Result |
|---|---|---|
| 1,000 objects | 60 frames, 640x360 | completed, no crash; 10-frame validation run = 0 |
| 10,000 objects | 20 frames | completed, no crash |
| 100,000 objects | 3 frames | completed, no crash/hang |
| Long run | 10 objects, 1000 frames | completed, no crash/hang |
| Repeated create/destroy | `api_smoke` 5 consecutive processes with validation | exits `0,0,0,0,0` |
| Rapid resize | `VLCR_RESIZE=1` at frame 20 | pass, 0 validation errors |
| Shadow resize | `VLCR_SHADOW_RESIZE=1` at frame 20 | pass, 0 validation errors |
| Empty stress frame | no draws, no text, no post, no SSAO | **validation=0** |
| Full-draw validation stress | 1000 objects, 10 frames | **validation=0** after QA-001 fix |

Raw per-frame data: `BENCHMARKS/stress_*.csv`.

## Findings

### QA-001 — P1: image sampled in UNDEFINED layout (FIXED)

Reproduced with `qa_stress` under `VK_LAYER_KHRONOS_validation`. Debug names
(`ssao_depth`, `ssao_color`, `velocity_color`) identified two resources:

- SSAO depth created `UNDEFINED`, sampled by the first prepass, then left in
  `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` while the descriptor declared read-only;
- velocity color created `UNDEFINED` but bound in the composite/TAA descriptor.

Fixes: depth/color initial transitions, SSAO white clear with `TRANSFER_DST`,
SSAO render-pass final layout `SHADER_READ_ONLY_OPTIMAL`, velocity sampled-layout
init, and the broken SSAO graph path disabled in favor of the explicit
prepass/SSAO pair.

Final validation:

```
qa_stress 1 object, empty: validation=0
qa_stress 1000 objects:    validation=0
api_smoke 90 frames:       validation=0
```

### Limits / not tested

- 100,000 objects completed but at ~340 ms CPU/frame; not a playable workload.
- Object count is ultimately limited by per-frame CPU cost and immediate-mode
  submission, not by an explicit API cap in the audited Vulkan path.
- GPU resource exhaustion was not injected.
- Device loss was not simulated; no recovery path exists in code (QA-008).
- Multithreaded stress was not possible: recording is single-threaded.
- Minimize/restore was not exercised; resize was.

## Stress verdict

No crashes, deadlocks, or hangs were observed in 1k/10k/100k runs, 1000-frame
run, repeated process create/destroy, or resize. Empty and 1000-object paths are
validation-clean after QA-001; device-loss/OOM and multithreaded stress remain
untested.
