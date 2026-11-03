#include "vcpu.h"
#include "vplatform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void ensureColorLUTs(void);

static uint32_t packColor(Color c) {
    return ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) |
           ((uint32_t)c.g << 8) | (uint32_t)c.b;
}

/* ------------------------------------------------------------------ */
/*  CPU lighting state                                                 */
/* ------------------------------------------------------------------ */

typedef struct CpuLightState {
    bool    directionalEnabled;
    Vector3 directionalDirection;   /* direction the light travels */
    Color   directionalColor;
    float   directionalIntensity;

    bool    ambientEnabled;
    Color   ambientColor;
    float   ambientIntensity;
} CpuLightState;

static CpuLightState g_cpuLight = {
    true,  { -0.4f, -0.7f, -0.3f }, { 255, 250, 240, 255 }, 1.5f,
    true,  { 50, 55, 70, 255 },     0.35f
};
static float g_cpuExposure = 1.0f;

typedef struct CpuShadowState {
    bool    enabled;
    int     size;
    float   bias;
    float   strength;
    float   softness;
    float  *depth;
    Matrix  lightViewProj;
    bool    valid;
} CpuShadowState;

static CpuShadowState g_cpuShadow = {
    false, 512, 0.003f, 0.65f, 1.5f, NULL, {0}, false
};
static int g_cpuShadowDepthSize = 0;

static VIBL_Bundle g_cpuIBL;
static float       g_cpuIBLIntensity = 0.0f;

static Color colorMul(Color a, Color b) {
    return (Color){
        (unsigned char)((a.r * b.r) / 255),
        (unsigned char)((a.g * b.g) / 255),
        (unsigned char)((a.b * b.b) / 255),
        (unsigned char)((a.a * b.a) / 255)
    };
}

static bool colorIsZero(Color c) {
    return (c.r | c.g | c.b | c.a) == 0;
}

static VCpuModelPart defaultPart(Color baseColor, const VCpuTexture *texture,
                                 int indexCount) {
    VCpuModelPart part;
    memset(&part, 0, sizeof(part));
    if (colorIsZero(baseColor)) baseColor = (Color){255, 255, 255, 255};
    part.firstIndex = 0;
    part.indexCount = indexCount;
    part.texture = texture;
    part.baseColor = baseColor;
    part.baseColorFactor[0] = baseColor.r / 255.0f;
    part.baseColorFactor[1] = baseColor.g / 255.0f;
    part.baseColorFactor[2] = baseColor.b / 255.0f;
    part.baseColorFactor[3] = baseColor.a / 255.0f;
    part.metallic = 0.0f;
    part.roughness = 0.5f;
    part.alphaMode = 0;
    part.alphaCutoff = 0.5f;
    part.doubleSided = 1;
    part.unlit = 1;
    return part;
}

void VCpuSetDirectionalLight(bool enabled, Vector3 direction, Color color,
                             float intensity) {
    float len = sqrtf(direction.x * direction.x + direction.y * direction.y +
                      direction.z * direction.z);
    if (len < 1e-6f) direction = (Vector3){ -0.4f, -0.7f, -0.3f };
    else { direction.x /= len; direction.y /= len; direction.z /= len; }
    g_cpuLight.directionalEnabled = enabled;
    g_cpuLight.directionalDirection = direction;
    g_cpuLight.directionalColor = color;
    g_cpuLight.directionalIntensity = intensity < 0.0f ? 0.0f : intensity;
}

void VCpuSetAmbientLight(bool enabled, Color color, float intensity) {
    g_cpuLight.ambientEnabled = enabled;
    g_cpuLight.ambientColor = color;
    g_cpuLight.ambientIntensity = intensity < 0.0f ? 0.0f : intensity;
}

void VCpuSetExposure(float exposure) {
    g_cpuExposure = exposure > 0.0f ? exposure : 1.0f;
}

void VCpuSetShadows(bool enabled, int mapSize, float bias, float strength) {
    g_cpuShadow.enabled = enabled;
    int size = mapSize >= 128 ? mapSize : 512;
    if (size > 1024) size = 1024;
    g_cpuShadow.size = size;
    g_cpuShadow.bias = bias > 0.0f ? bias : 0.003f;
    g_cpuShadow.strength = strength < 0.0f ? 0.0f
                         : (strength > 1.0f ? 1.0f : strength);
    if (!enabled) g_cpuShadow.valid = false;
}

void VCpuSetShadowSoftness(float radius) {
    g_cpuShadow.softness = radius < 1.0f ? 1.0f
                         : (radius > 3.0f ? 3.0f : radius);
}

bool VCpuShadowsEnabled(void) {
    return g_cpuShadow.enabled;
}

bool VCpuSetIBLBundle(VIBL_Bundle *bundle) {
    if (!bundle || !bundle->valid) return false;
    if (bundle == &g_cpuIBL) return true;
    VIBL_FreeBundle(&g_cpuIBL);
    g_cpuIBL = *bundle;
    memset(bundle, 0, sizeof(*bundle));
    g_cpuIBLIntensity = 1.0f;
    return true;
}

void VCpuUnloadIBL(void) {
    VIBL_FreeBundle(&g_cpuIBL);
    g_cpuIBLIntensity = 0.0f;
}

void VCpuSetIBLIntensity(float intensity) {
    g_cpuIBLIntensity = intensity < 0.0f ? 0.0f
                      : (intensity > 4.0f ? 4.0f : intensity);
}

bool VCpuIsIBLLoaded(void) {
    return g_cpuIBL.valid;
}

void VCpuShutdown(void) {
    VIBL_FreeBundle(&g_cpuIBL);
    g_cpuIBLIntensity = 0.0f;
    free(g_cpuShadow.depth);
    g_cpuShadow.depth = NULL;
    g_cpuShadowDepthSize = 0;
    g_cpuShadow.valid = false;
}

bool VCpuInit(VCpuCanvas *canvas, int width, int height) {
    if (!canvas || width <= 0 || height <= 0) return false;
    ensureColorLUTs();
    memset(canvas, 0, sizeof(*canvas));
    size_t pixels = (size_t)width * (size_t)height;
    canvas->color = (uint32_t *)malloc(pixels * sizeof(uint32_t));
    canvas->depth = (float *)malloc(pixels * sizeof(float));
    if (!canvas->color || !canvas->depth) {
        VCpuFree(canvas);
        return false;
    }
    canvas->width = width;
    canvas->height = height;
    canvas->scissorW = width;
    canvas->scissorH = height;
    VCpuClearDepth(canvas);
    return true;
}

void VCpuFree(VCpuCanvas *canvas) {
    if (!canvas) return;
    free(canvas->color);
    free(canvas->depth);
    canvas->color = NULL;
    canvas->depth = NULL;
    canvas->width = canvas->height = 0;
}

void VCpuClear(VCpuCanvas *canvas, Color color) {
    if (!canvas || !canvas->color) return;
    uint32_t px = packColor(color);
    size_t count = (size_t)canvas->width * (size_t)canvas->height;
    for (size_t i = 0; i < count; ++i) canvas->color[i] = px;
}

void VCpuClearDepth(VCpuCanvas *canvas) {
    if (!canvas || !canvas->depth) return;
    size_t count = (size_t)canvas->width * (size_t)canvas->height;
    for (size_t i = 0; i < count; ++i) canvas->depth[i] = 1.0f;
}

void VCpuSetScissor(VCpuCanvas *canvas, int x, int y, int w, int h) {
    if (!canvas) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > canvas->width) w = canvas->width - x;
    if (y + h > canvas->height) h = canvas->height - y;
    canvas->scissorX = x;
    canvas->scissorY = y;
    canvas->scissorW = w < 0 ? 0 : w;
    canvas->scissorH = h < 0 ? 0 : h;
    canvas->scissorEnabled = true;
}

void VCpuResetScissor(VCpuCanvas *canvas) {
    if (!canvas) return;
    canvas->scissorEnabled = false;
    canvas->scissorX = canvas->scissorY = 0;
    canvas->scissorW = canvas->width;
    canvas->scissorH = canvas->height;
}

static float edge(Vector2 a, Vector2 b, float px, float py) {
    return (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
}

static void blendPixel(VCpuCanvas *canvas, int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= canvas->width || y >= canvas->height) return;
    if (canvas->scissorEnabled &&
        (x < canvas->scissorX || y < canvas->scissorY ||
         x >= canvas->scissorX + canvas->scissorW ||
         y >= canvas->scissorY + canvas->scissorH)) return;
    uint32_t *dst = &canvas->color[(size_t)y * canvas->width + x];
    if (c.a == 255) {
        *dst = packColor(c);
        return;
    }
    if (c.a == 0) return;
    uint32_t s = *dst;
    float sa = c.a / 255.0f, da = ((s >> 24) & 0xFF) / 255.0f;
    float outA = sa + da * (1.0f - sa);
    if (outA <= 0.0f) { *dst = 0; return; }
    float inv = 1.0f / outA;
    unsigned r = (unsigned)((c.r * sa + ((s >> 16) & 0xFF) * da * (1.0f - sa)) * inv);
    unsigned g = (unsigned)((c.g * sa + ((s >> 8) & 0xFF) * da * (1.0f - sa)) * inv);
    unsigned b = (unsigned)((c.b * sa + (s & 0xFF) * da * (1.0f - sa)) * inv);
    *dst = ((unsigned)(outA * 255.0f) << 24) | (r << 16) | (g << 8) | b;
}

