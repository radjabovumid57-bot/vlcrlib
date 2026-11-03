/*
 * vgraphics_cpu.c — GPU-free graphics backend.
 *
 * This translation unit provides the small subset of the public vgraphics API
 * that the CPU renderer and the glTF loader need. It owns CPU-side texture,
 * material, mesh and model tables; no Vulkan headers or symbols are involved.
 */
#include "vgraphics.h"
#include "vcpu.h"
#include "vplatform.h"
#include "vgltf.h"
#include "vibl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CPU_MAX_TEXTURES 256
#define CPU_MAX_MESHES   512
#define CPU_MAX_MODELS   64
#define CPU_MAX_MATERIALS 128
#define CPU_MAX_MESHES_PER_MODEL 64

typedef struct CpuTextureEntry {
    uint32_t *pixels;       /* 0xAARRGGBB */
    int       width, height;
    unsigned  generation;
    int       used;
} CpuTextureEntry;

typedef struct CpuMeshEntry {
    Vertex3D *vertices;
    uint32_t *indices;
    int       vertexCount, indexCount;
    unsigned  generation;
    int       used;
} CpuMeshEntry;

typedef struct CpuMaterialEntry {
    Material material;
    unsigned generation;
    int      used;
} CpuMaterialEntry;

typedef struct CpuModelEntry {
    Mesh     meshes[CPU_MAX_MESHES_PER_MODEL];
    int      meshMaterials[CPU_MAX_MESHES_PER_MODEL];
    Matrix   transforms[CPU_MAX_MESHES_PER_MODEL];
    Material materials[CPU_MAX_MESHES_PER_MODEL];
    int      meshCount, materialCount;
    Texture2D *ownedTextures;
    int       ownedTextureCount, ownedTextureCapacity;
    int       used;
} CpuModelEntry;

static CpuTextureEntry  g_textures[CPU_MAX_TEXTURES];
static CpuMeshEntry     g_meshes[CPU_MAX_MESHES];
static CpuMaterialEntry g_materials[CPU_MAX_MATERIALS];
static CpuModelEntry    g_models[CPU_MAX_MODELS];
static unsigned         g_textureGenerations[CPU_MAX_TEXTURES];
static unsigned         g_meshGenerations[CPU_MAX_MESHES];
static unsigned         g_materialGenerations[CPU_MAX_MATERIALS];
static int              g_cpuWidth = 800, g_cpuHeight = 600;

static unsigned nextGeneration(unsigned *table, int index) {
    if (!table[index]) table[index] = 1;
    return table[index];
}

static void invalidateGeneration(unsigned *table, int index) {
    unsigned next = nextGeneration(table, index) + 1;
    table[index] = next ? next : 1;
}

static void packPixels(uint32_t *dst, const unsigned char *src, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        dst[i] = ((uint32_t)src[i * 4 + 3] << 24) |
                 ((uint32_t)src[i * 4 + 0] << 16) |
                 ((uint32_t)src[i * 4 + 1] << 8) |
                 (uint32_t)src[i * 4 + 2];
    }
}

static int allocateTexture(void) {
    for (int i = 1; i < CPU_MAX_TEXTURES; ++i)
        if (!g_textures[i].used) return i;
    return 0;
}

static int allocateMesh(void) {
    for (int i = 1; i < CPU_MAX_MESHES; ++i)
        if (!g_meshes[i].used) return i;
    return 0;
}

static int allocateMaterial(void) {
    for (int i = 1; i < CPU_MAX_MATERIALS; ++i)
        if (!g_materials[i].used) return i;
    return 0;
}

static int allocateModel(void) {
    for (int i = 1; i < CPU_MAX_MODELS; ++i)
        if (!g_models[i].used) return i;
    return 0;
}

static bool textureValid(Texture2D texture) {
    return texture.id > 0 && texture.id < CPU_MAX_TEXTURES &&
           texture.generation != 0 &&
           g_textures[texture.id].used &&
           g_textures[texture.id].generation == texture.generation;
}

static bool meshValid(Mesh mesh) {
    return mesh.id > 0 && mesh.id < CPU_MAX_MESHES &&
           mesh.generation != 0 &&
           g_meshes[mesh.id].used &&
           g_meshes[mesh.id].generation == mesh.generation;
}

static bool materialValid(Material material) {
    return material.id > 0 && material.id < CPU_MAX_MATERIALS &&
           material.generation != 0 &&
           g_materials[material.id].used &&
           g_materials[material.id].generation == material.generation;
}

/* ------------------------------------------------------------------ */
/*  Textures                                                           */
/* ------------------------------------------------------------------ */

