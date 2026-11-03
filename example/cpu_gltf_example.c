/* cpu_gltf_example.c — LoadModel(glTF) on the CPU backend. */
#include <vlcrlib.h>
#include <stdio.h>

int main(int argc, char **argv) {
    if (!VRenderBackendSelect(VRENDER_BACKEND_CPU)) return 1;
    if (!InitWindow(800, 600, "vlcrlib — CPU glTF")) return 1;
    SetTargetFPS(60);

    const char *path = argc > 1 ? argv[1] : "assets/DamagedHelmet.glb";
    Model model = LoadModel(path);
    if (!model.id) {
        fprintf(stderr, "LoadModel failed: %s\n", path);
        return 1;
    }

    Mesh groundMesh = GenMeshCube(10.0f, 0.1f, 10.0f);
    Model ground = LoadModelFromMesh(groundMesh);
    Model prop = LoadModelFromMesh(GenMeshCube(0.8f, 0.8f, 0.8f));

    vgraphics_SetDirectionalLight(true, (Vector3){-0.4f, -0.7f, -0.3f},
                                  (Color){255, 250, 240, 255}, 1.5f);
    vgraphics_SetAmbientLight(true, (Color){50, 55, 70, 255}, 0.15f);
    vgraphics_SetShadows(true, 256, 0.0030f, 0.90f);
    vgraphics_SetShadowSoftness(1.0f);
    vgraphics_LoadIBL(NULL); /* procedural studio IBL */
    vgraphics_SetIBLIntensity(0.05f);
    SetGraphicsExposure(1.0f);

    Camera3D cam = {0};
    cam.position = (Vector3){0, 1.0f, 5.2f};
    cam.target = (Vector3){0, -0.4f, 0};
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
        DrawModel(ground, (Vector3){0, -1.15f, 0}, (Vector3){0, 0, 0},
                  (Vector3){1, 1, 1}, (Color){55, 62, 75, 255});
        DrawModel(prop, (Vector3){-1.6f, -0.75f, 0.15f}, (Vector3){0, 0, 0},
                  (Vector3){1, 1, 1}, (Color){220, 150, 85, 255});
        DrawModel(model, (Vector3){0, 0, 0},
                  (Vector3){0, t * 0.5f, 0}, (Vector3){1, 1, 1},
                  (Color){210, 210, 220, 255});
        EndMode3D();
        char status[128];
        snprintf(status, sizeof(status),
                 "CPU glTF: shadows + IBL | %.1f ms/frame",
                 (double)GetFrameTime() * 1000.0);
        DrawText(status, 30, 30, 22, (Color){235, 240, 250, 255});
        EndDrawing();
    }
    UnloadModel(prop);
    UnloadModel(ground);
    UnloadModel(model);
    CloseWindow();
    return 0;
}
