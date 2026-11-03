/* web_hello.c — minimal cross-platform / web sample */
#include <vlcrlib.h>
#include "vplatform.h"
#include <stdio.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
static int g_running = 1;
static void frame(void *arg) {
    (void)arg;
    if (WindowShouldClose()) {
        g_running = 0;
        emscripten_cancel_main_loop();
        return;
    }
    BeginDrawing();
    ClearBackground((Color){ 32, 36, 48, 255 });
    DrawText("vlcrlib — Web / cross-platform", 24, 24, 28, WHITE);
    DrawText(VLCRLIB_PLATFORM_NAME, 24, 64, 20, LIGHTGRAY);
    DrawText(VLCRLIB_GRAPHICS_NAME, 24, 92, 20, LIGHTGRAY);
    DrawRectangle(24, 140, 200, 40, (Color){ 80, 140, 220, 255 });
    EndDrawing();
}
#endif

int main(void) {
    if (!InitWindow(960, 540, "vlcrlib web_hello")) return 1;
    SetTargetFPS(60);
    fprintf(stderr, "platform=%s graphics=%s\n", VLCRLIB_PLATFORM_NAME, VLCRLIB_GRAPHICS_NAME);

#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop_arg(frame, NULL, 0, 1);
#else
    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground((Color){ 32, 36, 48, 255 });
        DrawText("vlcrlib — desktop", 24, 24, 28, WHITE);
        DrawText(VLCRLIB_PLATFORM_NAME, 24, 64, 20, LIGHTGRAY);
        DrawText(VLCRLIB_GRAPHICS_NAME, 24, 92, 20, LIGHTGRAY);
        EndDrawing();
    }
    CloseWindow();
#endif
    return 0;
}