void VCpuTriangle2D(VCpuCanvas *canvas, Vector2 a, Vector2 b, Vector2 c,
                    Color color) {
    if (!canvas || !canvas->color) return;
    float area = edge(a, b, c.x, c.y);
    if (fabsf(area) < 1e-6f) return;
    if (area < 0.0f) { Vector2 t = b; b = c; c = t; area = -area; }

    int minX = (int)floorf(fminf(a.x, fminf(b.x, c.x)));
    int maxX = (int)ceilf(fmaxf(a.x, fmaxf(b.x, c.x)));
    int minY = (int)floorf(fminf(a.y, fminf(b.y, c.y)));
    int maxY = (int)ceilf(fmaxf(a.y, fmaxf(b.y, c.y)));
    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX >= canvas->width) maxX = canvas->width - 1;
    if (maxY >= canvas->height) maxY = canvas->height - 1;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = edge(b, c, px, py);
            float w1 = edge(c, a, px, py);
            float w2 = edge(a, b, px, py);
            if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
                blendPixel(canvas, x, y, color);
        }
    }
}

void VCpuLine2D(VCpuCanvas *canvas, Vector2 a, Vector2 b, Color color) {
    if (!canvas) return;
    int x0 = (int)lrintf(a.x), y0 = (int)lrintf(a.y);
    int x1 = (int)lrintf(b.x), y1 = (int)lrintf(b.y);
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        blendPixel(canvas, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static bool project(Vector3 v, Matrix mvp, float *sx, float *sy, float *sz) {
    float x = mvp.m0 * v.x + mvp.m4 * v.y + mvp.m8 * v.z + mvp.m12;
    float y = mvp.m1 * v.x + mvp.m5 * v.y + mvp.m9 * v.z + mvp.m13;
    float z = mvp.m2 * v.x + mvp.m6 * v.y + mvp.m10 * v.z + mvp.m14;
    float w = mvp.m3 * v.x + mvp.m7 * v.y + mvp.m11 * v.z + mvp.m15;
    if (w <= 1e-6f) return false;
    float inv = 1.0f / w;
    x *= inv; y *= inv; z *= inv;
    *sx = x; *sy = y; *sz = z;
    return true;
}

void VCpuTriangle3D(VCpuCanvas *canvas, Vector3 a, Vector3 b, Vector3 c,
                    Color color, Matrix mvp) {
    if (!canvas || !canvas->color) return;
    float ax, ay, az, bx, by, bz, cx, cy, cz;
    if (!project(a, mvp, &ax, &ay, &az) ||
        !project(b, mvp, &bx, &by, &bz) ||
        !project(c, mvp, &cx, &cy, &cz)) return;

    Vector2 sa = { (ax * 0.5f + 0.5f) * canvas->width,
                   (0.5f - ay * 0.5f) * canvas->height };
    Vector2 sb = { (bx * 0.5f + 0.5f) * canvas->width,
                   (0.5f - by * 0.5f) * canvas->height };
    Vector2 sc = { (cx * 0.5f + 0.5f) * canvas->width,
                   (0.5f - cy * 0.5f) * canvas->height };
    float area = edge(sa, sb, sc.x, sc.y);
    if (fabsf(area) < 1e-6f) return;
    if (area < 0.0f) {
        Vector2 t = sb; sb = sc; sc = t;
        float tz = by; by = cy; cy = tz;
        tz = bz; bz = cz; cz = tz;
        area = -area;
    }

    int minX = (int)floorf(fminf(sa.x, fminf(sb.x, sc.x)));
    int maxX = (int)ceilf(fmaxf(sa.x, fmaxf(sb.x, sc.x)));
    int minY = (int)floorf(fminf(sa.y, fminf(sb.y, sc.y)));
    int maxY = (int)ceilf(fmaxf(sa.y, fmaxf(sb.y, sc.y)));
    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX >= canvas->width) maxX = canvas->width - 1;
    if (maxY >= canvas->height) maxY = canvas->height - 1;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = edge(sb, sc, px, py) / area;
            float w1 = edge(sc, sa, px, py) / area;
            float w2 = edge(sa, sb, px, py) / area;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
            float z = w0 * az + w1 * bz + w2 * cz;
            if (z < 0.0f || z > 1.0f) continue;
            size_t at = (size_t)y * canvas->width + x;
            if (z >= canvas->depth[at]) continue;
            canvas->depth[at] = z;
            blendPixel(canvas, x, y, color);
        }
    }
}

static VCpuBackend g_cpuBackend = VCPU_BACKEND_AUTO;

void VCpuSetPreferredBackend(VCpuBackend backend) { g_cpuBackend = backend; }
VCpuBackend VCpuGetPreferredBackend(void) { return g_cpuBackend; }

bool VRenderBackendAvailable(VRenderBackend backend) {
    switch (backend) {
        case VRENDER_BACKEND_CPU: return true;
        case VRENDER_BACKEND_GPU:
            return (VLCRLIB_USE_VULKAN || VLCRLIB_USE_WEBGPU) ? true : false;
        case VRENDER_BACKEND_AUTO: return true;
        default: return false;
    }
}

bool VRenderBackendSelect(VRenderBackend backend) {
    if (!VRenderBackendAvailable(backend)) return false;
    switch (backend) {
        case VRENDER_BACKEND_CPU: g_cpuBackend = VCPU_BACKEND_CPU; break;
        case VRENDER_BACKEND_GPU:
        case VRENDER_BACKEND_AUTO: g_cpuBackend = VCPU_BACKEND_GPU; break;
        default: return false;
    }
    return true;
}

VRenderBackend VRenderBackendGet(void) {
    if (g_cpuBackend == VCPU_BACKEND_CPU) return VRENDER_BACKEND_CPU;
    /* A GPU-free build has no other usable backend. */
    if (!(VLCRLIB_USE_VULKAN || VLCRLIB_USE_WEBGPU)) return VRENDER_BACKEND_CPU;
    return VRENDER_BACKEND_GPU;
}

const char *VRenderBackendName(void) {
    return VRenderBackendGet() == VRENDER_BACKEND_CPU ? "cpu" : "gpu";
}

bool VCpuTextureFromImage(VCpuTexture *texture, Image image) {
    if (!texture) return false;
    memset(texture, 0, sizeof(*texture));
    if (!image.data || image.width <= 0 || image.height <= 0) return false;
    size_t count = (size_t)image.width * (size_t)image.height;
    texture->pixels = (uint32_t *)malloc(count * sizeof(uint32_t));
    if (!texture->pixels) return false;
    const unsigned char *src = (const unsigned char *)image.data;
    for (size_t i = 0; i < count; ++i) {
        texture->pixels[i] = ((uint32_t)src[i * 4 + 3] << 24) |
                             ((uint32_t)src[i * 4 + 0] << 16) |
                             ((uint32_t)src[i * 4 + 1] << 8) |
                             (uint32_t)src[i * 4 + 2];
    }
    texture->width = image.width;
    texture->height = image.height;
    return true;
}

void VCpuTextureFree(VCpuTexture *texture) {
    if (!texture) return;
    free(texture->pixels);
    memset(texture, 0, sizeof(*texture));
}

VCpuModel VCpuModelCreate(const VCpuVertex *vertices, int vertexCount,
                          const uint32_t *indices, int indexCount,
                          Color baseColor, VCpuTexture *texture) {
    VCpuModel model = {0};
    if (!vertices || vertexCount <= 0) return model;
    model.vertices = (VCpuVertex *)malloc(sizeof(VCpuVertex) * (size_t)vertexCount);
    if (!model.vertices) return model;
    memcpy(model.vertices, vertices, sizeof(VCpuVertex) * (size_t)vertexCount);
    model.vertexCount = vertexCount;
    if (indices && indexCount > 0) {
        model.indices = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)indexCount);
        if (!model.indices) { VCpuModelFree(&model); return model; }
        memcpy(model.indices, indices, sizeof(uint32_t) * (size_t)indexCount);
        model.indexCount = indexCount;
    } else {
        model.indices = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)vertexCount);
        if (!model.indices) { VCpuModelFree(&model); return model; }
        for (int i = 0; i < vertexCount; ++i) model.indices[i] = (uint32_t)i;
        model.indexCount = vertexCount;
    }
    model.baseColor = baseColor;
    model.texture = texture;
    model.parts = (VCpuModelPart *)calloc(1, sizeof(VCpuModelPart));
    if (!model.parts) { VCpuModelFree(&model); return model; }
    model.parts[0] = defaultPart(baseColor, texture, model.indexCount);
    model.partCount = 1;
    return model;
}

