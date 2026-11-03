/* jolt_example.c — floor + falling boxes (Jolt Physics) */
#include <vlcrlib.h>
#include "vjolt.h"
#include <stdio.h>
#include <math.h>

int main(void) {
    if (!InitWindow(1280, 720, "vlcrlib — Jolt Physics")) return 1;
    SetTargetFPS(60);

    if (!Jolt_Init()) {
        fprintf(stderr, "Jolt_Init failed (install joltphysics via vcpkg)\n");
        CloseWindow();
        return 1;
    }

    JoltWorld *world = Jolt_CreateWorld((Vector3){ 0, -9.81f, 0 });
    if (!world) {
        fprintf(stderr, "Jolt_CreateWorld failed\n");
        Jolt_Shutdown();
        CloseWindow();
        return 1;
    }

    /* Static floor */
    JoltBody *floor = Jolt_CreateBody(world, JOLT_SHAPE_BOX,
        (Vector3){ 0, -0.5f, 0 },
        (Vector3){ 20, 0.5f, 20 },
        JOLT_BODY_STATIC, 0, 0.8f, 0.1f);

    /* Dynamic boxes */
    JoltBody *boxes[8];
    for (int i = 0; i < 8; ++i) {
        boxes[i] = Jolt_CreateBody(world, JOLT_SHAPE_BOX,
            (Vector3){ (float)(i % 4) * 1.2f - 1.8f, 2.0f + (float)(i / 4) * 1.5f, 0 },
            (Vector3){ 0.5f, 0.5f, 0.5f },
            JOLT_BODY_DYNAMIC, 1.0f, 0.5f, 0.2f);
    }

    Camera3D cam = {0};
    cam.position = (Vector3){ 0, 6, 14 };
    cam.target   = (Vector3){ 0, 1, 0 };
    cam.up       = (Vector3){ 0, 1, 0 };
    cam.fovy     = 45;
    cam.nearPlane = 0.1f;
    cam.farPlane  = 200;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;
        Jolt_Step(world, dt);

        BeginDrawing();
        ClearBackground((Color){ 40, 44, 52, 255 });
        BeginMode3D(cam);

        /* Floor */
        DrawCube((Vector3){ 0, -0.5f, 0 }, (Vector3){ 40, 1, 40 }, (Color){ 80, 90, 100, 255 });

        for (int i = 0; i < 8; ++i) {
            if (!boxes[i]) continue;
            Vector3 p = Jolt_GetPosition(boxes[i]);
            DrawCube(p, (Vector3){ 1, 1, 1 }, (Color){ 220, 120, 60, 255 });
        }

        EndMode3D();
        DrawText("Jolt Physics — falling boxes", 24, 24, 28, WHITE);
        DrawText("Backend: Jolt (vcpkg joltphysics)", 24, 60, 18, LIGHTGRAY);
        EndDrawing();
    }

    for (int i = 0; i < 8; ++i)
        if (boxes[i]) Jolt_DestroyBody(boxes[i]);
    if (floor) Jolt_DestroyBody(floor);
    Jolt_DestroyWorld(world);
    Jolt_Shutdown();
    CloseWindow();
    return 0;
}
