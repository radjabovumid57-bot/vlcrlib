#include "vinput.h"
#include <SDL3/SDL.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ================================================================== */
/*  Утилиты                                                           */
/* ================================================================== */

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static int findAction(const VInputState *s, const char *name) {
    if (!s || !name) return -1;
    for (int i = 0; i < s->actionCount; ++i)
        if (strcmp(s->actions[i].name, name) == 0) return i;
    return -1;
}

static int addAction(VInputState *s, const char *name) {
    int idx = findAction(s, name);
    if (idx >= 0) return idx;
    if (s->actionCount >= VINPUT_MAX_ACTIONS) return -1;

    idx = s->actionCount++;
    memset(&s->actions[idx], 0, sizeof(s->actions[idx]));
    strncpy(s->actions[idx].name, name, sizeof(s->actions[idx].name) - 1);
    s->actions[idx].enabled = true;
    return idx;
}

static int findAxis(const VInputState *s, const char *name) {
    if (!s || !name) return -1;
    for (int i = 0; i < s->axisCount; ++i)
        if (strcmp(s->axes[i].name, name) == 0) return i;
    return -1;
}

static int addAxis(VInputState *s, const char *name) {
    int idx = findAxis(s, name);
    if (idx >= 0) return idx;
    if (s->axisCount >= VINPUT_MAX_AXES) return -1;

    idx = s->axisCount++;
    memset(&s->axes[idx], 0, sizeof(s->axes[idx]));
    strncpy(s->axes[idx].name, name, sizeof(s->axes[idx].name) - 1);
    s->axes[idx].deadzone  = 0.15f;
    s->axes[idx].smoothing = 0.25f;
    return idx;
}

/* ================================================================== */
/*  Lifecycle                                                         */
/* ================================================================== */

void VInputInit(VInputState *s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->lastUpdate = SDL_GetTicks();
}

void VInputReset(VInputState *s) {
    if (!s) return;
    s->actionCount = 0;
    s->axisCount   = 0;
    s->touchCount  = 0;
    s->dragActive  = false;
}

/* ================================================================== */
/*  Клавиатура — напрямую через SDL3                                  */
/* ================================================================== */

bool VInputKeyDown(const VInputState *s, int scancode) {
    (void)s;
    if (scancode < 0 || scancode >= SDL_SCANCODE_COUNT) return false;
    /* Keep held-key state on the same event-pumped path as pressed/released.
       SDL's cached keyboard array can lag or be empty when the app polls via
       its own SDL event loop, while vcore already tracks KEY_DOWN/KEY_UP. */
    return IsKeyDown(scancode);
}

/* Кэш pressed/released приходит из event loop SDL3 — см. vcore.c
   Ниже мы используем низкоуровневое состояние SDL для down,
   а pressed/released — из нашей копии, которая обновляется в vcore. */
extern bool vcore_KeyPressed(int scancode);
extern bool vcore_KeyReleased(int scancode);
extern bool vcore_MouseButtonPressed(int button);
extern bool vcore_MouseButtonReleased(int button);

bool VInputKeyPressed(const VInputState *s, int scancode) {
    (void)s;
    return vcore_KeyPressed(scancode);
}

bool VInputKeyReleased(const VInputState *s, int scancode) {
    (void)s;
    return vcore_KeyReleased(scancode);
}

const char *VInputKeyName(int scancode) {
    return SDL_GetScancodeName((SDL_Scancode)scancode);
}

int VInputScancodeFromName(const char *name) {
    if (!name) return 0;
    return (int)SDL_GetScancodeFromName(name);
}

/* ================================================================== */
/*  Мышь — напрямую через SDL3                                        */
/* ================================================================== */

Vector2 VInputMousePosition(const VInputState *s) {
    (void)s;
    float x = 0, y = 0;
    SDL_GetMouseState(&x, &y);   /* SDL3: возвращает (x, y) через указатели */
    return (Vector2){ x, y };
}

Vector2 VInputMouseDelta(const VInputState *s) {
    return s ? s->mouseDelta : (Vector2){0, 0};
}

float VInputWheelX(const VInputState *s) { return s ? s->wheelX : 0.0f; }
float VInputWheelY(const VInputState *s) { return s ? s->wheelY : 0.0f; }