Texture2D LoadTextureFromImageEx(Image image, const TextureOptions *options) {
    (void)options;
    Texture2D texture = {0};
    if (!image.data || image.width <= 0 || image.height <= 0) return texture;
    int index = allocateTexture();
    if (!index) return texture;
    size_t count = (size_t)image.width * (size_t)image.height;
    uint32_t *pixels = (uint32_t *)malloc(count * sizeof(uint32_t));
    if (!pixels) return texture;
    packPixels(pixels, (const unsigned char *)image.data, count);
    CpuTextureEntry *entry = &g_textures[index];
    entry->pixels = pixels;
    entry->width = image.width;
    entry->height = image.height;
    entry->generation = nextGeneration(g_textureGenerations, index);
    entry->used = 1;
    texture.id = (unsigned int)index;
    texture.width = image.width;
    texture.height = image.height;
    texture.mipmaps = 1;
    texture.format = 0;
    texture.generation = entry->generation;
    return texture;
}

Texture2D LoadTextureFromImage(Image image) {
    return LoadTextureFromImageEx(image, NULL);
}

Texture2D LoadTextureFromImageSRGB(Image image) {
    return LoadTextureFromImageEx(image, NULL);
}

Texture2D LoadTextureKTX2(const char *path) {
    (void)path;
    Texture2D texture = {0};
    return texture;
}

void UnloadTexture(Texture2D texture) {
    if (!textureValid(texture)) return;
    free(g_textures[texture.id].pixels);
    memset(&g_textures[texture.id], 0, sizeof(g_textures[texture.id]));
    invalidateGeneration(g_textureGenerations, (int)texture.id);
}

void UpdateTexture(Texture2D texture, const void *pixels) {
    if (!textureValid(texture) || !pixels) return;
    CpuTextureEntry *entry = &g_textures[texture.id];
    packPixels(entry->pixels, (const unsigned char *)pixels,
               (size_t)entry->width * (size_t)entry->height);
}

