#include <vlcrlib.h>

int main(void) {
    if (!InitWindow(1280, 720, "vlcrlib draw shapes")) return 1;
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground((Color){18, 22, 32, 255});

        Camera2D camera = {0};
        camera.zoom = 1.0f;
        BeginMode2D(camera);

        DrawText("vlcrlib shape drawing", 32, 24, 28, WHITE);

        DrawPixel(40, 100, YELLOW);
        DrawLine(80, 100, 220, 100, GREEN);
        DrawLineEx((Vector2){80, 130}, (Vector2){220, 160}, 5.0f, LIME);

        DrawCircle(320, 135, 55.0f, BLUE);
        DrawCircleV((Vector2){460, 135}, 55.0f, SKYBLUE);
        DrawRectangle(560, 80, 130, 100, ORANGE);
        DrawRectangleRec((Rectangle){735, 80, 130, 100}, RED);
        DrawRectanglePro((Rectangle){920, 130, 150, 90},
                         (Vector2){75, 45}, 15.0f, PURPLE);
        DrawRectangleLines(1100, 80, 130, 100, WHITE);

        DrawTriangle((Vector2){150, 320}, (Vector2){90, 440},
                     (Vector2){210, 440}, GOLD);
        DrawPoly((Vector2){360, 380}, 6, 70.0f, 30.0f, PINK);

        DrawBezierQuad((Vector2){500, 440}, (Vector2){620, 250},
                       (Vector2){740, 440}, 4.0f, GREEN, 32);
        DrawBezierCubic((Vector2){800, 440}, (Vector2){860, 230},
                        (Vector2){1060, 580}, (Vector2){1180, 320},
                        4.0f, GOLD, 32);

        EndMode2D();

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