bool VInputMouseDown(const VInputState *s, int button) {
    (void)s;
    /* SDL3: SDL_GetMouseState возвращает битовую маску нажатых кнопок */
    Uint32 mask = SDL_GetMouseState(NULL, NULL);
    return (mask & SDL_BUTTON_MASK(button)) != 0;
}

bool VInputMousePressed(const VInputState *s, int button) {
    (void)s;
    return vcore_MouseButtonPressed(button);
}

bool VInputMouseReleased(const VInputState *s, int button) {
    (void)s;
    return vcore_MouseButtonReleased(button);
}

/* ================================================================== */
/*  Геймпад — SDL3 Gamepad API                                        */
/* ================================================================== */

/* SDL3: SDL_GetGamepads(&count) возвращает массив SDL_JoystickID */
int VInputGamepadCount(void) {
    int count = 0;
    SDL_GetGamepads(&count);
    return count;
}

bool VInputGamepadAvailable(int padIndex) {
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    if (!ids) return false;
    bool ok = (padIndex >= 0 && padIndex < count);
    SDL_free(ids);
    return ok;
}

static SDL_Gamepad *getPad(int padIndex) {
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    if (!ids) return NULL;
    SDL_Gamepad *pad = NULL;
    if (padIndex >= 0 && padIndex < count)
        pad = SDL_GetGamepadFromID(ids[padIndex]);
    SDL_free(ids);
    return pad;
}

bool VInputGamepadButtonDown(int padIndex, int button) {
    SDL_Gamepad *pad = getPad(padIndex);
    if (!pad) return false;
    return SDL_GetGamepadButton(pad, (SDL_GamepadButton)button);
}

bool VInputGamepadButtonPressed(int padIndex, int button) {
    /* SDL3 не хранит историю нажатий сам по себе.
       Реализуем через статическую таблицу предыдущего кадра. */
    static bool prev[VINPUT_MAX_GAMEPADS][32] = {0};
    if (padIndex < 0 || padIndex >= VINPUT_MAX_GAMEPADS) return false;
    if (button  < 0 || button  >= 32) return false;

    bool now = VInputGamepadButtonDown(padIndex, button);
    bool pressed = now && !prev[padIndex][button];
    prev[padIndex][button] = now;
    return pressed;
}

bool VInputGamepadButtonReleased(int padIndex, int button) {
    static bool prev[VINPUT_MAX_GAMEPADS][32] = {0};
    if (padIndex < 0 || padIndex >= VINPUT_MAX_GAMEPADS) return false;
    if (button  < 0 || button  >= 32) return false;

    bool now = VInputGamepadButtonDown(padIndex, button);
    bool released = !now && prev[padIndex][button];
    prev[padIndex][button] = now;
    return released;
}

float VInputGamepadAxis(int padIndex, int axis) {
    SDL_Gamepad *pad = getPad(padIndex);
    if (!pad) return 0.0f;
    Sint16 v = SDL_GetGamepadAxis(pad, (SDL_GamepadAxis)axis);
    /* Нормализуем в -1..1 */
    if (v >= 0) return (float)v / 32767.0f;
    return (float)v / 32768.0f;
}

const char *VInputGamepadName(int padIndex) {
    SDL_Gamepad *pad = getPad(padIndex);
    if (!pad) return "";
    return SDL_GetGamepadName(pad);
}

/* ================================================================== */
/*  Touch                                                             */
/* ================================================================== */

int VInputTouchCount(const VInputState *s) { return s ? s->touchCount : 0; }
Vector2 VInputTouchPosition(const VInputState *s, int i) {
    if (!s || i < 0 || i >= s->touchCount) return (Vector2){0,0};
    return s->touchPositions[i];
}
bool VInputTouchDown(const VInputState *s, int i) {
    if (!s || i < 0 || i >= s->touchCount) return false;
    return s->touchDown[i];
}
bool VInputTouchPressed(const VInputState *s, int i) {
    if (!s || i < 0 || i >= s->touchCount) return false;
    return s->touchPressed[i];
}
bool VInputTouchReleased(const VInputState *s, int i) {
    if (!s || i < 0 || i >= s->touchCount) return false;
    return s->touchReleased[i];
}

