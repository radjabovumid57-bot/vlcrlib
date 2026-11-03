/* vcpu_web.c — standalone WebAssembly CPU renderer with a small pass graph.
 *
 * Passes: scene -> SSAO -> bloom -> TAA -> present.
 * This is a software approximation of the Vulkan/WebGPU graph, not a port of
 * WebGPU itself.
 */
#include "vcpu.h"
#include "vmath.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

static VCpuCanvas g_canvas;
static uint32_t *g_history;
static uint32_t *g_post;
static uint32_t *g_bright;
static uint32_t *g_blur;
static int g_historyReady;
static VCpuCanvas g_shadow[2];
static Matrix g_lightVP[2];
static int g_shadowReady;

static uint32_t pack(Color c) {
    return ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) |
           ((uint32_t)c.g << 8) | c.b;
}
static Color unpack(uint32_t p) {
    return (Color){ (p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF, (p >> 24) & 0xFF };
}
static uint32_t mulColor(uint32_t p, float f) {
    Color c = unpack(p);
    c.r = (unsigned char)fminf(255.0f, c.r * f);
    c.g = (unsigned char)fminf(255.0f, c.g * f);
    c.b = (unsigned char)fminf(255.0f, c.b * f);
    return pack(c);
}
static uint32_t addColor(uint32_t a, uint32_t b, float f) {
    Color ca = unpack(a), cb = unpack(b);
    ca.r = (unsigned char)fminf(255.0f, ca.r + cb.r * f);
    ca.g = (unsigned char)fminf(255.0f, ca.g + cb.g * f);
    ca.b = (unsigned char)fminf(255.0f, ca.b + cb.b * f);
    return pack(ca);
}

static void boxAt(VCpuCanvas *cv, Matrix mvp, Matrix bone, Vector3 center,
                  Vector3 half, Color color) {
    const Vector3 p[8] = {
        {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},
        {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}
    };
    const int f[6][4] = {
        {0,1,2,3}, {4,5,6,7}, {0,1,5,4},
        {2,3,7,6}, {1,2,6,5}, {3,0,4,7}
    };
    Vector3 w[8];
    for (int i = 0; i < 8; ++i) {
        Vector3 v = {
            center.x + p[i].x * half.x,
            center.y + p[i].y * half.y,
            center.z + p[i].z * half.z
        };
        w[i].x = bone.m0 * v.x + bone.m4 * v.y + bone.m8 * v.z + bone.m12;
        w[i].y = bone.m1 * v.x + bone.m5 * v.y + bone.m9 * v.z + bone.m13;
        w[i].z = bone.m2 * v.x + bone.m6 * v.y + bone.m10 * v.z + bone.m14;
    }
    for (int i = 0; i < 6; ++i) {
        VCpuTriangle3D(cv, w[f[i][0]], w[f[i][1]], w[f[i][2]], color, mvp);
        VCpuTriangle3D(cv, w[f[i][0]], w[f[i][2]], w[f[i][3]], color, mvp);
    }
}

static Vector3 skinPoint4(Vector3 v, const Matrix b[4], const float w[4]) {
    Vector3 o = {0, 0, 0};
    for (int i = 0; i < 4; ++i) {
        o.x += w[i] * (b[i].m0 * v.x + b[i].m4 * v.y + b[i].m8 * v.z + b[i].m12);
        o.y += w[i] * (b[i].m1 * v.x + b[i].m5 * v.y + b[i].m9 * v.z + b[i].m13);
        o.z += w[i] * (b[i].m2 * v.x + b[i].m6 * v.y + b[i].m10 * v.z + b[i].m14);
    }
    return o;
}

/* Four-bone hierarchical skinned tube with four normalized vertex weights. */
static void skinnedLimb(VCpuCanvas *cv, Matrix mvp, float t) {
    Matrix root = MatrixTranslate(1.35f, 0.0f, 0.0f);
    Matrix b[4];
    b[0] = MatrixMultiply(root, MatrixRotateZ(sinf(t) * 0.25f));
    b[1] = MatrixMultiply(b[0], MatrixMultiply(MatrixTranslate(0, -0.5f, 0),
        MatrixMultiply(MatrixRotateZ(sinf(t * 1.1f) * 0.35f),
                       MatrixTranslate(0, 0.5f, 0))));
    b[2] = MatrixMultiply(b[1], MatrixRotateZ(sinf(t * 0.9f + 1.0f) * 0.35f));
    b[3] = MatrixMultiply(b[2], MatrixMultiply(MatrixTranslate(0, 0.5f, 0),
        MatrixMultiply(MatrixRotateZ(sinf(t * 1.3f + 2.0f) * 0.40f),
                       MatrixTranslate(0, -0.5f, 0))));

    enum { RINGS = 9, SIDES = 4 };
    const float ys[RINGS] = {-1.0f, -0.75f, -0.5f, -0.25f, 0.0f,
                             0.25f, 0.5f, 0.75f, 1.0f};
    const float centers[4] = {-0.75f, -0.25f, 0.25f, 0.75f};
    const float radius = 0.20f;
    const float cx[SIDES] = {-1, 1, 1, -1};
    const float cz[SIDES] = {-1, -1, 1, 1};
    const Color sideColor[SIDES] = {
        {80, 160, 220, 255}, {220, 120, 80, 255},
        {90, 200, 150, 255}, {200, 180, 80, 255}
    };

    Vector3 p[RINGS][SIDES];
    for (int r = 0; r < RINGS; ++r) {
        float y = ys[r];
        float w[4];
        float sum = 0.0f;
        for (int i = 0; i < 4; ++i) {
            w[i] = fmaxf(0.0f, 1.0f - fabsf(y - centers[i]) / 0.55f);
            sum += w[i];
        }
        if (sum < 1e-6f) { w[0] = 1.0f; sum = 1.0f; }
        for (int i = 0; i < 4; ++i) w[i] /= sum;
        for (int s = 0; s < SIDES; ++s) {
            Vector3 v = {cx[s] * radius, y, cz[s] * radius};
            p[r][s] = skinPoint4(v, b, w);
        }
    }
    for (int r = 0; r + 1 < RINGS; ++r) {
        for (int s = 0; s < SIDES; ++s) {
            int n = (s + 1) % SIDES;
            Color col = sideColor[s];
            /* simple light-space shade per ring */
            float shade = 0.75f + 0.25f * ((float)r / (RINGS - 1));
            col.r = (unsigned char)(col.r * shade);
            col.g = (unsigned char)(col.g * shade);
            col.b = (unsigned char)(col.b * shade);
            VCpuTriangle3D(cv, p[r][s], p[r][n], p[r + 1][n], col, mvp);
            VCpuTriangle3D(cv, p[r][s], p[r + 1][n], p[r + 1][s], col, mvp);
        }
    }
    for (int s = 1; s + 1 < SIDES; ++s) {
        VCpuTriangle3D(cv, p[0][0], p[0][s], p[0][s + 1],
                       (Color){70, 90, 120, 255}, mvp);
        VCpuTriangle3D(cv, p[RINGS - 1][0], p[RINGS - 1][s],
                       p[RINGS - 1][s + 1], (Color){150, 120, 70, 255}, mvp);
    }
}

static void cube(VCpuCanvas *cv, Matrix mvp) {
    const float s = 0.7f;
    const Vector3 p[8] = {
        {-s,-s,-s}, { s,-s,-s}, { s, s,-s}, {-s, s,-s},
        {-s,-s, s}, { s,-s, s}, { s, s, s}, {-s, s, s}
    };
    const int f[6][4] = {
        {0,1,2,3}, {4,5,6,7}, {0,1,5,4},
        {2,3,7,6}, {1,2,6,5}, {3,0,4,7}
    };
    const Color faceColor[6] = {
        {220, 120, 70, 255}, {80, 140, 220, 255}, {220, 190, 80, 255},
        {90, 190, 120, 255}, {190, 90, 180, 255}, {70, 180, 200, 255}
    };
    for (int i = 0; i < 6; ++i) {
        Vector3 a = p[f[i][0]], b = p[f[i][1]];
        Vector3 c = p[f[i][2]], d = p[f[i][3]];
        VCpuTriangle3D(cv, a, b, c, faceColor[i], mvp);
        VCpuTriangle3D(cv, a, c, d, faceColor[i], mvp);
    }
}

static void pass_ssao(void) {
    int w = g_canvas.width, h = g_canvas.height;
    static const int dx[8] = {-3, 3, 0, 0, -2, 2, -2, 2};
    static const int dy[8] = {0, 0, -3, 3, -2, -2, 2, 2};
    for (int y = 3; y < h - 3; ++y) {
        for (int x = 3; x < w - 3; ++x) {
            size_t i = (size_t)y * w + x;
            float z = g_canvas.depth[i];
            if (z >= 0.999f) continue;
            int occ = 0;
            for (int k = 0; k < 8; ++k) {
                float nz = g_canvas.depth[(size_t)(y + dy[k]) * w + x + dx[k]];
                if (nz < z - 0.008f) ++occ;
            }
            if (occ) g_canvas.color[i] = mulColor(g_canvas.color[i], 1.0f - occ * 0.045f);
        }
    }
}

static void pass_bloom(void) {
    int w = g_canvas.width, h = g_canvas.height;
    for (int i = 0; i < w * h; ++i) {
        Color c = unpack(g_canvas.color[i]);
        float lum = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
        g_bright[i] = lum > 150.0f ? pack(c) : 0;
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint32_t sum = 0; int n = 0;
            for (int ky = -2; ky <= 2; ++ky) {
                int sy = y + ky; if (sy < 0 || sy >= h) continue;
                for (int kx = -2; kx <= 2; ++kx) {
                    int sx = x + kx; if (sx < 0 || sx >= w) continue;
                    uint32_t p = g_bright[(size_t)sy * w + sx];
                    if (p) { sum = addColor(sum, p, 1.0f); ++n; }
                }
            }
            g_blur[(size_t)y * w + x] = n ? mulColor(sum, 1.0f / n) : 0;
        }
    }
    for (int i = 0; i < w * h; ++i)
        g_canvas.color[i] = addColor(g_canvas.color[i], g_blur[i], 0.85f);
}

