/* web_3d_example.c — WebGPU 3D demo (cube + sphere + grid) */
#include <vlcrlib.h>
#include "vplatform.h"
#include <stdio.h>
#include <math.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

static Camera3D cam;
static float t0;

static void frame(void) {
    if (WindowShouldClose()) {
#if defined(__EMSCRIPTEN__)
        emscripten_cancel_main_loop();
#endif
        return;
    }
    float t = (float)GetTime() - t0;

    cam.position.x = sinf(t * 0.4f) * 8.0f;
    cam.position.z = cosf(t * 0.4f) * 8.0f;
    cam.position.y = 3.5f + sinf(t * 0.5f) * 0.5f;
    cam.target = (Vector3){ 0, 0.5f, 0 };

    BeginDrawing();
    ClearBackground((Color){ 28, 30, 38, 255 });

    BeginMode3D(cam);
    DrawGrid(20, 1.0f);
    DrawCube((Vector3){ 0, 0.5f, 0 }, (Vector3){ 1, 1, 1 }, (Color){ 220, 120, 60, 255 });
    DrawCube((Vector3){ 2.2f, 0.4f, -1.0f }, (Vector3){ 0.8f, 0.8f, 0.8f }, (Color){ 80, 160, 220, 255 });
    DrawSphere((Vector3){ -2.0f, 0.8f, 1.0f }, 0.8f, (Color){ 100, 200, 120, 255 });
    DrawLine3D((Vector3){ 0, 0, 0 }, (Vector3){ 0, 2, 0 }, GREEN);
    EndMode3D();

    DrawText("vlcrlib WebGPU 3D", 20, 20, 28, WHITE);
    DrawText(VLCRLIB_PLATFORM_NAME, 20, 56, 18, LIGHTGRAY);
    DrawText(VLCRLIB_GRAPHICS_NAME, 20, 80, 18, LIGHTGRAY);
    EndDrawing();
}

int main(void) {
    if (!InitWindow(960, 540, "vlcrlib — WebGPU 3D")) return 1;
    SetTargetFPS(60);
    fprintf(stderr, "platform=%s graphics=%s\n", VLCRLIB_PLATFORM_NAME, VLCRLIB_GRAPHICS_NAME);

    vgraphics_SetDirectionalLight(true,
        (Vector3){ -0.35f, -0.8f, -0.4f },
        (Color){ 255, 250, 240, 255 }, 1.4f);
    vgraphics_SetAmbientLight(true, (Color){ 60, 70, 90, 255 }, 0.4f);

    cam = (Camera3D){0};
    cam.position = (Vector3){ 0, 3.5f, 8 };
    cam.target = (Vector3){ 0, 0.5f, 0 };
    cam.up = (Vector3){ 0, 1, 0 };
    cam.fovy = 50;
    cam.nearPlane = 0.1f;
    cam.farPlane = 200;
    t0 = (float)GetTime();

#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop(frame, 0, 1);
#else
    while (!WindowShouldClose()) frame();
    CloseWindow();
#endif
    return 0;
}