void VCpuModelFree(VCpuModel *model) {
    if (!model) return;
    free(model->parts);
    model->parts = NULL;
    model->partCount = 0;
    if (model->ownedTextures) {
        for (int i = 0; i < model->ownedTextureCount; ++i)
            VCpuTextureFree(&model->ownedTextures[i]);
        free(model->ownedTextures);
        model->ownedTextures = NULL;
        model->ownedTextureCount = 0;
    }
    free(model->vertices);
    free(model->indices);
    memset(model, 0, sizeof(*model));
}

typedef struct CpuClipVertex {
    float x, y, z, w;
    float u, v;
} CpuClipVertex;

typedef struct CpuScreenVertex {
    float x, y, z;
    float uow, vow, invw;
} CpuScreenVertex;

static CpuClipVertex toClip(const VCpuVertex *v, Matrix m) {
    CpuClipVertex o;
    o.x = m.m0 * v->position.x + m.m4 * v->position.y +
          m.m8 * v->position.z + m.m12;
    o.y = m.m1 * v->position.x + m.m5 * v->position.y +
          m.m9 * v->position.z + m.m13;
    o.z = m.m2 * v->position.x + m.m6 * v->position.y +
          m.m10 * v->position.z + m.m14;
    o.w = m.m3 * v->position.x + m.m7 * v->position.y +
          m.m11 * v->position.z + m.m15;
    o.u = v->uv.x;
    o.v = v->uv.y;
    return o;
}

static CpuClipVertex lerpClip(CpuClipVertex a, CpuClipVertex b, float t) {
    CpuClipVertex o;
    o.x = a.x + (b.x - a.x) * t;
    o.y = a.y + (b.y - a.y) * t;
    o.z = a.z + (b.z - a.z) * t;
    o.w = a.w + (b.w - a.w) * t;
    o.u = a.u + (b.u - a.u) * t;
    o.v = a.v + (b.v - a.v) * t;
    return o;
}

/* Clip a clip-space polygon against the Vulkan near plane z >= 0. */
static int clipNear(const CpuClipVertex *in, int count, CpuClipVertex *out) {
    int n = 0;
    for (int i = 0; i < count; ++i) {
        const CpuClipVertex *a = &in[i];
        const CpuClipVertex *b = &in[(i + 1) % count];
        bool ain = a->z >= 0.0f;
        bool bin = b->z >= 0.0f;
        if (ain) out[n++] = *a;
        if (ain != bin) {
            float denom = a->z - b->z;
            if (fabsf(denom) > 1e-9f)
                out[n++] = lerpClip(*a, *b, a->z / denom);
        }
    }
    return n;
}

static Color sampleTexture(const VCpuTexture *t, float u, float v) {
    if (!t || !t->pixels || t->width <= 0 || t->height <= 0)
        return (Color){255, 255, 255, 255};
    int x = (int)floorf(u * t->width);
    int y = (int)floorf(v * t->height);
    x %= t->width; if (x < 0) x += t->width;
    y %= t->height; if (y < 0) y += t->height;
    uint32_t p = t->pixels[(size_t)y * t->width + x];
    return (Color){ (p >> 16) & 0xFF, (p >> 8) & 0xFF,
                    p & 0xFF, (p >> 24) & 0xFF };
}

static int wrapCoord(int v, int n) {
    v %= n;
    return v < 0 ? v + n : v;
}

static Color lerpColor(Color a, Color b, float t) {
    return (Color){
        (unsigned char)(a.r + (b.r - a.r) * t + 0.5f),
        (unsigned char)(a.g + (b.g - a.g) * t + 0.5f),
        (unsigned char)(a.b + (b.b - a.b) * t + 0.5f),
        (unsigned char)(a.a + (b.a - a.a) * t + 0.5f)
    };
}

/* Bilinear sample with repeat wrap. The nearest-neighbour sampler above is
   kept for the legacy 2D path; lit models use this one. */
static Color sampleTextureLinear(const VCpuTexture *t, float u, float v) {
    if (!t || !t->pixels || t->width <= 0 || t->height <= 0)
        return (Color){255, 255, 255, 255};
    float fx = u * (float)t->width - 0.5f;
    float fy = v * (float)t->height - 0.5f;
    int x0 = (int)floorf(fx);
    int y0 = (int)floorf(fy);
    float tx = fx - (float)x0;
    float ty = fy - (float)y0;
    int x1 = wrapCoord(x0 + 1, t->width);
    int y1 = wrapCoord(y0 + 1, t->height);
    x0 = wrapCoord(x0, t->width);
    y0 = wrapCoord(y0, t->height);
    Color c00 = { (unsigned char)((t->pixels[(size_t)y0 * t->width + x0] >> 16) & 0xFF),
                  (unsigned char)((t->pixels[(size_t)y0 * t->width + x0] >> 8) & 0xFF),
                  (unsigned char)(t->pixels[(size_t)y0 * t->width + x0] & 0xFF),
                  (unsigned char)((t->pixels[(size_t)y0 * t->width + x0] >> 24) & 0xFF) };
    Color c10 = { (unsigned char)((t->pixels[(size_t)y0 * t->width + x1] >> 16) & 0xFF),
                  (unsigned char)((t->pixels[(size_t)y0 * t->width + x1] >> 8) & 0xFF),
                  (unsigned char)(t->pixels[(size_t)y0 * t->width + x1] & 0xFF),
                  (unsigned char)((t->pixels[(size_t)y0 * t->width + x1] >> 24) & 0xFF) };
    Color c01 = { (unsigned char)((t->pixels[(size_t)y1 * t->width + x0] >> 16) & 0xFF),
                  (unsigned char)((t->pixels[(size_t)y1 * t->width + x0] >> 8) & 0xFF),
                  (unsigned char)(t->pixels[(size_t)y1 * t->width + x0] & 0xFF),
                  (unsigned char)((t->pixels[(size_t)y1 * t->width + x0] >> 24) & 0xFF) };
    Color c11 = { (unsigned char)((t->pixels[(size_t)y1 * t->width + x1] >> 16) & 0xFF),
                  (unsigned char)((t->pixels[(size_t)y1 * t->width + x1] >> 8) & 0xFF),
                  (unsigned char)(t->pixels[(size_t)y1 * t->width + x1] & 0xFF),
                  (unsigned char)((t->pixels[(size_t)y1 * t->width + x1] >> 24) & 0xFF) };
    return lerpColor(lerpColor(c00, c10, tx), lerpColor(c01, c11, tx), ty);
}

