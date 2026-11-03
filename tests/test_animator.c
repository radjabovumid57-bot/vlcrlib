#include "vanimator.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails = 0;
static float lastBlend = -1.0f;

static void expect(int condition, const char *name) {
    if (condition) printf("[PASS] %s\n", name);
    else { printf("[FAIL] %s\n", name); fails++; }
}

/* Render stubs: this test exercises the state machine, not the GPU. */
void DrawSkinnedModel(SkinnedModel model, Animation anim, float time,
                      Vector3 p, Vector3 r, Vector3 s, Color c) {
    (void)model; (void)anim; (void)time; (void)p; (void)r; (void)s; (void)c;
}
int GetSkinnedModelBoneCount(SkinnedModel model) { (void)model; return 1; }
bool GetAnimationPose(Animation animation, float time,
                      BonePose *out, int maxBones) {
    (void)animation; (void)time;
    if (out && maxBones > 0) {
        out[0].position = (Vector3){0,0,0};
        out[0].rotation = QuaternionIdentity();
        out[0].scale = (Vector3){1,1,1};
    }
    return true;
}
bool GetSkinnedModelRestPose(SkinnedModel model, BonePose *out, int maxBones) {
    (void)model;
    if (out && maxBones > 0) {
        out[0].position = (Vector3){0,0,0};
        out[0].rotation = QuaternionIdentity();
        out[0].scale = (Vector3){1,1,1};
    }
    return true;
}
void DrawSkinnedModelPose(SkinnedModel model, const BonePose *pose, int boneCount,
                          Vector3 p, Vector3 r, Vector3 s, Color c) {
    (void)model; (void)pose; (void)boneCount; (void)p; (void)r; (void)s; (void)c;
}
bool GetAnimationRootMotion(Animation animation, float fromTime, float toTime,
                            Vector3 *outDelta) {
    (void)animation; (void)fromTime; (void)toTime;
    if (outDelta) *outDelta = (Vector3){0.1f, 0.0f, 0.0f};
    return true;
}

void DrawSkinnedModelBlend(SkinnedModel model,
                           Animation from, float fromTime,
                           Animation to, float toTime, float blend,
                           Vector3 p, Vector3 r, Vector3 s, Color c) {
    (void)model; (void)from; (void)fromTime; (void)to; (void)toTime;
    (void)p; (void)r; (void)s; (void)c;
    lastBlend = blend;
}

int main(void) {
    SkinnedModel model = {1, 1, 1, 0, 1};
    Animator *a = CreateAnimator(model);
    expect(a != NULL, "create animator");

    Animation idle = {1, 1.0f, 1};
    Animation walk = {2, 1.0f, 1};
    expect(AnimatorAddState(a, "idle", idle, true), "add idle");
    expect(AnimatorAddState(a, "walk", walk, true), "add walk");
    expect(!AnimatorAddState(a, "", idle, true), "reject empty name");

    expect(!AnimatorPlay(a, "missing", 0.0f), "reject missing state");
    expect(AnimatorPlay(a, "idle", 0.0f), "play idle");
    expect(strcmp(AnimatorGetState(a), "idle") == 0, "current idle");

    AnimatorSetRootMotion(a, true);
    AnimatorUpdate(a, 0.25f);
    expect(fabsf(AnimatorGetTime(a) - 0.25f) < 1e-5f, "idle time advances");
    expect(fabsf(AnimatorGetRootMotion(a).x - 0.1f) < 1e-5f,
           "root motion delta exposed");

    expect(AnimatorPlay(a, "walk", 1.0f), "play walk with fade");
    expect(strcmp(AnimatorGetPreviousState(a), "idle") == 0, "previous idle");

    AnimatorUpdate(a, 0.5f);
    expect(fabsf(AnimatorGetBlend(a) - 0.5f) < 1e-4f, "blend midpoint");
    AnimatorDraw(a, (Vector3){0}, (Vector3){0}, (Vector3){1,1,1},
                 (Color){255,255,255,255});
    expect(fabsf(lastBlend - 0.5f) < 1e-4f, "draw uses blend");

    AnimatorUpdate(a, 0.5f);
    expect(AnimatorGetPreviousState(a)[0] == '\0', "fade completes");
    expect(AnimatorGetBlend(a) == 0.0f, "blend reset");

    /* Loop wrap: 1.0 s clip + 1.25 s update -> 0.25 s. */
    AnimatorUpdate(a, 1.25f);
    expect(fabsf(AnimatorGetTime(a) - 0.25f) < 1e-4f, "loop wraps");

    expect(AnimatorSetOverlay(a, "idle", 0.5f), "set overlay");
    AnimatorUpdate(a, 0.1f);
    AnimatorDraw(a, (Vector3){0}, (Vector3){0}, (Vector3){1,1,1},
                 (Color){255,255,255,255});
    expect(fabsf(lastBlend - 0.5f) < 1e-4f, "overlay weight reaches draw");
    AnimatorClearOverlay(a);
    expect(AnimatorGetOverlayWeight(a) == 0.0f, "clear overlay");

    expect(AnimatorPlay(a, "idle", 0.0f), "back to idle");
    expect(AnimatorAddTransition(a, "idle", "walk", "speed",
                                 ANIM_COND_GREATER, 0.5f, 0.2f),
           "add parameter transition");
    AnimatorSetFloat(a, "speed", 1.0f);
    AnimatorUpdate(a, 0.1f);
    expect(strcmp(AnimatorGetState(a), "walk") == 0,
           "transition fires on float parameter");
    expect(strcmp(AnimatorGetPreviousState(a), "idle") == 0,
           "transition leaves previous state");

    expect(AnimatorAddEvent(a, "idle", 0.5f, "footstep"), "add event");
    AnimatorPlay(a, "idle", 0.0f);
    AnimatorUpdate(a, 0.6f);
    const char *ev = AnimatorNextEvent(a);
    expect(ev && strcmp(ev, "footstep") == 0, "event fires at time");
    expect(AnimatorNextEvent(a) == NULL, "event queue drains");

    DestroyAnimator(a);
    printf("animator: %d failure(s)\n", fails);
    return fails ? 1 : 0;
}
