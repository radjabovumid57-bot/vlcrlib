#ifndef VLCRLIB_VGRAPHICS_H
#define VLCRLIB_VGRAPHICS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "vcore.h"

typedef enum VkInitResult {
    VKINIT_OK = 0,
    VKINIT_ERR_INSTANCE,
    VKINIT_ERR_SURFACE,
    VKINIT_ERR_PHYSICAL_DEVICE,
    VKINIT_ERR_DEVICE,
    VKINIT_ERR_SWAPCHAIN,
    VKINIT_ERR_RENDERPASS,
    VKINIT_ERR_PIPELINE,
    VKINIT_ERR_COMMAND,
    VKINIT_ERR_SYNC,
    VKINIT_ERR_TEXTURE
} VkInitResult;

/* ---------- Opaque handles ---------- */
typedef struct Shader      { unsigned int id; } Shader;
typedef struct Texture2D   { unsigned int id; int width, height, mipmaps, format; unsigned int generation; } Texture2D;
typedef struct Image       { void *data; int width, height, mipmaps, format; } Image;
typedef struct RenderTexture2D { unsigned int id; Texture2D texture; Texture2D depth; unsigned int generation; } RenderTexture2D;

typedef struct Vertex2D {
    float x, y;
    float u, v;
    unsigned char r, g, b, a;
} Vertex2D;

/* Software bridge used when VRenderBackendSelect(CPU) is active. */
bool vcore_IsCpuBackend(void);
void vcore_CpuDrawVertices(const Vertex2D *vertices, int count, Texture2D texture);

typedef struct Vertex3D {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
    float tx, ty, tz, tw;   /* tangent xyz + handedness (glTF TANGENT) */
    float cr, cg, cb, ca;   /* vertex color (glTF COLOR_0), default white in shader */
} Vertex3D;

typedef struct SkinnedVertex3D {
    float   x, y, z;
    float   nx, ny, nz;
    float   u, v;
    float   tx, ty, tz, tw;
    unsigned char joints[4];
    float   weights[4];
} SkinnedVertex3D;

typedef struct Mesh {
    unsigned int id;
    int vertexCount;
    int indexCount;
    unsigned int generation;
} Mesh;

typedef struct Material {
    unsigned int id;
    unsigned int generation;
    Color baseColor;              /* 8-bit display/API; not used as linear PBR factor */
    float baseColorFactor[4];     /* linear RGBA (glTF baseColorFactor) — written to UBO */
    float metallic;
    float roughness;
    float  ior;
    float  normalScale;
    float  aoStrength;
    Color  emissionColor;
    float  emissionStrength;

    /* KHR_materials_specular */
    float  specularFactor;
    float  specularColor[3];

    /* KHR_materials_emissive_strength */
    float  emissiveStrengthKhr;   /* multiplies emission, default 1 */

    /* KHR_materials_unlit */
    int    unlit;

    /* KHR_materials_clearcoat */
    float  clearcoatFactor;
    float  clearcoatRoughness;

    /* KHR_materials_sheen */
    float  sheenColor[3];
    float  sheenRoughness;

    /* KHR_materials_transmission */
    float  transmissionFactor;

    /* KHR_materials_volume */
    float  thicknessFactor;
    float  attenuationDistance;
    float  attenuationColor[3];

    /* KHR_materials_anisotropy */
    float  anisotropyStrength;
    float  anisotropyRotation;    /* radians */

    /* KHR_materials_iridescence */
    float  iridescenceFactor;
    float  iridescenceIor;
    float  iridescenceThicknessMin;
    float  iridescenceThicknessMax;

    /* KHR_materials_dispersion */
    float  dispersion;

    /* Core alpha (useful with transmission) */
    int    alphaMode;             /* 0=OPAQUE 1=MASK 2=BLEND */
    float  alphaCutoff;
    int    doubleSided;

    Texture2D albedoMap;
    Texture2D normalMap;
    Texture2D metallicRoughnessMap;
    Texture2D aoMap;
    Texture2D emissionMap;
    Texture2D specularMap;    /* KHR specularTexture (linear A) */
    Texture2D clearcoatMap;   /* KHR clearcoat textures packed or factor */
} Material;

/* ------------------------------------------------------------------ */
/*  Explicit GPU light records (optional SSBO path, binding 16)       */
/* ------------------------------------------------------------------ */

