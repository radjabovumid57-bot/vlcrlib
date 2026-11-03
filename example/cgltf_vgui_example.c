/*
 * cgltf_vgui_example.c — fastgltf viewer with a small orbital camera.
 *
 * cgltf was removed from the engine; this example now uses the same
 * LoadModelGLTFTiny() (fastgltf) path as the other examples.
 *
 * Controls: drag = orbit, wheel = zoom, R = reset, Esc = quit.
 */
#include <vlcrlib.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include "vgltf.h"
#include <math.h>
#include <stdio.h>

#define WIN_W 1280
#define WIN_H 720

typedef struct { float yaw, pitch, dist; Vector3 target; } Orbit;

static Camera3D to_cam(const Orbit *o) {
    float cp = cosf(o->pitch);
    Camera3D c = {0};
    c.position = (Vector3){
        o->target.x + o->dist * cp * sinf(o->yaw),
        o->target.y + o->dist * sinf(o->pitch),
        o->target.z + o->dist * cp * cosf(o->yaw)
    };
    c.target = o->target;
    c.up = (Vector3){0, 1, 0};
    c.fovy = 45.0f;
    c.nearPlane = 0.1f;
    c.farPlane = 100.0f;
    return c;
}

static Model load_model(const char *arg, char *out, size_t outSize) {
    const char *candidates[4];
    int n = 0;
    if (arg && arg[0]) candidates[n++] = arg;
    candidates[n++] = "DamagedHelmet.glb";
    candidates[n++] = "example/DamagedHelmet.glb";
    candidates[n++] = "../example/DamagedHelmet.glb";

    for (int i = 0; i < n; ++i) {
        FILE *f = fopen(candidates[i], "rb");
        if (!f) continue;
        fclose(f);
        Model m = LoadModelGLTFTiny(candidates[i]);
        if (m.id) {
            snprintf(out, outSize, "%s", candidates[i]);
            return m;
        }
    }
    if (out && outSize) out[0] = 0;
    return (Model){0};
}

int main(int argc, char **argv) {
    if (!InitWindow(WIN_W, WIN_H, "vlcrlib — glTF viewer")) return 1;
    SetTargetFPS(60);

    char path[1024] = {0};
    Model model = load_model(argc > 1 ? argv[1] : NULL, path, sizeof(path));

    vgraphics_SetDirectionalLight(true,
        (Vector3){ -0.35f, -0.70f, -0.60f },
        (Color){ 255, 250, 240, 255 }, 3.0f);
    vgraphics_SetAmbientLight(true, (Color){ 90, 100, 120, 255 }, 0.15f);
    bool ibl = vgraphics_LoadIBL(NULL);
    vgraphics_SetIBLIntensity(ibl ? 1.2f : 0.0f);
    vgraphics_SetSkybox(0);
    SetSSAO(1, 0.5f, 0.6f);
    SetBloom(0.9f, 0.15f);
    SetGraphicsExposure(1.0f);

    Orbit orbit = { -0.35f, 0.12f, 5.2f, (Vector3){0, 1.0f, 0} };
    Vector2 prev = {0};
    int dragging = 0;

    while (!WindowShouldClose()) {
        Vector2 mouse = GetMousePosition();
        float wheel = GetMouseWheelMove();

        if (IsKeyPressed(SDL_SCANCODE_R))
            orbit = (Orbit){ -0.35f, 0.12f, 5.2f, (Vector3){0, 1.0f, 0} };
        if (IsMouseButtonPressed(SDL_BUTTON_LEFT)) { dragging = 1; prev = mouse; }
        if (IsMouseButtonReleased(SDL_BUTTON_LEFT)) dragging = 0;
        if (dragging) {
            orbit.yaw   -= (mouse.x - prev.x) * 0.01f;
            orbit.pitch += (mouse.y - prev.y) * 0.01f;
            if (orbit.pitch >  1.5f) orbit.pitch =  1.5f;
            if (orbit.pitch < -1.5f) orbit.pitch = -1.5f;
            prev = mouse;
        }
        if (wheel != 0.0f) {
            orbit.dist *= (1.0f - 0.1f * wheel);
            if (orbit.dist < 0.5f)  orbit.dist = 0.5f;
            if (orbit.dist > 50.0f) orbit.dist = 50.0f;
        }

        BeginDrawing();
        ClearBackground((Color){ 118, 118, 118, 255 });
        if (model.id) {
            Camera3D cam = to_cam(&orbit);
            BeginMode3D(cam);
            DrawModel(model, (Vector3){0, 1, 0}, (Vector3){0, 0, 0},
                      (Vector3){1.0f, 1.0f, 1.0f}, WHITE);
            EndMode3D();
            DrawText("glTF viewer — fastgltf", 20, 16, 26, WHITE);
            DrawText("drag: orbit   wheel: zoom   R: reset", 20, 48, 16, LIGHTGRAY);
            DrawText(path, 20, 70, 14, LIGHTGRAY);
        } else {
            DrawText("MODEL NOT LOADED", 20, 24, 30, RED);
            DrawText("Put DamagedHelmet.glb next to the exe", 20, 70, 18, WHITE);
        }
        EndDrawing();
    }

    if (model.id) UnloadModel(model);
    CloseWindow();
    return model.id ? 0 : 1;
}
