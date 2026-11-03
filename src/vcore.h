#ifndef VLCRLIB_VCORE_H
#define VLCRLIB_VCORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include "vmath.h"

#define MOUSE_BUTTON_LEFT   1
#define MOUSE_BUTTON_MIDDLE 2
#define MOUSE_BUTTON_RIGHT  3

typedef struct Color     { unsigned char r, g, b, a; } Color;
typedef struct Rectangle { float x, y, width, height; } Rectangle;

typedef struct Camera2D {
    Vector2 offset;
    Vector2 target;
    float   rotation;
    float   zoom;
} Camera2D;

typedef struct Camera3D {
    Vector3 position;
    Vector3 target;
    Vector3 up;
    float   fovy;
    float   nearPlane;
    float   farPlane;
} Camera3D;

/* ---------- Window ---------- */
bool InitWindow(int width, int height, const char *title);
void CloseWindow(void);
bool WindowShouldClose(void);
void *GetWindowHandle(void);
bool IsWindowResized(void);
void SetWindowTitle(const char *title);
void SetWindowPosition(int x, int y);
void SetWindowSize(int width, int height);
void ToggleFullscreen(void);
void SetTargetFPS(int fps);
float GetFrameTime(void);
double GetTime(void);
void SetExitKey(int key);

/* ---------- Frame ---------- */
void BeginDrawing(void);
void EndDrawing(void);
void ClearBackground(Color color);

/* ---------- Input ---------- */
bool    IsKeyPressed(int key);
bool    IsKeyDown(int key);
bool    IsKeyReleased(int key);
int     GetKeyPressed(void);
int     GetCharPressed(char *buffer, size_t capacity);
bool    IsMouseButtonPressed(int button);
bool    IsMouseButtonDown(int button);
bool    IsMouseButtonReleased(int button);
Vector2 GetMousePosition(void);
float   GetMouseWheelMove(void);
void    SetMousePosition(int x, int y);
bool    IsGamepadAvailable(int gamepad);
bool    IsGamepadButtonPressed(int gamepad, int button);
bool    IsGamepadButtonDown(int gamepad, int button);
float   GetGamepadAxisMovement(int gamepad, int axis);
bool vcore_KeyPressed(int scancode);
bool vcore_KeyReleased(int scancode);
bool vcore_MouseButtonPressed(int button);
bool vcore_MouseButtonReleased(int button);

/* ---------- Camera modes ---------- */
void BeginMode2D(Camera2D camera);
void EndMode2D(void);
void BeginMode3D(Camera3D camera);
void EndMode3D(void);

void UpdateCamera2D(Camera2D *camera, int mode);
void UpdateCamera3D(Camera3D *camera, int mode);
void UpdateCamera(void *camera, int mode);

/* ---------- Internal ---------- */
void *vcore_GetWindow(void);
void  vcore_GetScreenSize(int *w, int *h);
/* CPU-backend model cache invalidation (no-op outside a Vulkan build). */
void  vcore_InvalidateCpuModel(unsigned int modelId);
void  vcore_InvalidateCpuModels(void);

/* Кэш нажатий/отпусканий для vinput */
bool vcore_KeyPressed(int scancode);
bool vcore_KeyReleased(int scancode);
bool vcore_MouseButtonPressed(int button);
bool vcore_MouseButtonReleased(int button);

/* FPS */
int  GetFPS(void);

#ifdef __cplusplus
}
#endif
#endif /* VLCRLIB_VCORE_H */