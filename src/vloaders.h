#ifndef VLCRLIB_VLOADERS_H
#define VLCRLIB_VLOADERS_H

#include "vgraphics.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Optional format loaders. They return Model with id == 0 when the
   corresponding backend is not compiled in. */
bool  vloaders_HasFBX(void);
Model LoadFBX(const char *path);
SkinnedModel LoadFBXSkinned(const char *path);

bool  vloaders_HasUSD(void);
Model LoadUSD(const char *path);

/* Wavefront OBJ. Uses Assimp when available (materials/MTL), otherwise the
   engine's built-in OBJ path. */
Model LoadOBJ(const char *path);

/* First cooker milestone: statically cooked single-mesh .vmesh. */
Model LoadCookedModel(const char *path);

/* Dispatch by extension: .glb/.gltf, .fbx, .usd/.usda/.usdc/.usdz, .obj. */
Model LoadModelAuto(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* VLCRLIB_VLOADERS_H */
