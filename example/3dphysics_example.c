#include <vlcrlib.h>
#include <SDL3/SDL_scancode.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#define MAX_BODIES    128
#define MAX_PARTICLES 1024

/* ============================================================ */
/*  Rigid body wrapper                                          */
/* ============================================================ */
typedef struct Body {
    PhysicsBody3D *body;
    int            type;     /* 0 = cube, 1 = sphere */
    float          size;
    Color          color;
} Body;

/* ============================================================ */
/*  3D particle (very simple)                                   */
/* ============================================================ */
typedef struct Particle3D {
    Vector3 pos;
    Vector3 vel;
    float   life;
    float   maxLife;
    float   size;
    Color   startColor;
    Color   endColor;
} Particle3D;

typedef struct Particle3DSystem {
    Particle3D items[MAX_PARTICLES];
    int        count;
    Vector3    gravity;
} Particle3DSystem;

/* ============================================================ */
/*  Scene state                                                 */
/* ============================================================ */
typedef struct PhysicsScene {
    PhysicsWorld3D *world;

    Body            items[MAX_BODIES];
    int             count;

    SoftBody3D     *cloth;
    SoftBody3D     *softSphere;

    Particle3DSystem particles;

    Mesh            cubeMesh;
    Mesh            sphereMesh;
} PhysicsScene;

/* ============================================================ */
/*  Particles                                                   */
/* ============================================================ */
static void particle_spawn_burst(Particle3DSystem *ps, Vector3 at, int count,
                                 Color startColor, Color endColor,
                                 float speed, float life)
{
    for (int i = 0; i < count; ++i) {
        if (ps->count >= MAX_PARTICLES) return;
        Particle3D *p = &ps->items[ps->count++];

        float theta = ((float)rand() / RAND_MAX) * 6.2831853f;
        float phi   = acosf(1.0f - 2.0f * ((float)rand() / RAND_MAX));
        float s     = speed * (0.4f + 0.8f * ((float)rand() / RAND_MAX));

        p->pos = at;
        p->vel.x = sinf(phi) * cosf(theta) * s;
        p->vel.y = cosf(phi) * s + 2.0f;
        p->vel.z = sinf(phi) * sinf(theta) * s;

        p->maxLife = life * (0.6f + 0.8f * ((float)rand() / RAND_MAX));
        p->life    = p->maxLife;
        p->size    = 0.05f + 0.08f * ((float)rand() / RAND_MAX);
        p->startColor = startColor;
        p->endColor   = endColor;
    }
}

static void particle_update(Particle3DSystem *ps, float dt) {
    int w = 0;
    for (int i = 0; i < ps->count; ++i) {
        Particle3D *p = &ps->items[i];
        p->life -= dt;
        if (p->life <= 0.0f) continue;

        p->vel.x += ps->gravity.x * dt;
        p->vel.y += ps->gravity.y * dt;
        p->vel.z += ps->gravity.z * dt;

        p->pos.x += p->vel.x * dt;
        p->pos.y += p->vel.y * dt;
        p->pos.z += p->vel.z * dt;

        /* отскок от земли */
        if (p->pos.y < 0.05f) {
            p->pos.y = 0.05f;
            p->vel.y = -p->vel.y * 0.3f;
            p->vel.x *= 0.7f;
            p->vel.z *= 0.7f;
        }

        if (w != i) ps->items[w] = *p;
        ++w;
    }
    ps->count = w;
}

static void particle_draw(Particle3DSystem *ps) {
    for (int i = 0; i < ps->count; ++i) {
        Particle3D *p = &ps->items[i];
        float t = 1.0f - p->life / p->maxLife;
        Color c = {
            (unsigned char)(p->startColor.r + (p->endColor.r - p->startColor.r) * t),
            (unsigned char)(p->startColor.g + (p->endColor.g - p->startColor.g) * t),
            (unsigned char)(p->startColor.b + (p->endColor.b - p->startColor.b) * t),
            (unsigned char)(255.0f * (1.0f - t))
        };
        DrawCube(p->pos, (Vector3){p->size, p->size, p->size}, c);
    }
}