bool vgraphics_GetCpuTexturePixels(Texture2D texture, const uint32_t **pixels,
                                   int *width, int *height) {
    if (!textureValid(texture)) return false;
    CpuTextureEntry *entry = &g_textures[texture.id];
    if (pixels) *pixels = entry->pixels;
    if (width) *width = entry->width;
    if (height) *height = entry->height;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Materials                                                          */
/* ------------------------------------------------------------------ */

Material LoadMaterialDefault(void) {
    Material material = {0};
    int index = allocateMaterial();
    if (!index) return material;
    CpuMaterialEntry *entry = &g_materials[index];
    memset(entry, 0, sizeof(*entry));
    entry->used = 1;
    entry->generation = nextGeneration(g_materialGenerations, index);
    material.id = (unsigned int)index;
    material.generation = entry->generation;
    material.baseColor = (Color){255, 255, 255, 255};
    for (int i = 0; i < 4; ++i) material.baseColorFactor[i] = 1.0f;
    material.metallic = 0.0f;
    material.roughness = 1.0f;
    material.ior = 1.5f;
    material.normalScale = 1.0f;
    material.aoStrength = 1.0f;
    material.emissionColor = (Color){0, 0, 0, 255};
    material.emissionStrength = 0.0f;
    material.specularFactor = 1.0f;
    material.specularColor[0] = material.specularColor[1] =
        material.specularColor[2] = 1.0f;
    material.emissiveStrengthKhr = 1.0f;
    material.attenuationColor[0] = material.attenuationColor[1] =
        material.attenuationColor[2] = 1.0f;
    material.iridescenceIor = 1.3f;
    material.iridescenceThicknessMin = 100.0f;
    material.iridescenceThicknessMax = 400.0f;
    material.alphaCutoff = 0.5f;
    entry->material = material;
    return material;
}

void SetMaterialTexture(Material *material, int mapType, Texture2D texture) {
    if (!material || !materialValid(*material)) return;
    if (texture.id && !textureValid(texture)) texture = (Texture2D){0};
    switch (mapType) {
        case MATERIAL_MAP_ALBEDO:            material->albedoMap = texture; break;
        case MATERIAL_MAP_NORMAL:            material->normalMap = texture; break;
        case MATERIAL_MAP_METALLICROUGHNESS: material->metallicRoughnessMap = texture; break;
        case MATERIAL_MAP_AO:                material->aoMap = texture; break;
        case MATERIAL_MAP_EMISSION:          material->emissionMap = texture; break;
        case MATERIAL_MAP_SPECULAR:          material->specularMap = texture; break;
        case MATERIAL_MAP_CLEARCOAT:         material->clearcoatMap = texture; break;
        default: return;
    }
    g_materials[material->id].material = *material;
    vcore_InvalidateCpuModels();
}

void SetMaterialColor(Material *material, Color color) {
    if (!material) return;
    material->baseColor = color;
    material->baseColorFactor[0] = color.r / 255.0f;
    material->baseColorFactor[1] = color.g / 255.0f;
    material->baseColorFactor[2] = color.b / 255.0f;
    material->baseColorFactor[3] = color.a / 255.0f;
    if (materialValid(*material)) g_materials[material->id].material = *material;
    vcore_InvalidateCpuModels();
}

void SetMaterialBaseColorFactor(Material *material, float r, float g, float b,
                                float a) {
    if (!material) return;
    material->baseColorFactor[0] = r < 0.f ? 0.f : r;
    material->baseColorFactor[1] = g < 0.f ? 0.f : g;
    material->baseColorFactor[2] = b < 0.f ? 0.f : b;
    material->baseColorFactor[3] = a < 0.f ? 0.f : a;
    material->baseColor = (Color){
        (unsigned char)(material->baseColorFactor[0] * 255.f + 0.5f),
        (unsigned char)(material->baseColorFactor[1] * 255.f + 0.5f),
        (unsigned char)(material->baseColorFactor[2] * 255.f + 0.5f),
        (unsigned char)(material->baseColorFactor[3] * 255.f + 0.5f)
    };
    if (materialValid(*material)) g_materials[material->id].material = *material;
    vcore_InvalidateCpuModels();
}

void SetMaterialPBR(Material *material, float metallic, float roughness) {
    if (!material) return;
    material->metallic = metallic;
    material->roughness = roughness;
    if (materialValid(*material)) g_materials[material->id].material = *material;
    vcore_InvalidateCpuModels();
}

void SetMaterialEmission(Material *material, Color color, float strength) {
    if (!material) return;
    material->emissionColor = color;
    material->emissionStrength = strength < 0.0f ? 0.0f : strength;
    if (materialValid(*material)) g_materials[material->id].material = *material;
    vcore_InvalidateCpuModels();
}

void UnloadMaterial(Material material) {
    if (!materialValid(material)) return;
    memset(&g_materials[material.id], 0, sizeof(g_materials[material.id]));
    invalidateGeneration(g_materialGenerations, (int)material.id);
    vcore_InvalidateCpuModels();
}

/* ------------------------------------------------------------------ */
/*  Meshes                                                             */
/* ------------------------------------------------------------------ */

Mesh vgraphics_RegisterMesh(const Vertex3D *vertices, int vertexCount,
                            const uint32_t *indices, int indexCount) {
    Mesh mesh = {0};
    if (!vertices || vertexCount <= 0 || indexCount < 0) return mesh;
    int index = allocateMesh();
    if (!index) return mesh;
    CpuMeshEntry *entry = &g_meshes[index];
    memset(entry, 0, sizeof(*entry));
    entry->vertices = (Vertex3D *)malloc(sizeof(Vertex3D) * (size_t)vertexCount);
    entry->indices = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)(indexCount > 0 ? indexCount : 1));
    if (!entry->vertices || !entry->indices) {
        free(entry->vertices);
        free(entry->indices);
        return mesh;
    }
    memcpy(entry->vertices, vertices, sizeof(Vertex3D) * (size_t)vertexCount);
    if (indices && indexCount > 0)
        memcpy(entry->indices, indices, sizeof(uint32_t) * (size_t)indexCount);
    else {
        for (int i = 0; i < vertexCount; ++i) entry->indices[i] = (uint32_t)i;
        indexCount = vertexCount;
    }
    entry->vertexCount = vertexCount;
    entry->indexCount = indexCount;
    entry->generation = nextGeneration(g_meshGenerations, index);
    entry->used = 1;
    mesh.id = (unsigned int)index;
    mesh.vertexCount = vertexCount;
    mesh.indexCount = indexCount;
    mesh.generation = entry->generation;
    return mesh;
}

void UnloadMesh(Mesh mesh) {
    if (!meshValid(mesh)) return;
    free(g_meshes[mesh.id].vertices);
    free(g_meshes[mesh.id].indices);
    memset(&g_meshes[mesh.id], 0, sizeof(g_meshes[mesh.id]));
    invalidateGeneration(g_meshGenerations, (int)mesh.id);
}