#define VLCRLIB_MAX_GPU_LIGHTS 64

enum {
    VLCR_LIGHT_TYPE_DIRECTIONAL = 0,
    VLCR_LIGHT_TYPE_POINT       = 1,
    VLCR_LIGHT_TYPE_SPOT        = 2,
    VLCR_LIGHT_TYPE_AREA        = 3
};

/* std430 layout: 5 * vec4 (80 bytes). position.w = range,
 * direction.w = innerConeCos (spot), color.w = outerConeCos (spot),
 * params = (type, enabled, shadowIndex, areaWidth), extra.x = areaHeight. */
typedef struct VLightData {
    float position[4];
    float direction[4];
    float color[4];
    float params[4];
    float extra[4];
} VLightData;

void vgraphics_SetLightArray(const VLightData *lights, int count);
int  vgraphics_AddLight(const VLightData *light);
void vgraphics_ClearLights(void);
int  vgraphics_GetLightCount(void);

VLightData VLightDataDirectional(Vector3 direction, Color color, float intensity);
VLightData VLightDataPoint(Vector3 position, Color color, float intensity, float range);
VLightData VLightDataSpot(Vector3 position, Vector3 direction, Color color,
                          float intensity, float range, float innerCone, float outerCone);
VLightData VLightDataArea(Vector3 position, Vector3 normal, float width, float height,
                          Color color, float intensity);

#ifndef VLCRLIB_MAX_MODELS
#define VLCRLIB_MAX_MODELS 64
#endif

typedef struct Model {
    unsigned int id;
    int meshCount;
    int materialCount;
} Model;

void vcore_CpuDrawModel(Model model, Vector3 position, Vector3 rotation,
                        Vector3 scale, Color tint);

typedef struct Animation {
    unsigned int id;
    float duration;
    int boneCount;
} Animation;

typedef struct Skeleton {
    unsigned int id;
    int boneCount;
} Skeleton;

typedef struct SkinnedModel {
    unsigned int id;
    int meshCount;
    int materialCount;
    int skeleton;
    int animationCount;
} SkinnedModel;

/* ---------- Material map types ---------- */
#define MATERIAL_MAP_ALBEDO             0
#define MATERIAL_MAP_NORMAL             1
#define MATERIAL_MAP_METALLICROUGHNESS  2
#define MATERIAL_MAP_AO                 3
#define MATERIAL_MAP_EMISSION           4
#define MATERIAL_MAP_SPECULAR           5  /* KHR_materials_specular: A = weight */
#define MATERIAL_MAP_CLEARCOAT          6  /* KHR_materials_clearcoat: R=factor-ish, G=rough */

/* ============================================================ */
/*  Lifecycle                                                    */
/* ============================================================ */
VkInitResult InitGraphicsDevice(void);
void         CloseGraphicsDevice(void);

/* ============================================================ */
/*  Frame                                                        */
/* ============================================================ */
void BeginVulkanFrame(void);
void EndVulkanFrame(void);
/* Requests swapchain recreation at the next present. Offscreen post-process
   targets are currently sized at device init; call this only when the
   post-process/offscreen path is disabled, or recreate the device/window for
   a fully correct resize. */
void vgraphics_Resize(void);

/* Lightweight frame counters for profiling and HUDs. Reset automatically at
   BeginVulkanFrame(); gpuFrameMs is 0 until GPU timestamps are implemented. */
typedef struct FrameStats {
    uint64_t frameIndex;
    int      width, height;
    int      drawCalls;
    int      shadowDraws;
    int      transparentDraws;
    int      culledDraws;
    uint64_t triangles;
    int      textureCount;
    int      meshCount;
    uint64_t estimatedGpuBytes;
    float    cpuFrameMs;
    float    gpuFrameMs;
    float    shadowMs;   /* shadow + SSAO prepasses */
    float    sceneMs;    /* scene render pass */
    float    postMs;     /* bloom + composite */
} FrameStats;

void vgraphics_GetFrameStats(FrameStats *out);
void vgraphics_ResetFrameStats(void);

#if defined(__EMSCRIPTEN__)
/* WebGPU validation hook: number of uncaptured device errors this run. */
int vgraphics_WebGPUErrorCount(void);
#endif

/* Capture the last presented frame to a PNG. Call outside BeginDrawing/
   EndDrawing; returns false if no frame has been presented yet. */
