/* skinned_preview.c — fastgltf skinning smoke/preview. */
#include <vlcrlib.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (!InitWindow(960, 640, "vlcrlib skinned preview")) return 1;

    char path[1024] = {0};
    if (argc > 1 && argv[1][0]) {
        snprintf(path, sizeof(path), "%s", argv[1]);
    } else {
        const char *base = SDL_GetBasePath();
        if (base) snprintf(path, sizeof(path), "%sskinned_quad.glb", base);
        else snprintf(path, sizeof(path), "skinned_quad.glb");
    }

    SkinnedModel model = LoadSkinnedModel(path);
    Animation anim = {0};
    if (model.id && GetSkinnedModelAnimationCount(model) > 0)
        anim = GetSkinnedModelAnimation(model, 0);

    vgraphics_SetDirectionalLight(true,
        (Vector3){ -0.35f, -0.70f, -0.60f },
        (Color){ 255, 250, 240, 255 }, 3.0f);
    vgraphics_SetAmbientLight(true, (Color){ 90, 100, 120, 255 }, 0.2f);
    bool ibl = vgraphics_LoadIBL(NULL);
    vgraphics_SetIBLIntensity(ibl ? 1.0f : 0.0f);
    vgraphics_SetSkybox(0);
    SetGraphicsExposure(1.0f);

    Camera3D cam = {0};
    cam.position = (Vector3){ 0.6f, 1.4f, 3.4f };
    cam.target   = (Vector3){ 0.5f, 1.0f, 0.0f };
    cam.up       = (Vector3){ 0, 1, 0 };
    cam.fovy = 45.0f;
    cam.nearPlane = 0.05f;
    cam.farPlane = 100.0f;

    float t0 = (float)GetTime();
    while (!WindowShouldClose()) {
        float t = (float)GetTime() - t0;
        BeginDrawing();
        ClearBackground((Color){ 46, 46, 51, 255 });
        if (model.id && anim.id) {
            BeginMode3D(cam);
            DrawSkinnedModel(model, anim, t, (Vector3){0, 1, 0},
                             (Vector3){0, 0, 0}, (Vector3){1, 1, 1}, WHITE);
            EndMode3D();
            DrawText("Skinned model", 20, 16, 26, WHITE);
        } else {
            DrawText("SKINNED MODEL NOT LOADED", 20, 24, 24, RED);
            DrawText(path, 20, 60, 14, LIGHTGRAY);
        }
        EndDrawing();
    }

    if (model.id) UnloadSkinnedModel(model);
    CloseWindow();
    return 0;
}