/* ================================================================== */
/*  Actions                                                           */
/* ================================================================== */

static void addBinding(VInputState *s, int actionIdx,
                       VInputSource src, int code, int pad,
                       float scale, float threshold)
{
    if (actionIdx < 0) return;
    VInputAction *a = &s->actions[actionIdx];
    if (a->bindingCount >= VINPUT_MAX_BINDINGS) return;

    VInputBinding *b = &a->bindings[a->bindingCount++];
    b->source    = src;
    b->code      = code;
    b->padIndex  = pad;
    b->scale     = scale;
    b->threshold = threshold;
}

void VInputBindKey(VInputState *s, const char *action, int scancode) {
    addBinding(s, addAction(s, action), VINPUT_SRC_KEY, scancode, 0, 1.0f, 0.5f);
}

void VInputBindMouseButton(VInputState *s, const char *action, int button) {
    addBinding(s, addAction(s, action), VINPUT_SRC_MOUSE_BTN, button, 0, 1.0f, 0.5f);
}

void VInputBindGamepadButton(VInputState *s, const char *action, int pad, int button) {
    addBinding(s, addAction(s, action), VINPUT_SRC_PAD_BTN, button, pad, 1.0f, 0.5f);
}

void VInputBindGamepadAxis(VInputState *s, const char *action,
                           int pad, int axis, float threshold)
{
    addBinding(s, addAction(s, action), VINPUT_SRC_PAD_AXIS, axis, pad, 1.0f, threshold);
}

void VInputUnbindAll(VInputState *s, const char *action) {
    int idx = findAction(s, action);
    if (idx >= 0) s->actions[idx].bindingCount = 0;
}

void VInputSetActionEnabled(VInputState *s, const char *action, bool e) {
    int idx = findAction(s, action);
    if (idx >= 0) s->actions[idx].enabled = e;
}

bool VInputIsActionDown(const VInputState *s, const char *action) {
    int idx = findAction(s, action);
    return (idx >= 0) ? s->actionDown[idx] : false;
}
bool VInputIsActionPressed(const VInputState *s, const char *action) {
    int idx = findAction(s, action);
    return (idx >= 0) ? s->actionPressed[idx] : false;
}
bool VInputIsActionReleased(const VInputState *s, const char *action) {
    int idx = findAction(s, action);
    return (idx >= 0) ? s->actionReleased[idx] : false;
}

/* ================================================================== */
/*  Axes                                                              */
/* ================================================================== */

void VInputBindAxisKeys(VInputState *s, const char *axis,
                        int negativeScan, int positiveScan)
{
    int idx = addAxis(s, axis);
    if (idx < 0) return;
    VInputAxis *a = &s->axes[idx];
    a->negative = (VInputBinding){ VINPUT_SRC_KEY, negativeScan, 0, 1.0f, 0.5f };
    a->positive = (VInputBinding){ VINPUT_SRC_KEY, positiveScan, 0, 1.0f, 0.5f };
    a->hasNegative = a->hasPositive = true;
}

void VInputBindAxisMouseButtons(VInputState *s, const char *axis,
                                int negativeButton, int positiveButton)
{
    int idx = addAxis(s, axis);
    if (idx < 0) return;
    VInputAxis *a = &s->axes[idx];
    a->negative = (VInputBinding){ VINPUT_SRC_MOUSE_BTN, negativeButton, 0, 1.0f, 0.5f };
    a->positive = (VInputBinding){ VINPUT_SRC_MOUSE_BTN, positiveButton, 0, 1.0f, 0.5f };
    a->hasNegative = a->hasPositive = true;
}

void VInputBindAxisGamepad(VInputState *s, const char *axis,
                           int pad, int axisCode, bool positive)
{
    int idx = addAxis(s, axis);
    if (idx < 0) return;
    VInputAxis *a = &s->axes[idx];
    VInputBinding b = { VINPUT_SRC_PAD_AXIS, axisCode, pad, 1.0f, 0.15f };
    if (positive) { a->positive = b; a->hasPositive = true; }
    else          { a->negative = b; a->hasNegative = true; }
}