bool vgraphics_CaptureScreenshot(const char *path);
/* Size of the target the 3D scene is rasterized into: the offscreen
   post-process image when active, otherwise the swapchain. */
void vgraphics_GetRenderTargetSize(int *width, int *height);

void SetVulkanClearColor(float r, float g, float b, float a);

/* Sprint B: debug views (0=final 1=albedo 2=normal 3=rough 4=metal 5=AO 6=emissive 7=shadow) */
#define VLCRLIB_DEBUG_VIEW_NONE     0
#define VLCRLIB_DEBUG_VIEW_ALBEDO   1
#define VLCRLIB_DEBUG_VIEW_NORMAL   2
#define VLCRLIB_DEBUG_VIEW_ROUGHNESS 3
#define VLCRLIB_DEBUG_VIEW_METALLIC 4
#define VLCRLIB_DEBUG_VIEW_AO       5
#define VLCRLIB_DEBUG_VIEW_EMISSIVE 6
#define VLCRLIB_DEBUG_VIEW_SHADOW   7
void SetGraphicsDebugView(int mode);
int  GetGraphicsDebugView(void);
void SetGraphicsDebugLog(int enabled);
/* Number of Vulkan validation errors observed in this process. */
int  vgraphics_GetValidationErrorCount(void);
/* Sprint D: 0=idle 1=acquire 2=shadow 3=scene 4=present */
int GetGraphicsFramePhase(void);
/* 0 means success; a nonzero VkResult means the frame loop failed and the
   graphics device must be closed and initialized again. */
int GetGraphicsLastFrameResult(void);
void SetGraphicsExposure(float exposure);  /* 1.0 default; replaces magic *1.4 */
float GetGraphicsExposure(void);
void SetShadowQuality(float bias, float strength, float softRadius); /* softRadius 1..3 texels */
/* Screen-space AO (in-shader; multiplies ambient). intensity 0=off */
void SetSSAO(int enabled, float radius, float intensity);
void SetPostProcess(int enabled); /* 1 = render main pass to offscreen scene */
int  GetPostProcess(void);
void GetSSAO(int *enabled, float *radius, float *intensity);
/* Soft bloom (in-shader threshold; 0 intensity = off) */
void SetBloom(float threshold, float intensity);
/* FXAA in the composite pass. Requires post-process; enabling it forces the
   offscreen scene target on. */
void SetFXAA(int enabled);
int  GetFXAA(void);
/* Temporal accumulation with depth reprojection; requires post-process. */
void SetTAA(int enabled);
int  GetTAA(void);
void SetVulkanViewport(int x, int y, int w, int h);
void SetVulkanViewMatrix(Matrix m);
void SetVulkanProjectionMatrix(Matrix m);

void vgraphics_Set2DViewportSize(int width, int height);
void vgraphics_Set3DMatrices(Matrix view, Matrix proj);
void vgraphics_SetCameraPos(Vector3 position);
void vgraphics_SetDirectionalLight(bool enabled, Vector3 direction, Color color, float intensity);
void vgraphics_SetPointLight(bool enabled, Vector3 position, Color color, float intensity, float range);
void vgraphics_SetSpotLight(bool enabled, Vector3 position, Vector3 direction, Color color,
                            float intensity, float range, float innerCone, float outerCone);
void vgraphics_SetAmbientLight(bool enabled, Color color, float intensity);

/* Area light (rectangular panel) */
void vgraphics_SetAreaLight(bool enabled, Vector3 position, Vector3 normal,
                            float width, float height, Color color, float intensity);
/* Distance fog. Linear mode uses start/end; exponential mode uses density. */
void vgraphics_SetFog(bool enabled, Color color, float start, float end, float density);

/* Directional shadow map (PCF). mapSize typically 1024 or 2048.
   Resource recreation waits for the device; call outside BeginDrawing/
   EndDrawing when changing mapSize. */
void vgraphics_SetShadows(bool enabled, int mapSize, float bias, float strength);
void vgraphics_SetShadowSoftness(float radius); /* 1..2 PCF radius */
void vgraphics_UpdateShadowMatrix(Matrix lightViewProj); /* optional override */
void vgraphics_ComputeDirectionalShadowMatrix(float radius, float nearZ, float farZ);
void vgraphics_ComputeDirectionalShadowCascades(void);
void vgraphics_SetSpotShadows(bool enabled, int mapSize, float bias, float strength);
void vgraphics_SetPointShadows(bool enabled, int mapSize, float bias, float strength);
void vgraphics_ComputeSpotShadowMatrix(float nearZ, float farZ);

