# WebGPU runtime validation

`example/web_validate_example.c` is a headless-friendly WebGPU smoke test. It
draws a ground plane, two props, DamagedHelmet and one animated skinned
instance (exercising the per-draw UBO ring, vertex arena and skinned pipelines),
enables a 4-cascade frustum-fit shadow atlas, four spot shadow maps, and four
point-light cube shadow maps (2×2 viewports + comparison samplers), loads procedural
IBL (exercising cubemap/BRDF upload), loads a 4×4 BC5 KTX2 fixture and samples it as AO and normal maps on validation
quads, attempts 4×4 ETC2 RGB8 and ASTC 4x4 fixtures (skipped when the adapter
lacks `texture-compression-etc2`/`-astc`), records
timestamp-query GPU time, and runs 150 frames while
toggling FXAA, TAA, bloom, SSAO, shadows and IBL. When the headless map callback
never resolves (confirmed with both virtual-time and real-time CDP capture),
`FrameStats.gpuFrameMs` is negative and the report prints `gpu=n/a` instead of
a misleading zero. It then prints:

```text
[webgpu-validation] frames=150 errors=0 backend=WebGPU draws=10 shadows=256 tris=15926 gpu=n/a shot=1
```

and sets the page title to `webgpu-validation errors=N`.

## Build

The target is built with `-sSINGLE_FILE=1`, so the generated
`web_validate_example.html` embeds the JS and wasm runtime. Endpoint security
cannot lock a separate `.js` artifact; only the Emscripten preload `.data` file
is served beside the HTML.

```powershell
# from an emsdk environment
emcmake cmake -S . -B build-web -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DVLCRLIB_SKIP_VCPKG=ON -DVLCRLIB_BUILD_TOOLS=OFF
cmake --build build-web --target web_validate_example
```

## Run

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File tools/web_validate.ps1 -BuildDir build-web
```

The script:

1. starts `web/serve_web.py` on port 8103;
2. launches headless Chrome with `--enable-unsafe-webgpu
   --use-angle=d3d11`;
3. waits up to 40 s of virtual time;
4. reads the `[webgpu-validation]` line from Chrome's log;
5. writes `docs/webgpu_validation.png` and fails if `errors != 0` or no frames
   were rendered.

On Windows, `--use-angle=d3d11` is required for Chrome's WebGPU/D3D11 interop;
the Vulkan/SwiftShader combinations yielded no adapter. The script's `-Chrome`
parameter accepts Edge or a Chromium build.

## Local result on this host (2026-10-29)

**Passed.** Chrome 156 headless, `--use-angle=d3d11`:

```text
[webgpu-validation] frames=150 errors=0 backend=WebGPU draws=10 shadows=256 tris=15926 gpu=n/a shot=1
```

The run covers five phases: FXAA on/off, TAA enabled at frame 30 and kept on,
bloom on/off, SSAO on/off,
shadows on/off and IBL on/off/restored. Screenshot: `docs/webgpu_validation.png`
(textured helmet, props, one skinned instance, grid, shadows, TAA and IBL;
HUD shows `fxaa=1 taa=1 shadow=1 bloom=0 ssao=0 spot=4 point=4 skin=1
ktx=1 bc5n=1 ibl=0.50`). Real runtime bugs found and fixed include:

1. `requestDevice` timed out at 500 ms; first D3D11 device creation needs more
   time. The wait is now 5 s.
2. Both color attachments had `depthSlice=0`; non-3D attachments require
   `WGPU_DEPTH_SLICE_UNDEFINED`.
3. The 2D pipeline was used inside the depth-enabled scene pass but had no
   `Depth24Plus` depth state; `g_p2` now matches the scene attachment state.
4. TAA’s WGSL declared `wp` as `let` and reassigned it; changed to `var`.
5. TAA sampled history/scene inside non-uniform control flow; switched to
   `textureSampleLevel(..., 0.0)`.

## API screenshot

The validation page also calls `vgraphics_CaptureScreenshot("/webgpu_screenshot.png")`
on its final frame; the report includes `shot=1`. The engine copies the final
composite texture (after FXAA/TAA/bloom/SSAO) to a staging buffer, maps it
asynchronously, converts BGRA→RGBA and writes PNG. Set
`window.vlcrDownloadScreenshots = true` to trigger a browser download; it is off
by default so headless validation can exit normally.

## Golden image

The validation scene is deterministic (animation is driven by frame count, not
wall time). `docs/webgpu_golden.png` is the committed baseline:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File tools/webgpu_golden.ps1 -NoBuild
```

Result on this host:

```text
vdiff: ... size=762x428 mean=0.0000 max=0 over_threshold=0.0000% (threshold 2.00)
  RESULT: OK
WebGPU golden: OK
```

Refresh the baseline only on a known-good GPU with `-UpdateGolden`. This is a
WebGPU self-regression check. A separate `cross_backend_example` +
`tools/cross_backend_golden.ps1` harness renders the same scene on Vulkan and
WebGPU; the latest comparison is `mean=1.5561` against a `6.0` threshold
(max-over 2.90%), so cross-golden passes. The harness drives Chrome over CDP and
forces a 960x540 viewport, because headless Chrome otherwise opens the canvas at
942x388 and creates a false aspect/FOV difference. This requires Node 22+ for the
built-in WebSocket client (`tools/cdp_capture.mjs`).

## CI

`.github/workflows/webgpu-runtime.yml` builds the page with Emscripten 3.1.58,
serves it, runs headless Chrome with SwiftShader WebGPU, requires
`errors=0`, and uploads `chrome.log` + `docs/webgpu_validation.png`. It starts
as a manual (`workflow_dispatch`) job; enable push/PR triggers after the first
green run on the runner.

## Why this is not run on every developer machine

On the current Windows host the desktop sandbox blocks Chrome's mojo/crashpad
child processes, and endpoint security makes external `.js` artifacts
unreadable. The single-file HTML avoids the `.js` problem, but browser process
startup still needs a normal CI/desktop session. The same sandbox also blocks
Emscripten's link-time subprocess pipes, so `em++` can hang after generating the
`.data` file even though object compilation succeeds. Run this script on a CI
runner (or locally outside the constrained sandbox) and attach the
screenshot/log to the change.

The validation is intentionally a smoke test, not a golden-image comparison:
the error count proves that the multi-object UBO/vertex offsets and the WebGPU
pipeline are accepted by Dawn. Golden frames belong to the next parity stage.
