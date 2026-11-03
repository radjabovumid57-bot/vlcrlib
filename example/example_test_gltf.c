/*
 * example_test_gltf.c — glTF load smoke (no long render loop)
 */
#include <vlcrlib.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include "vgltf.h"

static int fails = 0;
static void expect(int c, const char *n) {
    if (c) fprintf(stderr, "[PASS] %s\n", n);
    else { fprintf(stderr, "[FAIL] %s\n", n); fails++; }
}

int main(void) {
    if (!InitWindow(640, 480, "gltf test")) return 1;

    const char *base = SDL_GetBasePath();
    char buf[1024];
    Model m = {0};
    if (base) {
        snprintf(buf, sizeof(buf), "%sDamagedHelmet.glb", base);
        m = LoadModelGLTFTiny(buf);
    }
    if (!m.id) m = LoadModelGLTFTiny("DamagedHelmet.glb");
    if (!m.id) m = LoadModelGLTFTiny("example/DamagedHelmet.glb");
    if (!m.id) m = LoadModelGLTFTiny("../example/DamagedHelmet.glb");
    expect(m.id != 0, "LoadModelGLTFTiny");

    if (m.id) {
        /* One frame to exercise draw path */
        Camera3D cam = {0};
        cam.position = (Vector3){0, 1.5f, 4};
        cam.target = (Vector3){0, 1, 0};
        cam.up = (Vector3){0, 1, 0};
        cam.fovy = 45; cam.nearPlane = 0.05f; cam.farPlane = 100.f;

        BeginDrawing();
        ClearBackground((Color){20, 20, 30, 255});
        BeginMode3D(cam);
        DrawModel(m, (Vector3){0, 1, 0}, (Vector3){0,0,0}, (Vector3){1,1,1}, WHITE);
        EndMode3D();
        EndDrawing();
        expect(1, "draw one frame");
        UnloadModel(m);
        expect(1, "UnloadModel");
    }

    CloseWindow();
    fprintf(stderr, "[gltf_test] fails=%d\n", fails);
    return fails ? 1 : 0;
}
