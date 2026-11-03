#ifndef VLCRLIB_VCPU_H
#define VLCRLIB_VCPU_H

#include "vcore.h"
#include "vgraphics.h"
#include "vmath.h"
#include "vibl.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal software rasterizer used by the CPU-backend MVP.
   It owns a top-left-origin RGBA8 color buffer and a float depth buffer.
   Presentation is deliberately left to the caller (SDL texture, PNG, ...). */
typedef struct VCpuCanvas {
    int      width;
    int      height;
    uint32_t *color;   /* 0xAARRGGBB, matching SDL_PIXELFORMAT_ARGB8888 */
    float    *depth;
    int      scissorX, scissorY, scissorW, scissorH;
    bool     scissorEnabled;
} VCpuCanvas;

typedef enum VCpuBackend {
    VCPU_BACKEND_AUTO = 0,
    VCPU_BACKEND_GPU,
    VCPU_BACKEND_CPU
} VCpuBackend;

typedef struct VCpuTexture {
    int       width, height;
    uint32_t *pixels;   /* 0xAARRGGBB; owned */
} VCpuTexture;

typedef struct VCpuVertex {
    Vector3 position;
    Vector2 uv;
    /* Optional. A zero normal is replaced by the triangle's geometric normal
       at draw time; a zero colour falls back to opaque white. Keeping these
       fields at the end preserves aggregate initialisers written for the old
       two-field struct. */
    Vector3 normal;
    Color   color;
} VCpuVertex;

/* One material/draw range. A glTF primitive maps to one part. */
typedef struct VCpuModelPart {
    int              firstIndex, indexCount;
    const VCpuTexture *texture;        /* albedo; non-owning; may be NULL */
    const VCpuTexture *emissiveTexture; /* non-owning; may be NULL */
    Color            baseColor;        /* unlit fallback / legacy tint */
    float            baseColorFactor[4]; /* linear RGBA */
    float            metallic, roughness;
    float            emissive[3];      /* linear RGB */
    float            emissiveStrength;
    int              alphaMode;        /* 0=OPAQUE 1=MASK 2=BLEND */
    float            alphaCutoff;
    int              doubleSided;
    int              unlit;
} VCpuModelPart;

typedef struct VCpuModel {
    VCpuVertex *vertices;
    int         vertexCount;
    uint32_t   *indices;
    int         indexCount;
    Color       baseColor;
    VCpuTexture *texture;   /* non-owning legacy single-texture path */

    VCpuModelPart *parts;
    int            partCount;

    /* Textures copied by vgraphics_BuildCpuModel(). VCpuModelFree() owns
       these pixels; the legacy `texture` pointer above stays non-owning. */
    VCpuTexture   *ownedTextures;
    int            ownedTextureCount;
} VCpuModel;

/* Backend preference for applications that explicitly select CPU rendering.
   The engine's Vulkan/WebGPU path is unaffected; use VCpuCanvas APIs when CPU
   is selected. */
void       VCpuSetPreferredBackend(VCpuBackend backend);
VCpuBackend VCpuGetPreferredBackend(void);

bool VCpuTextureFromImage(VCpuTexture *texture, Image image);
void VCpuTextureFree(VCpuTexture *texture);

VCpuModel VCpuModelCreate(const VCpuVertex *vertices, int vertexCount,
                          const uint32_t *indices, int indexCount,
                          Color baseColor, VCpuTexture *texture);
void VCpuModelFree(VCpuModel *model);
/* Convert a GPU-loaded Model to CPU geometry (node transforms baked in).
   The GPU texture data is not duplicated, so the result is untextured. */
bool vgraphics_BuildCpuModel(Model model, VCpuModel *out);
void VCpuDrawModel(VCpuCanvas *canvas, const VCpuModel *model, Matrix mvp,
                   Color tint);

/* Lighting used by VCpuDrawModelShaded(). `direction` is the direction the
   light travels (same convention as vgraphics_SetDirectionalLight), so the
   vector towards the light is -direction. The shaded path also needs the
   model matrix for world-space normals and the camera position for specular. */
