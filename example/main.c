#include "vlcrlib.h"

int main(void) {
    InitWindow(1280, 720, "vlcrlib example");

    Font font = LoadFont("C:/Windows/Fonts/segoeui.ttf");

    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground((Color){ 30, 30, 40, 255 });

        if (font.ok)
            DrawTextEx(font, "Hello, vlcrlib!",
                       (Vector2){ 40, 40 }, 32.0f, 1.0f, RAYWHITE);

        DrawCircleV((Vector2){ 200, 200 }, 40.0f, PINK);
        DrawRectangleRec((Rectangle){ 300, 200, 120, 80 }, SKYBLUE);

        EndDrawing();
    }

    UnloadFont(font);
    CloseWindow();
    return 0;
}