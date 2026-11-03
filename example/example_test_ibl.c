/*
 * example_test_ibl.c — IBL bake/upload + environment skybox smoke test.
 *
 * Loads the glTF model first, then the procedural HDR environment, draws it
 * with the skybox background (forward and offscreen post-process paths),
 * unloads IBL and exits. No user input required.
 */
#include <vlcrlib.h>
#include "vgltf.h"
#include <stdio.h>

int main(void) {
    if (!InitWindow(960, 540, "vlcrlib - IBL + skybox test")) return 1;
    SetTargetFPS(60);

    /* Model first: the IBL upload must refresh live material descriptors. */
    Model model = LoadModelGLTFTiny("DamagedHelmet.glb");
    if (!model.id) model = LoadModelGLTFTiny("example/DamagedHelmet.glb");
    fprintf(stderr, "[ibl_test] model id=%u\n", model.id);

    if (!vgraphics_LoadIBL(NULL)) {
        fprintf(stderr, "[ibl_test] LoadIBL failed\n");
        CloseWindow();
        return 1;
    }
    vgraphics_SetIBLIntensity(1.0f);
    vgraphics_SetSkybox(1);

    Camera3D cam = {0};
    cam.position = (Vector3){ 0.0f, 1.6f, 4.2f };
    cam.target   = (Vector3){ 0.0f, 1.0f, 0.0f };
    cam.up       = (Vector3){ 0.0f, 1.0f, 0.0f };
    cam.fovy     = 45.0f;
    cam.nearPlane = 0.05f;
    cam.farPlane  = 100.0f;

    for (int f = 0; f < 4; ++f) {
        BeginDrawing();
        ClearBackground((Color){ 20, 22, 30, 255 });
        BeginMode3D(cam);
        if (model.id)
            DrawModel(model, (Vector3){0, 1, 0}, (Vector3){0, 0.15f * f, 0},
                      (Vector3){1, 1, 1}, WHITE);
        EndMode3D();
        DrawText("IBL + environment skybox", 20, 16, 22, WHITE);
        EndDrawing();
    }

    /* Offscreen scene pass + composite. */
    SetPostProcess(1);
    for (int f = 0; f < 4; ++f) {
        BeginDrawing();
        ClearBackground((Color){ 20, 22, 30, 255 });
        BeginMode3D(cam);
        if (model.id)
            DrawModel(model, (Vector3){0, 1, 0}, (Vector3){0, 0.15f * f, 0},
                      (Vector3){1, 1, 1}, WHITE);
        EndMode3D();
        EndDrawing();
    }
    SetPostProcess(0);

    vgraphics_UnloadIBL();
    if (model.id) UnloadModel(model);
    CloseWindow();
    fprintf(stderr, "[ibl_test] ok\n");
    return 0;
}
