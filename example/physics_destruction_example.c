/* Breakable wall made from pre-fractured Bullet rigid-body chunks. */
#include <vlcrlib.h>
#include <SDL3/SDL_scancode.h>

#define COLS 8
#define ROWS 5
#define CHUNK_COUNT (COLS * ROWS)

int main(void) {
    if (!InitWindow(1100, 700, "vlcrlib — Breakable Wall (Bullet)")) return 1;
    SetTargetFPS(60);
    PhysicsWorld3D *world = CreatePhysicsWorld3D((Vector3){0,-9.81f,0});
    PhysicsBody3D *ground = CreatePhysicsBody3D(world, PHYS_SHAPE_PLANE3D,
        (Vector3){0,0,0}, (Vector3){0,1,0}, PHYS_BODY_STATIC3D, 0, 0.9f, 0.0f);
    PhysicsBody3D *wall[CHUNK_COUNT] = {0};

    /* Stagger rows to make a stable block wall. Each chunk is its own rigid body. */
    for (int row = 0; row < ROWS; ++row) {
        for (int col = 0; col < COLS; ++col) {
            int i = row * COLS + col;
            float offset = (row & 1) ? 0.45f : 0.0f;
            wall[i] = CreatePhysicsBody3D(world, PHYS_SHAPE_BOX3D,
                (Vector3){-3.15f + (float)col * 0.9f + offset,
                          0.42f + (float)row * 0.84f, 0.0f},
                (Vector3){0.43f,0.4f,0.32f}, PHYS_BODY_DYNAMIC3D,
                2.0f, 0.65f, 0.08f);
        }
    }
    Camera3D camera = {0};
    camera.position = (Vector3){0,7,13}; camera.target = (Vector3){0,2,0};
    camera.up = (Vector3){0,1,0}; camera.fovy = 48; camera.nearPlane = 0.1f; camera.farPlane = 120;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime(); if (dt > 1.0f/30.0f) dt = 1.0f/30.0f;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector3 direction = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
            PhysicsRayHit3D hit = RaycastPhysics3D(world, camera.position, direction, 80.0f);
            if (hit.hit) {
                for (int i = 0; i < CHUNK_COUNT; ++i) {
                    if (wall[i] != hit.body) continue;
                    Vector3 impulse = Vector3Scale(direction, 16.0f);
                    ApplyPhysicsBody3DImpulseAtPoint(wall[i], impulse,
                        Vector3Subtract(hit.point, GetPhysicsBody3DPosition(wall[i])));
                    break;
                }
            }
        }
        if (IsKeyPressed(SDL_SCANCODE_R)) {
            for (int i = 0; i < CHUNK_COUNT; ++i) {
                if (wall[i]) DestroyPhysicsBody3D(wall[i]);
                wall[i] = NULL;
            }
            for (int row = 0; row < ROWS; ++row) {
                for (int col = 0; col < COLS; ++col) {
                    int i = row * COLS + col;
                    float offset = (row & 1) ? 0.45f : 0.0f;
                    wall[i] = CreatePhysicsBody3D(world, PHYS_SHAPE_BOX3D,
                        (Vector3){-3.15f + (float)col * 0.9f + offset,
                                  0.42f + (float)row * 0.84f, 0.0f},
                        (Vector3){0.43f,0.4f,0.32f}, PHYS_BODY_DYNAMIC3D,
                        2.0f, 0.65f, 0.08f);
                }
            }
        }
        StepPhysicsWorld3D(world, dt);
        BeginDrawing(); ClearBackground((Color){24,31,43,255});
        BeginMode3D(camera);
        DrawGrid(30,1.0f);
        DrawCube((Vector3){0,-0.25f,0},(Vector3){30,0.5f,30},(Color){70,82,95,255});
        for (int i = 0; i < CHUNK_COUNT; ++i) {
            if (wall[i]) {
                Vector3 p = GetPhysicsBody3DPosition(wall[i]);
                DrawCube(p,(Vector3){0.86f,0.8f,0.64f},(Color){190,110,65,255});
            }
        }
        EndMode3D();
        DrawText("Rigid-body destruction prototype",20,20,26,WHITE);
        DrawText("Click to shoot an impulse into a chunk | R rebuild the wall",20,56,18,LIGHTGRAY);
        DrawText("Chunks are authored ahead of time; this is not Chaos fracturing or Geometry Collections.",
                 20,84,17,YELLOW);
        EndDrawing();
    }
    for (int i = 0; i < CHUNK_COUNT; ++i) if (wall[i]) DestroyPhysicsBody3D(wall[i]);
    DestroyPhysicsBody3D(ground); DestroyPhysicsWorld3D(world); CloseWindow(); return 0;
}
