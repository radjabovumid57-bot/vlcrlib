/*
 * cgltf_min_example.c — minimal glTF/GLB viewer.
 *
 * The old cgltf loader was removed from vlcrlib. This example now uses the
 * engine's fastgltf path (LoadModelGLTFTiny, historical name) so the
 * project has a single glTF importer.
 *
 * Usage:
 *   cgltf_min_example.exe [model.glb]
 */
#include <vlcrlib.h>
#include "vgltf.h"
#include <stdio.h>
#include <string.h>

static Model load_model(const char *arg, char *out, size_t outSize) {
    const char *candidates[4];
    int n = 0;
    if (arg && arg[0]) candidates[n++] = arg;
    candidates[n++] = "DamagedHelmet.glb";
    candidates[n++] = "example/DamagedHelmet.glb";
    candidates[n++] = "../example/DamagedHelmet.glb";

    for (int i = 0; i < n; ++i) {
        FILE *f = fopen(candidates[i], "rb");
        if (!f) continue;
        fclose(f);
        Model m = LoadModelGLTFTiny(candidates[i]);
        if (m.id) {
            snprintf(out, outSize, "%s", candidates[i]);
            return m;
        }
    }
    if (out && outSize) out[0] = 0;
    return (Model){0};
}

int main(int argc, char **argv) {
    if (!InitWindow(1280, 720, "vlcrlib glTF (fastgltf)")) return 1;
    SetTargetFPS(60);

    char path[1024] = {0};
    Model model = load_model(argc > 1 ? argv[1] : NULL, path, sizeof(path));

    vgraphics_SetDirectionalLight(true,
        (Vector3){ -0.35f, -0.70f, -0.60f },
        (Color){ 255, 250, 240, 255 }, 3.0f);
    vgraphics_SetAmbientLight(true, (Color){ 90, 100, 120, 255 }, 0.15f);
    bool ibl = vgraphics_LoadIBL(NULL);
    vgraphics_SetIBLIntensity(ibl ? 1.2f : 0.0f);
    vgraphics_SetSkybox(0);
    SetSSAO(1, 0.5f, 0.6f);
    SetBloom(0.9f, 0.15f);
    SetGraphicsExposure(1.0f);

    Camera3D cam = {0};
    cam.position = (Vector3){ 2.4f, 1.45f, 4.2f };
    cam.target   = (Vector3){ 0, 1.0f, 0 };
    cam.up       = (Vector3){ 0, 1, 0 };
    cam.fovy     = 45.0f;
    cam.nearPlane = 0.05f;
    cam.farPlane  = 100.0f;

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground((Color){ 118, 118, 118, 255 });
        if (model.id) {
            BeginMode3D(cam);
            DrawModel(model, (Vector3){0, 1, 0}, (Vector3){0, 0, 0},
                      (Vector3){1.15f, 1.15f, 1.15f}, WHITE);
            EndMode3D();
            DrawText("glTF / Vulkan — fastgltf", 20, 16, 28, WHITE);
            DrawText(path, 20, 52, 14, LIGHTGRAY);
        } else {
            DrawText("MODEL NOT LOADED", 20, 24, 30, RED);
            DrawText("Put DamagedHelmet.glb next to the exe", 20, 70, 18, WHITE);
        }
        EndDrawing();
    }

    if (model.id) UnloadModel(model);
    CloseWindow();
    return model.id ? 0 : 1;
}
