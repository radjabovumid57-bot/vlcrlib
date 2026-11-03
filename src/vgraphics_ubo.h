/**
 * vgraphics_ubo.h — single source of truth for GPU uniform layouts (Sprint D)
 *
 * MUST match shaders/pbr.vert, pbr.frag, skin.vert (std140-compatible packing:
 * float[4]/vec4, float[16]=mat4 column-major).
 * Rebuild SPIR-V after any field change.
 */
#ifndef VLCRLIB_VGRAPHICS_UBO_H
#define VLCRLIB_VGRAPHICS_UBO_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Descriptor set 0 binding map (PBR / Skin) ---------- */
enum {
    VLCR_BIND_UBO_PBR          = 0,
    VLCR_BIND_ALBEDO           = 1,
    VLCR_BIND_NORMAL           = 2,
    VLCR_BIND_MR               = 3,
    VLCR_BIND_AO               = 4,
    VLCR_BIND_EMISSION         = 5,
    VLCR_BIND_SHADOW_DIR       = 6,
    VLCR_BIND_IBL_IRRADIANCE   = 7,
    VLCR_BIND_IBL_PREFILTERED  = 8,
    VLCR_BIND_IBL_BRDF_LUT     = 9,
    VLCR_BIND_SHADOW_SPOT      = 10,
    VLCR_BIND_SHADOW_POINT     = 11,
    VLCR_BIND_BONES            = 12, /* skin only (UBO); PBR binds white pad */
    VLCR_BIND_SPECULAR         = 13, /* KHR_materials_specular */
    VLCR_BIND_CLEARCOAT        = 14, /* KHR_materials_clearcoat */
    VLCR_BIND_SSAO             = 15, /* half-res SSAO texture */
    VLCR_BIND_COUNT            = 16
};

/* khrIriDisp.w flag bits (material) */
enum {
    VLCR_MATFLAG_UNLIT         = 1,
    VLCR_MATFLAG_ALPHAMASK     = 2,
    VLCR_MATFLAG_ALPHABLEND    = 4,
    VLCR_MATFLAG_DOUBLESIDED   = 8,
    VLCR_MATFLAG_DISPERSION    = 16,
    VLCR_MATFLAG_SPECULAR_MAP  = 32,
    VLCR_MATFLAG_CLEARCOAT_MAP = 64
};

/* ---------- Frame phases (CPU tracking) ---------- */
typedef enum VlcrFramePhase {
    VLCR_PHASE_IDLE = 0,
    VLCR_PHASE_ACQUIRE,
    VLCR_PHASE_SHADOW,
    VLCR_PHASE_SCENE,       /* opaque + transparent draws */
    VLCR_PHASE_PRESENT
} VlcrFramePhase;

/* ---------- UBO3D (simple lit mesh) ---------- */
typedef struct UBO3D {
    float  mvp[16];
    float  model[16];
    float  normalMat[16];
    float  lightDir[4];
    float  lightColor[4];
    float  baseColor[4];
    float  _pad[4];
} UBO3D;

/* ---------- UBO_PBR (matches pbr.frag uniform block) ---------- */
typedef struct UBO_PBR {
    float  mvp[16];
    float  model[16];
    float  normalMat[16];
    float  cameraPos[4];
    float  lightDir[4];
    float  lightColor[4];
    float  baseColor[4];
    float  metallicRoughnessAO[4];
    float  emissive[4];
    float  textureFlags[4];
    float  textureFlags2[4];   /* x=emMap y=debugView z=exposure w=shadowSoftRadius */
    float  materialParams[4];  /* x=ior y=normalScale z=iblIntensity w=alphaCutoff */
    float  directionalLight[4];
    float  directionalColor[4];
    float  pointLight[4];
    float  pointColor[4];
    float  spotLight[4];
    float  spotDirection[4];
    float  spotColor[4];
    float  spotParams[4];      /* x=cosInner y=cosOuter z=ssaoIntensity w=ssaoRadius */
    float  ambientLight[4];
    float  areaLightPos[4];
    float  areaLightColor[4];
    float  areaLightSize[4];   /* xy=size zw=bloomThreshold/Intensity */
    float  areaLightNormal[4];
    float  lightViewProj[16];
    float  shadowParams[4];    /* x=on y=bias z=strength w=mapSize */
    float  spotLightViewProj[16];
    float  shadowParamsSpot[4];
    float  shadowParamsPoint[4];
    float  specularExt[4];     /* rgb=specularColor a=specularFactor */
    float  khrClearcoat[4];
    float  khrSheenColor[4];
    float  khrVolume[4];
    float  khrAnisoIri[4];
    float  khrIriDisp[4];      /* w = VLCR_MATFLAG_* bits */
    float  fogParams[4];       /* x=enabled, y=start, z=end, w=density */
    float  fogColor[4];
    float  lightMeta[4];       /* x = explicit light-array count (SSBO binding 16) */
    float  cascadeVP[4][16];   /* directional CSM, concatenated matrices */
    float  cascadeSplits[4];   /* camera distance of each cascade end */
    float  cascadeAtlas[4];    /* xy = atlas scale, z = cascade count */
} UBO_PBR;

/* Expected size with float packing (no padding holes between float arrays).
 * If sizeof diverges on a platform, fix alignment — do not pad blindly. */
enum { VLCR_UBO_PBR_EXPECTED_MIN = 700 }; /* soft check; exact logged at init */

#ifdef __cplusplus
}
#endif

#endif /* VLCRLIB_VGRAPHICS_UBO_H */