static void pass_taa(void) {
    int w = g_canvas.width, h = g_canvas.height;
    if (!g_historyReady) {
        for (int i = 0; i < w * h; ++i) g_history[i] = g_canvas.color[i];
        g_historyReady = 1;
        return;
    }
    for (int i = 0; i < w * h; ++i) {
        Color cur = unpack(g_canvas.color[i]);
        Color prev = unpack(g_history[i]);
        float f = g_canvas.depth[i] < 0.999f ? 0.9f : 0.5f;
        cur.r = (unsigned char)(cur.r * f + prev.r * (1 - f));
        cur.g = (unsigned char)(cur.g * f + prev.g * (1 - f));
        cur.b = (unsigned char)(cur.b * f + prev.b * (1 - f));
        g_post[i] = pack(cur);
        g_history[i] = g_post[i];
    }
    for (int i = 0; i < w * h; ++i) g_canvas.color[i] = g_post[i];
}

/* Full 4x4 inverse (Gauss-Jordan); vmath's MatrixInvert is affine-only. */
static Matrix invert4(Matrix m) {
    float a[4][8] = {
        {m.m0, m.m4, m.m8,  m.m12, 1, 0, 0, 0},
        {m.m1, m.m5, m.m9,  m.m13, 0, 1, 0, 0},
        {m.m2, m.m6, m.m10, m.m14, 0, 0, 1, 0},
        {m.m3, m.m7, m.m11, m.m15, 0, 0, 0, 1}
    };
    for (int i = 0; i < 4; ++i) {
        int pivot = i;
        for (int r = i + 1; r < 4; ++r)
            if (fabsf(a[r][i]) > fabsf(a[pivot][i])) pivot = r;
        if (fabsf(a[pivot][i]) < 1e-9f) return MatrixIdentity();
        if (pivot != i)
            for (int c = 0; c < 8; ++c) {
                float tmp = a[i][c]; a[i][c] = a[pivot][c]; a[pivot][c] = tmp;
            }
        float inv = 1.0f / a[i][i];
        for (int c = 0; c < 8; ++c) a[i][c] *= inv;
        for (int r = 0; r < 4; ++r) {
            if (r == i) continue;
            float f = a[r][i];
            if (f == 0.0f) continue;
            for (int c = 0; c < 8; ++c) a[r][c] -= f * a[i][c];
        }
    }
    Matrix r;
    r.m0 = a[0][4]; r.m4 = a[0][5]; r.m8 = a[0][6]; r.m12 = a[0][7];
    r.m1 = a[1][4]; r.m5 = a[1][5]; r.m9 = a[1][6]; r.m13 = a[1][7];
    r.m2 = a[2][4]; r.m6 = a[2][5]; r.m10 = a[2][6]; r.m14 = a[2][7];
    r.m3 = a[3][4]; r.m7 = a[3][5]; r.m11 = a[3][6]; r.m15 = a[3][7];
    return r;
}

