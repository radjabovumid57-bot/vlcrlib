#include "vloaders.h"
#include "vgltf.h"
#include <SDL3/SDL.h>
#include <string.h>
#include <stdio.h>

Model LoadModelAuto(const char*path) {
    if (!path || !path[0]) return (Model){0};

    const char*ext = strrchr(path, '.');
    if (!ext) return LoadModelGLTFTiny(path);

    if (SDL_strcasecmp(ext, ".glb") == 0 || SDL_strcasecmp(ext, ".gltf") == 0)
        return LoadModelGLTFTiny(path);
    if (SDL_strcasecmp(ext, ".fbx") == 0)
        return LoadFBX(path);
    if (SDL_strcasecmp(ext, ".usd") == 0 || SDL_strcasecmp(ext, ".usda") == 0 ||
        SDL_strcasecmp(ext, ".usdc") == 0 || SDL_strcasecmp(ext, ".usdz") == 0)
        return LoadUSD(path);
    if (SDL_strcasecmp(ext, ".vmesh") == 0)
        return LoadCookedModel(path);
    if (SDL_strcasecmp(ext, ".obj") == 0)
        return LoadOBJ(path);

    // Unknown extension: try glTF first, then OBJ.
    Model m = LoadModelGLTFTiny(path);
    if (m.id) return m;
    return LoadModel(path);
}
