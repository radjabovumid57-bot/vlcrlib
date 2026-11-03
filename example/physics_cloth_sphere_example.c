/* Bullet cloth falling over a static sphere onto a static floor plane. */
#include <vlcrlib.h>
#include <SDL3/SDL_scancode.h>

#define CLOTH_SEGMENTS_X 40
#define CLOTH_SEGMENTS_Z 40
#define CLOTH_NODES_X (CLOTH_SEGMENTS_X + 1)
#define CLOTH_NODES_Z (CLOTH_SEGMENTS_Z + 1)

static SoftBody3D *CreateBlanket(PhysicsWorld3D *world)
{
    SoftBody3D *cloth = CreateSoftBodyCloth(world,
        (Vector3){ -2.6f, 3.4f, -2.6f }, (Vector3){ 2.6f, 3.4f, -2.6f },
        (Vector3){ -2.6f, 3.4f,  2.6f }, (Vector3){ 2.6f, 3.4f,  2.6f },
        CLOTH_NODES_X, CLOTH_NODES_Z, 2.0f);
    return cloth;
}

static void DrawCloth(SoftBody3D *cloth)
{
    if (!cloth) return;
    const Color wireColor = { 75, 220, 255, 255 };
    int faces = GetSoftBody3DFaceCount(cloth);
    for (int i = 0; i < faces; ++i) {
        Vector3 a, b, c;
        GetSoftBody3DFace(cloth, i, &a, &b, &c);
        /* Lift the wireframe slightly so it stays visible at floor contact. */
        a.y += 0.025f;
        b.y += 0.025f;
        c.y += 0.025f;
        DrawLine3D(a, b, wireColor);
        DrawLine3D(b, c, wireColor);
        DrawLine3D(c, a, wireColor);
    }
}

int main(void)
{
    if (!InitWindow(1100, 720, "vlcrlib — Cloth Over Static Sphere (Bullet)")) return 1;
    SetTargetFPS(60);

    PhysicsWorld3D *world = CreatePhysicsWorld3D((Vector3){ 0.0f, -9.81f, 0.0f });
    if (!world) {
        CloseWindow();
        return 1;
    }
    PhysicsBody3D *floor = CreatePhysicsBody3D(world, PHYS_SHAPE_BOX3D,
        (Vector3){ 0.0f, -0.2f, 0.0f }, (Vector3){ 12.0f, 0.15f, 12.0f },
        PHYS_BODY_STATIC3D, 0.0f, 0.85f, 0.0f);
    PhysicsBody3D *sphere = CreatePhysicsBody3D(world, PHYS_SHAPE_SPHERE3D,
        (Vector3){ 0.0f, 1.2f, 0.0f }, (Vector3){ 1.25f, 0.0f, 0.0f },
        PHYS_BODY_STATIC3D, 0.0f, 0.8f, 0.0f);
    if (!floor || !sphere) {
        if (sphere) DestroyPhysicsBody3D(sphere);
        if (floor) DestroyPhysicsBody3D(floor);
        DestroyPhysicsWorld3D(world);
        CloseWindow();
        return 1;
    }

    SoftBody3D *cloth = CreateBlanket(world);
    if (!cloth) {
        DestroyPhysicsBody3D(sphere);
        DestroyPhysicsBody3D(floor);
        DestroyPhysicsWorld3D(world);
        CloseWindow();
        return 1;
    }
    Camera3D camera = { 0 };
    camera.position = (Vector3){ 9.0f, 8.0f, 12.0f };
    camera.target = (Vector3){ 0.0f, 2.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 48.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 1.0f / 30.0f) dt = 1.0f / 30.0f;
        if (IsKeyPressed(SDL_SCANCODE_R)) {
            DestroySoftBody3D(cloth);
            cloth = CreateBlanket(world);
        }
        StepPhysicsWorld3D(world, dt);

        BeginDrawing();
        ClearBackground((Color){ 20, 28, 42, 255 });
        BeginMode3D(camera);
        DrawSphere((Vector3){ 0.0f, 1.2f, 0.0f }, 1.25f,
                   (Color){ 115, 125, 140, 255 });
        DrawCloth(cloth);
        EndMode3D();
        DrawText("40 x 40 CLOTH MESH OVER STATIC SPHERE", 20, 18, 24, WHITE);
        DrawText("Blue grid is the deforming cloth; floor collider is hidden", 20, 52, 18, LIGHTGRAY);
        DrawText("R: reset cloth", 20, 82, 18, YELLOW);
        EndDrawing();
    }

    DestroySoftBody3D(cloth);
    DestroyPhysicsBody3D(sphere);
    DestroyPhysicsBody3D(floor);
    DestroyPhysicsWorld3D(world);
    CloseWindow();
    return 0;
}