Mesh GenMeshCube(float width, float height, float depth) {
    float hx = width * 0.5f, hy = height * 0.5f, hz = depth * 0.5f;
    Vertex3D v[24] = {
        {-hx,-hy, hz, 0,0,1, 0,1}, { hx,-hy, hz, 0,0,1, 1,1},
        { hx, hy, hz, 0,0,1, 1,0}, {-hx, hy, hz, 0,0,1, 0,0},
        { hx,-hy,-hz, 0,0,-1, 0,1}, {-hx,-hy,-hz, 0,0,-1, 1,1},
        {-hx, hy,-hz, 0,0,-1, 1,0}, { hx, hy,-hz, 0,0,-1, 0,0},
        {-hx,-hy,-hz, -1,0,0, 0,1}, {-hx,-hy, hz, -1,0,0, 1,1},
        {-hx, hy, hz, -1,0,0, 1,0}, {-hx, hy,-hz, -1,0,0, 0,0},
        { hx,-hy, hz, 1,0,0, 0,1}, { hx,-hy,-hz, 1,0,0, 1,1},
        { hx, hy,-hz, 1,0,0, 1,0}, { hx, hy, hz, 1,0,0, 0,0},
        {-hx, hy, hz, 0,1,0, 0,1}, { hx, hy, hz, 0,1,0, 1,1},
        { hx, hy,-hz, 0,1,0, 1,0}, {-hx, hy,-hz, 0,1,0, 0,0},
        {-hx,-hy,-hz, 0,-1,0, 0,1}, { hx,-hy,-hz, 0,-1,0, 1,1},
        { hx,-hy, hz, 0,-1,0, 1,0}, {-hx,-hy, hz, 0,-1,0, 0,0},
    };
    uint32_t idx[36] = {
        0,1,2,0,2,3, 4,5,6,4,6,7, 8,9,10,8,10,11,
        12,13,14,12,14,15, 16,17,18,16,18,19, 20,21,22,20,22,23
    };
    for (int i = 0; i < 24; ++i) {
        v[i].cr = v[i].cg = v[i].cb = v[i].ca = 1.0f;
    }
    return vgraphics_RegisterMesh(v, 24, idx, 36);
}

Mesh GenMeshSphere(float radius, int rings, int slices) {
    if (rings < 3) rings = 8;
    if (slices < 3) slices = 12;
    int vertexCount = (rings + 1) * (slices + 1);
    int indexCount = rings * slices * 6;
    Vertex3D *vertices = (Vertex3D *)calloc((size_t)vertexCount, sizeof(Vertex3D));
    uint32_t *indices = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)indexCount);
    if (!vertices || !indices) { free(vertices); free(indices); return (Mesh){0}; }
    int vi = 0, ii = 0;
    for (int i = 0; i <= rings; ++i) {
        float phi = (float)(i * VLCRLIB_PI / rings);
        for (int j = 0; j <= slices; ++j) {
            float theta = (float)(j * 2.0 * VLCRLIB_PI / slices);
            float x = sinf(phi) * cosf(theta);
            float y = cosf(phi);
            float z = sinf(phi) * sinf(theta);
            vertices[vi++] = (Vertex3D){ radius*x, radius*y, radius*z,
                                         x, y, z,
                                         (float)j / slices, (float)i / rings,
                                         0,0,0,1, 1,1,1,1 };
        }
    }
    for (int i = 0; i < rings; ++i)
        for (int j = 0; j < slices; ++j) {
            uint32_t a = (uint32_t)(i * (slices + 1) + j);
            uint32_t b = a + (uint32_t)(slices + 1);
            indices[ii++] = a; indices[ii++] = b; indices[ii++] = a + 1;
            indices[ii++] = a + 1; indices[ii++] = b; indices[ii++] = b + 1;
        }
    Mesh mesh = vgraphics_RegisterMesh(vertices, vertexCount, indices, indexCount);
    free(vertices);
    free(indices);
    return mesh;
}

/* ------------------------------------------------------------------ */
/*  Models                                                             */
/* ------------------------------------------------------------------ */

Model vgraphics_CreateModelFromMeshesEx(const Mesh *meshes,
                                        const int *meshMaterials,
                                        const Matrix *transforms,
                                        int meshCount,
                                        const Material *materials,
                                        int materialCount) {
    Model model = {0};
    if (!meshes || meshCount <= 0 || meshCount > CPU_MAX_MESHES_PER_MODEL)
        return model;
    int index = allocateModel();
    if (!index) return model;
    CpuModelEntry *entry = &g_models[index];
    memset(entry, 0, sizeof(*entry));
    entry->used = 1;
    entry->meshCount = meshCount;
    entry->materialCount = materialCount > 0 ? materialCount : 1;
    if (entry->materialCount > CPU_MAX_MESHES_PER_MODEL)
        entry->materialCount = CPU_MAX_MESHES_PER_MODEL;
    for (int i = 0; i < meshCount; ++i) {
        entry->meshes[i] = meshes[i];
        int mi = meshMaterials ? meshMaterials[i] : 0;
        entry->meshMaterials[i] = (mi >= 0 && mi < entry->materialCount) ? mi : 0;
        entry->transforms[i] = transforms ? transforms[i] : MatrixIdentity();
    }
    if (materialCount > 0 && materials) {
        for (int i = 0; i < entry->materialCount; ++i)
            entry->materials[i] = materials[i];
    } else {
        entry->materials[0] = LoadMaterialDefault();
    }
    model.id = (unsigned int)index;
    model.meshCount = entry->meshCount;
    model.materialCount = entry->materialCount;
    return model;
}