static float clamp01f(float x) {
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

/* sRGB transfer functions are called several times per shaded pixel. A pair of
   tiny lookup tables removes the per-pixel powf cost. */
static float g_srgbToLinearLUT[256];
static unsigned char g_srgbByteLUT[1024];
static bool g_colorLUTReady = false;

static void ensureColorLUTs(void) {
    if (g_colorLUTReady) return;
    for (int i = 0; i < 256; ++i) {
        float c = (float)i / 255.0f;
        g_srgbToLinearLUT[i] = c <= 0.04045f
            ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
    }
    for (int i = 0; i < 1024; ++i) {
        float c = (float)i / 1023.0f;
        float s = c <= 0.0031308f
            ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
        g_srgbByteLUT[i] = (unsigned char)(clamp01f(s) * 255.0f + 0.5f);
    }
    g_colorLUTReady = true;
}

static float srgbToLinear(float c) {
    if (c <= 0.0f) return 0.0f;
    if (c >= 1.0f) return 1.0f;
    if (!g_colorLUTReady) ensureColorLUTs();
    return g_srgbToLinearLUT[(int)(c * 255.0f + 0.5f)];
}

static float acesToneMap(float x) {
    x *= g_cpuExposure;
    if (x < 0.0f) x = 0.0f;
    return clamp01f((x * (2.51f * x + 0.03f)) /
                    (x * (2.43f * x + 0.59f) + 0.14f));
}

static unsigned char byteFromLinear(float c) {
    if (!g_colorLUTReady) ensureColorLUTs();
    int i = (int)(clamp01f(c) * 1023.0f + 0.5f);
    if (i < 0) i = 0;
    if (i > 1023) i = 1023;
    return g_srgbByteLUT[i];
}

static Vector3 safeNormalize(Vector3 v) {
    float len2 = v.x * v.x + v.y * v.y + v.z * v.z;
    if (len2 < 1e-12f) return (Vector3){0.0f, 0.0f, 1.0f};
    float inv = 1.0f / sqrtf(len2);
    return (Vector3){ v.x * inv, v.y * inv, v.z * inv };
}

static float dot3(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static Vector3 cross3(Vector3 a, Vector3 b) {
    return (Vector3){ a.y * b.z - a.z * b.y,
                      a.z * b.x - a.x * b.z,
                      a.x * b.y - a.y * b.x };
}

static void rasterTextured(VCpuCanvas *canvas, CpuScreenVertex sv[3],
                           const VCpuTexture *texture, Color tint,
                           const Color *vertexColors, bool depthTest) {
    float area = (sv[1].x - sv[0].x) * (sv[2].y - sv[0].y) -
                 (sv[1].y - sv[0].y) * (sv[2].x - sv[0].x);
    if (fabsf(area) < 1e-6f) return;
    if (area < 0.0f) {
        CpuScreenVertex t = sv[1]; sv[1] = sv[2]; sv[2] = t;
        area = -area;
    }
    int minX = (int)floorf(fminf(sv[0].x, fminf(sv[1].x, sv[2].x)));
    int maxX = (int)ceilf(fmaxf(sv[0].x, fmaxf(sv[1].x, sv[2].x)));
    int minY = (int)floorf(fminf(sv[0].y, fminf(sv[1].y, sv[2].y)));
    int maxY = (int)ceilf(fmaxf(sv[0].y, fmaxf(sv[1].y, sv[2].y)));
    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX >= canvas->width) maxX = canvas->width - 1;
    if (maxY >= canvas->height) maxY = canvas->height - 1;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            float px = x + 0.5f, py = y + 0.5f;
            float e0 = (sv[1].x - px) * (sv[2].y - py) -
                       (sv[1].y - py) * (sv[2].x - px);
            float e1 = (sv[2].x - px) * (sv[0].y - py) -
                       (sv[2].y - py) * (sv[0].x - px);
            float e2 = (sv[0].x - px) * (sv[1].y - py) -
                       (sv[0].y - py) * (sv[1].x - px);
            if (e0 < 0.0f || e1 < 0.0f || e2 < 0.0f) continue;
            float b0 = e0 / area, b1 = e1 / area, b2 = e2 / area;
            float z = b0 * sv[0].z + b1 * sv[1].z + b2 * sv[2].z;
            if (z < 0.0f || z > 1.0f) continue;
            size_t at = (size_t)y * canvas->width + x;
            if (depthTest) {
                if (z >= canvas->depth[at]) continue;
                canvas->depth[at] = z;
            }
            float invw = b0 * sv[0].invw + b1 * sv[1].invw + b2 * sv[2].invw;
            if (fabsf(invw) < 1e-9f) continue;
            float u = (b0 * sv[0].uow + b1 * sv[1].uow + b2 * sv[2].uow) / invw;
            float v = (b0 * sv[0].vow + b1 * sv[1].vow + b2 * sv[2].vow) / invw;
            Color c = sampleTexture(texture, u, v);
            if (vertexColors) {
                c.r = (unsigned char)((c.r * (b0 * vertexColors[0].r +
                    b1 * vertexColors[1].r + b2 * vertexColors[2].r)) / 255);
                c.g = (unsigned char)((c.g * (b0 * vertexColors[0].g +
                    b1 * vertexColors[1].g + b2 * vertexColors[2].g)) / 255);
                c.b = (unsigned char)((c.b * (b0 * vertexColors[0].b +
                    b1 * vertexColors[1].b + b2 * vertexColors[2].b)) / 255);
                c.a = (unsigned char)((c.a * (b0 * vertexColors[0].a +
                    b1 * vertexColors[1].a + b2 * vertexColors[2].a)) / 255);
            } else {
                c.r = (unsigned char)((c.r * tint.r) / 255);
                c.g = (unsigned char)((c.g * tint.g) / 255);
                c.b = (unsigned char)((c.b * tint.b) / 255);
                c.a = (unsigned char)((c.a * tint.a) / 255);
            }
            blendPixel(canvas, x, y, c);
        }
    }
}

static bool screenFromClip(CpuClipVertex c, VCpuCanvas *canvas,
                           CpuScreenVertex *out) {
    if (c.w <= 1e-6f) return false;
    float invw = 1.0f / c.w;
    float nx = c.x * invw;
    float ny = c.y * invw;
    float nz = c.z * invw;
    out->x = (nx * 0.5f + 0.5f) * canvas->width;
    out->y = (0.5f - ny * 0.5f) * canvas->height;
    out->z = nz;
    out->uow = c.u * invw;
    out->vow = c.v * invw;
    out->invw = invw;
    return true;
}

/* ------------------------------------------------------------------ */
/*  CPU directional shadow map                                         */
/* ------------------------------------------------------------------ */

static void rasterShadowTriangle(CpuScreenVertex sv[3], float *depth,
                                int size) {
    float area = (sv[1].x - sv[0].x) * (sv[2].y - sv[0].y) -
                 (sv[1].y - sv[0].y) * (sv[2].x - sv[0].x);
    if (fabsf(area) < 1e-6f) return;
    if (area < 0.0f) {
        CpuScreenVertex t = sv[1];
        sv[1] = sv[2];
        sv[2] = t;
        area = -area;
    }
    int minX = (int)floorf(fminf(sv[0].x, fminf(sv[1].x, sv[2].x)));
    int maxX = (int)ceilf(fmaxf(sv[0].x, fmaxf(sv[1].x, sv[2].x)));
    int minY = (int)floorf(fminf(sv[0].y, fminf(sv[1].y, sv[2].y)));
    int maxY = (int)ceilf(fmaxf(sv[0].y, fmaxf(sv[1].y, sv[2].y)));
    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX >= size) maxX = size - 1;
    if (maxY >= size) maxY = size - 1;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            float px = x + 0.5f, py = y + 0.5f;
            float e0 = (sv[1].x - px) * (sv[2].y - py) -
                       (sv[1].y - py) * (sv[2].x - px);
            float e1 = (sv[2].x - px) * (sv[0].y - py) -
                       (sv[2].y - py) * (sv[0].x - px);
            float e2 = (sv[0].x - px) * (sv[1].y - py) -
                       (sv[0].y - py) * (sv[1].x - px);
            if (e0 < 0.0f || e1 < 0.0f || e2 < 0.0f) continue;
            float b0 = e0 / area, b1 = e1 / area, b2 = e2 / area;
            float z = b0 * sv[0].z + b1 * sv[1].z + b2 * sv[2].z;
            if (z < 0.0f || z > 1.0f) continue;
            size_t at = (size_t)y * (size_t)size + (size_t)x;
            if (z < depth[at]) depth[at] = z;
        }
    }
}

static void sampleCube(const VIBL_Cube *cube, Vector3 d, int mip, float out[3]) {
    out[0] = out[1] = out[2] = 0.0f;
    if (!cube || !cube->faces || cube->size < 1) return;
    int mips = cube->mips > 0 ? cube->mips : 1;
    if (mip < 0) mip = 0;
    if (mip >= mips) mip = mips - 1;
    int size = cube->size >> mip;
    if (size < 1) size = 1;
    float ax = fabsf(d.x), ay = fabsf(d.y), az = fabsf(d.z);
    int face;
    float u, v;
    if (ax >= ay && ax >= az) {
        float inv = ax > 1e-8f ? 1.0f / ax : 0.0f;
        if (d.x >= 0.0f) { face = 0; u = -d.z * inv; v = -d.y * inv; }
        else             { face = 1; u =  d.z * inv; v = -d.y * inv; }
    } else if (ay >= az) {
        float inv = ay > 1e-8f ? 1.0f / ay : 0.0f;
        if (d.y >= 0.0f) { face = 2; u = d.x * inv; v =  d.z * inv; }
        else             { face = 3; u = d.x * inv; v = -d.z * inv; }
    } else {
        float inv = az > 1e-8f ? 1.0f / az : 0.0f;
        if (d.z >= 0.0f) { face = 4; u =  d.x * inv; v = -d.y * inv; }
        else             { face = 5; u = -d.x * inv; v = -d.y * inv; }
    }
    int x = (int)((u * 0.5f + 0.5f) * (float)(size - 1) + 0.5f);
    int y = (int)((v * 0.5f + 0.5f) * (float)(size - 1) + 0.5f);
    if (x < 0) x = 0; if (x >= size) x = size - 1;
    if (y < 0) y = 0; if (y >= size) y = size - 1;
    const float *p = cube->faces[mip * 6 + face];
    if (!p) return;
    const float *rgb = p + ((size_t)y * (size_t)size + (size_t)x) * 3;
    out[0] = rgb[0]; out[1] = rgb[1]; out[2] = rgb[2];
}

static void sampleBRDF(float ndv, float roughness, float *scale, float *bias) {
    *scale = 1.0f;
    *bias = 0.0f;
    if (!g_cpuIBL.brdf.data || g_cpuIBL.brdf.size < 2) return;
    int size = g_cpuIBL.brdf.size;
    int x = (int)(clamp01f(ndv) * (float)(size - 1) + 0.5f);
    int y = (int)(clamp01f(roughness) * (float)(size - 1) + 0.5f);
    if (x < 0) x = 0; if (x >= size) x = size - 1;
    if (y < 0) y = 0; if (y >= size) y = size - 1;
    const float *p = g_cpuIBL.brdf.data + ((size_t)y * (size_t)size +
                                           (size_t)x) * 2;
    *scale = p[0];
    *bias = p[1];
}

