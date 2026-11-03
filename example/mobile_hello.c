/* mobile_hello.c — minimal Android/iOS SDL entry point. */
#include <vlcrlib.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    if (!InitWindow(720, 1280, "vlcrlib mobile")) {
        fprintf(stderr, "[mobile] InitWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    SetTargetFPS(60);
    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground((Color){24, 28, 38, 255});
        DrawText("vlcrlib mobile", 32, 64, 40, WHITE);
        DrawText("SDL3 + Vulkan/MoltenVK", 32, 120, 24, LIGHTGRAY);
        EndDrawing();
    }
    CloseWindow();
    return 0;
}