static Vector3 xform4(Matrix m, float x, float y, float z) {
    float w = m.m3 * x + m.m7 * y + m.m11 * z + m.m15;
    if (fabsf(w) < 1e-8f) w = 1e-8f;
    Vector3 o = {
        (m.m0 * x + m.m4 * y + m.m8 * z + m.m12) / w,
        (m.m1 * x + m.m5 * y + m.m9 * z + m.m13) / w,
        (m.m2 * x + m.m6 * y + m.m10 * z + m.m14) / w
    };
    return o;
}

static float shadowAt(int cascade, Vector3 world) {
    const VCpuCanvas *sm = &g_shadow[cascade];
    Vector3 n = xform4(g_lightVP[cascade], world.x, world.y, world.z);
    float u = n.x * 0.5f + 0.5f;
    float v = 0.5f - n.y * 0.5f;
    if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f ||
        n.z < 0.0f || n.z > 1.0f) return 1.0f;
    int cx = (int)(u * sm->width);
    int cy = (int)(v * sm->height);
    float lit = 0.0f, total = 0.0f;
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            int sx = cx + ox, sy = cy + oy;
            if (sx < 0 || sy < 0 || sx >= sm->width || sy >= sm->height) continue;
            float d = sm->depth[(size_t)sy * sm->width + sx];
            total += 1.0f;
            if (n.z - 0.0035f <= d) lit += 1.0f;
        }
    }
    return total > 0.0f ? lit / total : 1.0f;
}