void VCpuShadowBuild(const VCpuShadowCaster *casters, int count) {
    g_cpuShadow.valid = false;
    if (!g_cpuShadow.enabled || !casters || count <= 0) return;
    if (g_cpuShadow.size < 128) g_cpuShadow.size = 512;

    Vector3 mn = { 1e30f, 1e30f, 1e30f };
    Vector3 mx = { -1e30f, -1e30f, -1e30f };
    bool any = false;
    for (int c = 0; c < count; ++c) {
        const VCpuModel *m = casters[c].model;
        if (!m || !m->vertices || m->vertexCount <= 0) continue;
        Matrix w = casters[c].world;
        for (int i = 0; i < m->vertexCount; ++i) {
            const VCpuVertex *v = &m->vertices[i];
            float x = w.m0 * v->position.x + w.m4 * v->position.y +
                      w.m8 * v->position.z + w.m12;
            float y = w.m1 * v->position.x + w.m5 * v->position.y +
                      w.m9 * v->position.z + w.m13;
            float z = w.m2 * v->position.x + w.m6 * v->position.y +
                      w.m10 * v->position.z + w.m14;
            if (x < mn.x) mn.x = x; if (x > mx.x) mx.x = x;
            if (y < mn.y) mn.y = y; if (y > mx.y) mx.y = y;
            if (z < mn.z) mn.z = z; if (z > mx.z) mx.z = z;
            any = true;
        }
    }
    if (!any) return;

    Vector3 center = { (mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f,
                       (mn.z + mx.z) * 0.5f };
    Vector3 extent = { (mx.x - mn.x) * 0.5f, (mx.y - mn.y) * 0.5f,
                       (mx.z - mn.z) * 0.5f };
    float radius = sqrtf(extent.x * extent.x + extent.y * extent.y +
                         extent.z * extent.z);
    if (radius < 0.5f) radius = 0.5f;

    Vector3 dir = g_cpuLight.directionalDirection;
    float dlen = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (dlen < 1e-6f) dir = (Vector3){ -0.4f, -0.7f, -0.3f };
    else { dir.x /= dlen; dir.y /= dlen; dir.z /= dlen; }
    Vector3 eye = { center.x - dir.x * radius * 2.0f,
                    center.y - dir.y * radius * 2.0f,
                    center.z - dir.z * radius * 2.0f };
    Matrix view = MatrixLookAt(eye, center, (Vector3){ 0, 1, 0 });
    Matrix proj = MatrixOrtho(-radius * 1.1f, radius * 1.1f,
                              -radius * 1.1f, radius * 1.1f,
                              0.01f, radius * 4.0f + 0.1f);
    g_cpuShadow.lightViewProj = MatrixMultiply(proj, view);

    if (!g_cpuShadow.depth || g_cpuShadowDepthSize != g_cpuShadow.size) {
        free(g_cpuShadow.depth);
        g_cpuShadow.depth = (float *)malloc((size_t)g_cpuShadow.size *
                                            (size_t)g_cpuShadow.size *
                                            sizeof(float));
        g_cpuShadowDepthSize = g_cpuShadow.size;
    }
    if (!g_cpuShadow.depth) return;
    size_t pixels = (size_t)g_cpuShadow.size * (size_t)g_cpuShadow.size;
    for (size_t i = 0; i < pixels; ++i) g_cpuShadow.depth[i] = 1.0f;

    VCpuCanvas shadowCanvas;
    memset(&shadowCanvas, 0, sizeof(shadowCanvas));
    shadowCanvas.width = g_cpuShadow.size;
    shadowCanvas.height = g_cpuShadow.size;

    for (int c = 0; c < count; ++c) {
        const VCpuModel *m = casters[c].model;
        if (!m || !m->vertices || !m->indices) continue;
        Matrix mvp = MatrixMultiply(g_cpuShadow.lightViewProj, casters[c].world);
        int partCount = (m->parts && m->partCount > 0) ? m->partCount : 1;
        for (int p = 0; p < partCount; ++p) {
            int first = 0, indexCount = m->indexCount;
            if (m->parts && m->partCount > 0) {
                first = m->parts[p].firstIndex;
                indexCount = m->parts[p].indexCount;
                if (first < 0) first = 0;
                if (indexCount <= 0 || first + indexCount > m->indexCount)
                    indexCount = m->indexCount - first;
            }
            if (indexCount <= 0) continue;
            for (int i = first; i + 2 < first + indexCount; i += 3) {
                uint32_t i0 = m->indices[i];
                uint32_t i1 = m->indices[i + 1];
                uint32_t i2 = m->indices[i + 2];
                if (i0 >= (uint32_t)m->vertexCount ||
                    i1 >= (uint32_t)m->vertexCount ||
                    i2 >= (uint32_t)m->vertexCount) continue;
                CpuClipVertex tri[3] = {
                    toClip(&m->vertices[i0], mvp),
                    toClip(&m->vertices[i1], mvp),
                    toClip(&m->vertices[i2], mvp)
                };
                CpuClipVertex clipped[4];
                int n = clipNear(tri, 3, clipped);
                if (n < 3) continue;
                CpuScreenVertex s[4];
                bool ok = true;
                for (int k = 0; k < n; ++k) {
                    if (!screenFromClip(clipped[k], &shadowCanvas, &s[k])) {
                        ok = false;
                        break;
                    }
                }
                if (!ok) continue;
                for (int k = 1; k + 1 < n; ++k) {
                    CpuScreenVertex t[3] = { s[0], s[k], s[k + 1] };
                    rasterShadowTriangle(t, g_cpuShadow.depth,
                                         g_cpuShadow.size);
                }
            }
        }
    }
    g_cpuShadow.valid = true;
}

static float shadowFactor(Vector3 world, float ndl) {
    if (!g_cpuShadow.enabled || !g_cpuShadow.valid || !g_cpuShadow.depth)
        return 1.0f;
    Matrix m = g_cpuShadow.lightViewProj;
    float x = m.m0 * world.x + m.m4 * world.y + m.m8 * world.z + m.m12;
    float y = m.m1 * world.x + m.m5 * world.y + m.m9 * world.z + m.m13;
    float z = m.m2 * world.x + m.m6 * world.y + m.m10 * world.z + m.m14;
    float w = m.m3 * world.x + m.m7 * world.y + m.m11 * world.z + m.m15;
    if (fabsf(w) < 1e-8f) return 1.0f;
    x /= w; y /= w; z /= w;
    if (x < -1.0f || x > 1.0f || y < -1.0f || y > 1.0f ||
        z < 0.0f || z > 1.0f) return 1.0f;
    float u = x * 0.5f + 0.5f;
    float v = 0.5f - y * 0.5f;
    int tx = (int)(u * (float)g_cpuShadow.size);
    int ty = (int)(v * (float)g_cpuShadow.size);
    if (tx < 0) tx = 0;
    if (ty < 0) ty = 0;
    if (tx >= g_cpuShadow.size) tx = g_cpuShadow.size - 1;
    if (ty >= g_cpuShadow.size) ty = g_cpuShadow.size - 1;
    float bias = g_cpuShadow.bias + (1.0f - ndl) * 0.004f;
    int radius = (int)ceilf(g_cpuShadow.softness);
    if (radius < 1) radius = 1;
    if (radius > 3) radius = 3;
    int lit = 0, taps = 0;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            int sx = tx + dx, sy = ty + dy;
            if (sx < 0) sx = 0;
            if (sy < 0) sy = 0;
            if (sx >= g_cpuShadow.size) sx = g_cpuShadow.size - 1;
            if (sy >= g_cpuShadow.size) sy = g_cpuShadow.size - 1;
            float d = g_cpuShadow.depth[(size_t)sy *
                                        (size_t)g_cpuShadow.size + (size_t)sx];
            if (z - bias <= d) lit++;
            taps++;
        }
    }
    float visible = taps > 0 ? (float)lit / (float)taps : 1.0f;
    return 1.0f - (1.0f - visible) * g_cpuShadow.strength;
}

static void drawClipped(VCpuCanvas *canvas, const CpuClipVertex *poly, int n,
                        const VCpuTexture *texture, Color tint) {
    if (n < 3) return;
    CpuScreenVertex s[4];
    for (int i = 0; i < n; ++i)
        if (!screenFromClip(poly[i], canvas, &s[i])) return;
    for (int i = 1; i + 1 < n; ++i) {
        CpuScreenVertex tri[3] = { s[0], s[i], s[i + 1] };
        rasterTextured(canvas, tri, texture, tint, NULL, true);
    }
}

static void drawPartUnlit(VCpuCanvas *canvas, const VCpuModel *model,
                          int firstIndex, int indexCount, Matrix mvp,
                          const VCpuTexture *texture, Color tint) {
    for (int i = firstIndex; i + 2 < firstIndex + indexCount; i += 3) {
        uint32_t i0 = model->indices[i];
        uint32_t i1 = model->indices[i + 1];
        uint32_t i2 = model->indices[i + 2];
        if (i0 >= (uint32_t)model->vertexCount ||
            i1 >= (uint32_t)model->vertexCount ||
            i2 >= (uint32_t)model->vertexCount) continue;
        CpuClipVertex c[3] = {
            toClip(&model->vertices[i0], mvp),
            toClip(&model->vertices[i1], mvp),
            toClip(&model->vertices[i2], mvp)
        };
        CpuClipVertex clipped[4];
        int n = clipNear(c, 3, clipped);
        drawClipped(canvas, clipped, n, texture, tint);
    }
}

