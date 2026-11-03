/*
 * shadow_area_example.c — directional shadows + area light (PBR)
 *
 * Rebuild shaders after pulling pbr.frag:
 *   glslc shaders/pbr.frag -o build/shaders/pbr.frag.spv
 */
#include <vlcrlib.h>
#include "vgltf.h"
#include <stdio.h>
#include <math.h>

int main(void) {
    if (!InitWindow(1280, 720, "vlcrlib — Shadows + Area Light")) return 1;
    SetTargetFPS(60);

    vgraphics_SetDirectionalLight(true,
        (Vector3){ -0.45f, -1.0f, -0.25f },
        (Color){ 255, 245, 230, 255 }, 1.6f);
    vgraphics_SetAmbientLight(true, (Color){ 40, 45, 55, 255 }, 0.25f);

    /* Soft rectangular panel light */
    vgraphics_SetAreaLight(true,
        (Vector3){ 2.0f, 4.0f, 1.0f },
        (Vector3){ -0.3f, -1.0f, -0.2f },
        3.0f, 2.0f,
        (Color){ 255, 240, 220, 255 }, 8.0f);

    /* Shadow map settings (matrix from directional light) */
    vgraphics_SetShadows(true, 2048, 0.0025f, 0.9f);
    vgraphics_SetShadowSoftness(1.5f);
    vgraphics_ComputeDirectionalShadowMatrix(12.0f, 0.5f, 40.0f);

    Camera3D cam = {0};
    cam.position = (Vector3){ 6, 5, 8 };
    cam.target = (Vector3){ 0, 0.5f, 0 };
    cam.up = (Vector3){ 0, 1, 0 };
    cam.fovy = 50;
    cam.nearPlane = 0.1f;
    cam.farPlane = 100;

    Model helmet = LoadModelGLTFTiny("DamagedHelmet.glb");
    if (!helmet.id) helmet = LoadModelGLTFTiny("example/DamagedHelmet.glb");

    float t0 = (float)GetTime();
    while (!WindowShouldClose()) {
        float t = (float)GetTime() - t0;
        cam.position.x = cosf(t * 0.25f) * 8.f;
        cam.position.z = sinf(t * 0.25f) * 8.f;
        vgraphics_ComputeDirectionalShadowMatrix(12.0f, 0.5f, 40.0f);

        BeginDrawing();
        ClearBackground((Color){ 22, 24, 30, 255 });
        BeginMode3D(cam);
        DrawGrid(24, 1.0f);
        DrawCube((Vector3){ 0, -0.05f, 0 }, (Vector3){ 20, 0.1f, 20 }, (Color){ 70, 75, 85, 255 });
        DrawCube((Vector3){ -2, 0.5f, 0 }, (Vector3){ 1, 1, 1 }, (Color){ 200, 120, 80, 255 });
        if (helmet.id)
            DrawModel(helmet, (Vector3){ 1.5f, 0.9f, 0 }, (Vector3){ 0, t, 0 },
                      (Vector3){ 1, 1, 1 }, WHITE);
        DrawSphere((Vector3){ 0, 0.4f, 2 }, 0.4f, (Color){ 100, 160, 220, 255 });
        EndMode3D();
        DrawText("Area light + Shadow maps", 20, 16, 28, WHITE);
        DrawText("glslc shaders/pbr.frag -o shaders/pbr.frag.spv", 20, 52, 16, LIGHTGRAY);
        EndDrawing();
    }
    UnloadModel(helmet);
    CloseWindow();
    return 0;
}
