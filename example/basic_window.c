#include <vlcrlib.h>
#include <stdio.h>
#include <math.h>

int main(void) {
    if (!InitWindow(1280, 720, "vlcrlib — full")) return 1;
    SetTargetFPS(60);

#ifdef _WIN32
    Font uiFont = LoadFontEx("C:/Windows/Fonts/segoeui.ttf", 32, 32, 96);
    if (!uiFont.ok)
        uiFont = LoadFontEx("C:/Windows/Fonts/arial.ttf", 32, 32, 96);
#else
    Font uiFont = LoadFontEx("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                             32, 32, 96);
#endif

    InitNetwork();
    if (InitAudioDevice()) printf("[vlcrlib] audio OK\n");
    else                   printf("[vlcrlib] audio unavailable\n");

    /* ---------- 3D scene ---------- */
    PhysicsWorld3D *w3d = CreatePhysicsWorld3D((Vector3){ 0, -9.81f, 0 });

    PhysicsBody3D *ground = CreatePhysicsBody3D(
        w3d, PHYS_SHAPE_PLANE3D,
        (Vector3){0,0,0}, (Vector3){0,1,0},
        PHYS_BODY_STATIC3D, 0, 0.9f, 0.1f);

    #define N3D 5
    PhysicsBody3D *cubes[N3D];
    Model cubeModels[N3D];
    Mesh cubeMesh = GenMeshCube(1, 1, 1);
    cubeModels[0] = LoadModelFromMesh(cubeMesh);
    for (int i = 0; i < N3D; ++i) {
        cubes[i] = CreatePhysicsBody3D(
            w3d, PHYS_SHAPE_BOX3D,
            (Vector3){ (i - 2) * 1.5f, 5.0f + i * 0.7f, 0.0f },
            (Vector3){ 0.5f, 0.5f, 0.5f },
            PHYS_BODY_DYNAMIC3D, 1.0f, 0.6f, 0.3f);
        cubeModels[i] = cubeModels[0];
    }

    /* ---------- 2D scene ---------- */
    PhysicsWorld2D *w2d = CreatePhysicsWorld2D((Vector2){ 0, 12.0f });

    PhysicsBody2D *ground2d = CreatePhysicsBody2D(
        w2d, PHYS_SHAPE_BOX2D,
        (Vector2){ 0, -3.0f }, (Vector2){ 6.0f, 0.5f },
        PHYS_BODY_STATIC2D, 0, 0.9f, 0.1f);

    #define N2D 6
    PhysicsBody2D *sq2d[N2D];
    for (int i = 0; i < N2D; ++i) {
        sq2d[i] = CreatePhysicsBody2D(
            w2d, PHYS_SHAPE_BOX2D,
            (Vector2){ (i - 2.5f) * 0.9f, 1.0f + i * 0.6f },
            (Vector2){ 0.4f, 0.4f },
            PHYS_BODY_DYNAMIC2D, 1.0f, 0.5f, 0.2f);
    }

    /* ---------- Particle system ---------- */
    ParticleSystem sparks = CreateParticleSystem(1024);
    SetParticleEmitter(&sparks, (Vector2){ 300, 500 });
    SetParticleEmissionRate(&sparks, 80.0f);
    StartParticleEmission(&sparks);

    /* ---------- Camera 3D ---------- */
    Camera3D cam = {0};
    cam.position  = (Vector3){ 7, 5, 9 };
    cam.target    = (Vector3){ 0, 2, 0 };
    cam.up        = (Vector3){ 0, 1, 0 };
    cam.fovy      = 55.0f;
    cam.nearPlane = 0.1f;
    cam.farPlane  = 200.0f;

    float t = 0.0f;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        t += dt;

        StepPhysicsWorld3D(w3d, dt);

        StepPhysicsWorld2D(w2d, dt);
        UpdateParticleSystem(&sparks, dt);

        BeginDrawing();
        ClearBackground((Color){ 15, 18, 28, 255 });

        /* -------- 3D -------- */
        BeginMode3D(cam);
            DrawGrid(20, 1.0f);

            for (int i = 0; i < N3D; ++i) {
                Color c = { (unsigned char)(100 + i*30),
                            (unsigned char)(60 + i*20),
                            (unsigned char)(180 - i*20), 255 };
                Vector3 pos = GetPhysicsBody3DPosition(cubes[i]);
                DrawModel(cubeModels[i], pos,
                          (Vector3){0,0,0}, (Vector3){1,1,1}, c);
            }

            DrawBezierCubic3D(
                (Vector3){ -3, 0, 0 },
                (Vector3){ -1, 4, 2 },
                (Vector3){  1, -1, 2 },
                (Vector3){  3, 1, 0 },
                (Color){ 255, 220, 100, 255 }, 32);
        EndMode3D();

        /* -------- 2D -------- */
        DrawRectangle(10, 10, 380, 30, (Color){ 40, 40, 60, 220 });
        DrawText("VLCRLIB — full build", 20, 14, 20, WHITE);

        /* 2D physics — квадратики в правой части экрана */
        Camera2D c2 = {0};
        c2.offset = (Vector2){ 950.0f, 400.0f };
        c2.zoom   = 60.0f;
        BeginMode2D(c2);

            Vector2 gp = GetPhysicsBody2DPosition(ground2d);
            DrawRectangle((int)(gp.x*60 - 6.0f*60),
                          (int)(-gp.y*60 - 0.5f*60),
                          12*60, (int)(1.0f*60),
                          (Color){ 80, 80, 80, 255 });

            for (int i = 0; i < N2D; ++i) {
                Vector2 p = GetPhysicsBody2DPosition(sq2d[i]);
                float   r = GetPhysicsBody2DRotation(sq2d[i]);
                float   sz = 0.8f * 60.0f;

                float c = cosf(r), s = sinf(r);
                Vector2 v[4] = {
                    { -sz/2, -sz/2 }, { sz/2, -sz/2 },
                    {  sz/2,  sz/2 }, { -sz/2, sz/2 }
                };
                Vector2 sc[4];
                for (int k = 0; k < 4; ++k) {
                    sc[k].x = p.x*60 + v[k].x*c - v[k].y*s;
                    sc[k].y = -p.y*60 + v[k].x*s + v[k].y*c;
                }
                Color col = { (unsigned char)(80 + i*25),
                              (unsigned char)(200 - i*15),
                              (unsigned char)(120 + i*10), 255 };
                DrawTriangle(sc[0], sc[1], sc[2], col);
                DrawTriangle(sc[0], sc[2], sc[3], col);
                DrawLineEx(sc[0], sc[1], 2.0f, WHITE);
                DrawLineEx(sc[1], sc[2], 2.0f, WHITE);
                DrawLineEx(sc[2], sc[3], 2.0f, WHITE);
                DrawLineEx(sc[3], sc[0], 2.0f, WHITE);
            }
        EndMode2D();

        DrawParticleSystem(&sparks);

        EndDrawing();
    }

    /* ---------- Cleanup ---------- */
    for (int i = 0; i < N3D; ++i) {
        DestroyPhysicsBody3D(cubes[i]);
    }
    UnloadModel(cubeModels[0]);
    DestroyPhysicsBody3D(ground);
    DestroyPhysicsWorld3D(w3d);

    for (int i = 0; i < N2D; ++i) DestroyPhysicsBody2D(sq2d[i]);
    DestroyPhysicsBody2D(ground2d);
    DestroyPhysicsWorld2D(w2d);

    DestroyParticleSystem(&sparks);
    CloseAudioDevice();
    CloseNetwork();
    if (uiFont.ok) UnloadFont(uiFont);
    CloseWindow();
    printf("[vlcrlib] clean shutdown OK\n");
    return 0;
}
