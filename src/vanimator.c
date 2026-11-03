#include "vanimator.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ANIMATOR_MAX_STATES 32
#define ANIMATOR_MAX_BONES 256
#define ANIMATOR_MAX_PARAMS 32
#define ANIMATOR_MAX_TRANSITIONS 32
#define ANIMATOR_MAX_EVENTS 32
#define ANIMATOR_MAX_PENDING 16

typedef struct AnimatorEvent {
    char  state[64];
    char  name[64];
    float time;
} AnimatorEvent;

typedef struct AnimatorParam {
    char  name[32];
    float value;
    int   trigger;
} AnimatorParam;

typedef struct AnimatorTransition {
    char               from[64];
    char               to[64];
    char               parameter[32];
    AnimatorConditionOp op;
    float              threshold;
    float              fade;
} AnimatorTransition;

typedef struct AnimatorState {
    char      name[64];
    Animation clip;
    bool      loop;
    float     time;
} AnimatorState;

struct Animator {
    SkinnedModel  model;
    AnimatorState states[ANIMATOR_MAX_STATES];
    int           stateCount;
    int           current;
    int           previous;
    float         fadeTime;
    float         fadeDuration;
    bool          rootMotionEnabled;
    Vector3       rootMotion;
    AnimatorParam params[ANIMATOR_MAX_PARAMS];
    int           paramCount;
    AnimatorTransition transitions[ANIMATOR_MAX_TRANSITIONS];
    int           transitionCount;
    AnimatorEvent  events[ANIMATOR_MAX_EVENTS];
    int            eventCount;
    char           pending[ANIMATOR_MAX_PENDING][64];
    int            pendingCount;
    int           overlay;
    float         overlayWeight;
    float         overlayTime;
    bool          overlayAdditive;
};

static float animatorClamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void animatorAdvance(AnimatorState *state, float dt) {
    if (!state) return;
    float duration = state->clip.duration;
    if (duration <= 0.0001f) return;
    state->time += dt;
    if (state->loop) {
        state->time = fmodf(state->time, duration);
        if (state->time < 0.0f) state->time += duration;
    } else if (state->time > duration) {
        state->time = duration;
    }
}

Animator *CreateAnimator(SkinnedModel model) {
    if (model.id == 0) return NULL;
    Animator *a = (Animator *)calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->model = model;
    a->current = -1;
    a->previous = -1;
    a->overlay = -1;
    return a;
}

void DestroyAnimator(Animator *animator) {
    free(animator);
}

bool AnimatorAddState(Animator *animator, const char *name,
                      Animation clip, bool loop) {
    if (!animator || !name || !name[0] || clip.id == 0) return false;
    if (animator->stateCount >= ANIMATOR_MAX_STATES) return false;
    AnimatorState *s = &animator->states[animator->stateCount];
    snprintf(s->name, sizeof(s->name), "%s", name);
    s->clip = clip;
    s->loop = loop;
    s->time = 0.0f;
    animator->stateCount++;
    return true;
}

static int animatorFind(const Animator *a, const char *name) {
    for (int i = 0; i < a->stateCount; ++i)
        if (strcmp(a->states[i].name, name) == 0) return i;
    return -1;
}

static AnimatorParam *animatorParam(Animator *a, const char *name, bool create) {
    if (!a || !name || !name[0]) return NULL;
    for (int i = 0; i < a->paramCount; ++i)
        if (strcmp(a->params[i].name, name) == 0) return &a->params[i];
    if (!create || a->paramCount >= ANIMATOR_MAX_PARAMS) return NULL;
    AnimatorParam *p = &a->params[a->paramCount++];
    memset(p, 0, sizeof(*p));
    snprintf(p->name, sizeof(p->name), "%s", name);
    return p;
}

