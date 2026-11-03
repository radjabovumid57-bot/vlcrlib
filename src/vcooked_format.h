#pragma once
#include <stdint.h>

#pragma pack(push, 1)

/* Version 2. Version 1 files (plain header + vertices + indices) are still
   accepted by LoadCookedModel. */
typedef struct CookedHeader {
    char     magic[8];
    uint32_t version;
    uint32_t vertexCount;
    uint32_t indexCount;
    uint32_t indexType;
    float    bounds[6];
    uint32_t materialCount;
    uint32_t textureCount;
    uint32_t stringBytes;
    uint32_t meshCount;      /* v3; 0 in v2 files means one mesh */
} CookedHeader;

typedef struct CookVertex {
    float p[3];
    float n[3];
    float uv[2];
    float t[4];
    float c[4];
} CookVertex;

enum {
    COOK_TEXTURE_ALBEDO = 0,
    COOK_TEXTURE_NORMAL = 1,
    COOK_TEXTURE_METALLIC_ROUGHNESS = 2,
    COOK_TEXTURE_AO = 3,
    COOK_TEXTURE_EMISSIVE = 4,
    COOK_TEXTURE_SLOT_COUNT = 5
};

enum {
    COOK_TEX_FORMAT_BC7 = 0,
    COOK_TEX_FORMAT_BC5 = 1
};

typedef struct CookTextureRef {
    uint32_t semantic;
    uint32_t format;
    uint32_t width;
    uint32_t height;
    uint32_t pathOffset;   /* byte offset into the string blob */
    uint32_t pathLength;
} CookTextureRef;

typedef struct CookMesh {
    uint32_t vertexOffset;
    uint32_t vertexCount;
    uint32_t indexOffset;
    uint32_t indexCount;
    int32_t  materialIndex;
    uint32_t pad;
    float    boundsMin[3];
    float    boundsMax[3];
} CookMesh;

typedef struct CookMaterial {
    float    baseColorFactor[4];
    float    metallic;
    float    roughness;
    float    aoStrength;
    float    emissive[3];
    float    alphaCutoff;
    uint32_t flags;        /* bit0 doubleSided, bit1 unlit */
    uint32_t mapIndex[COOK_TEXTURE_SLOT_COUNT]; /* 0xFFFFFFFF = none */
    char     name[64];
} CookMaterial;

#pragma pack(pop)
