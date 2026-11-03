/*
 * example.cpp — minimal glTF/GLB load check.
 *
 * The old tinygltf demo was removed. The project uses fastgltf through
 * LoadModelGLTFTiny() (historical name) now.
 */
#include <vlcrlib.h>
#include "vgltf.h"
#include <cstdio>

int main(int argc, char **argv) {
    if (!InitWindow(640, 480, "vlcrlib glTF check")) return 1;
    const char *path = (argc > 1) ? argv[1] : "DamagedHelmet.glb";
    Model model = LoadModelGLTFTiny(path);
    std::printf("[example] %s model=%u meshes=%d materials=%d\n",
                model.id ? "loaded" : "failed", model.id,
                model.meshCount, model.materialCount);
    if (model.id) UnloadModel(model);
    CloseWindow();
    return model.id ? 0 : 1;
}