void VCpuSetDirectionalLight(bool enabled, Vector3 direction, Color color,
                             float intensity);
void VCpuSetAmbientLight(bool enabled, Color color, float intensity);
void VCpuSetExposure(float exposure);
void VCpuDrawModelShaded(VCpuCanvas *canvas, const VCpuModel *model, Matrix mvp,
                         Matrix modelMatrix, Vector3 cameraPosition,
                         Color tint);

/* CPU directional shadow map. Casters are recorded by the caller and rebuilt
   once per frame from the previous frame's list. */
void VCpuSetShadows(bool enabled, int mapSize, float bias, float strength);
void VCpuSetShadowSoftness(float radius);   /* 1..3 texels */
bool VCpuShadowsEnabled(void);

typedef struct VCpuShadowCaster {
    const VCpuModel *model;
    Matrix           world;
} VCpuShadowCaster;

/* Build the light matrix, depth map and PCF state for this frame. */
void VCpuShadowBuild(const VCpuShadowCaster *casters, int count);

/* CPU IBL: takes ownership of the bundle (moves it out of *bundle). */
bool VCpuSetIBLBundle(VIBL_Bundle *bundle);
void VCpuUnloadIBL(void);
void VCpuSetIBLIntensity(float intensity);
bool VCpuIsIBLLoaded(void);

/* Free CPU-global resources (shadow map, IBL bundle). */
void VCpuShutdown(void);

/* Deterministic FNV-1a hash over color+depth; used by CPU golden tests. */
uint64_t VCpuHash(const VCpuCanvas *canvas);
/* Minimal binary PPM screenshot (P6), no external image encoder. */
bool VCpuSavePPM(const VCpuCanvas *canvas, const char *path);

bool VCpuInit(VCpuCanvas *canvas, int width, int height);
void VCpuFree(VCpuCanvas *canvas);

void VCpuClear(VCpuCanvas *canvas, Color color);
void VCpuClearDepth(VCpuCanvas *canvas);
void VCpuSetScissor(VCpuCanvas *canvas, int x, int y, int w, int h);
void VCpuResetScissor(VCpuCanvas *canvas);

/* Solid triangle in framebuffer pixels (y grows down). */
void VCpuTriangle2D(VCpuCanvas *canvas, Vector2 a, Vector2 b, Vector2 c,
                    Color color);
void VCpuLine2D(VCpuCanvas *canvas, Vector2 a, Vector2 b, Color color);

/* Triangle transformed by an MVP; performs perspective divide and z test.
   Triangles crossing w <= 0 are rejected (MVP scope). */
void VCpuTriangle3D(VCpuCanvas *canvas, Vector3 a, Vector3 b, Vector3 c,
                    Color color, Matrix mvp);

/* Nearest-neighbour blit into an RGBA8 destination of the same layout. */
void VCpuBlit(const VCpuCanvas *canvas, uint32_t *dst, int dstWidth,
              int dstHeight, int dstPitchPixels);

/* Dynamic 2D triangle batch with one texture and scissor support. */
typedef struct VCpuBatch {
    float       *vertices;   /* x,y,u,v interleaved */
    uint32_t    *colors;
    int          vertexCount, vertexCapacity;
    const VCpuTexture *texture;
    VCpuCanvas  *canvas;
} VCpuBatch;

bool VCpuBatchBegin(VCpuBatch *batch, VCpuCanvas *canvas,
                    const VCpuTexture *texture);
void VCpuBatchPushTriangle(VCpuBatch *batch, Vector2 a, Vector2 b, Vector2 c,
                           Vector2 ua, Vector2 ub, Vector2 uc, Color color);
void VCpuBatchPushRect(VCpuBatch *batch, float x, float y, float w, float h,
                       Color color);
void VCpuBatchFlush(VCpuBatch *batch);
void VCpuBatchEnd(VCpuBatch *batch);

#ifdef __cplusplus
}
#endif
#endif /* VLCRLIB_VCPU_H */
