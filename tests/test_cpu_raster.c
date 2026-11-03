#include "vcpu.h"
#include "vplatform.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(c, name) do { \
    if (c) printf("[PASS] %s\n", name); \
    else { printf("[FAIL] %s\n", name); fails++; } \
} while (0)

static uint32_t px(const VCpuCanvas *c, int x, int y) {
    return c->color[(size_t)y * c->width + x];
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    VCpuCanvas c;
    CHECK(!VCpuInit(NULL, 8, 8), "VCpuInit(NULL)");
    CHECK(!VCpuInit(&c, 0, 8), "VCpuInit(zero width)");
    CHECK(VCpuInit(&c, 16, 16), "VCpuInit");

    VCpuClear(&c, (Color){255, 0, 0, 255});
    CHECK(px(&c, 0, 0) == 0xFFFF0000u, "clear color");

    VCpuTriangle2D(&c, (Vector2){1, 1}, (Vector2){14, 1},
                   (Vector2){8, 14}, (Color){0, 255, 0, 255});
    CHECK(px(&c, 8, 6) == 0xFF00FF00u, "2D triangle inside");
    CHECK(px(&c, 0, 0) == 0xFFFF0000u, "2D triangle outside");

    VCpuClear(&c, (Color){0, 0, 0, 255});
    VCpuClearDepth(&c);
    Matrix mvp = MatrixIdentity();
    /* NDC triangle at z=0.8 (far, red), then z=0.2 (near, blue). */
    VCpuTriangle3D(&c, (Vector3){-0.8f, -0.8f, 0.8f},
                   (Vector3){0.8f, -0.8f, 0.8f},
                   (Vector3){0.0f, 0.8f, 0.8f},
                   (Color){255, 0, 0, 255}, mvp);
    VCpuTriangle3D(&c, (Vector3){-0.5f, -0.5f, 0.2f},
                   (Vector3){0.5f, -0.5f, 0.2f},
                   (Vector3){0.0f, 0.5f, 0.2f},
                   (Color){0, 0, 255, 255}, mvp);
    CHECK(px(&c, 8, 8) == 0xFF0000FFu, "3D depth test keeps near triangle");

    VCpuCanvas small;
    CHECK(VCpuInit(&small, 4, 4), "VCpuInit small");
    uint32_t dst[8 * 8];
    for (int i = 0; i < 64; ++i) dst[i] = 0;
    VCpuBlit(&c, dst, 8, 8, 8);
    CHECK(dst[0] != 0, "VCpuBlit writes destination");

    VCpuFree(&small);

    /* Texture + model + UV path. */
    unsigned char rgba[2 * 2 * 4] = {
        255,0,0,255,   0,255,0,255,
        0,0,255,255,   255,255,255,255
    };
    VCpuTexture tex;
    CHECK(VCpuTextureFromImage(&tex, (Image){rgba, 2, 2, 1, 0}),
          "texture from image");
    VCpuVertex verts[4] = {
        {{-1,-1,0}, {0,0}}, {{1,-1,0}, {1,0}},
        {{1,1,0}, {1,1}},   {{-1,1,0}, {0,1}}
    };
    uint32_t idx[6] = {0,1,2,0,2,3};
    VCpuModel model = VCpuModelCreate(verts, 4, idx, 6,
                                      (Color){255,255,255,255}, &tex);
    CHECK(model.vertices && model.indices, "model create");
    VCpuClear(&c, (Color){0,0,0,255});
    VCpuClearDepth(&c);
    VCpuDrawModel(&c, &model, MatrixIdentity(), (Color){255,255,255,255});
    CHECK(px(&c, 8, 8) != 0xFF000000u, "model rasterizes");

    uint64_t h1 = VCpuHash(&c);
    VCpuClear(&c, (Color){0,0,0,255});
    VCpuClearDepth(&c);
    VCpuDrawModel(&c, &model, MatrixIdentity(), (Color){255,255,255,255});
    CHECK(VCpuHash(&c) == h1, "CPU render hash deterministic");
    CHECK(VCpuSavePPM(&c, "test_cpu_raster.ppm"), "PPM screenshot");
    remove("test_cpu_raster.ppm");

    VCpuClear(&c, (Color){0,0,0,255});
    VCpuClearDepth(&c);
    VCpuSetScissor(&c, 4, 4, 4, 4);
    VCpuTriangle2D(&c, (Vector2){0,0}, (Vector2){15,0},
                   (Vector2){8,15}, (Color){255,0,0,255});
    CHECK(px(&c, 2, 2) == 0xFF000000u, "scissor rejects outside");
    CHECK(px(&c, 6, 6) == 0xFFFF0000u, "scissor accepts inside");
    VCpuResetScissor(&c);

    unsigned char whitePx[4] = {255,255,255,255};
    VCpuTexture whiteTex;
    VCpuTextureFromImage(&whiteTex, (Image){whitePx, 1, 1, 1, 0});
    VCpuClear(&c, (Color){0,0,0,255});
    VCpuClearDepth(&c);
    VCpuBatch batch;
    CHECK(VCpuBatchBegin(&batch, &c, &whiteTex), "batch begin");
    VCpuBatchPushRect(&batch, 2, 2, 10, 10, (Color){0,255,0,255});
    VCpuBatchFlush(&batch);
    CHECK(px(&c, 4, 4) != 0xFF000000u, "batch rect draws");
    VCpuBatchEnd(&batch);

    VCpuSetPreferredBackend(VCPU_BACKEND_CPU);
    CHECK(VCpuGetPreferredBackend() == VCPU_BACKEND_CPU,
          "backend selector");
    CHECK(VRenderBackendAvailable(VRENDER_BACKEND_CPU),
          "CPU backend available");
    CHECK(VRenderBackendSelect(VRENDER_BACKEND_CPU),
          "select CPU backend");
    CHECK(VRenderBackendGet() == VRENDER_BACKEND_CPU,
          "CPU backend active");
    CHECK(strcmp(VRenderBackendName(), "cpu") == 0, "CPU backend name");
    VCpuModelFree(&model);
    VCpuTextureFree(&tex);

    VCpuFree(&c);
    CHECK(c.color == NULL && c.width == 0, "VCpuFree");

    printf("cpu_raster: %d failure(s)\n", fails);
    return fails ? 1 : 0;
}
