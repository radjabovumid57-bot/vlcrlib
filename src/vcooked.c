/*
 * vcooked.c — cooked mesh reader.
 *
 * v1: header + vertices + indices.
 * v2: one mesh + material block + texture refs.
 * v3: multiple meshes/materials sharing one vertex/index/string pool.
 *
 * Texture paths are relative to the .vmesh directory and normally point at
 * KTX2 BC7/BC5 files written by `vlcrc --ktx`.
 */
#include "vloaders.h"
#include "vtextures.h"
#include "vcooked_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *readBlob(FILE *f, uint32_t bytes) {
    if (bytes == 0) return NULL;
    char *p = (char *)malloc(bytes + 1);
    if (!p) return NULL;
    if (fread(p, 1, bytes, f) != bytes) { free(p); return NULL; }
    p[bytes] = 0;
    return p;
}

static void resolvePath(char *out, size_t cap, const char *meshPath, const char *rel) {
    const char *slash = strrchr(meshPath, '/');
    const char *bslash = strrchr(meshPath, '\\');
    if (bslash > slash) slash = bslash;
    if (!slash) { snprintf(out, cap, "%s", rel); return; }
    size_t n = (size_t)(slash - meshPath) + 1;
    if (n >= cap) n = cap - 1;
    memcpy(out, meshPath, n);
    out[n] = 0;
    snprintf(out + n, cap - n, "%s", rel);
}

static Material materialFromCook(const CookMaterial *cm,
                                 const CookTextureRef *refs, uint32_t refCount,
                                 const char *strings, uint32_t stringBytes,
                                 const char *meshPath) {
    Material mat = LoadMaterialDefault();
    memcpy(mat.baseColorFactor, cm->baseColorFactor, sizeof(mat.baseColorFactor));
    mat.baseColor = (Color){
        (unsigned char)(cm->baseColorFactor[0] * 255.0f),
        (unsigned char)(cm->baseColorFactor[1] * 255.0f),
        (unsigned char)(cm->baseColorFactor[2] * 255.0f),
        (unsigned char)(cm->baseColorFactor[3] * 255.0f) };
    mat.metallic = cm->metallic;
    mat.roughness = cm->roughness;
    mat.aoStrength = cm->aoStrength;
    mat.emissionColor = (Color){
        (unsigned char)(cm->emissive[0] * 255.0f),
        (unsigned char)(cm->emissive[1] * 255.0f),
        (unsigned char)(cm->emissive[2] * 255.0f), 255 };
    mat.emissionStrength = 1.0f;
    mat.doubleSided = (cm->flags & 1u) ? 1 : 0;
    mat.unlit = (cm->flags & 2u) ? 1 : 0;

    for (int slot = 0; slot < COOK_TEXTURE_SLOT_COUNT; ++slot) {
        uint32_t ti = cm->mapIndex[slot];
        if (ti >= refCount || !refs || !strings) continue;
        const CookTextureRef *tr = &refs[ti];
        if (tr->pathLength == 0 ||
            (uint64_t)tr->pathOffset + tr->pathLength > stringBytes)
            continue;
        char rel[512];
        size_t len = tr->pathLength < sizeof(rel) - 1 ? tr->pathLength : sizeof(rel) - 1;
        memcpy(rel, strings + tr->pathOffset, len);
        rel[len] = 0;
        char full[1024];
        resolvePath(full, sizeof(full), meshPath, rel);
        Texture2D tex = LoadTextureKTX2(full);
        if (!tex.id) tex = LoadTexture(full);
        if (!tex.id) continue;
        switch (slot) {
            case COOK_TEXTURE_ALBEDO:
                SetMaterialTexture(&mat, MATERIAL_MAP_ALBEDO, tex); break;
            case COOK_TEXTURE_NORMAL:
                SetMaterialTexture(&mat, MATERIAL_MAP_NORMAL, tex); break;
            case COOK_TEXTURE_METALLIC_ROUGHNESS:
                SetMaterialTexture(&mat, MATERIAL_MAP_METALLICROUGHNESS, tex); break;
            case COOK_TEXTURE_AO:
                SetMaterialTexture(&mat, MATERIAL_MAP_AO, tex); break;
            case COOK_TEXTURE_EMISSIVE:
                SetMaterialTexture(&mat, MATERIAL_MAP_EMISSION, tex); break;
            default: break;
        }
    }
    return mat;
}