/* IBL / HDR cubemap
 * hdrPath: path to Radiance .hdr (equirectangular), or NULL for procedural studio.
 * Loads HDR → 6-face cubemap → irradiance + prefiltered mips + BRDF LUT (CPU),
 * then rebuilds the IBL/material/skybox GPU resources.
 *
 * IMPORTANT: call between frames (outside BeginDrawing/EndDrawing). The
 * rebuild destroys and re-creates descriptor-bound images, which invalidates
 * a command buffer that is currently recording.
 */
void vgraphics_EnableStudioLighting(void);
bool vgraphics_LoadIBL(const char *hdrPath);
bool vgraphics_LoadHDRCubemap(const char *hdrPath, int cubeSize); /* cubemap only */
/* Disables IBL/skybox immediately. The GPU images stay resident until the
 * next vgraphics_LoadIBL() or device shutdown (safe teardown on NVIDIA). */
void vgraphics_UnloadIBL(void);
void vgraphics_SetIBLIntensity(float intensity); /* 0 = procedural in shader, 1 = maps */
bool vgraphics_IsIBLLoaded(void);

/* Environment background. Draws the HDR environment cubemap as a skybox
 * behind scene geometry (only when IBL is loaded). Enabled by default.
 * Automatically drawn by BeginMode3D(); exposed for custom render loops. */
void vgraphics_SetSkybox(int enabled);
int  vgraphics_GetSkybox(void);
void vgraphics_DrawSkybox(void);

/* Procedural atmosphere / celestial bodies for the environment background.
 * When enabled, skybox.frag renders a time-of-day sky (sun, moon, stars,
 * clouds, haze) instead of sampling the IBL cubemap. */
typedef struct AtmosphereParams {
    int     enabled;
    float   timeOfDay;         /* 0..24, 12 = solar noon */
    float   cloudCoverage;     /* 0 = clear, 1 = overcast */
    float   haze;              /* 0..1 horizon haze / fog */
    float   starIntensity;     /* 0..1 */
    Vector3 sunDirection;      /* world-space direction from surface to sun */
    float   sunIntensity;      /* HDR multiplier for the sun disk */
    Vector3 moonDirection;     /* world-space direction from surface to moon */
    float   moonIntensity;
    float   sunAngularRadius;  /* radians, e.g. 0.009 */
    float   moonAngularRadius;
} AtmosphereParams;

void vgraphics_SetAtmosphere(const AtmosphereParams *params);
void vgraphics_GetAtmosphere(AtmosphereParams *out);

/* ============================================================ */
/*  2D batch                                                     */
/* ============================================================ */
void vgraphics_DrawVertexBuffer(const Vertex2D *vertices, int count, Texture2D texture);
/* Draw the ray-tracing scene. Vulkan uses hardware Ray Query when available,
 * with a software fragment-shader fallback on other devices. */
void DrawRayTracedDemo(float timeSeconds);
bool vgraphics_IsHardwareRayTracingAvailable(void);

/* ============================================================ */
/*  Textures                                                     */
/* ============================================================ */

/* Sampling state for LoadTextureFromImageEx(). The numeric values mirror the
 * glTF 2.0 sampler enums so a loader can pass model data through directly:
 *
 *   minFilter: 0 NEAREST, 1 LINEAR, 2 NEAREST_MIPMAP_NEAREST,
 *              3 LINEAR_MIPMAP_NEAREST, 4 NEAREST_MIPMAP_LINEAR,
 *              5 LINEAR_MIPMAP_LINEAR
 *   magFilter: 0 NEAREST, 1 LINEAR
 *   wrapS/wrapT: 0 REPEAT, 1 CLAMP_TO_EDGE, 2 MIRRORED_REPEAT
 *
 * Note that REPEAT (0) is the glTF default, whereas the library's shared
 * fallback sampler clamps — so a glTF loader must pass the sampler through
 * here, otherwise tiling textures smear at the UV border.
 */
typedef struct TextureOptions {
    int srgb;         /* 1 = colour data, 0 = linear data (normal/MR/AO) */
    int mipmaps;      /* 1 = build a mip chain (default), 0 = single level */
    int minFilter;    /* default 5 */
    int magFilter;    /* default 1 */
    int wrapS;        /* default 0 */
    int wrapT;        /* default 0 */
} TextureOptions;