void VCpuDrawModel(VCpuCanvas *canvas, const VCpuModel *model, Matrix mvp,
                   Color tint) {
    if (!canvas || !model || !model->vertices || !model->indices) return;
    if (model->parts && model->partCount > 0) {
        for (int p = 0; p < model->partCount; ++p) {
            const VCpuModelPart *part = &model->parts[p];
            int first = part->firstIndex;
            int count = part->indexCount;
            if (first < 0) first = 0;
            if (count <= 0 || first + count > model->indexCount)
                count = model->indexCount - first;
            if (count <= 0) continue;
            Color base = colorIsZero(part->baseColor)
                       ? (Color){255, 255, 255, 255} : part->baseColor;
            drawPartUnlit(canvas, model, first, count, mvp, part->texture,
                          colorMul(base, tint));
        }
        return;
    }
    Color base = colorIsZero(model->baseColor)
               ? (Color){255, 255, 255, 255} : model->baseColor;
    drawPartUnlit(canvas, model, 0, model->indexCount, mvp, model->texture,
                  colorMul(base, tint));
}

/* ------------------------------------------------------------------ */
/*  Shaded 3D path                                                     */
/* ------------------------------------------------------------------ */

typedef struct CpuShadeClip {
    float x, y, z, w;
    float u, v;
    Vector3 world;
    Vector3 normal;
    Color   color;
} CpuShadeClip;

typedef struct CpuShadeScreen {
    float x, y, z;
    float uow, vow, invw;
    Vector3 world;
    Vector3 normal;
    Color   color;
} CpuShadeScreen;

static CpuShadeClip toClipShaded(const VCpuVertex *v, Matrix mvp, Matrix model,
                                 Matrix normalMat) {
    CpuShadeClip o;
    o.x = mvp.m0 * v->position.x + mvp.m4 * v->position.y +
          mvp.m8 * v->position.z + mvp.m12;
    o.y = mvp.m1 * v->position.x + mvp.m5 * v->position.y +
          mvp.m9 * v->position.z + mvp.m13;
    o.z = mvp.m2 * v->position.x + mvp.m6 * v->position.y +
          mvp.m10 * v->position.z + mvp.m14;
    o.w = mvp.m3 * v->position.x + mvp.m7 * v->position.y +
          mvp.m11 * v->position.z + mvp.m15;
    o.u = v->uv.x;
    o.v = v->uv.y;
    o.world.x = model.m0 * v->position.x + model.m4 * v->position.y +
                model.m8 * v->position.z + model.m12;
    o.world.y = model.m1 * v->position.x + model.m5 * v->position.y +
                model.m9 * v->position.z + model.m13;
    o.world.z = model.m2 * v->position.x + model.m6 * v->position.y +
                model.m10 * v->position.z + model.m14;
    o.normal.x = normalMat.m0 * v->normal.x + normalMat.m4 * v->normal.y +
                 normalMat.m8 * v->normal.z;
    o.normal.y = normalMat.m1 * v->normal.x + normalMat.m5 * v->normal.y +
                 normalMat.m9 * v->normal.z;
    o.normal.z = normalMat.m2 * v->normal.x + normalMat.m6 * v->normal.y +
                 normalMat.m10 * v->normal.z;
    o.color = v->color;
    return o;
}

static CpuShadeClip lerpShadeClip(CpuShadeClip a, CpuShadeClip b, float t) {
    CpuShadeClip o;
    o.x = a.x + (b.x - a.x) * t;
    o.y = a.y + (b.y - a.y) * t;
    o.z = a.z + (b.z - a.z) * t;
    o.w = a.w + (b.w - a.w) * t;
    o.u = a.u + (b.u - a.u) * t;
    o.v = a.v + (b.v - a.v) * t;
    o.world.x = a.world.x + (b.world.x - a.world.x) * t;
    o.world.y = a.world.y + (b.world.y - a.world.y) * t;
    o.world.z = a.world.z + (b.world.z - a.world.z) * t;
    o.normal.x = a.normal.x + (b.normal.x - a.normal.x) * t;
    o.normal.y = a.normal.y + (b.normal.y - a.normal.y) * t;
    o.normal.z = a.normal.z + (b.normal.z - a.normal.z) * t;
    o.color = lerpColor(a.color, b.color, t);
    return o;
}

static int clipNearShaded(const CpuShadeClip *in, int count, CpuShadeClip *out) {
    int n = 0;
    for (int i = 0; i < count; ++i) {
        const CpuShadeClip *a = &in[i];
        const CpuShadeClip *b = &in[(i + 1) % count];
        bool ain = a->z >= 0.0f;
        bool bin = b->z >= 0.0f;
        if (ain) out[n++] = *a;
        if (ain != bin) {
            float denom = a->z - b->z;
            if (fabsf(denom) > 1e-9f)
                out[n++] = lerpShadeClip(*a, *b, a->z / denom);
        }
    }
    return n;
}

static bool screenFromClipShaded(const CpuShadeClip *c, VCpuCanvas *canvas,
                                 CpuShadeScreen *out) {
    if (c->w <= 1e-6f) return false;
    float invw = 1.0f / c->w;
    float nx = c->x * invw;
    float ny = c->y * invw;
    out->x = (nx * 0.5f + 0.5f) * canvas->width;
    out->y = (0.5f - ny * 0.5f) * canvas->height;
    out->z = c->z * invw;
    out->uow = c->u * invw;
    out->vow = c->v * invw;
    out->invw = invw;
    out->world = c->world;
    out->normal = c->normal;
    out->color = c->color;
    return true;
}

static void ensureFaceNormal(CpuShadeScreen tri[3]) {
    Vector3 e1 = { tri[1].world.x - tri[0].world.x,
                   tri[1].world.y - tri[0].world.y,
                   tri[1].world.z - tri[0].world.z };
    Vector3 e2 = { tri[2].world.x - tri[0].world.x,
                   tri[2].world.y - tri[0].world.y,
                   tri[2].world.z - tri[0].world.z };
    Vector3 fn = cross3(e1, e2);
    if (dot3(fn, fn) < 1e-12f) return;
    fn = safeNormalize(fn);
    for (int i = 0; i < 3; ++i) {
        if (dot3(tri[i].normal, tri[i].normal) < 1e-12f)
            tri[i].normal = fn;
    }
}

