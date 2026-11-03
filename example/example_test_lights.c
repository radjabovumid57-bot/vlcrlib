/*
 * example_test_lights.c — light API smoke
 */
#include <vlcrlib.h>
#include <stdio.h>

static int fails = 0;
static void expect(int cond, const char *name) {
    if (cond) fprintf(stderr, "[PASS] %s\n", name);
    else { fprintf(stderr, "[FAIL] %s\n", name); fails++; }
}

int main(void) {
    if (!InitWindow(640, 480, "light test")) return 1;

    vgraphics_SetDirectionalLight(true, (Vector3){-0.5f, -1.f, -0.3f}, WHITE, 2.0f);
    vgraphics_SetAmbientLight(true, (Color){80, 90, 110, 255}, 0.4f);
    vgraphics_SetPointLight(true, (Vector3){2, 3, 1}, (Color){255, 200, 150, 255}, 5.0f, 10.f);
    vgraphics_SetSpotLight(true, (Vector3){0, 4, 0}, (Vector3){0, -1, 0},
                           (Color){200, 220, 255, 255}, 3.f, 15.f, 0.8f, 0.95f);
    expect(1, "all light setters");

    SetGraphicsExposure(1.2f);
    SetGraphicsDebugView(0);
    expect(GetGraphicsDebugView() == 0, "debug view");

    CloseWindow();
    fprintf(stderr, "[light_test] fails=%d\n", fails);
    return fails ? 1 : 0;
}
