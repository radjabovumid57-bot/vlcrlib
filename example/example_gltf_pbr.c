/*
 * example_gltf_pbr.c — tinygltf import verification.
 *
 * Loads DamagedHelmet.glb through LoadModelGLTFTiny (tinygltf), builds GPU
 * meshes/materials and reports the asset stats.
 */
#include <vlcrlib.h>
#include "vgltf.h"
#include <stdio.h>

int main(int argc, char **argv) {
    const char *path = (argc > 1 && argv[1][0]) ? argv[1] : "DamagedHelmet.glb";

    if (!InitWindow(640, 480, "vlcrlib tinygltf import")) return 1;
    Model model = LoadModelGLTFTiny(path);
    if (!model.id) {
        fprintf(stderr, "[tinygltf-test] load failed: %s\n", path);
        CloseWindow();
        return 1;
    }

    fprintf(stderr, "[tinygltf-test] meshes=%d materials=%d model=%u\n",
            model.meshCount, model.materialCount, model.id);
    fflush(stderr);

    UnloadModel(model);
    CloseWindow();
    return 0;
}
