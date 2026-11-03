/* audio3d_example.c — positional audio via the miniaudio mixer. */
#include <vlcrlib.h>
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>

int main(int argc, char **argv) {
    if (!InitAudio3D()) {
        fprintf(stderr, "[audio3d] no audio device / miniaudio disabled\n");
        return 0;
    }
    const char *path = argc > 1 ? argv[1] : "beep.wav";
    LoadReverbIR(argc > 2 ? argv[2] : "ir.wav", 0.35f);
    Sound3D s = LoadSound3D(path);
    if (!s.id) {
        fprintf(stderr, "[audio3d] cannot load %s\n", path);
        CloseAudio3D();
        return 1;
    }
    SetSound3DLooping(s, true);
    SetSound3DVolume(s, 0.4f);
    SetSound3DReverb(s, 0.35f);
    Sound3D music = LoadMusic3D(path);
    if (music.id) {
        SetSound3DVolume(music, 0.25f);
        SetSound3DLooping(music, true);
        PlaySound3D(music, (Vector3){0, 0, 0});
    }
    SetListener3D((Vector3){0, 0, 0}, (Vector3){0, 0, -1}, (Vector3){0, 1, 0});
    PlaySound3D(s, (Vector3){2, 0, -4});

    if (!InitWindow(800, 600, "vlcrlib — 3D audio")) {
        UnloadSound3D(s);
        CloseAudio3D();
        return 1;
    }
    SetTargetFPS(60);
    float t = 0;
    while (!WindowShouldClose()) {
        t += GetFrameTime();
        Vector3 pos = { cosf(t) * 3.0f, 0.5f, sinf(t) * 3.0f - 4.0f };
        SetSound3DPosition(s, pos);
        SetListener3D((Vector3){0, 1, 0}, (Vector3){0, 0, -1}, (Vector3){0, 1, 0});
        if (music.id && IsKeyPressed(SDL_SCANCODE_SPACE)) {
            if (IsSound3DPlaying(music)) PauseSound3D(music);
            else ResumeSound3D(music);
        }
        BeginDrawing();
        ClearBackground((Color){30, 34, 42, 255});
        DrawText("3D audio — source orbits around the listener", 20, 20, 20, WHITE);
        DrawText(IsSound3DPlaying(s) ? "playing" : "stopped", 20, 50, 16, LIGHTGRAY);
        DrawText("Space: pause/resume music", 20, 74, 16, LIGHTGRAY);
        EndDrawing();
    }
    UnloadSound3D(s);
    if (music.id) UnloadSound3D(music);
    CloseAudio3D();
    CloseWindow();
    return 0;
}
