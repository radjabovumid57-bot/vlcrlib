#ifndef VLCRLIB_VFASTGLTF_H
#define VLCRLIB_VFASTGLTF_H

/*
 * vfastgltf — fast, self-contained glTF 2.0 / GLB loader.
 *
 * Zero third-party dependencies: it parses the GLB container and the
 * .gltf JSON itself, so it does not need tinygltf, cgltf or vcpkg.
 *
 * Every accessor, bufferView, stride and index is bounds-checked before
 * it is read, so a malformed or hostile file fails cleanly (the load
 * returns a Model with id == 0) instead of reading out of bounds.
 *
 * Unlike the transform-baking loader, a mesh referenced by several
 * nodes is uploaded to the GPU once and drawn once per node, so glTF
 * instancing survives.
 *
 * Textures it creates are attached to the returned Model, so a single
 * UnloadModel() releases the meshes, materials and textures.
 */

#include "vgraphics.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FastLoadGltfOptions {
    float scale;          /* uniform scale applied to node positions (default 1)   */
    int   flipV;          /* 1: flip the V texture coordinate (default 0)          */
    int   srgbTextures;   /* 1: load base colour + emissive maps as sRGB (default 1)*/
    int   verbose;        /* 1: report the reason a load failed on stderr          */
} FastLoadGltfOptions;

typedef struct FastLoadGltfInfo {
    int ok;               /* 1 when the model loaded successfully                  */
    int drawCount;        /* number of draw slots (instances x primitives)         */
    int meshCount;        /* number of glTF meshes in the file                     */
    int materialCount;    /* number of materials actually referenced               */
    int textureCount;     /* number of GPU textures created                        */
    int nodeCount;        /* number of glTF nodes                                  */
    FastLoadGltfOptions sizes;  /* the options that were used                      */
} FastLoadGltfInfo;

/*
 * Load a .gltf or .glb file.
 *
 * Returns a Model ready for DrawModel(). On failure the returned
 * model.id is 0 and nothing has leaked. Release it with UnloadModel().
 */
Model FastLoadGltf(const char *path);

/*
 * Same, with explicit options and an optional report.
 * `options` and `info` may both be NULL.
 */
Model FastLoadGltfEx(const char *path, const FastLoadGltfOptions *options,
                     FastLoadGltfInfo *info);

#ifdef __cplusplus
}
#endif
#endif /* VLCRLIB_VFASTGLTF_H */