static void render_shadow_map(int cascade, Matrix lightVP, float t) {
    VCpuClearDepth(&g_shadow[cascade]);
    cube(&g_shadow[cascade], lightVP);
    skinnedLimb(&g_shadow[cascade], lightVP, t);
}

static void pass_ground(Matrix mvp, float t) {
    (void)t;
    int w = g_canvas.width, h = g_canvas.height;
    Matrix inv = invert4(mvp);
    for (int y = 0; y < h; ++y) {
        float ny = 1.0f - ((y + 0.5f) / h) * 2.0f;
        for (int x = 0; x < w; ++x) {
            float nx = ((x + 0.5f) / w) * 2.0f - 1.0f;
            Vector3 pn = xform4(inv, nx, ny, 0.0f);
            Vector3 pf = xform4(inv, nx, ny, 1.0f);
            float dy = pf.y - pn.y;
            if (fabsf(dy) < 1e-6f) continue;
            float k = (-1.0f - pn.y) / dy;
            if (k < 0.0f || k > 1.0f) continue;
            Vector3 wp = {pn.x + (pf.x - pn.x) * k, -1.0f,
                          pn.z + (pf.z - pn.z) * k};
            if (wp.x < -7.0f || wp.x > 7.0f || wp.z < -7.0f || wp.z > 7.0f)
                continue;
            Vector3 c = xform4(mvp, wp.x, wp.y, wp.z);
            if (c.z < 0.0f || c.z > 1.0f) continue;
            size_t i = (size_t)y * w + x;
            if (c.z >= g_canvas.depth[i]) continue;
            float dist = sqrtf(wp.x * wp.x + wp.z * wp.z);
            int cascade = dist < 3.0f ? 0 : 1;
            float sh = shadowAt(cascade, wp);
            float grid = (fmodf(fabsf(wp.x), 1.0f) < 0.05f ||
                          fmodf(fabsf(wp.z), 1.0f) < 0.05f) ? 0.18f : 0.0f;
            float b = (0.28f + 0.62f * sh) * (1.0f - grid);
            Color col = { (unsigned char)(55 * b), (unsigned char)(62 * b),
                          (unsigned char)(78 * b), 255 };
            g_canvas.depth[i] = c.z;
            g_canvas.color[i] = pack(col);
        }
    }
}