void VInputSetAxisDeadzone(VInputState *s, const char *axis, float dz) {
    int idx = findAxis(s, axis);
    if (idx >= 0) s->axes[idx].deadzone = clampf(dz, 0.0f, 0.9f);
}

void VInputSetAxisSmoothing(VInputState *s, const char *axis, float sm) {
    int idx = findAxis(s, axis);
    if (idx >= 0) s->axes[idx].smoothing = clampf(sm, 0.0f, 1.0f);
}

float VInputGetAxis(const VInputState *s, const char *axis) {
    int idx = findAxis(s, axis);
    return (idx >= 0) ? s->axisValue[idx] : 0.0f;
}

Vector2 VInputGetVector2(const VInputState *s,
                         const char *axisX, const char *axisY)
{
    Vector2 v;
    v.x = VInputGetAxis(s, axisX);
    v.y = VInputGetAxis(s, axisY);

    /* Нормализация диагонали — длина не больше 1 */
    float len2 = v.x * v.x + v.y * v.y;
    if (len2 > 1.0f) {
        float len = sqrtf(len2);
        v.x /= len;
        v.y /= len;
    }
    return v;
}

void VInputBindAxisKey(VInputState *s, const char *axis,
                       int negativeScan, int positiveScan)
{
    VInputBindAxisKeys(s, axis, negativeScan, positiveScan);
}

/* ================================================================== */
/*  Drag gesture                                                      */
/* ================================================================== */

bool    VInputIsDragging(const VInputState *s)  { return s && s->dragActive; }
Vector2 VInputDragStart(const VInputState *s)   { return s ? s->dragStart : (Vector2){0,0}; }
Vector2 VInputDragEnd(const VInputState *s)     { return s ? s->dragEnd   : (Vector2){0,0}; }
Vector2 VInputDragDelta(const VInputState *s) {
    if (!s) return (Vector2){0,0};
    return (Vector2){ s->dragEnd.x - s->dragStart.x,
                      s->dragEnd.y - s->dragStart.y };
}

/* ================================================================== */
/*  Update — вызывать каждый кадр                                     */
/* ================================================================== */