static bool animatorConditionMet(Animator *a, const AnimatorTransition *t) {
    AnimatorParam *p = animatorParam(a, t->parameter, false);
    float v = p ? p->value : 0.0f;
    bool ok = false;
    switch (t->op) {
        case ANIM_COND_ALWAYS:    ok = true; break;
        case ANIM_COND_GREATER:   ok = v > t->threshold; break;
        case ANIM_COND_LESS:      ok = v < t->threshold; break;
        case ANIM_COND_EQUAL:     ok = fabsf(v - t->threshold) < 1e-5f; break;
        case ANIM_COND_NOT_EQUAL: ok = fabsf(v - t->threshold) >= 1e-5f; break;
        case ANIM_COND_TRUE:      ok = v != 0.0f; break;
        case ANIM_COND_FALSE:     ok = v == 0.0f; break;
        case ANIM_COND_TRIGGER:   ok = p && p->trigger; break;
        default: break;
    }
    if (ok && t->op == ANIM_COND_TRIGGER && p) p->trigger = 0;
    return ok;
}

bool AnimatorPlay(Animator *animator, const char *state, float fadeSeconds) {
    if (!animator || !state) return false;
    int idx = animatorFind(animator, state);
    if (idx < 0) return false;
    if (idx == animator->current) return true;

    if (animator->current >= 0 && fadeSeconds > 0.0001f) {
        animator->previous = animator->current;
        animator->fadeTime = 0.0f;
        animator->fadeDuration = fadeSeconds;
    } else {
        animator->previous = -1;
        animator->fadeTime = 0.0f;
        animator->fadeDuration = 0.0f;
    }
    animator->current = idx;
    animator->states[idx].time = 0.0f;
    animator->rootMotion = (Vector3){0};
    return true;
}

void AnimatorUpdate(Animator *animator, float dt) {
    if (!animator || animator->current < 0) return;
    if (dt < 0.0f) dt = 0.0f;

    AnimatorState *cur = &animator->states[animator->current];
    float previousTime = cur->time;
    animatorAdvance(cur, dt);
    if (animator->overlay >= 0 && animator->overlayWeight > 0.0f) {
        AnimatorState *ov = &animator->states[animator->overlay];
        float dur = ov->clip.duration;
        animator->overlayTime += dt;
        if (dur > 0.0001f) {
            if (ov->loop) {
                animator->overlayTime = fmodf(animator->overlayTime, dur);
                if (animator->overlayTime < 0.0f) animator->overlayTime += dur;
            } else if (animator->overlayTime > dur) {
                animator->overlayTime = dur;
            }
        }
    }
    animator->rootMotion = (Vector3){0};
    if (animator->rootMotionEnabled && cur->clip.duration > 0.0001f) {
        Vector3 delta = {0};
        if (cur->loop && cur->time < previousTime) {
            Vector3 a = {0}, b = {0};
            GetAnimationRootMotion(cur->clip, previousTime, cur->clip.duration, &a);
            GetAnimationRootMotion(cur->clip, 0.0f, cur->time, &b);
            delta = Vector3Add(a, b);
        } else {
            GetAnimationRootMotion(cur->clip, previousTime, cur->time, &delta);
        }
        animator->rootMotion = delta;
    }

    for (int i = 0; i < animator->eventCount; ++i) {
        AnimatorEvent *ev = &animator->events[i];
        if (strcmp(ev->state, cur->name) != 0) continue;
        bool crossed;
        if (cur->loop && cur->time < previousTime)
            crossed = ev->time > previousTime || ev->time <= cur->time;
        else
            crossed = ev->time > previousTime && ev->time <= cur->time;
        if (crossed && animator->pendingCount < ANIMATOR_MAX_PENDING) {
            snprintf(animator->pending[animator->pendingCount],
                     sizeof(animator->pending[0]), "%s", ev->name);
            animator->pendingCount++;
        }
    }

    for (int i = 0; i < animator->transitionCount; ++i) {
        AnimatorTransition *t = &animator->transitions[i];
        if (strcmp(t->from, animator->states[animator->current].name) != 0)
            continue;
        if (animatorConditionMet(animator, t)) {
            AnimatorPlay(animator, t->to, t->fade);
            break;
        }
    }

    if (animator->previous >= 0) {
        animatorAdvance(&animator->states[animator->previous], dt);
        animator->fadeTime += dt;
        if (animator->fadeTime >= animator->fadeDuration) {
            animator->previous = -1;
            animator->fadeTime = 0.0f;
            animator->fadeDuration = 0.0f;
        }
    }
}

