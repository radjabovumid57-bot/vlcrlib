/*
 * example_test_materials.c — material API smoke (uses real signatures)
 */
#include <vlcrlib.h>
#include <stdio.h>

static int fails = 0;
static void expect(int cond, const char *name) {
    if (cond) fprintf(stderr, "[PASS] %s\n", name);
    else { fprintf(stderr, "[FAIL] %s\n", name); fails++; }
}

int main(void) {
    if (!InitWindow(640, 480, "material test")) return 1;

    Material m = LoadMaterialDefault();
    expect(m.id != 0, "LoadMaterialDefault");

    SetMaterialPBR(&m, 0.8f, 0.3f);
    SetMaterialIOR(&m, 1.5f);
    SetMaterialEmission(&m, (Color){255, 100, 50, 255}, 2.0f);
    SetMaterialBaseColorFactor(&m, 0.9f, 0.2f, 0.1f, 1.0f);
    expect(1, "material setters");

    SetSSAO(1, 0.4f, 0.8f);
    int en = 0; float r = 0, i = 0;
    GetSSAO(&en, &r, &i);
    expect(en == 1, "SSAO enabled");

    SetPostProcess(0);
    UnloadMaterial(m);
    CloseWindow();
    fprintf(stderr, "[example_test] fails=%d\n", fails);
    return fails ? 1 : 0;
}
