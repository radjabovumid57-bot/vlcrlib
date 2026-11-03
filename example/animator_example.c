/* animator_example.c — skinned two-bone asset + Animator crossfade. */
#include <vlcrlib.h>
#include <SDL3/SDL.h>
#include <stdio.h>

int main(void) {
    fprintf(stderr, "[animator] start\n");
    fflush(stderr);
    if (!InitWindow(1100, 700, "vlcrlib animator")) return 1;
    SetTargetFPS(60);

    fprintf(stderr, "[animator] loading skinned_anim.glb\n");
    fflush(stderr);
    SkinnedModel sk = LoadSkinnedModel("skinned_anim.glb");
    fprintf(stderr, "[animator] first load id=%u\n", sk.id);
    fflush(stderr);
    if (!sk.id) sk = LoadSkinnedModel("assets/skinned_anim.glb");
    fprintf(stderr, "[animator] load id=%u\n", sk.id);
    fflush(stderr);
    if (!sk.id) {
        fprintf(stderr, "animator_example: skinned_anim.glb not found\n");
        CloseWindow();
        return 1;
    }

    Animator *anim = CreateAnimator(sk);
    int clips = GetSkinnedModelAnimationCount(sk);
    for (int i = 0; i < clips; ++i) {
        char name[64];
        snprintf(name, sizeof(name), "%s",
                 GetSkinnedModelAnimationName(sk, i));
        AnimatorAddState(anim, name, GetSkinnedModelAnimation(sk, i), true);
    }

    fprintf(stderr, "[animator] clips=%d bones=%d\n", clips,
            GetSkinnedModelBoneCount(sk));

    const char *idle = clips > 0 ? GetSkinnedModelAnimationName(sk, 0) : "Idle";
    for (int i = 0; i < clips; ++i) {
        const char *name = GetSkinnedModelAnimationName(sk, i);
        if (SDL_strcasecmp(name, "Idle") == 0) { idle = name; break; }
    }
    AnimatorPlay(anim, idle, 0.0f);
    fprintf(stderr, "[animator] playing '%s'\n", idle);

    vgraphics_SetDirectionalLight(true,
        (Vector3){-0.3f, -0.7f, -0.5f}, WHITE, 3.0f);
    vgraphics_SetAmbientLight(true, (Color){90, 100, 120, 255}, 0.2f);
    vgraphics_LoadIBL(NULL);
    vgraphics_SetIBLIntensity(1.0f);
    SetPostProcess(1);
    SetTAA(1);

    Camera3D cam = {0};
    cam.position = (Vector3){2.2f, 1.4f, 3.2f};
    cam.target = (Vector3){0, 0.6f, 0};
    cam.up = (Vector3){0, 1, 0};
    cam.fovy = 45.0f;
    cam.nearPlane = 0.05f;
    cam.farPlane = 100.0f;

    Vector3 rootPos = {0, 0, 0};
    AnimatorSetRootMotion(anim, true);
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (IsKeyPressed(SDL_SCANCODE_1) && clips > 0)
            AnimatorPlay(anim, GetSkinnedModelAnimationName(sk, 0), 0.3f);
        if (IsKeyPressed(SDL_SCANCODE_2) && clips > 1)
            AnimatorPlay(anim, GetSkinnedModelAnimationName(sk, 1), 0.3f);
        AnimatorUpdate(anim, dt);
        rootPos = Vector3Add(rootPos, AnimatorGetRootMotion(anim));

        BeginDrawing();
        ClearBackground((Color){35, 38, 46, 255});
        BeginMode3D(cam);
        AnimatorDraw(anim, rootPos, (Vector3){0,0,0},
                     (Vector3){1,1,1}, WHITE);
        EndMode3D();

        DrawText("Animator  —  1: Idle   2: Walk", 20, 16, 18, WHITE);
        DrawText(AnimatorGetState(anim), 20, 42, 16, LIGHTGRAY);
        char blend[96];
        snprintf(blend, sizeof(blend), "prev: %s  blend: %.2f",
                 AnimatorGetPreviousState(anim), AnimatorGetBlend(anim));
        DrawText(blend, 20, 64, 14, LIGHTGRAY);
        EndDrawing();
    }

    DestroyAnimator(anim);
    UnloadSkinnedModel(sk);
    CloseWindow();
    return 0;
}
