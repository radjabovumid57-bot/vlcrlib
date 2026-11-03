# SDL3 Integration Audit

## What is verified

| Area | Status | Evidence |
|---|---|---|
| SDL initialization | PASS | `InitWindow` calls `SDL_Init(VIDEO|EVENTS|GAMEPAD)` with error logging |
| Window creation | PASS | `SDL_CreateWindow`; Vulkan flag on desktop, resizable on Web/CPU |
| Drawable size | PASS | `SDL_GetWindowSizeInPixels` preferred, logical size fallback; logs mismatch |
| Window resize | PASS | `SDL_EVENT_WINDOW_RESIZED` and `PIXEL_SIZE_CHANGED`; Vulkan resize smoke 0 errors |
| Event polling | PASS (code), NOT TESTED (input automation) | `SDL_PollEvent` handles quit/key/mouse/text; no automated input test |
| Input API | NOT TESTED | no headless input injection; manual/example only |
| Fullscreen | NOT TESTED | no fullscreen test executed |
| Minimize/restore | NOT TESTED | not exercised |
| Android integration | BUILD PASS | Gradle + NDK 26.1, SDL3 built from source, APK arm64/x86_64 |
| Emscripten integration | BUILD/RUNTIME PASS | SDL3 port built; WebGPU page 150 frames |
| macOS/iOS SDL3 | NOT TESTED | no Apple host/SDK |

## Platform-specific issue found

Headless Chrome opens the WebGPU canvas at **942x388** for a requested 960x540
window; the engine correctly logs the actual size. The cross-backend harness now
forces a CDP 960x540 viewport to avoid false aspect/FOV differences. This is a
Web/SDL3 canvas/headless behavior, documented rather than hidden.

## Not tested

- Keyboard/mouse/touch input assertions.
- Fullscreen transitions.
- Minimize/restore.
- Android touch/input runtime.
- iOS safe-area/orientation.

## Verdict

SDL3 usage is coherent and portable in the tested Windows/Web/Android build
paths. Input/fullscreen behavior is **NOT TESTED** and must not be called
verified.
