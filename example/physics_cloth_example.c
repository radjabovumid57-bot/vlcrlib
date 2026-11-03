/* Bullet soft-body cloth patch with a pinned edge and reset/gravity controls. */
#include <vlcrlib.h>
#include <SDL3/SDL_scancode.h>

static SoftBody3D *makeCloth(PhysicsWorld3D *world) {
    SoftBody3D *cloth = CreateSoftBodyCloth(world,
        (Vector3){-3.0f, 6.0f, 0.0f}, (Vector3){ 3.0f, 6.0f, 0.0f},
        (Vector3){-3.0f, 2.0f, 0.0f}, (Vector3){ 3.0f, 2.0f, 0.0f},
        24, 18, 0.7f);
    if (cloth) {
        int count = GetSoftBody3DVertexCount(cloth);
        for (int i = 0; i < count; ++i) {
            Vector3 p = GetSoftBody3DVertex(cloth, i);
            if (p.y > 5.99f) PinSoftBody3DVertex(cloth, i);
        }
    }
    return cloth;
}

static void drawCloth(SoftBody3D *cloth, Color color) {
    if (!cloth) return;
    int faces = GetSoftBody3DFaceCount(cloth);
    for (int i = 0; i < faces; ++i) {
        Vector3 a, b, c;
        GetSoftBody3DFace(cloth, i, &a, &b, &c);
        DrawLine3D(a, b, color);
        DrawLine3D(b, c, color);
        DrawLine3D(c, a, color);
    }
}

int main(void) {
    if (!InitWindow(1100, 720, "vlcrlib — Cloth Simulation (Bullet Soft Body)")) return 1;
    SetTargetFPS(60);
    PhysicsWorld3D *world = CreatePhysicsWorld3D((Vector3){0,-9.81f,0});
    PhysicsBody3D *ground = CreatePhysicsBody3D(world, PHYS_SHAPE_PLANE3D,
        (Vector3){0,0,0}, (Vector3){0,1,0}, PHYS_BODY_STATIC3D, 0, 0.8f, 0.0f);
    SoftBody3D *cloth = makeCloth(world);
    bool gravity = true;
    Camera3D camera = {0};
    camera.position = (Vector3){8,6,12}; camera.target = (Vector3){0,3,0};
    camera.up = (Vector3){0,1,0}; camera.fovy = 48; camera.nearPlane = 0.1f; camera.farPlane = 100;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime(); if (dt > 1.0f/30.0f) dt = 1.0f/30.0f;
        if (IsKeyPressed(SDL_SCANCODE_G)) {
            gravity = !gravity;
            SetPhysicsWorld3DGravity(world, gravity ? (Vector3){0,-9.81f,0} : (Vector3){0,0,0});
        }
        if (IsKeyPressed(SDL_SCANCODE_R)) {
            DestroySoftBody3D(cloth);
            SetPhysicsWorld3DGravity(world, (Vector3){0,-9.81f,0});
            gravity = true;
            cloth = makeCloth(world);
        }
        StepPhysicsWorld3D(world, dt);

        BeginDrawing(); ClearBackground((Color){20,28,42,255});
        BeginMode3D(camera);
        DrawGrid(24,1.0f);
        DrawCube((Vector3){0,-0.2f,0},(Vector3){24,0.4f,24},(Color){65,76,91,255});
        DrawCube((Vector3){0,6.15f,0},(Vector3){6.5f,0.18f,0.3f},(Color){120,90,55,255});
        drawCloth(cloth, (Color){70,190,235,235});
        EndMode3D();
        DrawText("Soft-body cloth patch",20,18,27,WHITE);
        DrawText("Bullet soft-body mesh | top row pinned | gravity deforms the fabric",20,54,18,LIGHTGRAY);
        DrawText(gravity ? "G: gravity ON" : "G: gravity OFF",20,84,18,YELLOW);
        DrawText("R: reset cloth",20,110,18,LIGHTGRAY);
        EndDrawing();
    }
    DestroySoftBody3D(cloth);
    DestroyPhysicsBody3D(ground);
    DestroyPhysicsWorld3D(world);
    CloseWindow();
    return 0;
}
