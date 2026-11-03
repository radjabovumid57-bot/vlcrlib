#ifndef VLCRLIB_VFASTGLTF_LIB_H
#define VLCRLIB_VFASTGLTF_LIB_H

/*
 * vfastgltf_lib — C ABI over spnda/fastgltf.
 *
 * fastgltf is a C++17/C++20 library, so it cannot be called from C. This
 * header is the plain-C surface; src/vfastgltf_lib.cpp is the only
 * translation unit that includes the C++ headers, exactly the way
 * vphysics.cpp wraps Bullet.
 *
 * The wrapper is compiled only when the build finds fastgltf:
 *     vcpkg install fastgltf
 *     cmake -DVLCRLIB_ENABLE_FASTGLTF=ON
 *
 * The C API below is declared UNCONDITIONALLY. An application can check
 * VLCRLIB_HAS_FASTGLTF (set by the generated vlcrlib_config.h) to choose
 * at compile time between this backend and the dependency-free
 * FastLoadGltf() in vfastgltf.h:
 *
 *     #if VLCRLIB_HAS_FASTGLTF
 *         FastGltfLibInfo info = {0};
 *         FastGltfLibAsset a = vfastgltf_lib_load(path, FASTGLTF_LIB_NONE, &info);
 *         Model m = vfastgltf_lib_build_model(a, NULL);
 *         vfastgltf_lib_release(a);
 *     #else
 *         Model m = FastLoadGltf(path);
 *     #endif
 *
 * Hiding the declarations behind that macro — as an earlier revision did —
 * turned "the wrapper was not compiled" into a confusing cascade of
 * "identifier is undefined" errors in the editor and in a plain compile.
 * Keeping them always visible means a build that calls the API without
 * linking the wrapper fails at link time with a useful symbol name, which
 * is what we actually want.
 *
 * Difference from FastLoadGltf (vfastgltf.c):
 *   FastLoadGltf      — no dependencies, hand-written parser, static
 *                       geometry, tolerates but skips skins/animations.
 *   vfastgltf_lib_*   — full glTF 2.0 spec plus extensions, sparse
 *                       accessors, SIMD parsing; needs fastgltf.
 * Both produce a vlcrlib Model, so both draw with DrawModel() and free
 * with UnloadModel().
 *
 * Two-stage use, because fastgltf's Asset is far richer than a Model:
 *
 *   FastGltfLibAsset a = vfastgltf_lib_load("scene.glb", 0, &info);
 *   if (a) {
 *       Model m = vfastgltf_lib_build_model(a, NULL);   // GPU work
 *       ... draw m ...
 *       UnloadModel(m);
 *       vfastgltf_lib_release(a);                        // CPU side
 *   }
 */

#include "vgraphics.h"

#include <stddef.h>

/* Feature-availability flag.
 *
 * 1 only in builds where fastgltf was found AND vfastgltf_lib.cpp was
 * compiled. The value comes from the generated vlcrlib_config.h; a bare
 * compile (no CMake, no generated header) falls back to 0.
 *
 * Applications use this to pick a backend; they do NOT use it to guard the
 * declarations below. Those are always visible. */
#if defined(__has_include)
#  if __has_include("vlcrlib_config.h")
#    include "vlcrlib_config.h"
#  endif
#endif
#if !defined(VLCRLIB_HAS_FASTGLTF)
#  define VLCRLIB_HAS_FASTGLTF 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef int FastGltfLibAsset;   /* 0 = invalid */

/* Texture map slots, kept in the same order as MATERIAL_MAP_*. */
#define FASTGLTF_LIB_MAP_ALBEDO            0
#define FASTGLTF_LIB_MAP_NORMAL            1
#define FASTGLTF_LIB_MAP_METALLICROUGHNESS 2
#define FASTGLTF_LIB_MAP_OCCLUSION         3
#define FASTGLTF_LIB_MAP_EMISSIVE          4
#define FASTGLTF_LIB_MAX_MAPS              5

typedef enum FastGltfLibOptions {
    FASTGLTF_LIB_NONE    = 0,
    FASTGLTF_LIB_VERBOSE = 1 << 0   /* report why a load failed on stderr */
} FastGltfLibOptions;

typedef struct FastGltfLibBuildOptions {
    int   sceneIndex;     /* scene to build; < 0 or out of range -> 0 */
    float scale;          /* uniform scale on node translations (default 1) */
    int   srgbTextures;   /* 1: base colour + emissive as sRGB (default 1) */
} FastGltfLibBuildOptions;

typedef struct FastGltfLibInfo {
    int ok;
    int meshCount;
    int materialCount;
    int textureCount;
    int imageCount;
    int nodeCount;
    int sceneCount;
    int animationCount;
    int skinnedMeshCount;
} FastGltfLibInfo;

typedef struct FastGltfLibMeshInfo {
    int primitiveCount;
    int vertexCount;
} FastGltfLibMeshInfo;

typedef struct FastGltfLibMaterial {
    float baseColor[4];        /* linear, straight from glTF */
    float metallic;
    float roughness;
    float emissive[3];         /* linear */
    float normalScale;
    float occlusionStrength;
    int   textureIndex[FASTGLTF_LIB_MAX_MAPS];  /* -1 = absent */
    int   doubleSided;
    int   alphaMode;           /* 0 opaque, 1 mask, 2 blend */
    float alphaCutoff;
} FastGltfLibMaterial;

/*
 * Read and parse a .gltf / .glb. Does no GPU work.
 * Returns 0 on failure; check `outInfo->ok` when outInfo is non-NULL.
 */
FastGltfLibAsset vfastgltf_lib_load(const char *path, FastGltfLibOptions options,
                                    FastGltfLibInfo *outInfo);

/* Free the parsed asset. The Model returned by build_model is separate:
 * it stays valid (and must be freed with UnloadModel) after this call. */
void vfastgltf_lib_release(FastGltfLibAsset handle);

size_t vfastgltf_lib_mesh_count(FastGltfLibAsset handle);
void   vfastgltf_lib_get_mesh_info(FastGltfLibAsset handle, size_t meshIndex,
                                   FastGltfLibMeshInfo *out);

/* Returns 1 on success. `materialIndex` is a glTF material index. */
int vfastgltf_lib_get_material(FastGltfLibAsset handle, size_t materialIndex,
                               FastGltfLibMaterial *out);

/*
 * Upload meshes/materials/textures and build a vlcrlib Model for the draw
 * list of one scene. `options` may be NULL for defaults.
 * Release the result with UnloadModel().
 */
Model vfastgltf_lib_build_model(FastGltfLibAsset handle,
                                const FastGltfLibBuildOptions *options);

/* Human-readable backend string.
 *
 * Always linkable: when the wrapper was not compiled, this returns
 * "fastgltf backend not compiled" so a caller can log which backend is
 * actually present without a link error. Every other symbol in this header
 * is only defined when VLCRLIB_HAS_FASTGLTF is 1; calling one of them in a
 * build without the wrapper is a link-time error, by design. */
const char *vfastgltf_lib_version(void);

#ifdef __cplusplus
}
#endif
#endif /* VLCRLIB_VFASTGLTF_LIB_H */