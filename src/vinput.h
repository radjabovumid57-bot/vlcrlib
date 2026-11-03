#ifndef VLCRLIB_VINPUT_H
#define VLCRLIB_VINPUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "vcore.h"
#include "vmath.h"
#include <stdint.h>
#include <stdbool.h>

/* ================================================================== */
/*  Константы                                                         */
/* ================================================================== */
#define VINPUT_MAX_ACTIONS   64
#define VINPUT_MAX_BINDINGS  8
#define VINPUT_MAX_AXES      32
#define VINPUT_MAX_GAMEPADS  4
#define VINPUT_MAX_TOUCHES   10

/* ================================================================== */
/*  Source — источник привязки                                        */
/* ================================================================== */
typedef enum VInputSource {
    VINPUT_SRC_KEY      = 0,   /* клавиатура: SDL_Scancode           */
    VINPUT_SRC_MOUSE_BTN,      /* кнопка мыши: SDL_BUTTON_*           */
    VINPUT_SRC_MOUSE_AXIS,     /* ось мыши: не используется           */
    VINPUT_SRC_WHEEL,          /* колесо мыши: ось X или Y            */
    VINPUT_SRC_PAD_BTN,        /* кнопка геймпада: SDL_GAMEPAD_BUTTON */
    VINPUT_SRC_PAD_AXIS,       /* ось геймпада: SDL_GAMEPAD_AXIS      */
    VINPUT_SRC_TOUCH           /* касание пальцем                     */
} VInputSource;

/* ================================================================== */
/*  Binding — конкретная привязка                                     */
/* ================================================================== */
typedef struct VInputBinding {
    VInputSource source;
    int          code;        /* scancode / button / axis            */
    int          padIndex;    /* индекс геймпада (0..3)              */
    float        scale;       /* +1 / -1                             */
    float        threshold;   /* для осей (по умолчанию 0.5)         */
} VInputBinding;

/* ================================================================== */
/*  Action — именованное действие                                     */
/* ================================================================== */
typedef struct VInputAction {
    char          name[32];
    VInputBinding bindings[VINPUT_MAX_BINDINGS];
    int           bindingCount;
    bool          enabled;
} VInputAction;

/* ================================================================== */
/*  Axis — двунаправленная ось                                        */
/* ================================================================== */
typedef struct VInputAxis {
    char          name[32];
    VInputBinding positive;
    VInputBinding negative;
    bool          hasPositive;
    bool          hasNegative;
    float         deadzone;
    float         smoothing;
} VInputAxis;

/* ================================================================== */
/*  Состояние ввода                                                   */
/* ================================================================== */
typedef struct VInputState {
    /* --- actions --- */
    VInputAction actions[VINPUT_MAX_ACTIONS];
    int          actionCount;
    bool         actionDown[VINPUT_MAX_ACTIONS];
    bool         actionPressed[VINPUT_MAX_ACTIONS];
    bool         actionReleased[VINPUT_MAX_ACTIONS];

    /* --- axes --- */
    VInputAxis   axes[VINPUT_MAX_AXES];
    int          axisCount;
    float        axisValue[VINPUT_MAX_AXES];       /* сглаженное */
    float        axisRaw[VINPUT_MAX_AXES];         /* мгновенное */

    /* --- mouse --- */
    Vector2      mousePosition;
    Vector2      mouseDelta;
    Vector2      mouseDeltaSmooth;
    float        wheelX;
    float        wheelY;

    /* --- touch --- */
    int          touchCount;
    Vector2      touchPositions[VINPUT_MAX_TOUCHES];
    bool         touchDown[VINPUT_MAX_TOUCHES];
    bool         touchPressed[VINPUT_MAX_TOUCHES];
    bool         touchReleased[VINPUT_MAX_TOUCHES];

    /* --- drag gesture --- */
    bool         dragActive;
    bool         dragJustEnded;
    Vector2      dragStart;
    Vector2      dragEnd;

    /* --- внутреннее --- */
    float        dt;
    uint64_t     lastUpdate;
} VInputState;

/* ================================================================== */
/*  Lifecycle                                                         */
/* ================================================================== */
void  VInputInit(VInputState *s);
void  VInputUpdate(VInputState *s, float dt);
void  VInputReset(VInputState *s);