int vcpu_init(int width, int height) {
    if (!VCpuInit(&g_canvas, width, height)) return 0;
    size_t n = (size_t)width * height;
    g_history = (uint32_t *)calloc(n, sizeof(uint32_t));
    g_post = (uint32_t *)calloc(n, sizeof(uint32_t));
    g_bright = (uint32_t *)calloc(n, sizeof(uint32_t));
    g_blur = (uint32_t *)calloc(n, sizeof(uint32_t));
    g_historyReady = 0;
    g_shadowReady = VCpuInit(&g_shadow[0], 512, 512) &&
                    VCpuInit(&g_shadow[1], 512, 512);
    return (g_history && g_post && g_bright && g_blur && g_shadowReady) ? 1 : 0;
}

unsigned int vcpu_frame(float t) {
    if (!g_canvas.color || !g_history || !g_post || !g_bright || !g_blur ||
        !g_shadowReady) return 0;
    Matrix view = MatrixLookAt((Vector3){3.2f, 2.4f, 4.2f},
                               (Vector3){0, 0, 0}, (Vector3){0, 1, 0});
    Matrix proj = MatrixPerspective(50.0f * VLCRLIB_DEG2RAD,
                                    (float)g_canvas.width / (float)g_canvas.height,
                                    0.1f, 100.0f);
    proj.m5 = -proj.m5;
    Matrix model = MatrixMultiply(MatrixRotateY(t * 0.8f),
                                  MatrixRotateX(t * 0.47f));
    Matrix mvp = MatrixMultiply(proj, MatrixMultiply(view, model));

    /* Directional light, two cascades. */
    Vector3 ld = {-0.55f, -1.0f, -0.35f};
    float ll = sqrtf(ld.x * ld.x + ld.y * ld.y + ld.z * ld.z);
    ld.x /= ll; ld.y /= ll; ld.z /= ll;
    Vector3 eye = {-ld.x * 12.0f, -ld.y * 12.0f, -ld.z * 12.0f};
    Matrix lview = MatrixLookAt(eye, (Vector3){0, 0, 0}, (Vector3){0, 1, 0});
    const float ext[2] = {3.5f, 8.0f};
    for (int c = 0; c < 2; ++c) {
        Matrix lp = MatrixOrtho(-ext[c], ext[c], -ext[c], ext[c], 0.1f, 40.0f);
        g_lightVP[c] = MatrixMultiply(lp, lview);
        render_shadow_map(c, g_lightVP[c], t);
    }

    VCpuClear(&g_canvas, (Color){18, 20, 28, 255});
    VCpuClearDepth(&g_canvas);
    pass_ground(mvp, t);
    cube(&g_canvas, mvp);
    skinnedLimb(&g_canvas, mvp, t);
    pass_ssao();
    pass_bloom();
    pass_taa();
    return (unsigned int)(uintptr_t)g_canvas.color;
}

unsigned int vcpu_hash(void) {
    return (unsigned int)VCpuHash(&g_canvas);
}