static void rasterShaded(VCpuCanvas *canvas, CpuShadeScreen sv[3],
                         const VCpuModelPart *part, Vector3 cameraPos,
                         Color tint) {
    float area = (sv[1].x - sv[0].x) * (sv[2].y - sv[0].y) -
                 (sv[1].y - sv[0].y) * (sv[2].x - sv[0].x);
    if (fabsf(area) < 1e-6f) return;
    if (area < 0.0f) {
        CpuShadeScreen t = sv[1];
        sv[1] = sv[2];
        sv[2] = t;
        area = -area;
    }
    int minX = (int)floorf(fminf(sv[0].x, fminf(sv[1].x, sv[2].x)));
    int maxX = (int)ceilf(fmaxf(sv[0].x, fmaxf(sv[1].x, sv[2].x)));
    int minY = (int)floorf(fminf(sv[0].y, fminf(sv[1].y, sv[2].y)));
    int maxY = (int)ceilf(fmaxf(sv[0].y, fmaxf(sv[1].y, sv[2].y)));
    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX >= canvas->width) maxX = canvas->width - 1;
    if (maxY >= canvas->height) maxY = canvas->height - 1;

    float br = part->baseColorFactor[0];
    float bg = part->baseColorFactor[1];
    float bb = part->baseColorFactor[2];
    float ba = part->baseColorFactor[3];
    if (br + bg + bb + ba <= 1e-6f) {
        Color bc = colorIsZero(part->baseColor)
                 ? (Color){255, 255, 255, 255} : part->baseColor;
        br = srgbToLinear(bc.r / 255.0f);
        bg = srgbToLinear(bc.g / 255.0f);
        bb = srgbToLinear(bc.b / 255.0f);
        ba = bc.a / 255.0f;
    }
    float tintR = tint.r / 255.0f;
    float tintG = tint.g / 255.0f;
    float tintB = tint.b / 255.0f;
    float tintA = tint.a / 255.0f;
    float baseR = br * tintR;
    float baseG = bg * tintG;
    float baseB = bb * tintB;
    float baseA = ba * tintA;
    bool unlit = part->unlit != 0;
    float metallic = clamp01f(part->metallic);
    float roughness = clamp01f(part->roughness);
    if (roughness < 0.04f) roughness = 0.04f;

    Vector3 L = {0, 0, 1};
    if (g_cpuLight.directionalEnabled && g_cpuLight.directionalIntensity > 0.0f)
        L = safeNormalize((Vector3){ -g_cpuLight.directionalDirection.x,
                                     -g_cpuLight.directionalDirection.y,
                                     -g_cpuLight.directionalDirection.z });
    float lightR = g_cpuLight.directionalColor.r / 255.0f *
                   g_cpuLight.directionalIntensity;
    float lightG = g_cpuLight.directionalColor.g / 255.0f *
                   g_cpuLight.directionalIntensity;
    float lightB = g_cpuLight.directionalColor.b / 255.0f *
                   g_cpuLight.directionalIntensity;
    float ambR = g_cpuLight.ambientColor.r / 255.0f *
                 g_cpuLight.ambientIntensity;
    float ambG = g_cpuLight.ambientColor.g / 255.0f *
                 g_cpuLight.ambientIntensity;
    float ambB = g_cpuLight.ambientColor.b / 255.0f *
                 g_cpuLight.ambientIntensity;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            float px = x + 0.5f, py = y + 0.5f;
            float e0 = (sv[1].x - px) * (sv[2].y - py) -
                       (sv[1].y - py) * (sv[2].x - px);
            float e1 = (sv[2].x - px) * (sv[0].y - py) -
                       (sv[2].y - py) * (sv[0].x - px);
            float e2 = (sv[0].x - px) * (sv[1].y - py) -
                       (sv[0].y - py) * (sv[1].x - px);
            if (e0 < 0.0f || e1 < 0.0f || e2 < 0.0f) continue;
            float b0 = e0 / area, b1 = e1 / area, b2 = e2 / area;
            float z = b0 * sv[0].z + b1 * sv[1].z + b2 * sv[2].z;
            if (z < 0.0f || z > 1.0f) continue;
            size_t at = (size_t)y * canvas->width + x;
            if (z >= canvas->depth[at]) continue;

            float invw = b0 * sv[0].invw + b1 * sv[1].invw +
                         b2 * sv[2].invw;
            if (fabsf(invw) < 1e-9f) continue;
            float u = (b0 * sv[0].uow + b1 * sv[1].uow + b2 * sv[2].uow) / invw;
            float v = (b0 * sv[0].vow + b1 * sv[1].vow + b2 * sv[2].vow) / invw;

            float wx = (b0 * sv[0].world.x * sv[0].invw +
                        b1 * sv[1].world.x * sv[1].invw +
                        b2 * sv[2].world.x * sv[2].invw) / invw;
            float wy = (b0 * sv[0].world.y * sv[0].invw +
                        b1 * sv[1].world.y * sv[1].invw +
                        b2 * sv[2].world.y * sv[2].invw) / invw;
            float wz = (b0 * sv[0].world.z * sv[0].invw +
                        b1 * sv[1].world.z * sv[1].invw +
                        b2 * sv[2].world.z * sv[2].invw) / invw;
            float nx = (b0 * sv[0].normal.x * sv[0].invw +
                        b1 * sv[1].normal.x * sv[1].invw +
                        b2 * sv[2].normal.x * sv[2].invw) / invw;
            float ny = (b0 * sv[0].normal.y * sv[0].invw +
                        b1 * sv[1].normal.y * sv[1].invw +
                        b2 * sv[2].normal.y * sv[2].invw) / invw;
            float nz = (b0 * sv[0].normal.z * sv[0].invw +
                        b1 * sv[1].normal.z * sv[1].invw +
                        b2 * sv[2].normal.z * sv[2].invw) / invw;

            float vr = (b0 * sv[0].color.r * sv[0].invw +
                        b1 * sv[1].color.r * sv[1].invw +
                        b2 * sv[2].color.r * sv[2].invw) / invw / 255.0f;
            float vg = (b0 * sv[0].color.g * sv[0].invw +
                        b1 * sv[1].color.g * sv[1].invw +
                        b2 * sv[2].color.g * sv[2].invw) / invw / 255.0f;
            float vb = (b0 * sv[0].color.b * sv[0].invw +
                        b1 * sv[1].color.b * sv[1].invw +
                        b2 * sv[2].color.b * sv[2].invw) / invw / 255.0f;
            float va = (b0 * sv[0].color.a * sv[0].invw +
                        b1 * sv[1].color.a * sv[1].invw +
                        b2 * sv[2].color.a * sv[2].invw) / invw / 255.0f;
            if (va <= 0.0f) { vr = vg = vb = va = 1.0f; }

            float ar = 1.0f, ag = 1.0f, ab = 1.0f, aa = 1.0f;
            if (part->texture) {
                Color tex = sampleTextureLinear(part->texture, u, v);
                ar = srgbToLinear(tex.r / 255.0f);
                ag = srgbToLinear(tex.g / 255.0f);
                ab = srgbToLinear(tex.b / 255.0f);
                aa = tex.a / 255.0f;
            }
            ar *= baseR * vr;
            ag *= baseG * vg;
            ab *= baseB * vb;
            aa *= baseA * va;

            if (part->alphaMode == 1 && aa < part->alphaCutoff) continue;
            unsigned char outA = part->alphaMode == 2
                ? (unsigned char)(clamp01f(aa) * 255.0f + 0.5f) : 255;

            float outR, outG, outB;
            if (unlit) {
                outR = ar; outG = ag; outB = ab;
            } else {
                Vector3 N = safeNormalize((Vector3){nx, ny, nz});
                Vector3 V = safeNormalize((Vector3){ cameraPos.x - wx,
                                                     cameraPos.y - wy,
                                                     cameraPos.z - wz });
                if (dot3(N, V) < 0.0f) {
                    N.x = -N.x; N.y = -N.y; N.z = -N.z;
                }
                float ndl = dot3(N, L);
                if (ndl < 0.0f) ndl = 0.0f;
                float shadow = shadowFactor((Vector3){ wx, wy, wz }, ndl);
                float lr = 0.0f, lg = 0.0f, lb = 0.0f;
                if (g_cpuLight.directionalEnabled && ndl > 0.0f &&
                    !colorIsZero(g_cpuLight.directionalColor)) {
                    Vector3 H = safeNormalize((Vector3){ L.x + V.x,
                                                         L.y + V.y,
                                                         L.z + V.z });
                    float ndh = dot3(N, H);
                    if (ndh < 0.0f) ndh = 0.0f;
                    float ndv = dot3(N, V);
                    if (ndv < 0.0f) ndv = 0.0f;
                    float vdh = dot3(V, H);
                    if (vdh < 0.0f) vdh = 0.0f;
                    float a = roughness * roughness;
                    float a2 = a * a;
                    float dnm = ndh * ndh * (a2 - 1.0f) + 1.0f;
                    float D = a2 / (3.14159265f * dnm * dnm + 1e-5f);
                    float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
                    float G = (ndv / (ndv * (1.0f - k) + k)) *
                              (ndl / (ndl * (1.0f - k) + k));
                    float spec = D * G / (4.0f * ndv * ndl + 1e-4f);
                    float f0r = 0.04f + (ar - 0.04f) * metallic;
                    float f0g = 0.04f + (ag - 0.04f) * metallic;
                    float f0b = 0.04f + (ab - 0.04f) * metallic;
                    float fbase = 1.0f - vdh;
                    float f2 = fbase * fbase;
                    float fres = f2 * f2 * fbase;
                    float fr = f0r + (1.0f - f0r) * fres;
                    float fg = f0g + (1.0f - f0g) * fres;
                    float fb = f0b + (1.0f - f0b) * fres;
                    float kd = 1.0f - metallic;
                    lr = (ar * kd + fr * spec) * lightR * ndl * shadow;
                    lg = (ag * kd + fg * spec) * lightG * ndl * shadow;
                    lb = (ab * kd + fb * spec) * lightB * ndl * shadow;
                }
                float ambScale = (g_cpuIBL.valid && g_cpuIBLIntensity > 0.0f)
                               ? 0.35f : 1.0f;
                if (g_cpuLight.ambientEnabled) {
                    lr += ar * ambR * ambScale;
                    lg += ag * ambG * ambScale;
                    lb += ab * ambB * ambScale;
                }
                if (g_cpuIBL.valid && g_cpuIBLIntensity > 0.0f) {
                    float irr[3];
                    sampleCube(&g_cpuIBL.irradiance, N, 0, irr);
                    float kdIbl = (1.0f - metallic) * g_cpuIBLIntensity;
                    lr += ar * irr[0] * kdIbl;
                    lg += ag * irr[1] * kdIbl;
                    lb += ab * irr[2] * kdIbl;

                    float ndvI = dot3(N, V);
                    if (ndvI < 1e-4f) ndvI = 1e-4f;
                    Vector3 R = { -V.x + 2.0f * ndvI * N.x,
                                  -V.y + 2.0f * ndvI * N.y,
                                  -V.z + 2.0f * ndvI * N.z };
                    R = safeNormalize(R);
                    int mipCount = g_cpuIBL.prefiltered.mips > 0
                                 ? g_cpuIBL.prefiltered.mips : 1;
                    int mip = (int)(roughness * (float)(mipCount - 1) + 0.5f);
                    float pre[3];
                    sampleCube(&g_cpuIBL.prefiltered, R, mip, pre);
                    float sc = 1.0f, bi = 0.0f;
                    sampleBRDF(ndvI, roughness, &sc, &bi);
                    float f0r = 0.04f + (ar - 0.04f) * metallic;
                    float f0g = 0.04f + (ag - 0.04f) * metallic;
                    float f0b = 0.04f + (ab - 0.04f) * metallic;
                    lr += pre[0] * (f0r * sc + bi) * g_cpuIBLIntensity;
                    lg += pre[1] * (f0g * sc + bi) * g_cpuIBLIntensity;
                    lb += pre[2] * (f0b * sc + bi) * g_cpuIBLIntensity;
                }
                float er = part->emissive[0] * part->emissiveStrength;
                float eg = part->emissive[1] * part->emissiveStrength;
                float eb = part->emissive[2] * part->emissiveStrength;
                if (part->emissiveTexture) {
                    Color e = sampleTextureLinear(part->emissiveTexture, u, v);
                    er *= srgbToLinear(e.r / 255.0f);
                    eg *= srgbToLinear(e.g / 255.0f);
                    eb *= srgbToLinear(e.b / 255.0f);
                }
                lr += er;
                lg += eg;
                lb += eb;
                outR = acesToneMap(lr);
                outG = acesToneMap(lg);
                outB = acesToneMap(lb);
            }
            Color outc = { byteFromLinear(outR), byteFromLinear(outG),
                           byteFromLinear(outB), outA };
            if (part->alphaMode != 2) canvas->depth[at] = z;
            blendPixel(canvas, x, y, outc);
        }
    }
}