void AnimatorSetTime(Animator *animator, float time) {
    if (!animator || animator->current < 0) return;
    AnimatorState *s = &animator->states[animator->current];
    float duration = s->clip.duration;
    if (duration > 0.0001f) {
        if (s->loop) {
            time = fmodf(time, duration);
            if (time < 0.0f) time += duration;
        } else {
            time = animatorClamp(time, 0.0f, duration);
        }
    }
    s->time = time;
    animator->rootMotion = (Vector3){0};
}

void AnimatorSetRootMotion(Animator *animator, bool enabled) {
    if (!animator) return;
    animator->rootMotionEnabled = enabled;
    if (!enabled) animator->rootMotion = (Vector3){0};
}

Vector3 AnimatorGetRootMotion(const Animator *animator) {
    return animator ? animator->rootMotion : (Vector3){0};
}

bool AnimatorAddTransition(Animator *animator,
                           const char *fromState, const char *toState,
                           const char *parameter, AnimatorConditionOp op,
                           float threshold, float fadeSeconds) {
    if (!animator || !fromState || !toState) return false;
    if (animatorFind(animator, fromState) < 0 ||
        animatorFind(animator, toState) < 0) return false;
    if (op < ANIM_COND_ALWAYS || op > ANIM_COND_TRIGGER) return false;
    if (animator->transitionCount >= ANIMATOR_MAX_TRANSITIONS) return false;
    AnimatorTransition *t = &animator->transitions[animator->transitionCount++];
    memset(t, 0, sizeof(*t));
    snprintf(t->from, sizeof(t->from), "%s", fromState);
    snprintf(t->to, sizeof(t->to), "%s", toState);
    if (parameter) snprintf(t->parameter, sizeof(t->parameter), "%s", parameter);
    t->op = op;
    t->threshold = threshold;
    t->fade = fadeSeconds < 0.0f ? 0.0f : fadeSeconds;
    return true;
}

bool AnimatorAddEvent(Animator *animator, const char *stateName,
                      float time, const char *eventName) {
    if (!animator || !stateName || !eventName || !eventName[0]) return false;
    if (animatorFind(animator, stateName) < 0) return false;
    if (animator->eventCount >= ANIMATOR_MAX_EVENTS) return false;
    AnimatorEvent *ev = &animator->events[animator->eventCount++];
    snprintf(ev->state, sizeof(ev->state), "%s", stateName);
    snprintf(ev->name, sizeof(ev->name), "%s", eventName);
    ev->time = time < 0.0f ? 0.0f : time;
    return true;
}

const char *AnimatorNextEvent(Animator *animator) {
    static char out[64];
    if (!animator || animator->pendingCount <= 0) return NULL;
    snprintf(out, sizeof(out), "%s", animator->pending[0]);
    for (int i = 1; i < animator->pendingCount; ++i)
        memcpy(animator->pending[i - 1], animator->pending[i],
               sizeof(animator->pending[0]));
    animator->pendingCount--;
    return out;
}

void AnimatorSetFloat(Animator *animator, const char *parameter, float value) {
    AnimatorParam *p = animatorParam(animator, parameter, true);
    if (p) p->value = value;
}

void AnimatorSetBool(Animator *animator, const char *parameter, bool value) {
    AnimatorParam *p = animatorParam(animator, parameter, true);
    if (p) p->value = value ? 1.0f : 0.0f;
}

void AnimatorTrigger(Animator *animator, const char *parameter) {
    AnimatorParam *p = animatorParam(animator, parameter, true);
    if (p) p->trigger = 1;
}

bool AnimatorSetOverlay(Animator *animator, const char *stateName, float weight) {
    if (!animator || !stateName) return false;
    int idx = animatorFind(animator, stateName);
    if (idx < 0) return false;
    if (weight < 0.0f) weight = 0.0f;
    if (weight > 1.0f) weight = 1.0f;
    animator->overlay = idx;
    animator->overlayWeight = weight;
    animator->overlayTime = 0.0f;
    animator->overlayAdditive = false;
    return true;
}

bool AnimatorSetAdditiveOverlay(Animator *animator, const char *stateName, float weight) {
    if (!AnimatorSetOverlay(animator, stateName, weight)) return false;
    animator->overlayAdditive = true;
    return true;
}