Model vgraphics_CreateModelFromMeshes(const Mesh *meshes,
                                       const int *meshMaterials, int meshCount,
                                       const Material *materials,
                                       int materialCount) {
    return vgraphics_CreateModelFromMeshesEx(meshes, meshMaterials, NULL,
                                             meshCount, materials, materialCount);
}

Model LoadModelFromMesh(Mesh mesh) {
    Model model = {0};
    if (!meshValid(mesh)) return model;
    Material material = LoadMaterialDefault();
    int slot = 0;
    return vgraphics_CreateModelFromMeshesEx(&mesh, &slot, NULL, 1,
                                             &material, 1);
}

bool vgraphics_TrackModelTexture(Model model, Texture2D texture) {
    if (model.id == 0 || model.id >= CPU_MAX_MODELS) return false;
    CpuModelEntry *entry = &g_models[model.id];
    if (!entry->used) return false;
    if (entry->ownedTextureCount == entry->ownedTextureCapacity) {
        int capacity = entry->ownedTextureCapacity ? entry->ownedTextureCapacity * 2 : 8;
        Texture2D *grown = (Texture2D *)realloc(
            entry->ownedTextures, sizeof(Texture2D) * (size_t)capacity);
        if (!grown) return false;
        entry->ownedTextures = grown;
        entry->ownedTextureCapacity = capacity;
    }
    entry->ownedTextures[entry->ownedTextureCount++] = texture;
    return true;
}

void UnloadModel(Model model) {
    if (model.id == 0 || model.id >= CPU_MAX_MODELS) return;
    CpuModelEntry *entry = &g_models[model.id];
    if (!entry->used) return;
    for (int i = 0; i < entry->materialCount; ++i)
        UnloadMaterial(entry->materials[i]);
    for (int i = 0; i < entry->meshCount; ++i)
        UnloadMesh(entry->meshes[i]);
    for (int i = 0; i < entry->ownedTextureCount; ++i)
        UnloadTexture(entry->ownedTextures[i]);
    free(entry->ownedTextures);
    memset(entry, 0, sizeof(*entry));
    vcore_InvalidateCpuModel(model.id);
}

static bool extensionIs(const char *path, const char *wanted) {
    const char *extension = path ? strrchr(path, '.') : NULL;
    if (!extension) return false;
    while (*extension && *wanted) {
        char a = *extension, b = *wanted;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
        ++extension; ++wanted;
    }
    return *extension == '\0' && *wanted == '\0';
}

Model LoadModel(const char *path) {
    if (!path || !path[0]) return (Model){0};
    if (extensionIs(path, ".glb") || extensionIs(path, ".gltf"))
        return LoadModelGLTFTiny(path);
    return (Model){0};
}

/* ------------------------------------------------------------------ */
/*  CPU model bridge                                                   */
/* ------------------------------------------------------------------ */

