#ifndef VLCRLIB_VANIMATOR_H
#define VLCRLIB_VANIMATOR_H

#include "vgraphics.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Small animation state machine with timed crossfades.
   States are named clips from one SkinnedModel; conditions are driven by
   AnimatorPlay(). */
typedef struct Animator Animator;

Animator *CreateAnimator(SkinnedModel model);
void      DestroyAnimator(Animator *animator);

bool AnimatorAddState(Animator *animator, const char *name,
                      Animation clip, bool loop);
bool AnimatorPlay(Animator *animator, const char *state, float fadeSeconds);
void AnimatorUpdate(Animator *animator, float dt);
void AnimatorSetTime(Animator *animator, float time);

/* Opt-in root motion: AnimatorGetRootMotion returns the root-bone delta for
   the last AnimatorUpdate. Add it to the draw position yourself. */
void     AnimatorSetRootMotion(Animator *animator, bool enabled);
Vector3  AnimatorGetRootMotion(const Animator *animator);

/* Parameter-driven transitions. The state machine checks transitions out of
   the current state at the end of AnimatorUpdate. */
typedef enum AnimatorConditionOp {
    ANIM_COND_ALWAYS = 0,
    ANIM_COND_GREATER,
    ANIM_COND_LESS,
    ANIM_COND_EQUAL,
    ANIM_COND_NOT_EQUAL,
    ANIM_COND_TRUE,
    ANIM_COND_FALSE,
    ANIM_COND_TRIGGER
} AnimatorConditionOp;

bool AnimatorAddTransition(Animator *animator,
                           const char *fromState, const char *toState,
                           const char *parameter, AnimatorConditionOp op,
                           float threshold, float fadeSeconds);
void AnimatorSetFloat(Animator *animator, const char *parameter, float value);
void AnimatorSetBool(Animator *animator, const char *parameter, bool value);
void AnimatorTrigger(Animator *animator, const char *parameter);

/* One overlay clip blended over the current state (weight 0..1). This is a
   crossfade-style layer, not an additive bone-space layer. */
bool AnimatorSetOverlay(Animator *animator, const char *stateName, float weight);
bool AnimatorSetAdditiveOverlay(Animator *animator, const char *stateName, float weight);

/* Named animation events at clip times. Poll with AnimatorNextEvent(). */
bool        AnimatorAddEvent(Animator *animator, const char *stateName,
                             float time, const char *eventName);
const char *AnimatorNextEvent(Animator *animator);
void AnimatorClearOverlay(Animator *animator);
float AnimatorGetOverlayWeight(const Animator *animator);

/* Draw the current state, crossfading from the previous one. */
void AnimatorDraw(Animator *animator,
                  Vector3 position, Vector3 rotation, Vector3 scale,
                  Color tint);

const char *AnimatorGetState(const Animator *animator);
const char *AnimatorGetPreviousState(const Animator *animator);
float       AnimatorGetTime(const Animator *animator);
float       AnimatorGetBlend(const Animator *animator); /* 0..1, 0 if no fade */
bool        AnimatorIsPlaying(const Animator *animator);

#ifdef __cplusplus
}
#endif

#endif /* VLCRLIB_VANIMATOR_H */
