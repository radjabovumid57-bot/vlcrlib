/**
 * Unit tests for vmath (pure functions, no Vulkan/GPU required).
 * Build: gcc -I../src tests/test_math.c -o tests/test_math && ./tests/test_math
 */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "vmath.h"

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); g_fail++; } \
    else { printf("OK: %s\n", msg); } \
} while (0)

static int nearly(float a, float b) { return fabsf(a - b) < 1e-4f; }

int main(void) {
    /* Identity */
    Matrix I = MatrixIdentity();
    CHECK(nearly(I.m0, 1.f) && nearly(I.m5, 1.f) && nearly(I.m10, 1.f) && nearly(I.m15, 1.f),
          "MatrixIdentity diagonal");

    /* Multiply by identity */
    Matrix T = MatrixTranslate(1.f, 2.f, 3.f);
    Matrix TI = MatrixMultiply(T, I);
    CHECK(nearly(TI.m12, 1.f) && nearly(TI.m13, 2.f) && nearly(TI.m14, 3.f),
          "Translate * Identity");

    /* Scale */
    Matrix S = MatrixScale(2.f, 3.f, 4.f);
    CHECK(nearly(S.m0, 2.f) && nearly(S.m5, 3.f) && nearly(S.m10, 4.f),
          "MatrixScale");

    /* LookAt basic */
    Vector3 eye = {0, 0, 5};
    Vector3 target = {0, 0, 0};
    Vector3 up = {0, 1, 0};
    Matrix V = MatrixLookAt(eye, target, up);
    CHECK(nearly(V.m14, -5.f) && nearly(V.m0, 1.f) &&
          nearly(V.m5, 1.f), "MatrixLookAt constructed");

    /* Ortho */
    Matrix O = MatrixOrtho(-1, 1, -1, 1, 0.1f, 100.f);
    CHECK(O.m15 != 0.f, "MatrixOrtho non-zero");

    /* Two-bone IK */
    Vector3 ikRoot = {0, 0, 0}, ikMid = {1, 0, 0}, ikEnd = {2, 0, 0};
    Vector3 ikTarget = {1.0f, 1.0f, 0.0f}, ikPole = {0, 0, 1};
    Vector3 ikMidOut, ikEndOut;
    CHECK(Vector3SolveTwoBoneIK(ikRoot, ikMid, ikEnd, ikTarget, ikPole,
                                &ikMidOut, &ikEndOut),
          "two-bone IK solves");
    CHECK(nearly(Vector3Distance(ikRoot, ikMidOut), 1.0f),
          "IK upper bone length preserved");
    CHECK(nearly(Vector3Distance(ikMidOut, ikEndOut), 1.0f),
          "IK lower bone length preserved");
    CHECK(Vector3Distance(ikEndOut, ikTarget) < 1e-3f,
          "IK reaches target");

    /* TRS decomposition round-trip */
    Vector3 tp = {1, 2, 3}, tr = {0.3f, 0.5f, -0.2f}, ts = {2, 3, 4};
    Matrix m = MatrixMultiply(
        MatrixTranslate(tp.x, tp.y, tp.z),
        MatrixMultiply(MatrixRotateXYZ(tr),
                       MatrixScale(ts.x, ts.y, ts.z)));
    Vector3 dt, dr, ds;
    CHECK(MatrixDecomposeTRS(m, &dt, &dr, &ds), "TRS decompose");
    Matrix rebuilt = MatrixMultiply(
        MatrixTranslate(dt.x, dt.y, dt.z),
        MatrixMultiply(MatrixRotateXYZ(dr),
                       MatrixScale(ds.x, ds.y, ds.z)));
    CHECK(nearly(rebuilt.m12, m.m12) && nearly(rebuilt.m13, m.m13) &&
          nearly(rebuilt.m14, m.m14), "TRS translation round-trip");
    CHECK(nearly(rebuilt.m0, m.m0) && nearly(rebuilt.m1, m.m1) &&
          nearly(rebuilt.m2, m.m2), "TRS rotation round-trip");

    /* Negative scale must round-trip as a negative X scale, not as rotation. */
    Matrix neg = MatrixMultiply(
        MatrixTranslate(0.5f, -1.0f, 2.0f),
        MatrixMultiply(MatrixRotateXYZ((Vector3){0.2f, -0.4f, 0.1f}),
                       MatrixScale(-2.0f, 3.0f, 4.0f)));
    Vector3 nt, nr, ns;
    CHECK(MatrixDecomposeTRS(neg, &nt, &nr, &ns), "negative TRS decompose");
    CHECK(ns.x < 0.0f, "negative X scale preserved");
    Matrix negRebuilt = MatrixMultiply(
        MatrixTranslate(nt.x, nt.y, nt.z),
        MatrixMultiply(MatrixRotateXYZ(nr),
                       MatrixScale(ns.x, ns.y, ns.z)));
    CHECK(nearly(negRebuilt.m0, neg.m0) && nearly(negRebuilt.m1, neg.m1) &&
          nearly(negRebuilt.m2, neg.m2) && nearly(negRebuilt.m8, neg.m8) &&
          nearly(negRebuilt.m9, neg.m9) && nearly(negRebuilt.m10, neg.m10),
          "negative TRS rotation round-trip");

    /* Affine inverse round-trip. */
    Matrix inv = MatrixInvert(m);
    Matrix prod = MatrixMultiply(m, inv);
    CHECK(nearly(prod.m0, 1.f) && nearly(prod.m5, 1.f) &&
          nearly(prod.m10, 1.f) && nearly(prod.m15, 1.f) &&
          nearly(prod.m12, 0.f) && nearly(prod.m13, 0.f) &&
          nearly(prod.m14, 0.f), "MatrixInvert affine round-trip");

    /* Degenerate projection inputs must not produce NaNs. */
    Matrix p = MatrixPerspective(0.f, 0.f, 0.f, 0.f);
    CHECK(isfinite(p.m0) && isfinite(p.m5) && isfinite(p.m10) &&
          isfinite(p.m14), "MatrixPerspective degenerate inputs");

    printf("\n%d failures\n", g_fail);
    return g_fail ? 1 : 0;
}
