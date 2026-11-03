#include <vlcrlib.h>
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>

int main(void) {
    if (!InitWindow(1280, 720, "vlcrlib GLB model — DamagedHelmet")) return 1;
    SetTargetFPS(60);

    char modelPath[1024] = {0};
    const char *basePath = SDL_GetBasePath();
    Model model = {0};
    if (basePath) {
        snprintf(modelPath, sizeof(modelPath), "%sDamagedHelmet.glb", basePath);
        model = LoadModel(modelPath);
    }
    if (!model.id) {
        snprintf(modelPath, sizeof(modelPath), "DamagedHelmet.glb");
        model = LoadModel(modelPath);
    }
    if (!model.id) {
        snprintf(modelPath, sizeof(modelPath), "example/DamagedHelmet.glb");
        model = LoadModel(modelPath);
    }
    if (!model.id) {
        snprintf(modelPath, sizeof(modelPath), "../example/DamagedHelmet.glb");
        model = LoadModel(modelPath);
    }
    if (!model.id) {
        fprintf(stderr, "[pbr_example] Could not load DamagedHelmet.glb\n");
        CloseWindow();
        return 1;
    }

    Camera3D camera = {0};
    camera.up        = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.target    = (Vector3){ 0.0f, 1.2f, 0.0f };
    camera.fovy      = 45.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane  = 100.0f;

    // Key light: from front-top-left, shining into the scene
    vgraphics_SetDirectionalLight(true,
        (Vector3){ -0.35f, -0.70f, -0.60f },
        (Color){ 255, 250, 240, 255 }, 3.0f);

    // Ambient: very subtle, cool fill
    vgraphics_SetAmbientLight(true, (Color){ 60, 75, 100, 255 }, 0.12f);

    /* DamagedHelmet is metallic; without IBL it renders almost black.
       Load the procedural studio environment (same as the PBR gallery). */
    bool iblOk = vgraphics_LoadIBL(NULL);
    vgraphics_SetIBLIntensity(iblOk ? 1.2f : 0.0f);
    vgraphics_SetSkybox(0);   /* neutral gray background, like Windows 3D Viewer */
    SetSSAO(1, 0.5f, 0.6f);
    SetBloom(0.9f, 0.18f);
    SetGraphicsExposure(1.0f);

    float time0 = (float)GetTime();

    while (!WindowShouldClose()) {
        float time  = (float)GetTime() - time0;
        float orbit = -0.35f + time * 0.12f;

        // Camera orbits around the front of the helmet
        camera.position.x = sinf(orbit) * 4.0f;
        camera.position.y = 1.55f + sinf(time * 0.5f) * 0.15f;
        camera.position.z = cosf(orbit) * 4.0f; // Start on +Z side

        BeginDrawing();
        ClearBackground((Color){ 118, 118, 118, 255 }); // neutral gray

        BeginMode3D(camera);
        DrawModel(model, (Vector3){ 0.0f, 1.0f, 0.0f }, (Vector3){ 0, 0, 0 },
                  (Vector3){ 1.2f, 1.2f, 1.2f }, WHITE);
        EndMode3D();

        DrawText("GLB / glTF PBR — DamagedHelmet", 28, 24, 28, WHITE);
        DrawText("Corrected sRGB handling, emissive, and tone mapping",
                 30, 62, 17, LIGHTGRAY);
        EndDrawing();
    }

    UnloadModel(model);
    CloseWindow();
    return 0;
}
