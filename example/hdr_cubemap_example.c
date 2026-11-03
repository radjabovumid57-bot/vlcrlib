/*
 * hdr_cubemap_example.c — загрузка HDR equirectangular → cubemap
 *
 * Usage:
 *   hdr_cubemap_example.exe
 *   hdr_cubemap_example.exe studio.hdr
 *   hdr_cubemap_example.exe C:\maps\kloppenheim_06_4k.hdr
 *
 * Скачать free HDRI: https://polyhaven.com/hdris  (формат .hdr)
 */
#include <vlcrlib.h>
#include "vgltf.h"
#include <stdio.h>
#include <math.h>

int main(int argc, char **argv) {
    if (!InitWindow(1280, 720, "vlcrlib — HDR Cubemap Load")) return 1;
    SetTargetFPS(60);

    vgraphics_SetDirectionalLight(true,
        (Vector3){ -0.4f, -0.8f, -0.3f }, WHITE, 0.5f);
    vgraphics_SetAmbientLight(true, (Color){ 40, 45, 55, 255 }, 0.2f);

    const char *hdr = NULL;
    if (argc > 1) hdr = argv[1];

    /* Вариант A: полный IBL bake (env + irradiance + prefilter + BRDF) */
    if (vgraphics_LoadIBL(hdr)) {
        fprintf(stderr, "LoadIBL OK, loaded=%d\n", vgraphics_IsIBLLoaded());
    }

    /* Вариант B: только cubemap (без полного bake) — если передан путь */
    if (hdr && hdr[0]) {
        if (vgraphics_LoadHDRCubemap(hdr, 128))
            fprintf(stderr, "LoadHDRCubemap OK\n");
        else
            fprintf(stderr, "LoadHDRCubemap failed (file missing or not .hdr?)\n");
    } else {
        fprintf(stderr, "No HDR path — procedural studio IBL\n");
        fprintf(stderr, "  Tip: hdr_cubemap_example.exe path/to/file.hdr\n");
    }

    Camera3D cam = {0};
    cam.position = (Vector3){ 0, 1.5f, 4 };
    cam.target = (Vector3){ 0, 0.5f, 0 };
    cam.up = (Vector3){ 0, 1, 0 };
    cam.fovy = 50;
    cam.nearPlane = 0.1f;
    cam.farPlane = 100;

    Model model = LoadModelGLTFTiny("DamagedHelmet.glb");
    if (!model.id) model = LoadModelGLTFTiny("example/DamagedHelmet.glb");

    float t0 = (float)GetTime();
    while (!WindowShouldClose()) {
        float t = (float)GetTime() - t0;
        cam.position.x = sinf(t * 0.3f) * 4.f;
        cam.position.z = cosf(t * 0.3f) * 4.f;

        BeginDrawing();
        ClearBackground((Color){ 16, 18, 24, 255 });
        BeginMode3D(cam);
        DrawGrid(16, 1.0f);
        if (model.id)
            DrawModel(model, (Vector3){0, 0, 0}, (Vector3){0, t * 0.4f, 0},
                      (Vector3){1,1,1}, WHITE);
        else
            DrawSphere((Vector3){0, 0.8f, 0}, 0.8f, (Color){180, 190, 200, 255});
        EndMode3D();

        DrawText("HDR cubemap load", 20, 16, 28, WHITE);
        DrawText(hdr ? hdr : "procedural (pass .hdr path as argv[1])", 20, 52, 18, LIGHTGRAY);
        DrawText(vgraphics_IsIBLLoaded() ? "IBL: loaded" : "IBL: none", 20, 80, 18,
                 vgraphics_IsIBLLoaded() ? GREEN : RED);
        EndDrawing();
    }

    UnloadModel(model);
    vgraphics_UnloadIBL();
    CloseWindow();
    return 0;
}