/* ================================================================== */
/*  Клавиатура                                                        */
/* ================================================================== */
bool  VInputKeyDown(const VInputState *s, int scancode);
bool  VInputKeyPressed(const VInputState *s, int scancode);
bool  VInputKeyReleased(const VInputState *s, int scancode);
const char *VInputKeyName(int scancode);
int   VInputScancodeFromName(const char *name);
void  VInputBindAxisKeys(VInputState *s, const char *axis,
                         int negativeScan, int positiveScan);
void  VInputBindAxisKey(VInputState *s, const char *axis,
                        int negativeScan, int positiveScan);


/* ================================================================== */
/*  Мышь                                                              */
/* ================================================================== */
Vector2 VInputMousePosition(const VInputState *s);
Vector2 VInputMouseDelta(const VInputState *s);
float   VInputWheelX(const VInputState *s);
float   VInputWheelY(const VInputState *s);
bool    VInputMouseDown(const VInputState *s, int button);
bool    VInputMousePressed(const VInputState *s, int button);
bool    VInputMouseReleased(const VInputState *s, int button);

/* ================================================================== */
/*  Геймпад                                                           */
/* ================================================================== */
bool    VInputGamepadAvailable(int padIndex);
bool    VInputGamepadButtonDown(int padIndex, int button);
bool    VInputGamepadButtonPressed(int padIndex, int button);
bool    VInputGamepadButtonReleased(int padIndex, int button);
float   VInputGamepadAxis(int padIndex, int axis);
const char *VInputGamepadName(int padIndex);
int     VInputGamepadCount(void);

/* ================================================================== */
/*  Touch                                                             */
/* ================================================================== */
int     VInputTouchCount(const VInputState *s);
Vector2 VInputTouchPosition(const VInputState *s, int index);
bool    VInputTouchDown(const VInputState *s, int index);
bool    VInputTouchPressed(const VInputState *s, int index);
bool    VInputTouchReleased(const VInputState *s, int index);

/* ================================================================== */
/*  Actions                                                           */
/* ================================================================== */
void  VInputBindKey(VInputState *s, const char *action, int scancode);
void  VInputBindMouseButton(VInputState *s, const char *action, int button);
void  VInputBindGamepadButton(VInputState *s, const char *action, int pad, int button);
void  VInputBindGamepadAxis(VInputState *s, const char *action, int pad, int axis,
                            float threshold);

void  VInputUnbindAll(VInputState *s, const char *action);
void  VInputSetActionEnabled(VInputState *s, const char *action, bool enabled);

bool  VInputIsActionDown(const VInputState *s, const char *action);
bool  VInputIsActionPressed(const VInputState *s, const char *action);
bool  VInputIsActionReleased(const VInputState *s, const char *action);

/* ================================================================== */
/*  Axes                                                              */
/* ================================================================== */
void  VInputBindAxisKeys(VInputState *s, const char *axis,
                         int negativeScan, int positiveScan);
void  VInputBindAxisMouseButtons(VInputState *s, const char *axis,
                                 int negativeButton, int positiveButton);
void  VInputBindAxisGamepad(VInputState *s, const char *axis,
                            int pad, int axisCode, bool positive);

void  VInputSetAxisDeadzone(VInputState *s, const char *axis, float deadzone);
void  VInputSetAxisSmoothing(VInputState *s, const char *axis, float smoothing);

float VInputGetAxis(const VInputState *s, const char *axis);
Vector2 VInputGetVector2(const VInputState *s,
                         const char *axisX, const char *axisY);

/* ================================================================== */
/*  Drag gesture                                                      */
/* ================================================================== */
bool    VInputIsDragging(const VInputState *s);
Vector2 VInputDragStart(const VInputState *s);
Vector2 VInputDragEnd(const VInputState *s);
Vector2 VInputDragDelta(const VInputState *s);

/* ================================================================== */
/*  Debug                                                             */
/* ================================================================== */
void  VInputDump(const VInputState *s);

#ifdef __cplusplus
}
#endif
#endif /* VLCRLIB_VINPUT_H */