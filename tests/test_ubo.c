/**
 * Unit tests for UBO layout consistency (std140 packing).
 * Verifies sizeof and field offsets match expected GPU layout.
 */
#include <stdio.h>
#include <stddef.h>
#include "vgraphics_ubo.h"

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); g_fail++; } \
    else { printf("OK: %s\n", msg); } \
} while (0)

int main(void) {
    CHECK(sizeof(UBO_PBR) >= 700, "UBO_PBR size >= 700");
    CHECK(sizeof(UBO_PBR) % 16 == 0, "UBO_PBR 16-byte aligned");

    /* Key offsets (std140: mat4=64, vec4=16) */
    CHECK(offsetof(UBO_PBR, mvp) == 0, "mvp at 0");
    CHECK(offsetof(UBO_PBR, model) == 64, "model at 64");
    CHECK(offsetof(UBO_PBR, normalMat) == 128, "normalMat at 128");
    CHECK(offsetof(UBO_PBR, cameraPos) == 192, "cameraPos at 192");
    CHECK(offsetof(UBO_PBR, specularExt) > 0, "specularExt present");
    CHECK(offsetof(UBO_PBR, khrIriDisp) > offsetof(UBO_PBR, specularExt), "khr flags after specular");

    /* Binding enum */
    CHECK(VLCR_BIND_UBO_PBR == 0, "bind UBO = 0");
    CHECK(VLCR_BIND_ALBEDO == 1, "bind albedo = 1");
    CHECK(VLCR_BIND_SSAO == 15, "bind SSAO = 15");
    CHECK(VLCR_BIND_COUNT == 16, "bind count = 16");

    /* Material flags */
    CHECK(VLCR_MATFLAG_UNLIT == 1, "flag unlit");
    CHECK(VLCR_MATFLAG_ALPHABLEND == 4, "flag blend");
    CHECK(VLCR_MATFLAG_SPECULAR_MAP == 32, "flag specular map");
    CHECK(VLCR_MATFLAG_CLEARCOAT_MAP == 64, "flag clearcoat map");

    printf("UBO_PBR sizeof = %zu\n", sizeof(UBO_PBR));
    printf("\n%d failures\n", g_fail);
    return g_fail ? 1 : 0;
}
