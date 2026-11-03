/* model_viewer.c — auto loader demo: glTF/GLB, FBX, USD, OBJ. */
#include <vlcrlib.h>
#include <SDL3/SDL.h>
#include "vloaders.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (!InitWindow(1280, 720, "vlcrlib model viewer")) return 1;

    char path[1024] = {0};
    if (argc > 1 && argv[1][0]) snprintf(path, sizeof(path), "%s", argv[1]);
    else {
        const char *base = SDL_GetBasePath();
        snprintf(path, sizeof(path), "%sDamagedHelmet.glb", base ? base : "");
    }

    Model model = LoadModelAuto(path);
    fprintf(stderr, "[model_viewer] model=%u meshes=%d materials=%d path=%s\n",
            model.id, model.meshCount, model.materialCount, path);

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
    cam.target = (Vector3){ 0, 1.0f, 0 };
    cam.up = (Vector3){ 0, 1, 0 };
    cam.fovy = 45.0f;
    cam.nearPlane = 0.05f;
    cam.farPlane = 100.0f;

    int frames = 0;
    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground((Color){ 118, 118, 118, 255 });
        if (model.id) {
            BeginMode3D(cam);
            DrawModel(model, (Vector3){ 0, 1, 0 }, (Vector3){ 0, 0, 0 },
                      (Vector3){ 1, 1, 1 }, WHITE);
            EndMode3D();
            DrawText("Auto model viewer", 20, 16, 26, WHITE);
            DrawText(path, 20, 50, 14, LIGHTGRAY);
        } else {
            DrawText("MODEL NOT LOADED", 20, 24, 26, RED);
            DrawText("glTF/GLB/OBJ native; FBX needs assimp; USD needs OpenUSD",
                     20, 60, 16, WHITE);
            DrawText(path, 20, 86, 14, LIGHTGRAY);
        }
        EndDrawing();
        frames++;
        const char *shot = getenv("VLCR_SCREENSHOT");
        if (shot && frames == 40) vgraphics_CaptureScreenshot(shot);
    }

    if (model.id) UnloadModel(model);
    CloseWindow();
    return 0;
}