void AnimatorClearOverlay(Animator *animator) {
    if (!animator) return;
    animator->overlay = -1;
    animator->overlayWeight = 0.0f;
    animator->overlayTime = 0.0f;
    animator->overlayAdditive = false;
}

float AnimatorGetOverlayWeight(const Animator *animator) {
    return animator ? animator->overlayWeight : 0.0f;
}

float AnimatorGetBlend(const Animator *animator) {
    if (!animator || animator->previous < 0 || animator->fadeDuration <= 0.0f)
        return 0.0f;
    return animatorClamp(animator->fadeTime / animator->fadeDuration, 0.0f, 1.0f);
}

static Quaternion animatorQInv(Quaternion q) {
    return (Quaternion){-q.x, -q.y, -q.z, q.w};
}

static bool animatorDrawAdditive(Animator *animator,
                                 Vector3 position, Vector3 rotation,
                                 Vector3 scale, Color tint) {
    int count = GetSkinnedModelBoneCount(animator->model);
    if (count <= 0 || count > ANIMATOR_MAX_BONES) return false;
    BonePose base[ANIMATOR_MAX_BONES];
    BonePose over[ANIMATOR_MAX_BONES];
    BonePose rest[ANIMATOR_MAX_BONES];
    BonePose out[ANIMATOR_MAX_BONES];
    AnimatorState *cur = &animator->states[animator->current];
    AnimatorState *ov = &animator->states[animator->overlay];
    if (!GetAnimationPose(cur->clip, cur->time, base, count) ||
        !GetAnimationPose(ov->clip, animator->overlayTime, over, count) ||
        !GetSkinnedModelRestPose(animator->model, rest, count))
        return false;
    float w = animatorClamp(animator->overlayWeight, 0.0f, 1.0f);
    for (int i = 0; i < count; ++i) {
        out[i].position = Vector3Add(base[i].position,
            Vector3Scale(Vector3Subtract(over[i].position, rest[i].position), w));
        out[i].scale = Vector3Add(base[i].scale,
            Vector3Scale(Vector3Subtract(over[i].scale, rest[i].scale), w));
        Quaternion delta = QuaternionMultiply(over[i].rotation,
                                               animatorQInv(rest[i].rotation));
        Quaternion wd = QuaternionSlerp(QuaternionIdentity(), delta, w);
        out[i].rotation = QuaternionNormalize(QuaternionMultiply(wd, base[i].rotation));
    }
    DrawSkinnedModelPose(animator->model, out, count, position, rotation, scale, tint);
    return true;
}

void AnimatorDraw(Animator *animator,
                  Vector3 position, Vector3 rotation, Vector3 scale,
                  Color tint) {
    if (!animator || animator->current < 0) return;
    AnimatorState *cur = &animator->states[animator->current];
    if (animator->previous >= 0) {
        AnimatorState *prev = &animator->states[animator->previous];
        DrawSkinnedModelBlend(animator->model,
                              prev->clip, prev->time,
                              cur->clip, cur->time,
                              AnimatorGetBlend(animator),
                              position, rotation, scale, tint);
    } else if (animator->overlay >= 0 && animator->overlayWeight > 0.0f) {
        if (animator->overlayAdditive) {
            if (animatorDrawAdditive(animator, position, rotation, scale, tint))
                return;
        }
        AnimatorState *ov = &animator->states[animator->overlay];
        DrawSkinnedModelBlend(animator->model,
                              cur->clip, cur->time,
                              ov->clip, animator->overlayTime,
                              animator->overlayWeight,
                              position, rotation, scale, tint);
    } else {
        DrawSkinnedModel(animator->model, cur->clip, cur->time,
                         position, rotation, scale, tint);
    }
}

const char *AnimatorGetState(const Animator *animator) {
    if (!animator || animator->current < 0) return "";
    return animator->states[animator->current].name;
}

const char *AnimatorGetPreviousState(const Animator *animator) {
    if (!animator || animator->previous < 0) return "";
    return animator->states[animator->previous].name;
}

float AnimatorGetTime(const Animator *animator) {
    if (!animator || animator->current < 0) return 0.0f;
    return animator->states[animator->current].time;
}

bool AnimatorIsPlaying(const Animator *animator) {
    return animator && animator->current >= 0;
}