static float clamp01(float value) {
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

static int storeTexture(const Texture2D *texture, VCpuTexture *owned,
                        uint64_t *keys, int maxTextures, int *count) {
    if (!texture || !texture->id || !count || *count >= maxTextures) return -1;
    uint64_t key = ((uint64_t)texture->id << 32) | texture->generation;
    for (int i = 0; i < *count; ++i)
        if (keys[i] == key) return i;
    const uint32_t *pixels = NULL;
    int width = 0, height = 0;
    if (!vgraphics_GetCpuTexturePixels(*texture, &pixels, &width, &height) ||
        !pixels || width <= 0 || height <= 0)
        return -1;
    uint32_t *copy = (uint32_t *)malloc((size_t)width * (size_t)height *
                                        sizeof(uint32_t));
    if (!copy) return -1;
    memcpy(copy, pixels, (size_t)width * (size_t)height * sizeof(uint32_t));
    owned[*count].width = width;
    owned[*count].height = height;
    owned[*count].pixels = copy;
    keys[*count] = key;
    return (*count)++;
}

bool vgraphics_BuildCpuModel(Model model, VCpuModel *out) {
    if (out) memset(out, 0, sizeof(*out));
    if (!out || model.id == 0 || model.id >= CPU_MAX_MODELS) return false;
    CpuModelEntry *entry = &g_models[model.id];
    if (!entry->used || entry->meshCount <= 0) return false;

    int totalVertices = 0, totalIndices = 0;
    for (int i = 0; i < entry->meshCount; ++i) {
        Mesh mesh = entry->meshes[i];
        if (!meshValid(mesh)) continue;
        totalVertices += g_meshes[mesh.id].vertexCount;
        totalIndices += g_meshes[mesh.id].indexCount;
    }
    if (totalVertices <= 0 || totalIndices <= 0) return false;

    VCpuVertex *vertices = (VCpuVertex *)malloc(sizeof(VCpuVertex) * (size_t)totalVertices);
    uint32_t *indices = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)totalIndices);
    VCpuModelPart *parts = (VCpuModelPart *)calloc((size_t)entry->meshCount,
                                                   sizeof(VCpuModelPart));
    int maxTextures = entry->meshCount * 2;
    if (maxTextures < 2) maxTextures = 2;
    VCpuTexture *owned = (VCpuTexture *)calloc((size_t)maxTextures, sizeof(VCpuTexture));
    uint64_t *keys = (uint64_t *)calloc((size_t)maxTextures, sizeof(uint64_t));
    if (!vertices || !indices || !parts || !owned || !keys) {
        free(vertices); free(indices); free(parts); free(owned); free(keys);
        return false;
    }

    int vo = 0, io = 0, partCount = 0, ownedCount = 0;
    for (int i = 0; i < entry->meshCount; ++i) {
        Mesh mesh = entry->meshes[i];
        if (!meshValid(mesh)) continue;
        CpuMeshEntry *source = &g_meshes[mesh.id];
        Matrix node = entry->transforms[i];
        Matrix normalMat = MatrixNormalFromModel(node);

        VCpuModelPart part;
        memset(&part, 0, sizeof(part));
        part.firstIndex = io;
        part.indexCount = source->indexCount;
        part.baseColor = (Color){255, 255, 255, 255};
        for (int c = 0; c < 4; ++c) part.baseColorFactor[c] = 1.0f;
        part.roughness = 0.5f;
        part.alphaCutoff = 0.5f;
        part.doubleSided = 1;

        int materialIndex = entry->meshMaterials[i];
        const Material *material = (materialIndex >= 0 &&
                                    materialIndex < entry->materialCount)
                                 ? &entry->materials[materialIndex] : NULL;
        if (material) {
            part.baseColor = material->baseColor;
            float sum = 0.0f;
            for (int c = 0; c < 4; ++c) {
                part.baseColorFactor[c] = material->baseColorFactor[c];
                sum += material->baseColorFactor[c] < 0.0f ? 0.0f
                                                           : material->baseColorFactor[c];
            }
            if (sum <= 1e-6f) {
                part.baseColorFactor[0] = material->baseColor.r / 255.0f;
                part.baseColorFactor[1] = material->baseColor.g / 255.0f;
                part.baseColorFactor[2] = material->baseColor.b / 255.0f;
                part.baseColorFactor[3] = material->baseColor.a / 255.0f;
            }
            part.metallic = material->metallic;
            part.roughness = material->roughness;
            part.emissive[0] = material->emissionColor.r / 255.0f *
                               material->emissionStrength;
            part.emissive[1] = material->emissionColor.g / 255.0f *
                               material->emissionStrength;
            part.emissive[2] = material->emissionColor.b / 255.0f *
                               material->emissionStrength;
            part.emissiveStrength = 1.0f;
            part.alphaMode = material->alphaMode;
            part.alphaCutoff = material->alphaCutoff > 0.0f
                             ? material->alphaCutoff : 0.5f;
            part.doubleSided = material->doubleSided;
            part.unlit = material->unlit;

            int albedo = storeTexture(&material->albedoMap, owned, keys,
                                      maxTextures, &ownedCount);
            if (albedo >= 0) part.texture = &owned[albedo];
            int emissive = storeTexture(&material->emissionMap, owned, keys,
                                        maxTextures, &ownedCount);
            if (emissive >= 0) part.emissiveTexture = &owned[emissive];
        }

        int baseVertex = vo;
        for (int v = 0; v < source->vertexCount; ++v) {
            const Vertex3D *src = &source->vertices[v];
            vertices[vo].position.x = node.m0 * src->x + node.m4 * src->y +
                                      node.m8 * src->z + node.m12;
            vertices[vo].position.y = node.m1 * src->x + node.m5 * src->y +
                                      node.m9 * src->z + node.m13;
            vertices[vo].position.z = node.m2 * src->x + node.m6 * src->y +
                                      node.m10 * src->z + node.m14;
            vertices[vo].uv = (Vector2){ src->u, src->v };
            vertices[vo].normal.x = normalMat.m0 * src->nx +
                                    normalMat.m4 * src->ny +
                                    normalMat.m8 * src->nz;
            vertices[vo].normal.y = normalMat.m1 * src->nx +
                                    normalMat.m5 * src->ny +
                                    normalMat.m9 * src->nz;
            vertices[vo].normal.z = normalMat.m2 * src->nx +
                                    normalMat.m6 * src->ny +
                                    normalMat.m10 * src->nz;
            float cr = src->cr, cg = src->cg, cb = src->cb, ca = src->ca;
            if (cr <= 0.0f && cg <= 0.0f && cb <= 0.0f && ca <= 0.0f)
                cr = cg = cb = ca = 1.0f;
            vertices[vo].color = (Color){
                (unsigned char)(clamp01(cr) * 255.0f + 0.5f),
                (unsigned char)(clamp01(cg) * 255.0f + 0.5f),
                (unsigned char)(clamp01(cb) * 255.0f + 0.5f),
                (unsigned char)(clamp01(ca) * 255.0f + 0.5f)
            };
            vo++;
        }
        for (int k = 0; k < source->indexCount; ++k)
            indices[io++] = source->indices[k] + (uint32_t)baseVertex;
        parts[partCount++] = part;
    }

    VCpuModel created = VCpuModelCreate(vertices, vo, indices, io,
                                        (Color){255, 255, 255, 255}, NULL);
    if (!created.vertices) {
        free(vertices); free(indices);
        for (int i = 0; i < ownedCount; ++i) free(owned[i].pixels);
        free(parts); free(owned); free(keys);
        return false;
    }
    free(created.parts);
    created.parts = parts;
    created.partCount = partCount;
    created.ownedTextures = owned;
    created.ownedTextureCount = ownedCount;
    created.texture = NULL;
    created.baseColor = (Color){255, 255, 255, 255};
    free(vertices);
    free(indices);
    free(keys);
    *out = created;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Drawing / frame                                                    */
