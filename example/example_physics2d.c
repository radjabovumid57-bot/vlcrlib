#include <vlcrlib.h>

#define BOX_COUNT 8

static Vector2 screen_point(Vector2 world) {
    return (Vector2){640.0f + world.x * 55.0f, 620.0f - world.y * 55.0f};
}

static void draw_body(PhysicsBody2D *body, Color color) {
    Vector2 center = screen_point(GetPhysicsBody2DPosition(body));
    float angle = -GetPhysicsBody2DRotation(body);
    float size = 44.0f;
    float c = cosf(angle);
    float s = sinf(angle);
    Vector2 corners[4] = {
        {-size, -size}, {size, -size}, {size, size}, {-size, size}
    };

    for (int i = 0; i < 4; ++i) {
        float x = corners[i].x;
        float y = corners[i].y;
        corners[i] = (Vector2){
            center.x + x * c - y * s,
            center.y + x * s + y * c
        };
    }

    DrawTriangle(corners[0], corners[1], corners[2], color);
    DrawTriangle(corners[0], corners[2], corners[3], color);
    DrawLineEx(corners[0], corners[1], 2.0f, WHITE);
    DrawLineEx(corners[1], corners[2], 2.0f, WHITE);
    DrawLineEx(corners[2], corners[3], 2.0f, WHITE);
    DrawLineEx(corners[3], corners[0], 2.0f, WHITE);
}

int main(void) {
    if (!InitWindow(1280, 720, "vlcrlib 2D physics")) return 1;
    SetTargetFPS(60);

    PhysicsWorld2D *world = CreatePhysicsWorld2D((Vector2){0.0f, -9.8f});
    PhysicsBody2D *ground = CreatePhysicsBody2D(
        world, PHYS_SHAPE_BOX2D, (Vector2){0.0f, -0.8f},
        (Vector2){10.0f, 0.5f}, PHYS_BODY_STATIC2D,
        0.0f, 0.7f, 0.2f);

    PhysicsBody2D *boxes[BOX_COUNT];
    for (int i = 0; i < BOX_COUNT; ++i) {
        boxes[i] = CreatePhysicsBody2D(
            world, PHYS_SHAPE_BOX2D,
            (Vector2){(i % 3 - 1) * 1.8f, 1.0f + i * 1.1f},
            (Vector2){0.75f, 0.75f}, PHYS_BODY_DYNAMIC2D,
            1.0f, 0.5f, 0.25f);
    }

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;
        StepPhysicsWorld2D(world, dt);

        BeginDrawing();
        ClearBackground((Color){22, 27, 38, 255});
        DrawText("vlcrlib 2D physics", 32, 28, 28, WHITE);
        DrawText("Bullet-free Box2D simulation", 34, 62, 18, LIGHTGRAY);

        BeginMode2D((Camera2D){0});
        Vector2 groundCenter = screen_point(GetPhysicsBody2DPosition(ground));
        DrawRectangle((int)(groundCenter.x - 550), (int)(groundCenter.y - 28),
                      1100, 56, (Color){70, 85, 110, 255});
        DrawRectangleLines((int)(groundCenter.x - 550), (int)(groundCenter.y - 28),
                           1100, 56, WHITE);

        for (int i = 0; i < BOX_COUNT; ++i) {
            Color color = {
                (unsigned char)(80 + i * 18),
                (unsigned char)(180 - i * 10),
                (unsigned char)(230 - i * 12),
                255
            };
            draw_body(boxes[i], color);
        }

        Vector2 rayOrigin = (Vector2){-8.0f, 8.0f};
        Vector2 rayDirection = (Vector2){1.0f, -0.7f};
        PhysicsRayHit2D hit = RaycastPhysics2D(world, rayOrigin, rayDirection, 20.0f);
        Vector2 rayStart = screen_point(rayOrigin);
        Vector2 rayEnd = screen_point(Vector2Add(rayOrigin, Vector2Scale(Vector2Normalize(rayDirection), 20.0f)));
        DrawLineEx(rayStart, rayEnd, 2.0f, hit.hit ? GOLD : DARKGRAY);
        if (hit.hit) DrawCircleV(screen_point(hit.point), 7.0f, GOLD);
        EndMode2D();

        EndDrawing();
    }

    for (int i = 0; i < BOX_COUNT; ++i) DestroyPhysicsBody2D(boxes[i]);
    DestroyPhysicsBody2D(ground);
    DestroyPhysicsWorld2D(world);
    CloseWindow();
    return 0;
}
