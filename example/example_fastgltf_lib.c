/*
 * example_fastgltf_lib.c — glTF viewer (Windows / Linux / Android APK)
 *
 * Desktop:  puts DamagedHelmet.glb next to the exe (CMake POST_BUILD).
 * Android:  pack model into assets/DamagedHelmet.glb (see android/).
 *
 * Usage:
 *   example_fastgltf_lib.exe [path.glb]
 */
#include <vlcrlib.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
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

/* Copy from Android assets (or any SDL_IOFromFile path) to writable path. */
static int copy_io_to_file(const char *srcUri, const char *dstPath) {
    SDL_IOStream *in = SDL_IOFromFile(srcUri, "rb");
    if (!in) {
        flog("[gltf] SDL_IOFromFile failed: %s (%s)\n", srcUri, SDL_GetError());
        return 0;
    }
    Sint64 sz = SDL_GetIOSize(in);
    if (sz <= 0 || sz > 200 * 1024 * 1024) {
        SDL_CloseIO(in);
        return 0;
    }
    void *buf = SDL_malloc((size_t)sz);
    if (!buf) { SDL_CloseIO(in); return 0; }
    if (SDL_ReadIO(in, buf, (size_t)sz) != (size_t)sz) {
        SDL_free(buf);
        SDL_CloseIO(in);
        return 0;
    }
    SDL_CloseIO(in);

    FILE *out = fopen(dstPath, "wb");
    if (!out) {
        SDL_free(buf);
        flog("[gltf] cannot write %s\n", dstPath);
        return 0;
    }
    fwrite(buf, 1, (size_t)sz, out);
    fclose(out);
    SDL_free(buf);
    flog("[gltf] extracted %s -> %s (%lld bytes)\n", srcUri, dstPath, (long long)sz);
    return 1;
}

/*
 * Resolve model path:
 *  1) argv
 *  2) next to executable (desktop)
 *  3) Android assets via SDL (assets/DamagedHelmet.glb)
 *  4) relative fallbacks
 */
static Model load_model(const char *argv1, char *outPath, size_t outSz) {
    char candidates[16][1024];
    int n = 0;

    if (argv1 && argv1[0]) {
        snprintf(candidates[n++], 1024, "%s", argv1);
    }

    const char *base = SDL_GetBasePath();
    if (base) {
        snprintf(candidates[n++], 1024, "%sDamagedHelmet.glb", base);
        snprintf(candidates[n++], 1024, "%sassets/DamagedHelmet.glb", base);
        snprintf(candidates[n++], 1024, "%sexample/DamagedHelmet.glb", base);
    }

    /* Prefer already-extracted cache on Android internal storage */
    const char *pref = SDL_GetPrefPath("vlcrlib", "gltf");
    char cached[1024] = {0};
    if (pref) {
        snprintf(cached, sizeof(cached), "%sDamagedHelmet.glb", pref);
        if (file_exists(cached))
            snprintf(candidates[n++], 1024, "%s", cached);
    }

    snprintf(candidates[n++], 1024, "DamagedHelmet.glb");
    snprintf(candidates[n++], 1024, "example/DamagedHelmet.glb");
    snprintf(candidates[n++], 1024, "assets/DamagedHelmet.glb");

    for (int i = 0; i < n; ++i) {
        flog("[gltf] try: %s\n", candidates[i]);
        if (!file_exists(candidates[i])) continue;
        Model m = LoadModelGLTFTiny(candidates[i]);
        flog("[gltf] LoadModel id=%u\n", m.id);
        if (m.id) {
            if (outPath && outSz) snprintf(outPath, outSz, "%s", candidates[i]);
            return m;
        }
    }

    /* Android: assets are not normal files — extract via SDL_IOFromFile */
    if (pref && cached[0]) {
        const char *assetNames[] = {
            "DamagedHelmet.glb",
            "assets/DamagedHelmet.glb",
            NULL
        };
        for (int i = 0; assetNames[i]; ++i) {
            if (copy_io_to_file(assetNames[i], cached)) {
                Model m = LoadModelGLTFTiny(cached);
                flog("[gltf] from assets id=%u\n", m.id);
                if (m.id) {
                    if (outPath && outSz) snprintf(outPath, outSz, "%s", cached);
                    return m;
                }
            }
        }
    }

    if (outPath && outSz) outPath[0] = 0;
    return (Model){0};
}

int main(int argc, char **argv) {
    flog("[gltf] start platform example_fastgltf_lib\n");

    if (!InitWindow(1280, 720, "vlcrlib glTF")) {
        flog("[gltf] InitWindow FAILED: %s\n", SDL_GetError());
        return 1;
    }
    SetTargetFPS(60);
    flog("[gltf] window ok\n");

    char path[1024] = {0};
    Model model = load_model(argc > 1 ? argv[1] : NULL, path, sizeof(path));
    flog("[gltf] model id=%u path=%s\n", model.id, path[0] ? path : "(none)");

    vgraphics_EnableStudioLighting();
    vgraphics_SetIBLIntensity(vgraphics_IsIBLLoaded() ? 1.0f : 0.0f);

    Camera3D cam = {0};
    cam.position = (Vector3){ 0, 1.4f, 4.5f };
    cam.target   = (Vector3){ 0, 1.0f, 0 };
    cam.up       = (Vector3){ 0, 1, 0 };
    cam.fovy = 45.f;
    cam.nearPlane = 0.05f;
    cam.farPlane = 100.f;

    float t0 = (float)GetTime();
    int frame = 0;

    while (!WindowShouldClose()) {
        float t = (float)GetTime() - t0;
        float o = t * 0.35f;
        cam.position.x = sinf(o) * 4.5f;
        cam.position.z = cosf(o) * 4.5f;
        cam.position.y = 1.35f;

        BeginDrawing();
        ClearBackground((Color){ 28, 32, 40, 255 });

        if (model.id) {
            BeginMode3D(cam);
            DrawGrid(10, 1.0f);
            DrawModel(model, (Vector3){0, 1, 0}, (Vector3){0, 0, 0},
                      (Vector3){1.15f, 1.15f, 1.15f}, WHITE);
            EndMode3D();
            DrawText("glTF / Vulkan", 20, 16, 28, WHITE);
            DrawText(path[0] ? path : "", 20, 52, 14, LIGHTGRAY);
        } else {
            DrawText("MODEL NOT LOADED", 20, 24, 30, RED);
            DrawText("Desktop: DamagedHelmet.glb next to exe", 20, 70, 18, WHITE);
            DrawText("Android: assets/DamagedHelmet.glb in APK", 20, 96, 18, LIGHTGRAY);
        }
        EndDrawing();

        if (frame == 0)
            flog("[gltf] first frame ok model=%u\n", model.id);
        frame++;
    }

    if (model.id) UnloadModel(model);
    CloseWindow();
    fprintf(stderr, "[exit] Press Enter...\n"); fflush(stderr);
    getchar();
    return model.id ? 0 : 1;
}
