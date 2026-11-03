/* test_coords.c — locks the vlcrlib coordinate contract.
 *
 * Contract (see the conventions block in src/vmath.h):
 *   world  : right-handed, Y up, floor = XZ plane at y = 0, gravity (0,-9.81,0)
 *   camera : look-at, up = (0,1,0)
 *   clip   : Vulkan, x/y in [-1,1], z in [0,1]  (near -> 0, far -> 1)
 *   screen : framebuffer origin TOP-LEFT, y grows DOWN
 *            3D  -> positive-height viewport, projection negates Y
 *            2D  -> negative-height viewport, ortho with y = 0 at the top
 *
 * No GPU required. Build with -DVLCRLIB_BUILD_TESTS=ON.
 */
#include "vmath.h"
#include <stdio.h>
#include <math.h>

static int g_fail = 0;
static int g_pass = 0;

static void check(int cond, const char *what) {
    if (cond) { g_pass++; }
    else      { g_fail++; printf("FAIL: %s\n", what); }
}

static void check_near(float got, float want, float eps, const char *what) {
    if (fabsf(got - want) <= eps) { g_pass++; }
    else { g_fail++; printf("FAIL: %s (got %g, want %g)\n", what, got, want); }
}

/* Apply a 4x4 matrix to a point.
 *
 * Matrix is declared as `m0, m4, m8, m12, m1, m5, ...`, i.e. the struct's
 * MEMORY order is row-major while the field names are column-major
 * (mCR = row R, column C). Reading the struct as a flat array therefore
 * gives mem[r*4 + c] = M(r,c), which is what we need here. */
static void xform(const Matrix *m, const float p[4], float out[4]) {
    const float *e = &m->m0;
    for (int r = 0; r < 4; ++r) {
        out[r] = e[r*4 + 0] * p[0] + e[r*4 + 1] * p[1] +
                 e[r*4 + 2] * p[2] + e[r*4 + 3] * p[3];
    }
}

/* world -> clip -> NDC -> framebuffer pixels, exactly as the GPU does it. */
typedef struct { float ndc_x, ndc_y, ndc_z, w; int fb_x, fb_y; } ScreenPos;

static ScreenPos project(const Matrix *viewProj, float x, float y, float z,
                         int fbw, int viewportHeight /* +h or -h */,
                         int viewportY) {
    ScreenPos s = {0};
    float p[4] = { x, y, z, 1.0f }, c[4];
    xform(viewProj, p, c);
    if (c[3] == 0.0f) { s.w = 0.0f; return s; }
    s.ndc_x = c[0] / c[3];
    s.ndc_y = c[1] / c[3];
    s.ndc_z = c[2] / c[3];
    s.w = c[3];
    /* Vulkan viewport transform */
    float vx = 0.0f, vy = (float)viewportY, vw = (float)fbw, vh = (float)viewportHeight;
    s.fb_x = (int)(vx + vw * 0.5f * (s.ndc_x + 1.0f));
    s.fb_y = (int)(vy + vh * 0.5f * (s.ndc_y + 1.0f));
    return s;
}

