/* golden_skinned_capture.c — deterministic skinned + TAA golden frame. */
#include <vlcrlib.h>
#include <SDL3/SDL.h>
#include "vmath.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : "golden_skinned.png";
    const char *modelPath = argc > 2 ? argv[2] : "skinned_anim.glb";
    if (!InitWindow(800, 600, "vlcrlib — golden skinned")) return 1;
    SetTargetFPS(60);

    SkinnedModel sk = LoadSkinnedModel(modelPath);
    if (!sk.id) {
        fprintf(stderr, "[golden] cannot load %s\n", modelPath);
        CloseWindow();
        return 1;
    }
    int clips = GetSkinnedModelAnimationCount(sk);
    Animator *an = CreateAnimator(sk);
    for (int i = 0; i < clips; ++i)
        AnimatorAddState(an, GetSkinnedModelAnimationName(sk, i),
                         GetSkinnedModelAnimation(sk, i), true);
    if (clips > 0)
        AnimatorPlay(an, GetSkinnedModelAnimationName(sk, 0), 0.0f);

    vgraphics_SetDirectionalLight(true, (Vector3){-0.3f, -0.7f, -0.5f},
                                  WHITE, 2.0f);
    vgraphics_SetAmbientLight(true, (Color){90, 100, 120, 255}, 0.2f);
    vgraphics_LoadIBL(NULL);
    vgraphics_SetIBLIntensity(0.8f);
    SetPostProcess(1);
    SetTAA(1);

    Camera3D cam = {0};
    cam.position = (Vector3){2.2f, 1.4f, 3.2f};
    cam.target = (Vector3){0, 0.6f, 0};
    cam.up = (Vector3){0, 1, 0};
    cam.fovy = 45.0f;
    cam.nearPlane = 0.05f;
    cam.farPlane = 100.0f;

    const float dt = 1.0f / 60.0f;
    for (int frame = 0; frame < 80; ++frame) {
        AnimatorUpdate(an, dt);
        BeginDrawing();
        ClearBackground((Color){30, 34, 42, 255});
        BeginMode3D(cam);
        AnimatorDraw(an, (Vector3){0, 0, 0}, (Vector3){0, 0, 0},
                     (Vector3){1, 1, 1}, WHITE);
        EndMode3D();
        DrawText("golden skinned + TAA", 20, 20, 20, WHITE);
        EndDrawing();
        if (frame == 60) {
            if (!vgraphics_CaptureScreenshot(out))
                fprintf(stderr, "[golden] capture failed\n");
            else
                fprintf(stderr, "[golden] wrote %s\n", out);
        }
    }

    DestroyAnimator(an);
    UnloadSkinnedModel(sk);
    CloseWindow();
    return 0;
}
