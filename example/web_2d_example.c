/* web_2d_example.c — 2D shapes + textures (WebGPU / Vulkan) */
#include <vlcrlib.h>
#include "vplatform.h"
#include <stdio.h>
#include <math.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

static float t0;
static Texture2D logo; /* optional */

static void frame(void) {
    if (WindowShouldClose()) {
#if defined(__EMSCRIPTEN__)
        emscripten_cancel_main_loop();
#endif
        return;
    }
    float t = (float)GetTime() - t0;
    int w = 960;
    int h = 540;

    BeginDrawing();
    ClearBackground((Color){ 28, 30, 38, 255 });

    /* filled shapes */
    DrawRectangle(40, 40, 180, 100, (Color){ 220, 100, 70, 255 });
    DrawRectangleLines(40, 40, 180, 100, WHITE);
    DrawCircle(320, 90, 50, (Color){ 80, 170, 220, 255 });
    DrawCircleLines(320, 90, 50, WHITE);
    DrawTriangle(
        (Vector2){ 420, 140 },
        (Vector2){ 520, 140 },
        (Vector2){ 470, 40 },
        (Color){ 100, 200, 120, 255 });

    /* lines */
    for (int i = 0; i < 12; ++i) {
        float a = t * 2.f + i * 0.5f;
        DrawLineEx(
            (Vector2){ 600, 90 },
            (Vector2){ 600 + cosf(a) * 70, 90 + sinf(a) * 70 },
            3.f,
            (Color){ 255, 200, 80, 255 });
    }

    /* animated bar */
    float pulse = 0.5f + 0.5f * sinf(t * 3.f);
    DrawRectangle(40, 180, (int)(300 * pulse), 24, (Color){ 140, 100, 220, 255 });
    DrawRectangleLines(40, 180, 300, 24, LIGHTGRAY);

    /* grid of pixels / small rects */
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 16; ++x)
            DrawRectangle(40 + x * 18, 230 + y * 18, 14, 14,
                (Color){ (unsigned char)(40 + x * 12), (unsigned char)(80 + y * 15), 160, 255 });

    if (logo.id)
        DrawTextureEx(logo, (Vector2){ (float)(w - logo.width - 40), 40 },
                      t * 20.f, 1.0f, WHITE);

    DrawText("vlcrlib 2D", 40, h - 80, 32, WHITE);
    DrawText("rect / circle / triangle / line / texture", 40, h - 44, 18, LIGHTGRAY);
    DrawText(VLCRLIB_GRAPHICS_NAME, w - 160, h - 40, 18, (Color){140,150,170,255});
    EndDrawing();
}

int main(void) {
    if (!InitWindow(960, 540, "vlcrlib — 2D")) return 1;
    SetTargetFPS(60);
    t0 = (float)GetTime();
    logo = LoadTexture("logo.png"); /* optional */

#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop(frame, 0, 1);
#else
    while (!WindowShouldClose()) frame();
    if (logo.id) UnloadTexture(logo);
    CloseWindow();
#endif
    return 0;
}