Model LoadCookedModel(const char *path) {
    Model empty = {0};
    if (!path || !path[0]) return empty;
    FILE *f = fopen(path, "rb");
    if (!f) return empty;

    CookedHeader h;
    memset(&h, 0, sizeof(h));
    if (fread(&h, 1, 48, f) != 48 ||
        memcmp(h.magic, "VLCRMSH", 7) != 0 ||
        h.vertexCount == 0 || h.indexCount == 0 || h.indexType != 32) {
        fclose(f);
        return empty;
    }
    if (h.version >= 2) {
        if (fread((char *)&h + 48, 1, 12, f) != 12) {
            fclose(f);
            return empty;
        }
    }
    if (h.version >= 3) {
        if (fread(&h.meshCount, 1, 4, f) != 4) {
            fclose(f);
            return empty;
        }
    }
    if (h.version < 2) {
        h.materialCount = 0;
        h.textureCount = 0;
        h.stringBytes = 0;
    }
    if (h.meshCount == 0) h.meshCount = 1;
    if (h.meshCount > 4096 || h.materialCount > 4096 || h.textureCount > 65536) {
        fclose(f);
        return empty;
    }

    Vertex3D *verts = (Vertex3D *)malloc((size_t)h.vertexCount * sizeof(Vertex3D));
    uint32_t *indices = (uint32_t *)malloc((size_t)h.indexCount * sizeof(uint32_t));
    if (!verts || !indices) {
        free(verts); free(indices); fclose(f); return empty;
    }
    if (fread(verts, sizeof(Vertex3D), h.vertexCount, f) != h.vertexCount ||
        fread(indices, sizeof(uint32_t), h.indexCount, f) != h.indexCount) {
        free(verts); free(indices); fclose(f); return empty;
    }

    char *strings = NULL;
    CookTextureRef *texRefs = NULL;
    CookMaterial *cookMats = NULL;
    CookMesh *cookMeshes = NULL;
    Material *mats = NULL;

    if (h.version >= 2 && h.materialCount > 0) {
        strings = readBlob(f, h.stringBytes);
        texRefs = (CookTextureRef *)calloc(h.textureCount, sizeof(CookTextureRef));
        cookMats = (CookMaterial *)calloc(h.materialCount, sizeof(CookMaterial));
        bool ok = strings && texRefs && cookMats;
        if (ok && h.textureCount > 0)
            ok = fread(texRefs, sizeof(CookTextureRef), h.textureCount, f) == h.textureCount;
        if (ok)
            ok = fread(cookMats, sizeof(CookMaterial), h.materialCount, f) == h.materialCount;
        if (ok && h.version >= 3) {
            cookMeshes = (CookMesh *)calloc(h.meshCount, sizeof(CookMesh));
            ok = cookMeshes &&
                 fread(cookMeshes, sizeof(CookMesh), h.meshCount, f) == h.meshCount;
        }
        if (!ok) {
            free(strings); free(texRefs); free(cookMats); free(cookMeshes);
            free(verts); free(indices); fclose(f); return empty;
        }

        mats = (Material *)calloc(h.materialCount, sizeof(Material));
        if (!mats) {
            free(strings); free(texRefs); free(cookMats); free(cookMeshes);
            free(verts); free(indices); fclose(f); return empty;
        }
        for (uint32_t i = 0; i < h.materialCount; ++i)
            mats[i] = materialFromCook(&cookMats[i], texRefs, h.textureCount,
                                       strings, h.stringBytes, path);
    }
    fclose(f);

    if (!cookMeshes) {
        cookMeshes = (CookMesh *)calloc(1, sizeof(CookMesh));
        if (cookMeshes) {
            cookMeshes[0].vertexCount = h.vertexCount;
            cookMeshes[0].indexCount = h.indexCount;
            cookMeshes[0].materialIndex = 0;
        }
        h.meshCount = 1;
    }
    if (!cookMeshes) {
        free(strings); free(texRefs); free(cookMats);
        free(verts); free(indices); return empty;
    }

    Mesh *meshes = (Mesh *)calloc(h.meshCount, sizeof(Mesh));
    int *meshMaterials = (int *)calloc(h.meshCount, sizeof(int));
    if (!meshes || !meshMaterials) {
        free(meshes); free(meshMaterials); free(strings); free(texRefs);
        free(cookMats); free(cookMeshes); free(verts); free(indices); free(mats);
        return empty;
    }

    for (uint32_t i = 0; i < h.meshCount; ++i) {
        const CookMesh *cm = &cookMeshes[i];
        if ((uint64_t)cm->vertexOffset + cm->vertexCount > h.vertexCount ||
            (uint64_t)cm->indexOffset + cm->indexCount > h.indexCount) {
            continue;
        }
        meshes[i] = vgraphics_RegisterMesh(verts + cm->vertexOffset,
                                           (int)cm->vertexCount,
                                           indices + cm->indexOffset,
                                           (int)cm->indexCount);
        meshMaterials[i] = cm->materialIndex;
    }

    Material fallback = LoadMaterialDefault();
    if (!mats) mats = &fallback;
    int materialCount = (int)h.materialCount;
    if (materialCount <= 0) materialCount = 1;

    Model model = vgraphics_CreateModelFromMeshesEx(meshes, meshMaterials, NULL,
                                                    (int)h.meshCount,
                                                    mats, materialCount);
    free(meshes); free(meshMaterials);
    free(strings); free(texRefs); free(cookMats); free(cookMeshes);
    free(verts); free(indices);
    if (mats != &fallback) free(mats);
    return model;
}