int main(void) {
    const int FBW = 800, FBH = 600;

    /* A camera above the floor, looking at a point slightly above it. */
    Vector3 eye    = { 0.0f, 5.0f, 10.0f };
    Vector3 target = { 0.0f, 0.0f, 0.0f };
    Vector3 up     = { 0.0f, 1.0f, 0.0f };

    Matrix view = MatrixLookAt(eye, target, up);
    Matrix proj = MatrixPerspective(45.0f * VLCRLIB_DEG2RAD,
                                    (float)FBW / (float)FBH, 0.1f, 100.0f);
    Matrix vp   = MatrixMultiply(proj, view);

    /* ---------------------------------------------------------------- */
    printf("== right-handed view (camera looks down -Z) ==\n");
    {
        /* A point in FRONT of the camera must land at positive w (= -z_view)
           and be inside the frustum; a point BEHIND must give w < 0. */
        float p[4] = { eye.x, eye.y, eye.z, 1.0f }, c[4];
        Vector3 fwd = Vector3Normalize(Vector3Subtract(target, eye));
        Vector3 ahead = Vector3Add(eye, Vector3Scale(fwd, 3.0f));
        p[0] = ahead.x; p[1] = ahead.y; p[2] = ahead.z;
        xform(&view, p, c);
        check(c[2] < 0.0f, "a point in front of the camera has view z < 0 (right-handed)");
        check(c[3] == 1.0f, "view matrix keeps w = 1");
    }

    /* ---------------------------------------------------------------- */
    printf("== Vulkan ZO depth: near -> 0, far -> 1 ==\n");
    {
        const float zn = 0.1f, zf = 100.0f;
        float nearPt[4] = { eye.x, eye.y, eye.z - zn, 1.0f };
        float farPt[4]  = { eye.x, eye.y, eye.z - zf, 1.0f };
        float cn[4], cf[4];
        Vector3 fwd = Vector3Normalize(Vector3Subtract(target, eye));
        nearPt[0] = eye.x + fwd.x*zn; nearPt[1] = eye.y + fwd.y*zn; nearPt[2] = eye.z + fwd.z*zn;
        farPt[0]  = eye.x + fwd.x*zf; farPt[1]  = eye.y + fwd.y*zf; farPt[2]  = eye.z + fwd.z*zf;
        xform(&vp, nearPt, cn);
        xform(&vp, farPt,  cf);
        if (cn[3] != 0.0f) check_near(cn[2] / cn[3], 0.0f, 0.002f, "near plane maps to depth 0");
        if (cf[3] != 0.0f) check_near(cf[2] / cf[3], 1.0f, 0.002f, "far plane maps to depth 1");

        /* Nothing inside the frustum may produce a negative NDC depth:
           Vulkan clips z < 0, which is what silently ate the near half of
           the depth range while the matrix was OpenGL-style. */
        int negative = 0;
        for (int i = 1; i <= 20; ++i) {
            float d = zn + (zf - zn) * (float)i / 20.0f;
            if (d >= zf) continue;
            float q[4] = { eye.x + fwd.x*d, eye.y + fwd.y*d, eye.z + fwd.z*d, 1.0f }, cc[4];
            xform(&vp, q, cc);
            if (cc[3] != 0.0f && (cc[2] / cc[3]) < -1e-4f) negative++;
        }
        check(negative == 0, "no in-frustum point has negative NDC depth (ZO convention)");
    }

    /* ---------------------------------------------------------------- */
    printf("== Y up on screen (3D path: positive viewport, Y-negating proj) ==\n");
    {
        const int VPY = 0, VPH = FBH;   /* positive height, origin top-left */
        ScreenPos center = project(&vp, target.x, target.y, target.z, FBW, VPH, VPY);
        ScreenPos above  = project(&vp, 0.0f, 3.0f, 0.0f, FBW, VPH, VPY);
        ScreenPos floor_ = project(&vp, 0.0f, 0.0f, 3.0f, FBW, VPH, VPY);

        check(above.w > 0.0f && center.w > 0.0f, "points are in front of the camera");
        check(above.fb_y < center.fb_y,
              "world +Y renders ABOVE the target (smaller framebuffer y)");
        check(center.fb_y > 0 && center.fb_y < FBH, "target is inside the framebuffer");
        check(floor_.fb_y > center.fb_y,
              "the floor point nearer the camera renders LOWER on screen");
        /* The floor must appear in the bottom half, not the top half:
           with the camera above the plane this is the 180-degree tell. */
        check(floor_.fb_y > FBH / 2, "floor stays in the bottom half of the screen");
    }

    /* ---------------------------------------------------------------- */
    printf("== X right on screen ==\n");
    {
        const int VPY = 0, VPH = FBH;
        ScreenPos right = project(&vp, 3.0f, 0.0f, 0.0f, FBW, VPH, VPY);
        ScreenPos left  = project(&vp, -3.0f, 0.0f, 0.0f, FBW, VPH, VPY);
        check(right.fb_x > FBW / 2, "world +X renders on the right half");
        check(left.fb_x < FBW / 2, "world -X renders on the left half");
        check(right.fb_x > left.fb_x, "+X is to the right of -X");
    }

    /* ---------------------------------------------------------------- */
    printf("== 2D path: (0,0) is the top-left corner ==\n");
    {
        Matrix ortho = MatrixOrtho(0.0f, (float)FBW, (float)FBH, 0.0f, -1.0f, 1.0f);
        const int VPY = FBH, VPH = -FBH;   /* negative height, as vgraphics uses */
        ScreenPos tl = project(&ortho, 0.0f, 0.0f, 0.0f, FBW, VPH, VPY);
        ScreenPos br = project(&ortho, (float)FBW, (float)FBH, 0.0f, FBW, VPH, VPY);
        ScreenPos tr = project(&ortho, (float)FBW, 0.0f, 0.0f, FBW, VPH, VPY);

        check(tl.fb_x == 0 && tl.fb_y == 0, "2D (0,0) -> framebuffer (0,0) top-left");
        check(tr.fb_x == FBW && tr.fb_y == 0, "2D (w,0) -> top-right");
        check(br.fb_x == FBW && br.fb_y == FBH, "2D (w,h) -> bottom-right");
        check(tl.ndc_z >= 0.0f && tl.ndc_z <= 1.0f, "2D depth stays inside [0,1]");
    }

    /* ---------------------------------------------------------------- */
    printf("== orthographic (shadow) projection also uses ZO depth ==\n");
    {
        const float r = 10.0f, zn = 1.0f, zf = 50.0f;
        Matrix o = MatrixOrtho(-r, r, -r, r, zn, zf);
        /* View-space -zn (the near plane) must give 0, -zf must give 1. */
        float pn[4] = { 0, 0, -zn, 1 }, pf[4] = { 0, 0, -zf, 1 }, cn[4], cf[4];
        xform(&o, pn, cn);
        xform(&o, pf, cf);
        check_near(cn[2], 0.0f, 0.001f, "ortho near plane -> depth 0");
        check_near(cf[2], 1.0f, 0.001f, "ortho far plane -> depth 1");
        check_near(cn[0], 0.0f, 1e-5f, "ortho centre maps to x = 0");
    }

    /* ---------------------------------------------------------------- */
    printf("== degenerate inputs do not produce NaN ==\n");
    {
        Matrix p0 = MatrixPerspective(0.0f, 0.0f, 0.0f, 0.0f);
        Matrix o0 = MatrixOrtho(0, 0, 0, 0, 0, 0);
        int bad = 0;
        for (int i = 0; i < 16; ++i) {
            float a = (&p0.m0)[i], b = (&o0.m0)[i];
            if (!isfinite(a) || !isfinite(b)) bad++;
        }
        check(bad == 0, "singular inputs are clamped instead of producing NaN/Inf");
    }

    /* ---------------------------------------------------------------- */
    printf("== look-at survives a camera pointing straight down ==\n");
    {
        /* up == (0,1,0) parallel to the view direction: the naive cross
           product is zero and the view matrix becomes NaN. */
        Matrix v = MatrixLookAt((Vector3){0, 5, 0}, (Vector3){0, 0, 0},
                                (Vector3){0, 1, 0});
        int bad = 0;
        for (int i = 0; i < 16; ++i)
            if (!isfinite((&v.m0)[i])) bad++;
        check(bad == 0, "straight-down look-at produces finite values");

        /* The floor point in front must still be in front of the camera. */
        float p[4] = { 0.0f, 0.0f, 1.0f, 1.0f }, c[4];
        xform(&v, p, c);
        check(isfinite(c[2]) && c[2] < 0.0f,
              "straight-down look-at keeps the scene in front of the camera (view z < 0)");
    }

    /* ---------------------------------------------------------------- */
    printf("== negative control: the old GL-style depth must FAIL ==\n");
    {
        /* Exactly the matrix this library used before the ZO fix. If this
           block ever stops detecting the problem, the checks above are not
           proving anything. */
        const float zn = 0.1f, zf = 100.0f;
        Matrix gl = {0};
        float f = 1.0f / tanf(45.0f * VLCRLIB_DEG2RAD * 0.5f);
        gl.m0  = f;
        gl.m5  = -f;
        gl.m10 = (zf + zn) / (zn - zf);         /* GL [-1,1] depth */
        gl.m11 = -1.0f;
        gl.m14 = (2.0f * zf * zn) / (zn - zf);

        Vector3 fwd = Vector3Normalize(Vector3Subtract(target, eye));
        float np[4] = { eye.x + fwd.x*zn, eye.y + fwd.y*zn, eye.z + fwd.z*zn, 1.0f };
        float c[4];
        Matrix glVP = MatrixMultiply(gl, view);   /* projection AND view */
        xform(&glVP, np, c);
        float ndc_z = (c[3] != 0.0f) ? c[2] / c[3] : 0.0f;
        check(ndc_z < -0.5f,
              "negative control: GL-style depth puts the near plane at z < 0 (clipped by Vulkan)");
        printf("   (GL-style near-plane NDC depth = %.3f, ours = 0.000)\n", ndc_z);
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
