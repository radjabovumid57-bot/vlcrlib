/* cpu_3d_example.c — transparent CPU path for GenMesh + DrawModel. */
#include <vlcrlib.h>

int main(void) {
    if (!VRenderBackendSelect(VRENDER_BACKEND_CPU)) return 1;
    if (!InitWindow(800, 600, "vlcrlib — CPU 3D backend")) return 1;
    SetTargetFPS(60);

    Mesh cube = GenMeshCube(2.0f, 2.0f, 2.0f);
    Model model = LoadModelFromMesh(cube);
    if (!model.id) return 1;

    Camera3D cam = {0};
    cam.position = (Vector3){3.2f, 2.4f, 4.2f};
    cam.target = (Vector3){0, 0, 0};
    cam.up = (Vector3){0, 1, 0};
    cam.fovy = 50.0f;
    cam.nearPlane = 0.1f;
    cam.farPlane = 100.0f;

    float t = 0;
    while (!WindowShouldClose()) {
        t += GetFrameTime();
        BeginDrawing();
        ClearBackground((Color){18, 20, 28, 255});
        BeginMode3D(cam);
        DrawModel(model, (Vector3){0, 0, 0},
                  (Vector3){t * 0.31f, t * 0.47f, 0},
                  (Vector3){1, 1, 1}, (Color){220, 140, 80, 255});
        EndMode3D();
        DrawText("CPU 3D: GenMeshCube + DrawModel", 30, 30, 22,
                 (Color){235, 240, 250, 255});
        DrawText("transparent InitWindow / BeginMode3D", 30, 60, 18,
                 (Color){160, 200, 170, 255});
        EndDrawing();
    }

    UnloadModel(model);
    CloseWindow();
    return 0;
}