/* ============================================================ */
/*  Rigid helpers                                               */
/* ============================================================ */
static void scene_destroy_rigid(PhysicsScene *s) {
    for (int i = 0; i < s->count; ++i)
        DestroyPhysicsBody3D(s->items[i].body);
    s->count = 0;
}

static void scene_add_cube(PhysicsScene *s, Vector3 pos,
                           float half, Color c, float mass) {
    if (s->count >= MAX_BODIES) return;
    Body *b = &s->items[s->count];
    b->body = CreatePhysicsBody3D(s->world,
        PHYS_SHAPE_BOX3D, pos,
        (Vector3){ half, half, half },
        PHYS_BODY_DYNAMIC3D, mass, 0.6f, 0.15f);
    b->type  = 0;
    b->size  = half;
    b->color = c;
    s->count++;
}

static void scene_add_sphere(PhysicsScene *s, Vector3 pos,
                             float radius, Color c, float mass) {
    if (s->count >= MAX_BODIES) return;
    Body *b = &s->items[s->count];
    b->body = CreatePhysicsBody3D(s->world,
        PHYS_SHAPE_SPHERE3D, pos,
        (Vector3){ radius, 0, 0 },
        PHYS_BODY_DYNAMIC3D, mass, 0.6f, 0.35f);
    b->type  = 1;
    b->size  = radius;
    b->color = c;
    s->count++;
}

static void scene_populate(PhysicsScene *s) {
    scene_destroy_rigid(s);

    const Color palette[] = {
        {230,  41,  55, 255},
        {  0, 228,  48, 255},
        {  0, 121, 241, 255},
        {255, 203,   0, 255},
        {200, 122, 255, 255},
        {255, 109, 194, 255},
    };

    /* Пирамидка кубиков */
    const float S  = 0.5f;
    const float SP = 1.05f;
    for (int layer = 0; layer < 4; ++layer) {
        int n = 4 - layer;
        float y = S + 0.05f + layer * SP;
        for (int i = 0; i < n; ++i) {
            float x = (i - (n - 1) * 0.5f) * SP;
            scene_add_cube(s, (Vector3){ x, y, 0 },
                           S, palette[(layer * 2 + i) % 6], 1.0f);
        }
    }

    /* Падающие шары */
    scene_add_sphere(s, (Vector3){ -2.5f, 8.0f,  2.0f }, 0.55f,
                     (Color){255, 109, 194, 255}, 1.0f);
    scene_add_sphere(s, (Vector3){  2.8f, 9.5f, -1.5f }, 0.65f,
                     (Color){255, 203,   0, 255}, 1.0f);
}

static void scene_draw_rigid(PhysicsScene *s) {
    for (int i = 0; i < s->count; ++i) {
        Body *b = &s->items[i];
        Vector3    pos = GetPhysicsBody3DPosition(b->body);
        Quaternion q   = GetPhysicsBody3DRotation(b->body);

        Matrix m = MatrixMultiply(
            MatrixTranslate(pos.x, pos.y, pos.z),
            QuaternionToMatrix(q));

        if (b->type == 0) {
            Matrix full = MatrixMultiply(m,
                MatrixScale(b->size * 2, b->size * 2, b->size * 2));
            DrawMesh(s->cubeMesh, (Texture2D){0}, b->color, full);
        } else {
            Matrix full = MatrixMultiply(m,
                MatrixScale(b->size, b->size, b->size));
            DrawMesh(s->sphereMesh, (Texture2D){0}, b->color, full);
        }
    }
}

/* ============================================================ */
/*  Soft body drawing (wireframe)                               */
/* ============================================================ */
static void draw_soft_body_wireframe(SoftBody3D *sb, Color color) {
    if (!sb) return;
    int fc = GetSoftBody3DFaceCount(sb);
    for (int i = 0; i < fc; ++i) {
        Vector3 v0, v1, v2;
        GetSoftBody3DFace(sb, i, &v0, &v1, &v2);
        DrawLine3D(v0, v1, color);
        DrawLine3D(v1, v2, color);
        DrawLine3D(v2, v0, color);
    }
}

