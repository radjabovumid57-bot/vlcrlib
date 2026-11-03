/*
 * scene3d_example.c — пункт 1: пример 3D-сцены (raylib-стиль)
 *
 * Desktop (Vulkan):  build\example\Release\scene3d_example.exe
 * Web (WebGPU):      build-web/example/scene3d_example.html
 *
 * Сцена: пол-сетка, «здание» из кубов, сфера, оси, орбитальная камера.
 */
#include <vlcrlib.h>
#include "vplatform.h"
#include <stdio.h>
#include <math.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

static Camera3D cam;
static float t0;
static float yaw = 0.6f;
static float pitch = 0.35f;
static float dist = 10.0f;

static Vector2 prevMouse;
static int hasPrevMouse;

static void update_camera(float dt) {
    /* мышь: зажать ЛКМ и двигать — орбита */
    Vector2 m = GetMousePosition();
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && hasPrevMouse) {
        float dx = m.x - prevMouse.x;
        float dy = m.y - prevMouse.y;
        yaw   -= dx * 0.005f;
        pitch += dy * 0.005f;
        if (pitch > 1.4f) pitch = 1.4f;
        if (pitch < 0.05f) pitch = 0.05f;
    }
    prevMouse = m;
    hasPrevMouse = 1;
    float wheel = GetMouseWheelMove();
    if (wheel != 0.f) {
        dist *= (wheel > 0 ? 0.9f : 1.1f);
        if (dist < 3.f) dist = 3.f;
        if (dist > 40.f) dist = 40.f;
    }
    (void)dt;

    Vector3 target = { 0, 0.8f, 0 };
    cam.target = target;
    cam.position.x = target.x + dist * cosf(pitch) * sinf(yaw);
    cam.position.y = target.y + dist * sinf(pitch);
    cam.position.z = target.z + dist * cosf(pitch) * cosf(yaw);
    cam.up = (Vector3){ 0, 1, 0 };
}

static void draw_scene(float t) {
    /* Пол */
    DrawGrid(24, 1.0f);

    /* Оси в начале координат */
    DrawLine3D((Vector3){0,0,0}, (Vector3){2,0,0}, RED);
    DrawLine3D((Vector3){0,0,0}, (Vector3){0,2,0}, GREEN);
    DrawLine3D((Vector3){0,0,0}, (Vector3){0,0,2}, BLUE);

    /* «Здание»: основание + этажи */
    DrawCube((Vector3){ 0, 0.15f, 0 }, (Vector3){ 6, 0.3f, 4 }, (Color){ 70, 75, 85, 255 });
    DrawCube((Vector3){ 0, 1.0f, 0 }, (Vector3){ 5, 1.4f, 3.2f }, (Color){ 180, 140, 100, 255 });
    DrawCube((Vector3){ 0, 2.2f, 0 }, (Vector3){ 4.2f, 1.0f, 2.6f }, (Color){ 160, 120, 90, 255 });
    DrawCube((Vector3){ 0, 3.1f, 0 }, (Vector3){ 1.2f, 0.8f, 1.2f }, (Color){ 120, 90, 70, 255 }); /* башня */

    /* Колонны */
    for (int i = -1; i <= 1; i += 2) {
        DrawCube((Vector3){ (float)i * 2.2f, 0.9f, 1.4f }, (Vector3){ 0.35f, 1.6f, 0.35f },
                 (Color){ 200, 190, 170, 255 });
        DrawCube((Vector3){ (float)i * 2.2f, 0.9f, -1.4f }, (Vector3){ 0.35f, 1.6f, 0.35f },
                 (Color){ 200, 190, 170, 255 });
    }

    /* Прыгающая сфера */
    float bob = 0.8f + fabsf(sinf(t * 2.0f)) * 1.2f;
    DrawSphere((Vector3){ 3.5f, bob, 0 }, 0.55f, (Color){ 90, 190, 220, 255 });

    /* Вращающийся куб */
    float ang = t * 1.2f;
    Vector3 cpos = { -3.5f, 1.0f + 0.2f * sinf(t), 0 };
    /* аппроксимация вращения: просто куб (полный quat — позже) */
    DrawCube(cpos, (Vector3){ 0.9f, 0.9f, 0.9f }, (Color){ 230, 100, 80, 255 });
    (void)ang;
}

static void frame(void) {
    if (WindowShouldClose()) {
#if defined(__EMSCRIPTEN__)
        emscripten_cancel_main_loop();
#endif
        return;
    }

    float dt = GetFrameTime();
    if (dt > 0.05f) dt = 0.05f;
    float t = (float)GetTime() - t0;
    update_camera(dt);

    BeginDrawing();
    ClearBackground((Color){ 24, 26, 34, 255 });

    BeginMode3D(cam);
    draw_scene(t);
    EndMode3D();

    DrawText("3D scene — vlcrlib", 20, 16, 28, WHITE);
    DrawText("LMB drag: orbit | wheel: zoom", 20, 52, 18, LIGHTGRAY);
    DrawText(VLCRLIB_PLATFORM_NAME, 20, 78, 16, (Color){140,150,170,255});
    DrawText(VLCRLIB_GRAPHICS_NAME, 120, 78, 16, (Color){140,150,170,255});
    EndDrawing();
}

int main(void) {
    if (!InitWindow(1280, 720, "vlcrlib — 3D Scene")) return 1;
    SetTargetFPS(60);

    vgraphics_SetDirectionalLight(true,
        (Vector3){ -0.4f, -0.85f, -0.35f },
        (Color){ 255, 248, 235, 255 }, 1.35f);
    vgraphics_SetAmbientLight(true, (Color){ 55, 65, 85, 255 }, 0.38f);

    cam = (Camera3D){0};
    cam.fovy = 50.f;
    cam.nearPlane = 0.1f;
    cam.farPlane = 250.f;
    t0 = (float)GetTime();
    update_camera(0.f);

    fprintf(stderr, "[scene3d] platform=%s graphics=%s\n",
            VLCRLIB_PLATFORM_NAME, VLCRLIB_GRAPHICS_NAME);

#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop(frame, 0, 1);
#else
    while (!WindowShouldClose()) frame();
    CloseWindow();
#endif
    return 0;
}