/* ------------------------------------------------------------------ */

void vgraphics_DrawVertexBuffer(const Vertex2D *vertices, int count,
                                Texture2D texture) {
    vcore_CpuDrawVertices(vertices, count, texture);
}

void DrawModel(Model model, Vector3 position, Vector3 rotation, Vector3 scale,
               Color tint) {
    vcore_CpuDrawModel(model, position, rotation, scale, tint);
}

void vgraphics_Set2DViewportSize(int width, int height) {
    if (width > 0) g_cpuWidth = width;
    if (height > 0) g_cpuHeight = height;
}

void vgraphics_GetRenderTargetSize(int *width, int *height) {
    if (width) *width = g_cpuWidth;
    if (height) *height = g_cpuHeight;
}

void vgraphics_Set3DMatrices(Matrix view, Matrix projection) {
    (void)view; (void)projection;
}

void vgraphics_SetCameraPos(Vector3 position) { (void)position; }
void vgraphics_DrawSkybox(void) {}
void vgraphics_SetSkybox(int enabled) { (void)enabled; }
int  vgraphics_GetSkybox(void) { return 0; }
void vgraphics_Resize(void) {}
void SetVulkanClearColor(float r, float g, float b, float a) {
    (void)r; (void)g; (void)b; (void)a;
}
void SetVulkanViewport(int x, int y, int w, int h) {
    (void)x; (void)y; (void)w; (void)h;
}
void SetVulkanViewMatrix(Matrix m) { (void)m; }
void SetVulkanProjectionMatrix(Matrix m) { (void)m; }
void BeginVulkanFrame(void) {}
void EndVulkanFrame(void) {}
void vgraphics_FlushTransparent(void) {}
VkInitResult InitGraphicsDevice(void) { return VKINIT_ERR_DEVICE; }
void CloseGraphicsDevice(void) {}

/* ------------------------------------------------------------------ */
/*  Lights / shadows / IBL                                             */
/* ------------------------------------------------------------------ */

void vgraphics_SetDirectionalLight(bool enabled, Vector3 direction, Color color,
                                   float intensity) {
    VCpuSetDirectionalLight(enabled, direction, color, intensity);
}
void vgraphics_SetAmbientLight(bool enabled, Color color, float intensity) {
    VCpuSetAmbientLight(enabled, color, intensity);
}
void SetGraphicsExposure(float exposure) { VCpuSetExposure(exposure); }
float GetGraphicsExposure(void) { return 1.0f; }

void vgraphics_SetShadows(bool enabled, int mapSize, float bias, float strength) {
    VCpuSetShadows(enabled, mapSize, bias, strength);
}
void vgraphics_SetShadowSoftness(float radius) {
    VCpuSetShadowSoftness(radius);
}
void vgraphics_ComputeDirectionalShadowMatrix(float radius, float nearZ,
                                              float farZ) {
    (void)radius; (void)nearZ; (void)farZ;
}
void vgraphics_ComputeDirectionalShadowCascades(void) {}
void vgraphics_ComputeSpotShadowMatrix(float nearZ, float farZ) {
    (void)nearZ; (void)farZ;
}
void vgraphics_UpdateShadowMatrix(Matrix lightViewProj) {
    (void)lightViewProj;
}
void vgraphics_SetSpotShadows(bool enabled, int mapSize, float bias,
                              float strength) {
    (void)enabled; (void)mapSize; (void)bias; (void)strength;
}
void vgraphics_SetPointShadows(bool enabled, int mapSize, float bias,
                               float strength) {
    (void)enabled; (void)mapSize; (void)bias; (void)strength;
}

void vgraphics_EnableStudioLighting(void) {
    vgraphics_SetDirectionalLight(true, (Vector3){-0.35f, -0.85f, -0.40f},
                                  (Color){255, 248, 240, 255}, 2.8f);
    vgraphics_SetAmbientLight(true, (Color){90, 100, 125, 255}, 0.45f);
    vgraphics_SetIBLIntensity(0.0f);
}

