/*
 * example_fastload.c — FastLoadGltf viewer (fixes black screen)
 *
 * Common black-screen causes:
 *  1) DamagedHelmet.glb not next to exe
 *  2) model.id == 0 (load failed)
 *  3) no lights / black clear + no UI text
 *  4) missing shaders/*.spv
 *
 * Usage:
 *   example_fastload.exe
 *   example_fastload.exe path\to\model.glb
 */
#include <vlcrlib.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include "vfastgltf.h"
#include "vgltf.h"

static void flog(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fflush(stderr);
}

static int file_exists(const char *path) {
    if (!path || !path[0]) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static Model try_load(const char *argv1, char *out, size_t outSz) {
    char buf[12][1024];
    const char *list[20];
    int n = 0;

    if (argv1 && argv1[0])
        list[n++] = argv1;

    const char *base = SDL_GetBasePath();
    if (base) {
        snprintf(buf[0], 1024, "%sDamagedHelmet.glb", base);
        snprintf(buf[1], 1024, "%sexample/DamagedHelmet.glb", base);
        snprintf(buf[2], 1024, "%sexample\\DamagedHelmet.glb", base);
        snprintf(buf[3], 1024, "%s../example/DamagedHelmet.glb", base);
        snprintf(buf[4], 1024, "%s..\\example\\DamagedHelmet.glb", base);
        snprintf(buf[5], 1024, "%s../../example/DamagedHelmet.glb", base);
        for (int i = 0; i < 6; ++i) list[n++] = buf[i];
    }
    list[n++] = "DamagedHelmet.glb";
    list[n++] = "example/DamagedHelmet.glb";
    list[n++] = "example\\DamagedHelmet.glb";

    for (int i = 0; i < n; ++i) {
        flog("[fastload] try: %s\n", list[i]);
        if (!file_exists(list[i])) {
            flog("[fastload]   not found\n");
            continue;
        }
        flog("[fastload]   loading via FastLoadGltf...\n");
        Model m = FastLoadGltf(list[i]);
        if (!m.id) {
            flog("[fastload]   FastLoadGltf fallback to LoadModelGLTFTiny...\n");
            m = LoadModelGLTFTiny(list[i]);
        }
        flog("[fastload]   id=%u\n", m.id);
        if (m.id) {
            if (out && outSz) snprintf(out, outSz, "%s", list[i]);
            return m;
        }
        flog("[fastload]   load failed\n");
    }
    if (out && outSz) out[0] = 0;
    return (Model){0};
}

int main(int argc, char **argv) {
    flog("[fastload] start\n");

    if (!InitWindow(1280, 720, "vlcrlib - FastLoadGltf")) {
        flog("[fastload] InitWindow FAILED: %s\n", SDL_GetError());
        fprintf(stderr, "Press Enter...\n");
        getchar();
        return 1;
    }
    SetTargetFPS(60);
    flog("[fastload] window ok\n");

    char path[1024] = {0};
    Model model = try_load(argc > 1 ? argv[1] : NULL, path, sizeof(path));
    flog("[fastload] model id=%u path=%s\n", model.id, path[0] ? path : "(none)");

    /* Visible background + strong lights (black clear hides failures) */
    vgraphics_SetDirectionalLight(true,
        (Vector3){ -0.4f, -0.85f, -0.35f },
        (Color){ 255, 250, 240, 255 }, 2.6f);
    vgraphics_SetAmbientLight(true, (Color){ 90, 100, 120, 255 }, 0.40f);
    /* IBL optional — can fail on some GPUs; intensity 0 = procedural in shader */
    if (vgraphics_IsIBLLoaded())
        vgraphics_SetIBLIntensity(1.0f);
    else
        vgraphics_SetIBLIntensity(0.0f);
    /* Shadows off by default (need shadow.vert.spv); enable if ready */
    /* Shadows off by default */

    flog("[fastload] lights ok (ibl=%d)\n", vgraphics_IsIBLLoaded() ? 1 : 0);

    Camera3D cam = {0};
    cam.position = (Vector3){ 0.0f, 1.4f, 4.5f };
    cam.target   = (Vector3){ 0.0f, 1.0f, 0.0f };
    cam.up       = (Vector3){ 0.0f, 1.0f, 0.0f };
    cam.fovy     = 45.0f;
    cam.nearPlane = 0.05f;
    cam.farPlane  = 100.0f;

    float t0 = (float)GetTime();
    int frame = 0;

    while (!WindowShouldClose()) {
        float t = (float)GetTime() - t0;
        float o = t * 0.35f;
        cam.position.x = sinf(o) * 4.5f;
        cam.position.z = cosf(o) * 4.5f;
        cam.position.y = 1.35f;

        BeginDrawing();
        /* Not pure black — if you only see this gray, 3D failed but 2D works */
        ClearBackground((Color){ 36, 40, 48, 255 });

        if (model.id) {
            BeginMode3D(cam);
            DrawGrid(10, 1.0f);
            DrawModel(model,
                      (Vector3){ 0.0f, 1.0f, 0.0f },
                      (Vector3){ 0.0f, 0.0f, 0.0f },
                      (Vector3){ 1.15f, 1.15f, 1.15f },
                      WHITE);
            EndMode3D();
            DrawText("FastLoadGltf OK", 20, 16, 28, WHITE);
            DrawText(path, 20, 50, 14, LIGHTGRAY);
        } else {
            DrawText("MODEL NOT LOADED", 20, 20, 32, RED);
            DrawText("Put DamagedHelmet.glb next to this .exe", 20, 70, 20, WHITE);
            DrawText("Or: example_fastload.exe path\\to\\model.glb", 20, 100, 18, LIGHTGRAY);
            DrawText("Check console for [fastload] try: ...", 20, 130, 18, YELLOW);
        }

        EndDrawing();

        if (frame == 0)
            flog("[fastload] first frame model=%u\n", model.id);
        frame++;
    }

    if (model.id) UnloadModel(model);
    CloseWindow();
    flog("[fastload] exit\n");
    return model.id ? 0 : 1;
}
