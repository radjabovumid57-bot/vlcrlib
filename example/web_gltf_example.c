/* web_gltf_example.c — glTF + PBR on WebGPU / Vulkan */
#include <vlcrlib.h>
#include "vgltf.h"
#include "vplatform.h"
#include <stdio.h>
#include <math.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

static Camera3D cam;
static Model model;
static float t0;

static void frame(void) {
    if (WindowShouldClose()) {
#if defined(__EMSCRIPTEN__)
        emscripten_cancel_main_loop();
#endif
        return;
    }
    float t = (float)GetTime() - t0;
    cam.position.x = sinf(t * 0.35f) * 3.5f;
    cam.position.z = cosf(t * 0.35f) * 3.5f;
    cam.position.y = 1.2f;
    cam.target = (Vector3){0, 0.8f, 0};

    BeginDrawing();
    ClearBackground((Color){30, 32, 40, 255});
    BeginMode3D(cam);
    DrawGrid(16, 1.0f);
    if (model.id)
        DrawModel(model, (Vector3){0, 0, 0}, (Vector3){0, t * 0.4f, 0}, (Vector3){1,1,1}, WHITE);
    else
        DrawCube((Vector3){0, 0.5f, 0}, (Vector3){1,1,1}, ORANGE);
    EndMode3D();

    if (!model.id)
        DrawText("Model not loaded", 20, 120, 20, RED);

    DrawText("glTF + PBR", 20, 16, 28, WHITE);
    DrawText(VLCRLIB_GRAPHICS_NAME, 20, 52, 18, LIGHTGRAY);
    EndDrawing();
}

int main(void) {
    if (!InitWindow(1280, 720, "vlcrlib — glTF PBR")) return 1;
    SetTargetFPS(60);
    vgraphics_SetDirectionalLight(true, (Vector3){-0.4f,-0.7f,-0.3f}, (Color){255,250,240,255}, 1.5f);
    vgraphics_SetAmbientLight(true, (Color){50,55,70,255}, 0.35f);
    cam = (Camera3D){0};
    cam.position = (Vector3){0, 1.2f, 3.5f};
    cam.target = (Vector3){0, 0.8f, 0};
    cam.up = (Vector3){0,1,0};
    cam.fovy = 45; cam.nearPlane = 0.1f; cam.farPlane = 100;
    t0 = (float)GetTime();

    const char *paths[] = {
        "DamagedHelmet.glb",
        "example/DamagedHelmet.glb",
        "../example/DamagedHelmet.glb",
        "assets/DamagedHelmet.glb",
        NULL
    };
    for (int i = 0; paths[i]; ++i) {
        model = LoadModelGLTFTiny(paths[i]);
        if (model.id) { fprintf(stderr, "loaded %s id=%u\n", paths[i], model.id); break; }
    }
    if (!model.id) fprintf(stderr, "put DamagedHelmet.glb next to the executable\n");

#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop(frame, 0, 1);
#else
    while (!WindowShouldClose()) frame();
    UnloadModel(model);
    CloseWindow();
#endif
    return 0;
}
