/* Bullet rigid-body example: gravity, contact, rotation, impulses and reset. */
#include <vlcrlib.h>
#include <SDL3/SDL_scancode.h>

#define MAX_BODIES 32

typedef struct DemoBody {
    PhysicsBody3D *body;
    bool sphere;
    float size;
    Color color;
} DemoBody;

static DemoBody bodies[MAX_BODIES];
static int bodyCount = 0;

static bool AddDemoBody(PhysicsWorld3D *world, bool sphere, Vector3 position,
                        float size, Color color)
{
    if (bodyCount >= MAX_BODIES) return false;
    PhysicsBody3D *body = CreatePhysicsBody3D(world,
        sphere ? PHYS_SHAPE_SPHERE3D : PHYS_SHAPE_BOX3D,
        position,
        sphere ? (Vector3){ size, 0.0f, 0.0f }
               : (Vector3){ size, size, size },
        PHYS_BODY_DYNAMIC3D, sphere ? 2.0f : 4.0f, 0.62f, 0.12f);
    if (!body) return false;
    bodies[bodyCount++] = (DemoBody){ body, sphere, size, color };
    return true;
}

static void ClearDemoBodies(void)
{
    for (int i = 0; i < bodyCount; ++i)
        if (bodies[i].body) DestroyPhysicsBody3D(bodies[i].body);
    bodyCount = 0;
}

static void ResetDemo(PhysicsWorld3D *world)
{
    ClearDemoBodies();
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            Vector3 p = { (float)(col - 1) * 1.05f,
                          0.65f + (float)row * 1.05f, 0.0f };
            Color c = (Color){ (unsigned char)(80 + col * 55),
                               (unsigned char)(145 + row * 28), 220, 255 };
            AddDemoBody(world, false, p, 0.48f, c);
        }
    }
    AddDemoBody(world, true, (Vector3){ 2.0f, 5.0f, 0.0f }, 0.55f,
                (Color){ 245, 165, 65, 255 });
}

int main(void)
{
    if (!InitWindow(1100, 700, "vlcrlib — Rigid Body Physics (Bullet)")) return 1;
    SetTargetFPS(60);

    PhysicsWorld3D *world = CreatePhysicsWorld3D((Vector3){ 0.0f, -9.81f, 0.0f });
    if (!world) {
        CloseWindow();
        return 1;
    }
    PhysicsBody3D *floor = CreatePhysicsBody3D(world, PHYS_SHAPE_BOX3D,
        (Vector3){ 0.0f, -0.25f, 0.0f }, (Vector3){ 12.0f, 0.25f, 12.0f },
        PHYS_BODY_STATIC3D, 0.0f, 0.9f, 0.0f);
    if (!floor) {
        DestroyPhysicsWorld3D(world);
        CloseWindow();
        return 1;
    }
    ResetDemo(world);

    Mesh cubeMesh = GenMeshCube(1.0f, 1.0f, 1.0f);
    Camera3D camera = { 0 };
    camera.position = (Vector3){ 9.0f, 8.0f, 13.0f };
    camera.target = (Vector3){ 0.0f, 1.5f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 50.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 150.0f;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 1.0f / 30.0f) dt = 1.0f / 30.0f;

        if (IsKeyPressed(SDL_SCANCODE_R)) ResetDemo(world);
        if (IsKeyPressed(SDL_SCANCODE_SPACE)) {
            AddDemoBody(world, (bodyCount & 1) != 0,
                (Vector3){ 0.0f, 7.0f, -1.0f }, 0.5f,
                (Color){ 230, 115, 85, 255 });
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector3 rayDirection = Vector3Normalize(
                Vector3Subtract(camera.target, camera.position));
            PhysicsRayHit3D hit = RaycastPhysics3D(world, camera.position,
                                                    rayDirection, 40.0f);
            if (hit.hit && hit.body && hit.body != floor) {
                ApplyPhysicsBody3DImpulseAtPoint(hit.body,
                    (Vector3){ rayDirection.x * 8.0f, rayDirection.y * 8.0f,
                               rayDirection.z * 8.0f },
                    Vector3Subtract(hit.point, GetPhysicsBody3DPosition(hit.body)));
            }
        }

        StepPhysicsWorld3D(world, dt);
        BeginDrawing();
        ClearBackground((Color){ 24, 31, 44, 255 });
        BeginMode3D(camera);
        DrawCube((Vector3){ 0.0f, -0.25f, 0.0f }, (Vector3){ 24.0f, 0.5f, 24.0f },
                 (Color){ 72, 84, 99, 255 });
        DrawGrid(24, 1.0f);
        for (int i = 0; i < bodyCount; ++i) {
            Vector3 p = GetPhysicsBody3DPosition(bodies[i].body);
            if (bodies[i].sphere) {
                DrawSphere(p, bodies[i].size, bodies[i].color);
            } else {
                Quaternion q = GetPhysicsBody3DRotation(bodies[i].body);
                Matrix transform = MatrixMultiply(
                    MatrixTranslate(p.x, p.y, p.z),
                    MatrixMultiply(QuaternionToMatrix(q),
                                   MatrixScale(bodies[i].size * 2.0f,
                                               bodies[i].size * 2.0f,
                                               bodies[i].size * 2.0f)));
                DrawMesh(cubeMesh, (Texture2D){ 0 }, bodies[i].color, transform);
            }
        }
        EndMode3D();
        DrawText("RIGID BODY PHYSICS — BULLET", 20, 18, 25, WHITE);
        DrawText("Space: drop body | Left click: impulse | R: reset", 20, 52, 18, LIGHTGRAY);
        DrawText("Dynamic boxes and spheres collide with the static floor", 20, 80, 17, YELLOW);
        EndDrawing();
    }

    ClearDemoBodies();
    UnloadMesh(cubeMesh);
    DestroyPhysicsBody3D(floor);
    DestroyPhysicsWorld3D(world);
    CloseWindow();
    return 0;
}
