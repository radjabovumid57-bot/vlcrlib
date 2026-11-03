#include <vlcrlib.h>

int main(void) {
    if (!InitWindow(1280, 720, "vlcrlib — Vulkan shader ray tracing")) return 1;
    SetTargetFPS(60);

    float elapsed = 0.0f;
    while (!WindowShouldClose()) {
        elapsed += GetFrameTime();
        BeginDrawing();
        ClearBackground((Color){ 7, 9, 18, 255 });
        DrawRayTracedDemo(elapsed);
        DrawText(vgraphics_IsHardwareRayTracingAvailable()
                     ? "Vulkan hardware Ray Query: Cornell box, shadows and glass"
                     : "Vulkan software fallback: Cornell box and area-light shadows",
                 18, 18, 20, (Color){ 245, 245, 255, 255 });
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
