/*
 * example_test_khr.c — KHR material extension setters smoke
 */
#include <vlcrlib.h>
#include <stdio.h>

static int fails = 0;
static void expect(int c, const char *n) {
    if (c) fprintf(stderr, "[PASS] %s\n", n);
    else { fprintf(stderr, "[FAIL] %s\n", n); fails++; }
}

int main(void) {
    if (!InitWindow(640, 480, "khr test")) return 1;

    Material m = LoadMaterialDefault();
    expect(m.id != 0, "default material");

    SetMaterialIOR(&m, 1.5f);
    SetMaterialSpecular(&m, 0.5f, 1.0f, 1.0f, 1.0f);
    SetMaterialEmission(&m, (Color){255, 200, 100, 255}, 1.5f);
    SetMaterialPBR(&m, 0.0f, 0.4f);
    SetMaterialBaseColorFactor(&m, 0.8f, 0.2f, 0.1f, 1.0f);
    expect(1, "KHR/PBR setters");

    UnloadMaterial(m);
    CloseWindow();
    fprintf(stderr, "[khr_test] fails=%d\n", fails);
    return fails ? 1 : 0;
}