/* ============================================================ */
/*  Soft body setup                                             */
/* ============================================================ */
static void scene_setup_soft(PhysicsScene *s) {
    /* Ткань сверху — закрепим верхний край */
    s->cloth = CreateSoftBodyCloth(s->world,
        (Vector3){-3.0f, 7.0f, -1.5f},
        (Vector3){ 3.0f, 7.0f, -1.5f},
        (Vector3){-3.0f, 7.0f,  1.5f},
        (Vector3){ 3.0f, 7.0f,  1.5f},
        12, 12, 0.5f);

    int vc = GetSoftBody3DVertexCount(s->cloth);
    for (int i = 0; i < vc; ++i) {
        Vector3 p = GetSoftBody3DVertex(s->cloth, i);
        if (p.y > 6.9f) PinSoftBody3DVertex(s->cloth, i);
    }

    /* Мягкий шар — падает и отскакивает */
    s->softSphere = CreateSoftBodySphere(s->world,
        (Vector3){ 0.0f, 4.5f, 2.5f }, 0.8f, 12, 3.0f);
}

/* ============================================================ */
/*  Main                                                        */
/* ============================================================ */
int main(void) {
    if (!InitWindow(1280, 720, "vlcrlib — 3D Physics: rigid + soft + particles")) return 1;
    SetTargetFPS(60);

    Font font = {0};
#ifdef _WIN32
    font = LoadFontEx("C:/Windows/Fonts/segoeui.ttf", 32, 32, 96);
#endif

    PhysicsScene scene = {0};
    scene.world = CreatePhysicsWorld3D((Vector3){ 0, -9.81f, 0 });
    scene.particles.gravity = (Vector3){ 0, -9.81f, 0 };

    /* Ground plane */
    PhysicsBody3D *ground = CreatePhysicsBody3D(scene.world,
        PHYS_SHAPE_PLANE3D,
        (Vector3){ 0, 0, 0 },
        (Vector3){ 0, 1, 0 },
        PHYS_BODY_STATIC3D, 0.0f, 0.9f, 0.2f);

    scene.cubeMesh   = GenMeshCube(1, 1, 1);
    scene.sphereMesh = GenMeshSphere(1.0f, 20, 32);

    scene_populate(&scene);
    scene_setup_soft(&scene);

    Camera3D camera = {0};
    camera.target    = (Vector3){ 0, 3.0f, 0 };
    camera.up        = (Vector3){ 0, 1, 0 };
    camera.fovy      = 55.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane  = 200.0f;

    float camAngle  = 0.7f;
    float camHeight = 9.0f;
    float camDist   = 16.0f;
    bool  autoRotate = true;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 1.0f / 20.0f) dt = 1.0f / 20.0f;

        /* ---------- Camera ---------- */
        if (IsKeyDown(SDL_SCANCODE_LEFT))  { camAngle -= 1.2f * dt; autoRotate = false; }
        if (IsKeyDown(SDL_SCANCODE_RIGHT)) { camAngle += 1.2f * dt; autoRotate = false; }
        if (IsKeyDown(SDL_SCANCODE_UP))    camHeight += 5.0f * dt;
        if (IsKeyDown(SDL_SCANCODE_DOWN))  camHeight -= 5.0f * dt;
        if (camHeight < 1.0f)  camHeight = 1.0f;
        if (camHeight > 30.0f) camHeight = 30.0f;

        if (IsKeyDown(SDL_SCANCODE_EQUALS)) camDist -= 10.0f * dt;
        if (IsKeyDown(SDL_SCANCODE_MINUS))  camDist += 10.0f * dt;
        if (camDist < 3.0f)  camDist = 3.0f;
        if (camDist > 40.0f) camDist = 40.0f;

        if (autoRotate) camAngle += 0.15f * dt;

        camera.position.x = sinf(camAngle) * camDist;
        camera.position.z = cosf(camAngle) * camDist;
        camera.position.y = camHeight;

        /* ---------- Actions ---------- */
        if (IsKeyPressed(SDL_SCANCODE_R)) {
            scene_populate(&scene);
            DestroySoftBody3D(scene.cloth);
            DestroySoftBody3D(scene.softSphere);
            scene_setup_soft(&scene);
            scene.particles.count = 0;
        }

        if (IsKeyPressed(SDL_SCANCODE_SPACE)) {
            Vector3 dir = Vector3Normalize(
                Vector3Subtract(camera.target, camera.position));
            Vector3 spawn = Vector3Add(camera.position,
                Vector3Scale(dir, 1.5f));
            scene_add_sphere(&scene, spawn, 0.4f,
                             (Color){255, 255, 255, 255}, 2.0f);
            Body *b = &scene.items[scene.count - 1];
            SetPhysicsBody3DVelocity(b->body, Vector3Scale(dir, 25.0f));
        }

        if (IsMouseButtonPressed(1)) {
            Vector3 dir = Vector3Normalize(
                Vector3Subtract(camera.target, camera.position));
            PhysicsRayHit3D hit = RaycastPhysics3D(scene.world,
                camera.position, dir, 80.0f);
            if (hit.hit) {
                /* Взрыв частиц в точке попадания */
                particle_spawn_burst(&scene.particles, hit.point, 60,
                    (Color){255, 220, 100, 255},
                    (Color){230,  41,  55,   0},
                    6.0f, 1.6f);

                /* Импульс ближайшему rigid телу */
                int   nearest = -1;
                float bestD   = 1e9f;
                for (int i = 0; i < scene.count; ++i) {
                    Vector3 p = GetPhysicsBody3DPosition(scene.items[i].body);
                    float d = Vector3Distance(p, hit.point);
                    if (d < bestD) { bestD = d; nearest = i; }
                }
                if (nearest >= 0 && bestD < 2.0f) {
                    ApplyPhysicsBody3DImpulse(scene.items[nearest].body,
                        Vector3Scale(dir, 25.0f));
                }
            }
        }

        /* ---------- Physics ---------- */
        StepPhysicsWorld3D(scene.world, dt);
        particle_update(&scene.particles, dt);

        /* ---------- Draw ---------- */
        SetVulkanClearColor(15.0f / 255.0f, 18.0f / 255.0f, 28.0f / 255.0f, 1.0f);
        BeginDrawing();
        ClearBackground((Color){ 15, 18, 28, 255 });

        BeginMode3D(camera);
        {
            DrawGrid(24, 1.0f);

            scene_draw_rigid(&scene);

            draw_soft_body_wireframe(scene.cloth,
                (Color){102, 191, 255, 200});
            draw_soft_body_wireframe(scene.softSphere,
                (Color){255, 109, 194, 200});

            particle_draw(&scene.particles);
        }
        EndMode3D();

        /* ---------- HUD ---------- */
        if (font.ok) {
            char buf[160];
            float y = 20.0f;

            DrawTextEx(font, "3D Physics: Rigid + Soft + Particles",
                       (Vector2){ 20, y }, 24.0f, 1.0f,
                       (Color){ 245, 245, 250, 255 });
            y += 32.0f;

            snprintf(buf, sizeof(buf),
                     "Arrows: camera | +/-: zoom | Space: throw ball | "
                     "Click: explode + impulse | R: reset");
            DrawTextEx(font, buf, (Vector2){ 20, y }, 16.0f, 1.0f,
                       (Color){ 170, 180, 200, 255 });
            y += 24.0f;

            snprintf(buf, sizeof(buf),
                     "Rigid: %d | Cloth faces: %d | Soft sphere faces: %d | Particles: %d",
                     scene.count,
                     GetSoftBody3DFaceCount(scene.cloth),
                     GetSoftBody3DFaceCount(scene.softSphere),
                     scene.particles.count);
            DrawTextEx(font, buf, (Vector2){ 20, y }, 18.0f, 1.0f,
                       (Color){ 255, 109, 194, 255 });
        }

        EndDrawing();
    }

    /* ---------- Cleanup ---------- */
    DestroySoftBody3D(scene.cloth);
    DestroySoftBody3D(scene.softSphere);
    scene_destroy_rigid(&scene);
    DestroyPhysicsBody3D(ground);
    DestroyPhysicsWorld3D(scene.world);
    UnloadMesh(scene.cubeMesh);
    UnloadMesh(scene.sphereMesh);
    UnloadFont(font);
    CloseWindow();
    return 0;
}
