/*
 * mobile_gltf.c — mobile glTF viewer (Android APK / iOS).
 *
 * This is the entry point built into libmain.so by android/app/src/main/cpp.
 * The model lives inside the APK at assets/DamagedHelmet.glb. SDL resolves
 * that relative name through the Android AssetManager, so the file is copied
 * once to writable storage and then loaded through the regular glTF path.
 * The same source runs on desktop (model next to the executable), which makes
 * the viewer testable without installing an APK.
 *
 * Touch: one finger orbits, two fingers pinch to zoom.
 */
#include <vlcrlib.h>
#include <vgltf.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MODEL_NAME "DamagedHelmet.glb"
#define CACHE_LIMIT ((Sint64)256 * 1024 * 1024)

static bool copy_asset(const char *name, const char *dst) {
    SDL_IOStream *in = SDL_IOFromFile(name, "rb");
    if (!in) return false;

    Sint64 size = SDL_GetIOSize(in);
    if (size <= 0 || size > CACHE_LIMIT) {
        SDL_CloseIO(in);
        return false;
    }
    void *data = SDL_malloc((size_t)size);
    if (!data) {
        SDL_CloseIO(in);
        return false;
    }
    bool readOk = SDL_ReadIO(in, data, (size_t)size) == (size_t)size;
    SDL_CloseIO(in);
    if (!readOk) {
        SDL_free(data);
        return false;
    }

    FILE *out = fopen(dst, "wb");
    if (!out) {
        SDL_free(data);
        SDL_Log("[mobile_gltf] cannot write %s", dst);
        return false;
    }
    size_t written = fwrite(data, 1, (size_t)size, out);
    fclose(out);
    SDL_free(data);

    SDL_Log("[mobile_gltf] extracted %s -> %s (%lld bytes)",
            name, dst, (long long)size);
    return written == (size_t)size;
}

static Model try_load(const char *path, char *resolved, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return (Model){0};
    fclose(f);

    Model m = LoadModelGLTFTiny(path);
    if (m.id) {
        snprintf(resolved, cap, "%s", path);
        SDL_Log("[mobile_gltf] loaded %s (model id=%u)", path, m.id);
    }
    return m;
}

static Model load_model(char *resolved, size_t cap) {
    static const char *localCandidates[] = {
        MODEL_NAME, "assets/" MODEL_NAME, "example/" MODEL_NAME, NULL
    };

    char cached[1024] = {0};
    char *pref = SDL_GetPrefPath("vlcrlib", "mobile_gltf");
    if (pref) {
        snprintf(cached, sizeof(cached), "%s" MODEL_NAME, pref);
        SDL_free(pref);
    }

    if (cached[0]) {
        Model m = try_load(cached, resolved, cap);
        if (m.id) return m;
    }

    for (int i = 0; localCandidates[i]; ++i) {
        Model m = try_load(localCandidates[i], resolved, cap);
        if (m.id) return m;
    }

    /* Android: APK assets are not regular files, copy them out first. */
    if (cached[0] && copy_asset(MODEL_NAME, cached)) {
        Model m = try_load(cached, resolved, cap);
        if (m.id) return m;
    }
    if (cached[0] && copy_asset("assets/" MODEL_NAME, cached)) {
        Model m = try_load(cached, resolved, cap);
        if (m.id) return m;
    }

    resolved[0] = '\0';
    SDL_Log("[mobile_gltf] model not found — pack %s into the APK assets",
            MODEL_NAME);
    return (Model){0};
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (!InitWindow(1280, 720, "vlcrlib glTF")) {
        const char *err = SDL_GetError();
        SDL_Log("[mobile_gltf] InitWindow failed: %s", err);
        /* On a phone the window never appears if Vulkan init fails, so show
           the reason instead of silently exiting to a black screen. */
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "vlcrlib init failed",
                                 (err && err[0]) ? err : "Vulkan initialization failed",
                                 NULL);
        return 1;
    }
    SetTargetFPS(60);

    vgraphics_EnableStudioLighting();
    vgraphics_SetIBLIntensity(vgraphics_IsIBLLoaded() ? 1.0f : 0.0f);

    char path[1024] = {0};
    Model model = load_model(path, sizeof(path));

    VInputState input;
    VInputInit(&input);

    const Vector3 target = { 0.0f, 1.0f, 0.0f };
    float yaw = 0.6f;
    float pitch = 0.35f;
    float dist = 4.6f;

    int prevTouches = 0;
    Vector2 prevTouch = { 0.0f, 0.0f };
    float prevPinch = 0.0f;
    double lastTouchTime = -1e9;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        VInputUpdate(&input, dt);

        int touches = VInputTouchCount(&input);
        if (touches > 0) {
            Vector2 a = VInputTouchPosition(&input, 0);
            if (prevTouches == 1 && touches == 1) {
                yaw   -= (a.x - prevTouch.x) * 0.006f;
                pitch += (a.y - prevTouch.y) * 0.004f;
            }
            if (touches >= 2) {
                Vector2 b = VInputTouchPosition(&input, 1);
                float dx = a.x - b.x;
                float dy = a.y - b.y;
                float pinch = sqrtf(dx * dx + dy * dy);
                if (prevPinch > 1.0f && pinch > 1.0f)
                    dist *= prevPinch / pinch;
                prevPinch = pinch;
            } else {
                prevPinch = 0.0f;
            }
            prevTouch = a;
            lastTouchTime = GetTime();
        } else {
            prevPinch = 0.0f;
        }
        prevTouches = touches;

        if (pitch < -0.1f) pitch = -0.1f;
        if (pitch > 1.15f) pitch = 1.15f;
        if (dist < 2.0f) dist = 2.0f;
        if (dist > 12.0f) dist = 12.0f;

        /* Idle auto-orbit resumes shortly after the last touch. */
        if (GetTime() - lastTouchTime > 2.0) yaw += dt * 0.4f;

        float cp = cosf(pitch);
        Camera3D cam = {0};
        cam.position = (Vector3){
            target.x + dist * cp * sinf(yaw),
            target.y + dist * sinf(pitch),
            target.z + dist * cp * cosf(yaw)
        };
        cam.target = target;
        cam.up = (Vector3){ 0.0f, 1.0f, 0.0f };
        cam.fovy = 45.0f;
        cam.nearPlane = 0.05f;
        cam.farPlane = 100.0f;

        BeginDrawing();
        ClearBackground((Color){ 24, 28, 38, 255 });

        if (model.id) {
            BeginMode3D(cam);
            DrawGrid(10, 1.0f);
            DrawModel(model, target, (Vector3){ 0.0f, 0.0f, 0.0f },
                      (Vector3){ 1.15f, 1.15f, 1.15f }, WHITE);
            EndMode3D();
        }

        int sw = 0, sh = 0;
        vcore_GetScreenSize(&sw, &sh);

        char hud[64];
        snprintf(hud, sizeof(hud), "FPS %d", GetFPS());
        DrawText("vlcrlib glTF", 24, 18, 26, WHITE);
        DrawText(model.id ? path : "model not loaded", 24, 52, 14, LIGHTGRAY);
        DrawText(hud, sw - MeasureText(hud, 16) - 24, 20, 16, WHITE);
        DrawText("drag: orbit    pinch: zoom", 24, sh - 34, 14, LIGHTGRAY);

        if (!model.id)
            DrawText("Pack DamagedHelmet.glb into android/app/src/main/assets/",
                     24, 100, 16, RED);

        EndDrawing();
    }

    if (model.id) UnloadModel(model);
    CloseWindow();
    return 0;
}