static void drawPartShaded(VCpuCanvas *canvas, const VCpuModel *model,
                           const VCpuModelPart *part, int firstIndex,
                           int indexCount, Matrix mvp, Matrix modelMat,
                           Matrix normalMat, Vector3 cameraPos, Color tint) {
    for (int i = firstIndex; i + 2 < firstIndex + indexCount; i += 3) {
        uint32_t i0 = model->indices[i];
        uint32_t i1 = model->indices[i + 1];
        uint32_t i2 = model->indices[i + 2];
        if (i0 >= (uint32_t)model->vertexCount ||
            i1 >= (uint32_t)model->vertexCount ||
            i2 >= (uint32_t)model->vertexCount) continue;
        CpuShadeClip c[3] = {
            toClipShaded(&model->vertices[i0], mvp, modelMat, normalMat),
            toClipShaded(&model->vertices[i1], mvp, modelMat, normalMat),
            toClipShaded(&model->vertices[i2], mvp, modelMat, normalMat)
        };
        CpuShadeClip clipped[4];
        int n = clipNearShaded(c, 3, clipped);
        if (n < 3) continue;
        CpuShadeScreen s[4];
        bool ok = true;
        for (int k = 0; k < n; ++k) {
            if (!screenFromClipShaded(&clipped[k], canvas, &s[k])) {
                ok = false;
                break;
            }
        }
        if (!ok) continue;
        for (int k = 1; k + 1 < n; ++k) {
            CpuShadeScreen tri[3] = { s[0], s[k], s[k + 1] };
            ensureFaceNormal(tri);
            rasterShaded(canvas, tri, part, cameraPos, tint);
        }
    }
}

void VCpuDrawModelShaded(VCpuCanvas *canvas, const VCpuModel *model, Matrix mvp,
                         Matrix modelMatrix, Vector3 cameraPosition,
                         Color tint) {
    if (!canvas || !model || !model->vertices || !model->indices) return;
    ensureColorLUTs();
    Matrix normalMat = MatrixNormalFromModel(modelMatrix);
    if (model->parts && model->partCount > 0) {
        for (int p = 0; p < model->partCount; ++p) {
            const VCpuModelPart *part = &model->parts[p];
            int first = part->firstIndex;
            int count = part->indexCount;
            if (first < 0) first = 0;
            if (count <= 0 || first + count > model->indexCount)
                count = model->indexCount - first;
            if (count <= 0) continue;
            drawPartShaded(canvas, model, part, first, count, mvp, modelMatrix,
                           normalMat, cameraPosition, tint);
        }
        return;
    }
    VCpuModelPart part = defaultPart(model->baseColor, model->texture,
                                     model->indexCount);
    part.unlit = 0;
    drawPartShaded(canvas, model, &part, 0, model->indexCount, mvp, modelMatrix,
                   normalMat, cameraPosition, tint);
}

uint64_t VCpuHash(const VCpuCanvas *canvas) {
    uint64_t h = 1469598103934665603ULL;
    if (!canvas || !canvas->color) return h;
    size_t count = (size_t)canvas->width * (size_t)canvas->height;
    for (size_t i = 0; i < count; ++i) {
        h ^= canvas->color[i];
        h *= 1099511628211ULL;
        uint32_t dbits;
        memcpy(&dbits, &canvas->depth[i], sizeof(dbits));
        h ^= dbits;
        h *= 1099511628211ULL;
    }
    return h;
}

bool VCpuSavePPM(const VCpuCanvas *canvas, const char *path) {
    if (!canvas || !canvas->color || !path || !path[0]) return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", canvas->width, canvas->height);
    for (size_t i = 0; i < (size_t)canvas->width * canvas->height; ++i) {
        uint32_t p = canvas->color[i];
        unsigned char rgb[3] = { (p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return true;
}

bool VCpuBatchBegin(VCpuBatch *batch, VCpuCanvas *canvas,
                    const VCpuTexture *texture) {
    if (!batch || !canvas) return false;
    batch->canvas = canvas;
    batch->texture = texture;
    batch->vertices = NULL;
    batch->colors = NULL;
    batch->vertexCount = 0;
    batch->vertexCapacity = 0;
    return true;
}

static bool batchReserve(VCpuBatch *batch, int add) {
    int need = batch->vertexCount + add;
    if (need <= batch->vertexCapacity) return true;
    int cap = batch->vertexCapacity ? batch->vertexCapacity * 2 : 256;
    while (cap < need) cap *= 2;
    float *v = (float *)realloc(batch->vertices, sizeof(float) * 4 * (size_t)cap);
    if (!v) return false;
    batch->vertices = v;
    uint32_t *c = (uint32_t *)realloc(batch->colors, sizeof(uint32_t) * (size_t)cap);
    if (!c) return false;
    batch->colors = c;
    batch->vertexCapacity = cap;
    return true;
}

void VCpuBatchPushTriangle(VCpuBatch *batch, Vector2 a, Vector2 b, Vector2 c,
                           Vector2 ua, Vector2 ub, Vector2 uc, Color color) {
    if (!batch || !batchReserve(batch, 3)) return;
    int i = batch->vertexCount;
    float *v = batch->vertices + i * 4;
    v[0] = a.x; v[1] = a.y; v[2] = ua.x; v[3] = ua.y;
    v[4] = b.x; v[5] = b.y; v[6] = ub.x; v[7] = ub.y;
    v[8] = c.x; v[9] = c.y; v[10] = uc.x; v[11] = uc.y;
    batch->colors[i] = batch->colors[i + 1] = batch->colors[i + 2] =
        packColor(color);
    batch->vertexCount += 3;
}

void VCpuBatchPushRect(VCpuBatch *batch, float x, float y, float w, float h,
                       Color color) {
    Vector2 a = {x, y}, b = {x + w, y};
    Vector2 c = {x + w, y + h}, d = {x, y + h};
    Vector2 uv0 = {0, 0}, uv1 = {1, 0}, uv2 = {1, 1}, uv3 = {0, 1};
    VCpuBatchPushTriangle(batch, a, b, c, uv0, uv1, uv2, color);
    VCpuBatchPushTriangle(batch, a, c, d, uv0, uv2, uv3, color);
}

void VCpuBatchFlush(VCpuBatch *batch) {
    if (!batch || !batch->canvas) return;
    for (int i = 0; i + 2 < batch->vertexCount; i += 3) {
        CpuScreenVertex s[3];
        for (int k = 0; k < 3; ++k) {
            const float *v = batch->vertices + (i + k) * 4;
            s[k].x = v[0];
            s[k].y = v[1];
            s[k].z = 0.5f;
            s[k].uow = v[2];
            s[k].vow = v[3];
            s[k].invw = 1.0f;
        }
        Color colors[3];
        for (int k = 0; k < 3; ++k) {
            uint32_t p = batch->colors[i + k];
            colors[k] = (Color){ (p >> 16) & 0xFF, (p >> 8) & 0xFF,
                                 p & 0xFF, (p >> 24) & 0xFF };
        }
        rasterTextured(batch->canvas, s, batch->texture,
                       (Color){255,255,255,255}, colors, false);
    }
    batch->vertexCount = 0;
}

void VCpuBatchEnd(VCpuBatch *batch) {
    VCpuBatchFlush(batch);
    if (!batch) return;
    free(batch->vertices);
    free(batch->colors);
    memset(batch, 0, sizeof(*batch));
}

void VCpuBlit(const VCpuCanvas *canvas, uint32_t *dst, int dstWidth,
              int dstHeight, int dstPitchPixels) {
    if (!canvas || !canvas->color || !dst || dstWidth <= 0 || dstHeight <= 0)
        return;
    if (dstPitchPixels < dstWidth) dstPitchPixels = dstWidth;
    for (int y = 0; y < dstHeight; ++y) {
        int sy = canvas->height > 0 ? y * canvas->height / dstHeight : 0;
        for (int x = 0; x < dstWidth; ++x) {
            int sx = canvas->width > 0 ? x * canvas->width / dstWidth : 0;
            dst[(size_t)y * dstPitchPixels + x] =
                canvas->color[(size_t)sy * canvas->width + sx];
        }
    }
}
