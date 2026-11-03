/**
 * Unit tests for material flags, alpha modes, and binding constants.
 */
#include <stdio.h>
#include "vgraphics_ubo.h"

static int g_fail = 0;
#define CHECK(c, m) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", m); g_fail++; } else printf("OK: %s\n", m); } while (0)

int main(void) {
    /* Flags are bitmasks */
    CHECK((VLCR_MATFLAG_UNLIT & VLCR_MATFLAG_ALPHAMASK) == 0, "unlit != mask");
    CHECK((VLCR_MATFLAG_ALPHABLEND | VLCR_MATFLAG_ALPHAMASK) != 0, "blend|mask nonzero");
    CHECK(VLCR_MATFLAG_DOUBLESIDED == 8, "double sided bit");
    CHECK(VLCR_MATFLAG_SPECULAR_MAP == 32, "specular map bit");
    CHECK(VLCR_MATFLAG_CLEARCOAT_MAP == 64, "clearcoat map bit");

    /* Combined flags */
    int flags = VLCR_MATFLAG_UNLIT | VLCR_MATFLAG_DOUBLESIDED;
    CHECK((flags & VLCR_MATFLAG_UNLIT) != 0, "combined unlit");
    CHECK((flags & VLCR_MATFLAG_DOUBLESIDED) != 0, "combined double");
    CHECK((flags & VLCR_MATFLAG_ALPHABLEND) == 0, "combined no blend");

    /* Bindings monotonic */
    CHECK(VLCR_BIND_NORMAL > VLCR_BIND_ALBEDO, "normal > albedo");
    CHECK(VLCR_BIND_SHADOW_DIR > VLCR_BIND_EMISSION, "shadow > emissive");
    CHECK(VLCR_BIND_SSAO == VLCR_BIND_COUNT - 1, "ssao is last");

    /* Expected min size */
    CHECK(VLCR_UBO_PBR_EXPECTED_MIN >= 700, "expected min sane");

    printf("\n%d failures\n", g_fail);
    return g_fail ? 1 : 0;
}