Texture2D LoadTextureFromImage(Image image);
Texture2D LoadTextureFromImageSRGB(Image image);
/* Same, with an explicit sampler. `options` may be NULL to use the shared
   fallback sampler (linear filtering, full mip chain, CLAMP_TO_EDGE). Pass a
   zero-initialized TextureOptions only after filling every field you rely on:
   zero means single level, NEAREST filters and REPEAT. */
Texture2D LoadTextureFromImageEx(Image image, const TextureOptions *options);
/* Raw KTX2 (supercompression=0) with BC7/BC5 payload, as written by
   `vlcrc --ktx` / `--ktx-bc5`. */
Texture2D LoadTextureKTX2(const char *path);
void      UnloadTexture(Texture2D texture);
void      UpdateTexture(Texture2D texture, const void *pixels);
/* CPU-side RGBA8 copy of a loaded texture, for the software 2D path. */
bool      vgraphics_GetCpuTexturePixels(Texture2D texture,
                                        const uint32_t **pixels,
                                        int *width, int *height);

/* ============================================================ */
/*  3D meshes                                                    */
/* ============================================================ */
Mesh GenMeshCube(float w, float h, float d);
Mesh GenMeshSphere(float radius, int rings, int slices);
Mesh GenMeshPlane(float w, float d, int resX, int resZ);
Mesh GenMeshCylinder(float radius, float height, int slices);
Mesh GenMeshTorus(float r1, float r2, int ringSeg, int tubeSeg);
void UnloadMesh(Mesh mesh);

/* Low-level 3D drawing */
void DrawMesh(Mesh mesh, Texture2D texture, Color baseColor, Matrix model);

/* Convenience wrappers */
void DrawCube(Vector3 position, Vector3 size, Color color);
void DrawSphere(Vector3 center, float radius, Color color);
void DrawGrid(int slices, float spacing);
void DrawLine3D(Vector3 start, Vector3 end, Color color);

/* ============================================================ */
/*  Materials / Models                                           */
/* ============================================================ */
Material LoadMaterialDefault(void);
void     SetMaterialTexture(Material *material, int mapType, Texture2D tex);
void     SetMaterialColor(Material *material, Color color);
void     SetMaterialBaseColorFactor(Material *material, float r, float g, float b, float a);
void     SetMaterialPBR(Material *material, float metallic, float roughness);
void     SetMaterialIOR(Material *material, float ior);
void     SetMaterialEmission(Material *material, Color color, float strength);
void     SetMaterialSpecular(Material *material, float specularFactor,
                             float specularColorR, float specularColorG, float specularColorB);
void     UnloadMaterial(Material material);

Model    LoadModel(const char *path);
Model    LoadModelFromMesh(Mesh mesh);
void     UnloadModel(Model model);
void     DrawModel(Model model, Vector3 position, Vector3 rotation, Vector3 scale, Color tint);
void     vlcrlib_replace_model_material(Model model, int materialIndex, Material material);

/* ============================================================ */
/*  Render textures                                              */
/* ============================================================ */
RenderTexture2D LoadRenderTexture(int width, int height);
/* RenderTexture2D owns its color texture; release both together with this call. */
void            UnloadRenderTexture(RenderTexture2D rt);
void            BeginTextureMode(RenderTexture2D rt);
void            EndTextureMode(void);

/* ============================================================ */
/*  Skinning                                                     */
/* ============================================================ */
SkinnedModel LoadSkinnedModel(const char *path);

/* Build a skinned model from uploaded CPU data (fastgltf loader path).
   Creates one mesh, one material and a bind-pose animation. */
SkinnedModel vgraphics_CreateSkinnedModel(
    const SkinnedVertex3D *vertices, int vertexCount,
    const uint32_t *indices, int indexCount,
    const Material *material,
    const Matrix *inverseBind,
    const int *parents,
    const Vector3 *restT, const Quaternion *restR, const Vector3 *restS,
    int boneCount, const char *const *boneNames);

typedef struct AnimVec3Key { float t; Vector3 v; } AnimVec3Key;
typedef struct AnimQuatKey { float t; Quaternion q; } AnimQuatKey;
typedef struct AnimTrackData {
    const AnimVec3Key *pos;   int posCount;
    const AnimQuatKey *rot;   int rotCount;
    const AnimVec3Key *scale; int scaleCount;
} AnimTrackData;