bool vgraphics_LoadIBL(const char *hdrPath) {
    VIBL_Bundle bundle = hdrPath && hdrPath[0]
        ? VIBL_BuildFromHDRFile(hdrPath, 64, 16)
        : VIBL_BuildProcedural(64, 16);
    if (!bundle.valid) return false;
    if (!VCpuSetIBLBundle(&bundle)) {
        VIBL_FreeBundle(&bundle);
        return false;
    }
    return true;
}

void vgraphics_UnloadIBL(void) { VCpuUnloadIBL(); }
bool vgraphics_IsIBLLoaded(void) { return VCpuIsIBLLoaded(); }
void vgraphics_SetIBLIntensity(float intensity) { VCpuSetIBLIntensity(intensity); }
bool vgraphics_LoadHDRCubemap(const char *path, int cubeSize) {
    (void)path; (void)cubeSize;
    return false;
}

void vgraphics_SetPointLight(bool enabled, Vector3 position, Color color,
                             float intensity, float range) {
    (void)enabled; (void)position; (void)color; (void)intensity; (void)range;
}
void vgraphics_SetSpotLight(bool enabled, Vector3 position, Vector3 direction,
                            Color color, float intensity, float range,
                            float innerCone, float outerCone) {
    (void)enabled; (void)position; (void)direction; (void)color;
    (void)intensity; (void)range; (void)innerCone; (void)outerCone;
}
void vgraphics_SetAreaLight(bool enabled, Vector3 position, Vector3 normal,
                            float width, float height, Color color,
                            float intensity) {
    (void)enabled; (void)position; (void)normal; (void)width; (void)height;
    (void)color; (void)intensity;
}
void vgraphics_SetFog(bool enabled, Color color, float start, float end,
                      float density) {
    (void)enabled; (void)color; (void)start; (void)end; (void)density;
}
void vgraphics_SetLightArray(const VLightData *lights, int count) {
    (void)lights; (void)count;
}
int vgraphics_AddLight(const VLightData *light) { (void)light; return -1; }
void vgraphics_ClearLights(void) {}
int vgraphics_GetLightCount(void) { return 0; }
void vgraphics_UploadBoneMatrices(const Matrix *matrices, int count) {
    (void)matrices; (void)count;
}

/* ------------------------------------------------------------------ */
/*  Stats / screenshots / skinning / optional loaders                  */
/* ------------------------------------------------------------------ */

void vgraphics_GetFrameStats(FrameStats *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->width = g_cpuWidth;
    out->height = g_cpuHeight;
}
void vgraphics_ResetFrameStats(void) {}
bool vgraphics_CaptureScreenshot(const char *path) { (void)path; return false; }
bool vgraphics_IsHardwareRayTracingAvailable(void) { return false; }

SkinnedModel LoadSkinnedModel(const char *path) {
    (void)path;
    SkinnedModel model = {0};
    return model;
}
SkinnedModel vgraphics_CreateSkinnedModel(
    const SkinnedVertex3D *vertices, int vertexCount,
    const uint32_t *indices, int indexCount, const Material *material,
    const Matrix *inverseBind, const int *parents,
    const Vector3 *restT, const Quaternion *restR, const Vector3 *restS,
    int boneCount, const char *const *boneNames) {
    (void)vertices; (void)vertexCount; (void)indices; (void)indexCount;
    (void)material; (void)inverseBind; (void)parents; (void)restT;
    (void)restR; (void)restS; (void)boneCount; (void)boneNames;
    SkinnedModel model = {0};
    return model;
}
int vgraphics_AddSkinnedAnimation(SkinnedModel model, const char *name,
                                  float duration, const AnimTrackData *tracks,
                                  int trackCount) {
    (void)model; (void)name; (void)duration; (void)tracks; (void)trackCount;
    return -1;
}
const char *GetSkinnedModelAnimationName(SkinnedModel model, int animIndex) {
    (void)model; (void)animIndex;
    return NULL;
}
void UnloadSkinnedModel(SkinnedModel model) { (void)model; }
void DrawSkinnedModel(SkinnedModel model, Animation anim, float time,
                      Vector3 position, Vector3 rotation, Vector3 scale,
                      Color tint) {
    (void)model; (void)anim; (void)time; (void)position; (void)rotation;
    (void)scale; (void)tint;
}
void DrawSkinnedModelBlend(SkinnedModel model, Animation from, float fromTime,
                           Animation to, float toTime, float blend,
                           Vector3 position, Vector3 rotation, Vector3 scale,
                           Color tint) {
    (void)model; (void)from; (void)fromTime; (void)to; (void)toTime;
    (void)blend; (void)position; (void)rotation; (void)scale; (void)tint;
}
