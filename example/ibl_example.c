/*
 * ibl_example.c — IBL (procedural studio or HDR file)
 *
 * Usage:
 *   ibl_example.exe
 *   ibl_example.exe path/to/studio.hdr
 *
 * Rebuild shader:
 *   glslc shaders/pbr.frag -o build/shaders/pbr.frag.spv
 */
#include <vlcrlib.h>
#include "vgltf.h"
#include <stdio.h>
#include <math.h>

int main(int argc, char **argv) {
    if (!InitWindow(1280, 720, "vlcrlib — IBL")) return 1;
    SetTargetFPS(60);

    vgraphics_SetDirectionalLight(true,
        (Vector3){ -0.35f, -0.8f, -0.25f },
        (Color){ 255, 250, 240, 255 }, 0.6f);
    vgraphics_SetAmbientLight(true, (Color){ 30, 35, 45, 255 }, 0.15f);

    const char *hdr = (argc > 1) ? argv[1] : NULL;
    if (vgraphics_LoadIBL(hdr))
        fprintf(stderr, "IBL loaded (%s)\n", hdr ? hdr : "procedural");
    vgraphics_SetIBLIntensity(0.0f); /* 0 = procedural studio in shader */

    Camera3D cam = {0};
    cam.position = (Vector3){ 0, 1.2f, 3.5f };
    cam.target = (Vector3){ 0, 0.8f, 0 };
    cam.up = (Vector3){ 0, 1, 0 };
    cam.fovy = 45;
    cam.nearPlane = 0.1f;
    cam.farPlane = 50;

    Model m = LoadModelGLTFTiny("DamagedHelmet.glb");
    if (!m.id) m = LoadModelGLTFTiny("example/DamagedHelmet.glb");

    float t0 = (float)GetTime();
    while (!WindowShouldClose()) {
        float t = (float)GetTime() - t0;
        cam.position.x = sinf(t * 0.35f) * 3.2f;
        cam.position.z = cosf(t * 0.35f) * 3.2f;

        BeginDrawing();
        ClearBackground((Color){ 18, 20, 26, 255 });
        BeginMode3D(cam);
        DrawGrid(12, 1.0f);
        if (m.id)
            DrawModel(m, (Vector3){0,0,0}, (Vector3){0, t*0.3f, 0}, (Vector3){1,1,1}, WHITE);
        else
            DrawSphere((Vector3){0,0.8f,0}, 0.8f, (Color){200,200,210,255});
        EndMode3D();
        DrawText("IBL — BRDF LUT + prefilter (CPU bake)", 20, 16, 24, WHITE);
        DrawText(hdr ? hdr : "procedural studio environment", 20, 48, 16, LIGHTGRAY);
        EndDrawing();
    }
    UnloadModel(m);
    vgraphics_UnloadIBL();
    CloseWindow();
    return 0;
}