/* Add a linear-interpolated animation clip to a skinned model.
   `tracks` is indexed by bone (skin joint) order. Returns clip index >= 0
   or -1. The clip is copied; caller data may be freed afterwards. */
int  vgraphics_AddSkinnedAnimation(SkinnedModel model, const char *name,
                                   float duration,
                                   const AnimTrackData *tracks, int trackCount);
const char *GetSkinnedModelAnimationName(SkinnedModel model, int animIndex);

void         UnloadSkinnedModel(SkinnedModel model);
void         DrawSkinnedModel(SkinnedModel model,
                              Animation anim, float time,
                              Vector3 position, Vector3 rotation, Vector3 scale,
                              Color tint);

/* Crossfade two clips of the same skinned model. `blend` 0 = from, 1 = to. */
void         DrawSkinnedModelBlend(SkinnedModel model,
                                   Animation from, float fromTime,
                                   Animation to, float toTime, float blend,
                                   Vector3 position, Vector3 rotation,
                                   Vector3 scale, Color tint);

void SetSkinnedModelMaterial(SkinnedModel model, int idx, Material mat);
int  GetSkinnedModelAnimationCount(SkinnedModel model);
int  GetSkinnedModelBoneCount(SkinnedModel model);
const char *GetSkinnedModelBoneName(SkinnedModel model, int boneIndex);
bool GetSkinnedModelBounds(SkinnedModel model, Vector3 *outMin, Vector3 *outMax);
float GetSkinnedModelAnimationDuration(SkinnedModel model, int animIndex);
/* Root-bone translation delta between two clip times (local space). */
bool GetAnimationRootMotion(Animation animation, float fromTime, float toTime,
                            Vector3 *outDelta);

typedef struct BonePose {
    Vector3    position;
    Quaternion rotation;
    Vector3    scale;
} BonePose;

bool GetSkinnedModelRestPose(SkinnedModel model, BonePose *out, int maxBones);
bool GetAnimationPose(Animation animation, float time,
                      BonePose *out, int maxBones);
void DrawSkinnedModelPose(SkinnedModel model, const BonePose *pose, int boneCount,
                          Vector3 position, Vector3 rotation, Vector3 scale,
                          Color tint);

/* Two-bone IK on a pose: upper -> mid -> end chain. target/pole are in model
   space (same space as the pose). Returns false for invalid indices. */
bool ApplyTwoBoneIKToPose(SkinnedModel model, BonePose *pose, int boneCount,
                          int upperBone, int midBone, int endBone,
                          Vector3 target, Vector3 pole);
/* Returns Animation handle for DrawSkinnedModel; id==0 on failure. */
Animation GetSkinnedModelAnimation(SkinnedModel model, int animIndex);

void vgraphics_UploadBoneMatrices(const Matrix *matrices, int count);

/* ============================================================ */
/*  Shader stubs (Phase 6+ не требует)                           */
/* ============================================================ */
/* Not implemented yet: returns {0}. Kept in the public header for ABI
   stability; use the built-in pipelines or vgraphics_RegisterMesh instead. */
Shader LoadShader(const char *vsPath, const char *fsPath);
void   UnloadShader(Shader shader);

Mesh  vgraphics_RegisterMesh(const Vertex3D *verts, int vertexCount,
                              const uint32_t *indices, int indexCount);

Model vgraphics_CreateModelFromMeshes(const Mesh *meshes,
                                       const int *meshMaterials, int meshCount,
                                       const Material *materials,
                                       int materialCount);

/* Same as CreateModelFromMeshes, plus optional per-mesh world matrices
   from the glTF node hierarchy (vkguide-style scene graph). */
Model vgraphics_CreateModelFromMeshesEx(const Mesh *meshes,
                                        const int *meshMaterials,
                                        const Matrix *transforms,
                                        int meshCount,
                                        const Material *materials,
                                        int materialCount);

/* Attach an already-uploaded texture to a Model so that a single
   UnloadModel() releases it together with the meshes and materials.
   Used by the glTF loaders for textures referenced by several materials.
   Returns false when the model id is invalid or allocation failed. */
bool vgraphics_TrackModelTexture(Model model, Texture2D texture);

#ifdef __cplusplus
}
#endif
#endif /* VLCRLIB_VGRAPHICS_H */
