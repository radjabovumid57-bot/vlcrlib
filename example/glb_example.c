/*
 * glb_example.c — stable DamagedHelmet viewer
 * No IBL by default (can crash on some drivers). Multi-path GLB. Logs every step.
 */
#include <vlcrlib.h>
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "vgltf.h"

/*
 * NOTE: this helper is deliberately NOT named `logf`.
 * <math.h> declares `float logf(float)` as an MSVC intrinsic, and a
 * variadic `logf(const char *, ...)` shadows it. MSVC then refuses to emit
 * the intrinsic (C2169) and rejects every call site (C2167). Any name that
 * is not a standard libc identifier works: logmsg, elog, xlog, trace.
 */
static void logmsg(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fflush(stderr);
}

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static Model try_load_helmet(char *outPath, size_t outSz) {
    const char *base = SDL_GetBasePath();
    char bufs[10][1024];
    const char *cands[16];
    int n = 0;

    if (base) {
        snprintf(bufs[0], 1024, "%sDamagedHelmet.glb", base);
        snprintf(bufs[1], 1024, "%sexample\\DamagedHelmet.glb", base);
        snprintf(bufs[2], 1024, "%sexample/DamagedHelmet.glb", base);
        snprintf(bufs[3], 1024, "%s..\\example\\DamagedHelmet.glb", base);
        snprintf(bufs[4], 1024, "%s../example/DamagedHelmet.glb", base);
        for (int i = 0; i < 5; ++i) cands[n++] = bufs[i];
    }
    cands[n++] = "DamagedHelmet.glb";
    cands[n++] = "example\\DamagedHelmet.glb";
    cands[n++] = "example/DamagedHelmet.glb";

    for (int i = 0; i < n; ++i) {
        logmsg("[glb] try %s\n", cands[i]);
        if (!file_exists(cands[i])) {
            logmsg("[glb]   not found\n");
            continue;
        }
        logmsg("[glb]   file exists, loading...\n");
        Model m = LoadModelGLTFTiny(cands[i]);
        logmsg("[glb]   LoadModel returned id=%u\n", m.id);
        if (m.id) {
            if (outPath && outSz) snprintf(outPath, outSz, "%s", cands[i]);
            return m;
        }
    }
    if (outPath && outSz) outPath[0] = 0;
    return (Model){0};
}

int main(int argc, char **argv) {
    /* argc is used below (argv handling); do NOT cast it to void here. */
    logmsg("[glb] start\n");

    if (!InitWindow(1280, 720, "vlcrlib GLB — DamagedHelmet")) {
        logmsg("[glb] InitWindow FAILED\n");
        fprintf(stderr, "Press Enter...\n");
        getchar();
        return 1;
    }
    logmsg("[glb] window ok\n");
    SetTargetFPS(60);

    char path[1024] = {0};
    Model model = {0};
    if (argc > 1 && argv[1] && argv[1][0]) {
        logmsg("[glb] argv model: %s\n", argv[1]);
        model = LoadModelGLTFTiny(argv[1]);
        if (model.id) snprintf(path, sizeof(path), "%s", argv[1]);
    }
    if (!model.id)
        model = try_load_helmet(path, sizeof(path));

    logmsg("[glb] after load id=%u path=%s\n", model.id, path[0] ? path : "(none)");

    /*
     * Studio lighting.
     *
     * The old call `vgraphics_EnableStudioLighting()` does not exist in the
     * public API (see vgraphics.h) and was never defined anywhere in the
     * tree, so it linked-failed. Reproduce its intent by configuring the
     * directional and ambient lights directly.
     */
    vgraphics_SetDirectionalLight(true,
        (Vector3){ -0.40f, -0.85f, -0.35f },
        (Color){ 255, 250, 240, 255 }, 2.6f);
    vgraphics_SetAmbientLight(true,
        (Color){ 90, 100, 120, 255 }, 0.40f);

    /* Optional HDR: glb_example.exe model.glb studio.hdr */
    if (argc > 2 && argv[2] && argv[2][0]) {
        if (vgraphics_LoadIBL(argv[2]))
            logmsg("[glb] HDR IBL loaded: %s\n", argv[2]);
        else
            logmsg("[glb] HDR IBL failed, keeping procedural\n");
    }
    vgraphics_SetIBLIntensity(vgraphics_IsIBLLoaded() ? 1.0f : 0.0f);

    /* Shadows disabled by default (need shadow.vert.spv; can TDR some GPUs). */
    logmsg("[glb] lights+IBL ready (ibl=%d)\n", vgraphics_IsIBLLoaded() ? 1 : 0);

    Camera3D cam = {0};
    cam.position  = (Vector3){ 0, 1.5f, 4.5f };
    cam.target    = (Vector3){ 0, 1.0f, 0 };
    cam.up        = (Vector3){ 0, 1, 0 };
    cam.fovy      = 45.f;
    cam.nearPlane = 0.1f;
    cam.farPlane  = 100.f;

    float t0 = (float)GetTime();
    int frame = 0;
    logmsg("[glb] enter loop\n");

    while (!WindowShouldClose()) {
        float t = (float)GetTime() - t0;
        float o = t * 0.35f;
        cam.position.x = sinf(o) * 4.5f;
        cam.position.z = cosf(o) * 4.5f;
        cam.position.y = 1.4f;

        BeginDrawing();
        ClearBackground((Color){ 30, 34, 42, 255 });

        if (model.id) {
            BeginMode3D(cam);
            DrawGrid(10, 1.0f);
            DrawModel(model, (Vector3){0, 1, 0}, (Vector3){0, 0, 0},
                      (Vector3){1.2f, 1.2f, 1.2f}, WHITE);
            EndMode3D();
            DrawText("DamagedHelmet PBR", 20, 16, 28, WHITE);
            DrawText(path, 20, 50, 16, LIGHTGRAY);
        } else {
            DrawText("MODEL NOT LOADED", 20, 20, 32, RED);
            DrawText("Copy DamagedHelmet.glb next to exe", 20, 60, 20, WHITE);
            DrawText("Or: glb_example.exe path\\to\\file.glb", 20, 90, 18, LIGHTGRAY);
        }

        EndDrawing();

        if (frame == 0)
            logmsg("[glb] first frame drawn, model.id=%u\n", model.id);
        frame++;
    }

    logmsg("[glb] exit loop frames=%d\n", frame);
    if (model.id) UnloadModel(model);
    CloseWindow();
    return model.id ? 0 : 1;
}