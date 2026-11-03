/*
 * example_test_postprocess.c — post-process API smoke
 * Enables offscreen path briefly and checks it doesn't crash.
 */
#include <vlcrlib.h>
#include <stdio.h>

static int fails = 0;
static void expect(int c, const char *n) {
    if (c) fprintf(stderr, "[PASS] %s\n", n);
    else { fprintf(stderr, "[FAIL] %s\n", n); fails++; }
}

int main(void) {
    if (!InitWindow(640, 480, "postprocess test")) return 1;

    SetPostProcess(0);
    expect(1, "postprocess off");

    SetSSAO(1, 0.5f, 1.0f);
    int en = 0; float r = 0, i = 0;
    GetSSAO(&en, &r, &i);
    expect(en == 1 && r >= 0.4f && i >= 0.5f, "ssao params");

    SetGraphicsExposure(1.5f);
    SetGraphicsDebugView(1);
    expect(GetGraphicsDebugView() == 1, "debug view 1");
    SetGraphicsDebugView(0);

    /* Enable post-process for a few frames */
    SetPostProcess(1);
    Camera3D cam = {0};
    cam.position = (Vector3){0, 1, 3};
    cam.target = (Vector3){0, 0, 0};
    cam.up = (Vector3){0, 1, 0};
    cam.fovy = 45; cam.nearPlane = 0.1f; cam.farPlane = 50.f;

    for (int f = 0; f < 10; ++f) {
        BeginDrawing();
        ClearBackground((Color){30, 30, 40, 255});
        BeginMode3D(cam);
        DrawGrid(4, 1.0f);
        EndMode3D();
        EndDrawing();
    }
    expect(1, "10 frames with postprocess");
    SetPostProcess(0);

    CloseWindow();
    fprintf(stderr, "[postprocess_test] fails=%d\n", fails);
    return fails ? 1 : 0;
}
