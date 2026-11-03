# API audit — 2026-10-26

Scope: public headers under `src/`, the `api_smoke` GPU smoke, the C++ header
compile contract, unit tests, validation-enabled runs, and the skinned+TAA
golden test.

## Test matrix

| Check | Command | Result |
|---|---|---|
| Unit tests | `ctest --test-dir build-check -C Release` | 10/10 |
| C++17 header | `test_cpp_header` | pass |
| Null/empty contracts | `test_api_contracts` | 44/44 |
| GPU smoke | `api_smoke` + `VLCRLIB_VALIDATION=1` | 0 validation errors |
| Resize smoke | `VLCR_RESIZE=1` | 0 validation errors, offscreen targets reallocated |
| Shadow resize | `VLCR_SHADOW_RESIZE=1` | 0 validation errors |
| Golden | `--target golden_skinned` | `vdiff` OK |
| Strict warning build | `VLCRLIB_STRICT=ON` | pass |
| Android APK | `gradle :app:assembleDebug` (NDK 26.1) | `app-debug.apk` with arm64 libs |
| CPU rasterizer | `test_cpu_raster` (models/textures/clip/hash) | pass / window runs |
| CPU 2D transparent | `cpu_2d_example` with `VRenderBackendSelect(CPU)` | InitWindow/DrawText/DrawRectangle work |
| CPU 3D transparent | `cpu_3d_example` | GenMeshCube + DrawModel + text work |
| WebGPU build | Emscripten 3.1.58, `cmake --build build-web` | 6 HTML/wasm examples link; offscreen scene + composite |
| CPU WASM | `cmake --build build-web --target vcpu_wasm` | standalone wasm; 4-bone skinning + 2-cascade CSM render in Chrome |

## Fixed during audit

1. `LoadModel(NULL)` dereferenced path (`strrchr`). Added null/device guard.
2. `SceneLoadModels(scene, NULL, runtime)` dereferenced `scenePath` in
   `joinPath`. Added guard.
3. `SceneSave()` wrote Windows paths unescaped; `assets\model.glb` reloaded as
   `assetsmodel.glb`. Added JSON escaping + round-trip test.
4. Named color macros used C99 compound literals and did not compile as C++17.
   Switched to `VLCRLIB_COMPOUND_LITERAL`; `VLCRLIB_DEFAULT_GRAVITY` too.
5. Calling `InitGraphicsDevice()` twice memset live Vulkan handles (leak/UB).
   It is now idempotent while a device is live.
6. Changing shadow/spot/point map size never recreated resources because the
   setter compared the new size against the field it had just overwritten.
   Fixed; resource recreation also waits for the device.
7. `vgraphics_CreateModelFromMeshesEx()` dereferenced null `meshes`/`materials`
   and allowed out-of-range `meshMaterials` indices. It now rejects null
   arrays and clamps material indices.
8. `registerMesh()` dereferenced null vertices/indices and lacked an
   initialized-device guard.
9. Texture, KTX2, render-target, material, model and skinned-model creation
   touched Vulkan state before `InitGraphicsDevice`. They now fail closed with
   a zero handle.
10. Skinned velocity descriptor sets were never freed; reloading models could
    exhaust `velocitySkinPool`. They are freed on unload.
11. Velocity casters were recorded only when shadows/SSAO were on, so TAA
    velocity was empty with shadows and SSAO disabled. TAA now also records
    casters.
12. `MatrixDecomposeTRS()` turned negative scale into a rotation. It now folds
    the mirror into negative X scale.
13. `QuaternionSlerp()` could call `acosf` with `d > 1`; clamped.
14. `MatrixLookAt` unit test had `|| 1`, so it always passed. Now checks the
    actual view translation and basis.
15. `LoadTextureFromImageEx` docs claimed NULL options meant REPEAT; the shared
    fallback sampler clamps. Docs corrected.
16. `vfiles` now fails closed for NULL paths/data and handles `ftell` failures;
    POSIX/Windows directory checks keep their platform split.
17. `vaudio` includes the POSIX `unistd.h`/`limits.h` needed for `access` and
    `realpath`; networking ignores `SIGPIPE` on POSIX.
18. macOS MoltenVK portability instance/device extensions are enabled, and
    platform audio frameworks/dl/m are linked by CMake.
19. `GetFileSize` collided with Win32's `GetFileSize` whenever a consumer
    included `<windows.h>` after `vfiles.h`. The canonical name is now
    `GetFileSizeBytes`; POSIX keeps a `GetFileSize` source-compat macro.
20. File reads/writes use SDL_IOStream, so Android `assets/`/content URIs and
    the Emscripten virtual filesystem work. glTF/GLB, fonts and shader
    binaries go through this path.
21. tinygltf now receives `FsCallbacks` backed by `vfiles`, so external
    `.bin`/image resources use SDL's mobile/web filesystem too.

## Remaining issues

### Resize — fixed in this pass

`vgraphics_internal_recreate()` now reallocates scene color/depth, TAA history,
velocity, bloom, SSAO and their framebuffers, then refreshes the bloom/SSAO/
composite/PBR descriptors. The scene pass uses the offscreen extent for its
render area, viewport and projection aspect (`vgraphics_GetRenderTargetSize()`).
`VLCR_RESIZE=1` reports 0 validation errors and the previous floor/sky seam is
gone.

### Medium — `LoadShader` is a stub

`vgraphics.h` exposes `LoadShader`, but the implementation always returns
`{0}`. It is now documented as not implemented. Either implement it or remove
it from the supported API surface.

### Medium — scene hierarchy visibility

`SceneDraw()` checks each node's `visible` flag independently. A hidden parent
does not hide its visible children. Decide the contract and either propagate
visibility or document per-node independence.

### Medium — missing models are silent

`SceneLoadModels()` returns `true` even when every model path fails; it only
records `runtime->loaded[i] = 0`. Callers cannot distinguish "loaded empty
scene" from "all models missing". Return `false`/a count when any load fails.

### Medium — convolution reverb scope

The FIR node is global, capped at 4096 taps, and only routes sounds created
after `LoadReverbIR()`. It is true convolution, but not per-zone and not
partitioned/FFT. Documented in the audio API; a per-zone send and partitioned
FFT are the next step.

### Medium — audio node mutation thread safety

`SetSound3DOcclusion()` calls `ma_lpf_node_reinit()` and `SetSound3DReverb()`
mutates delay nodes from the caller thread while the audio thread is processing.
The API does not document thread affinity. Either marshal these changes through
the engine or document that all setters must be called from the audio/main
thread consistently.

### Low — `SceneLoad` control-character escapes

`SceneSave()` writes `\u00XX` for control bytes, but `parseString()` does not
decode `\u`. Paths with control characters do not round-trip; normal paths do.

### Low — `MatrixLookAt` degenerate eye == target

Returns a degenerate matrix instead of an identity/error. Not hit by the scene
or camera paths, but a valid public-math edge case.

### Low — `vrgAddImage` descriptor validation

Zero extent/undefined format descriptors are accepted at planning time and
only fail when Vulkan allocation is attempted. Add validation for early,
actionable errors.

## Recommended next actions

1. Implement an offscreen-target recreate path keyed by swapchain extent.
2. Remove or implement `LoadShader`.
3. Define and test parent-visibility semantics in `SceneDraw`.
4. Return an explicit failure count from `SceneLoadModels`.
5. Add a per-zone convolution send and partitioned FFT if the global FIR tail
   is not sufficient.
