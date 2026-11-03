/* cpu_2d_example.c — transparent engine path: same InitWindow/Draw* API on CPU. */
#include <vlcrlib.h>

int main(void) {
    if (!VRenderBackendSelect(VRENDER_BACKEND_CPU)) return 1;
    if (!InitWindow(800, 600, "vlcrlib — CPU 2D backend")) return 1;
    SetTargetFPS(60);

    float t = 0.0f;
    while (!WindowShouldClose()) {
        t += GetFrameTime();
        BeginDrawing();
        ClearBackground((Color){18, 20, 28, 255});
        DrawRectangle(40, 40, 720, 520, (Color){28, 34, 46, 255});
        DrawRectangleLines(40, 40, 720, 520, (Color){80, 100, 130, 255});
        DrawText("CPU backend: InitWindow / BeginDrawing / DrawText", 70, 80, 24,
                 (Color){230, 235, 245, 255});
        DrawCircleV((Vector2){220, 300}, 80.0f, (Color){220, 120, 70, 255});
        DrawRectanglePro((Rectangle){420, 220, 180, 160},
                         (Vector2){510, 300}, t * 0.7f,
                         (Color){80, 160, 220, 255});
        DrawText("No Vulkan. No WebGPU. Same public API.", 70, 440, 20,
                 (Color){160, 200, 170, 255});
        EndDrawing();
    }
    CloseWindow();
    return 0;
}