void VInputUpdate(VInputState *s, float dt) {
    if (!s) return;
    s->dt = dt;

    /* ---------- Mouse position / delta ---------- */
    float mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);
    Vector2 newPos = { mx, my };
    s->mouseDelta = (Vector2){ newPos.x - s->mousePosition.x,
                               newPos.y - s->mousePosition.y };
    s->mousePosition = newPos;

    /* Сглаживание delta */
    s->mouseDeltaSmooth.x += (s->mouseDelta.x - s->mouseDeltaSmooth.x) * 0.3f;
    s->mouseDeltaSmooth.y += (s->mouseDelta.y - s->mouseDeltaSmooth.y) * 0.3f;

    /* ---------- Actions ---------- */
    for (int i = 0; i < s->actionCount; ++i) {
        VInputAction *a = &s->actions[i];
        bool down = false;

        if (a->enabled) {
            for (int bi = 0; bi < a->bindingCount && !down; ++bi) {
                VInputBinding *b = &a->bindings[bi];
                switch (b->source) {
                    case VINPUT_SRC_KEY:
                        if (VInputKeyDown(s, b->code)) down = true;
                        break;
                    case VINPUT_SRC_MOUSE_BTN:
                        if (VInputMouseDown(s, b->code)) down = true;
                        break;
                    case VINPUT_SRC_PAD_BTN:
                        if (VInputGamepadButtonDown(b->padIndex, b->code)) down = true;
                        break;
                    case VINPUT_SRC_PAD_AXIS: {
                        float v = VInputGamepadAxis(b->padIndex, b->code);
                        if (fabsf(v) >= b->threshold) down = true;
                        break;
                    }
                    case VINPUT_SRC_TOUCH:
                        if (s->touchCount > 0) down = true;
                        break;
                    default: break;
                }
            }
        }

        s->actionPressed[i]  = down && !s->actionDown[i];
        s->actionReleased[i] = !down && s->actionDown[i];
        s->actionDown[i]     = down;
    }

    /* ---------- Axes ---------- */
    for (int i = 0; i < s->axisCount; ++i) {
        VInputAxis *a = &s->axes[i];
        float value = 0.0f;

        if (a->hasPositive) {
            switch (a->positive.source) {
                case VINPUT_SRC_KEY:
                    if (VInputKeyDown(s, a->positive.code)) value += 1.0f;
                    break;
                case VINPUT_SRC_MOUSE_BTN:
                    if (VInputMouseDown(s, a->positive.code)) value += 1.0f;
                    break;
                case VINPUT_SRC_PAD_AXIS:
                    value += VInputGamepadAxis(a->positive.padIndex,
                                               a->positive.code);
                    break;
                default: break;
            }
        }
        if (a->hasNegative) {
            switch (a->negative.source) {
                case VINPUT_SRC_KEY:
                    if (VInputKeyDown(s, a->negative.code)) value -= 1.0f;
                    break;
                case VINPUT_SRC_MOUSE_BTN:
                    if (VInputMouseDown(s, a->negative.code)) value -= 1.0f;
                    break;
                case VINPUT_SRC_PAD_AXIS:
                    value -= VInputGamepadAxis(a->negative.padIndex,
                                               a->negative.code);
                    break;
                default: break;
            }
        }

        if (fabsf(value) < a->deadzone) value = 0.0f;
        value = clampf(value, -1.0f, 1.0f);

        s->axisRaw[i]   = value;
        /* Сглаживание (EMA) */
        float sm = a->smoothing;
        s->axisValue[i] = s->axisValue[i] * sm + value * (1.0f - sm);
    }

    /* ---------- Touch ---------- */
    {
        SDL_GetTouchDevices(NULL);  /* SDL3 lazy-init touch */
        /* Простой подсчёт активных пальцев через SDL_GetTouchFinger */
        s->touchCount = 0;
        int n = 0;
        SDL_TouchID *devices = SDL_GetTouchDevices(&n);
        if (devices) {
            for (int d = 0; d < n && s->touchCount < VINPUT_MAX_TOUCHES; ++d) {
                int fn = 0;
                SDL_Finger **fingers = SDL_GetTouchFingers(devices[d], &fn);
                if (fingers) {
                    for (int f = 0; f < fn && s->touchCount < VINPUT_MAX_TOUCHES; ++f) {
                        SDL_Finger *finger = fingers[f];
                        int idx = s->touchCount;
                        s->touchPositions[idx] = (Vector2){
                            finger->x * 1280.0f,
                            finger->y * 720.0f
                        };
                        s->touchDown[idx] = true;
                        s->touchCount++;
                    }
                    SDL_free(fingers);
                }
            }
            SDL_free(devices);
        }
    }

    /* ---------- Drag gesture ---------- */
    {
        bool down = VInputMouseDown(s, MOUSE_BUTTON_LEFT);
        if (down && !s->dragActive) {
            s->dragActive = true;
            s->dragStart  = s->mousePosition;
            s->dragEnd    = s->dragStart;
        } else if (down && s->dragActive) {
            s->dragEnd = s->mousePosition;
        } else if (!down && s->dragActive) {
            s->dragActive = false;
            s->dragJustEnded = true;
        }
    }
}

/* ================================================================== */
/*  Debug                                                             */
/* ================================================================== */

void VInputDump(const VInputState *s) {
    if (!s) return;
    printf("=== VInput State ===\n");
    printf("Mouse: (%.0f, %.0f) delta=(%.1f, %.1f)\n",
           s->mousePosition.x, s->mousePosition.y,
           s->mouseDelta.x, s->mouseDelta.y);

    printf("Actions (%d):\n", s->actionCount);
    for (int i = 0; i < s->actionCount; ++i) {
        printf("  [%-16s] down=%d pressed=%d released=%d bindings=%d\n",
               s->actions[i].name,
               s->actionDown[i],
               s->actionPressed[i],
               s->actionReleased[i],
               s->actions[i].bindingCount);
    }

    printf("Axes (%d):\n", s->axisCount);
    for (int i = 0; i < s->axisCount; ++i) {
        printf("  [%-16s] = %.3f (raw %.3f)\n",
               s->axes[i].name,
               s->axisValue[i],
               s->axisRaw[i]);
    }
    printf("Gamepads: %d\n", VInputGamepadCount());
}
