/*
 * light_array_example.c — explicit light data flow.
 *
 * CPU builds VLightData records, uploads them to the SSBO at binding 16, and
 * pbr.frag loops over the array:
 *
 *   VLightData[] -> vkMapMemory -> VkBuffer(SSBO) -> descriptor 16
 *                -> pbr.frag: for (i < lightMeta.x) shade(...)
 *
 * Demonstrates directional + point + spot + area lights, per-light colors,
 * inverse-square falloff and an array-driven forward renderer.
 *
 * Controls:
 *   + / -      add/remove point lights
 *   SPACE      pause light animation
 *   S          directional shadows
 *
 * LIGHT_ARRAY_TEST=1 runs six frames and exits.
 * LIGHT_ARRAY_COUNT=N selects the initial point-light count (4..20).
 */
#include <vlcrlib.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define LA_PI     3.14159265358979323846f
#define WIN_W     1280
#define WIN_H     720
#define GRID_COLS 5
#define GRID_ROWS 4
#define MAX_POINTS (GRID_COLS * GRID_ROWS)

static float clampf(float v, float a, float b) {
    return v < a ? a : (v > b ? b : v);
}

static Color HueColor(float h, float s, float v) {
    h = h - floorf(h);
    float r = clampf(fabsf(h * 6.0f - 3.0f) - 1.0f, 0.0f, 1.0f);
    float g = clampf(2.0f - fabsf(h * 6.0f - 2.0f), 0.0f, 1.0f);
    float b = clampf(2.0f - fabsf(h * 6.0f - 4.0f), 0.0f, 1.0f);
    r = (r - 1.0f) * s + 1.0f;
    g = (g - 1.0f) * s + 1.0f;
    b = (b - 1.0f) * s + 1.0f;
    return (Color){ (unsigned char)(clampf(r, 0, 1) * v * 255.0f),
                    (unsigned char)(clampf(g, 0, 1) * v * 255.0f),
                    (unsigned char)(clampf(b, 0, 1) * v * 255.0f), 255 };
}

static Model CreateModel(Mesh mesh, Material material) {
    int index = 0;
    return vgraphics_CreateModelFromMeshes(&mesh, &index, 1, &material, 1);
}

static Material MakeMaterial(float r, float g, float b,
                             float metallic, float roughness) {
    Material m = LoadMaterialDefault();
    SetMaterialBaseColorFactor(&m, r, g, b, 1.0f);
    SetMaterialPBR(&m, metallic, roughness);
    return m;
}

int main(void) {
    if (!InitWindow(WIN_W, WIN_H, "vlcrlib - Explicit Light Array (SSBO)")) return 1;
    SetTargetFPS(60);

    Font uiFont = {0};
#ifdef _WIN32
    uiFont = LoadFontEx("C:/Windows/Fonts/segoeui.ttf", 32, 32, 96);
    if (!uiFont.ok) uiFont = LoadFontEx("C:/Windows/Fonts/arial.ttf", 32, 32, 96);
#else
    uiFont = LoadFontEx("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 32, 32, 96);
#endif

    bool ibl = vgraphics_LoadIBL(NULL);
    if (ibl) vgraphics_SetIBLIntensity(0.12f);
    vgraphics_SetSkybox(1);

    vgraphics_SetShadows(true, 2048, 0.002f, 0.70f);
    vgraphics_SetSpotShadows(false, 1024, 0.003f, 0.8f);
    vgraphics_SetPointShadows(false, 512, 0.004f, 0.8f);
    vgraphics_SetAmbientLight(true, (Color){ 55, 65, 85, 255 }, 0.03f);
    SetSSAO(1, 0.55f, 0.5f);
    SetBloom(0.95f, 0.18f);
    SetGraphicsExposure(1.0f);

    Mesh plane = GenMeshPlane(44.0f, 44.0f, 1, 1);
    Mesh cube = GenMeshCube(1.0f, 1.0f, 1.0f);
    Mesh sphere = GenMeshSphere(1.0f, 20, 28);

    Material groundMat = MakeMaterial(0.13f, 0.14f, 0.16f, 0.0f, 0.90f);
    Material sphereMat = MakeMaterial(0.55f, 0.57f, 0.62f, 0.15f, 0.42f);
    Material obeliskMat = MakeMaterial(0.18f, 0.19f, 0.22f, 0.2f, 0.5f);

    Model ground = CreateModel(plane, groundMat);
    Model sphereModel = CreateModel(sphere, sphereMat);
    Model obelisk = CreateModel(cube, obeliskMat);

    int pointCount = MAX_POINTS;
    {
        const char *c = getenv("LIGHT_ARRAY_COUNT");
        if (c) {
            pointCount = atoi(c);
            if (pointCount < 4) pointCount = 4;
            if (pointCount > MAX_POINTS) pointCount = MAX_POINTS;
        }
    }
    bool paused = false;
    bool shadows = true;
    bool shadowsPrev = true;
    float animTime = 0.0f;
    int frame = 0;
    int testFrames = getenv("LIGHT_ARRAY_TEST") ? 6 : -1;

    Camera3D cam;
    memset(&cam, 0, sizeof(cam));
    cam.position = (Vector3){ 0.0f, 9.5f, 20.0f };
    cam.target = (Vector3){ 0.0f, 2.0f, 0.0f };
    cam.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    cam.fovy = 50.0f;
    cam.nearPlane = 0.1f;
    cam.farPlane = 120.0f;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (!paused) animTime += dt;

        if (IsKeyPressed(SDL_SCANCODE_SPACE)) paused = !paused;
        if (IsKeyPressed(SDL_SCANCODE_EQUALS) && pointCount < MAX_POINTS) pointCount++;
        if (IsKeyPressed(SDL_SCANCODE_MINUS) && pointCount > 4) pointCount--;
        if (IsKeyPressed(SDL_SCANCODE_S)) shadows = !shadows;
        if (shadows != shadowsPrev) {
            vgraphics_SetShadows(shadows, 2048, 0.002f, 0.70f);
            shadowsPrev = shadows;
        }
        vgraphics_ComputeDirectionalShadowMatrix(34.0f, 0.5f, 90.0f);

        /* Build the explicit light array on the CPU. */
        VLightData lights[VLCRLIB_MAX_GPU_LIGHTS];
        int lightCount = 0;

        Vector3 sun = Vector3Normalize((Vector3){ 0.42f, 0.90f, 0.38f });
        VLightData sunLight = VLightDataDirectional(
            Vector3Scale(sun, -1.0f), (Color){ 255, 242, 220, 255 }, 1.4f);
        sunLight.params[2] = 0.0f; /* shadow map 0: directional */
        lights[lightCount++] = sunLight;

        for (int i = 0; i < pointCount; ++i) {
            int col = i % GRID_COLS;
            int row = i / GRID_COLS;
            float x = ((float)col - 2.0f) * 3.4f;
            float z = ((float)row - 1.5f) * 3.4f;
            float y = 3.1f + sinf(animTime * 1.7f + (float)i * 0.7f) * 0.55f;
            Color c = HueColor((float)i / (float)MAX_POINTS, 0.85f, 1.0f);
            VLightData p = VLightDataPoint(
                (Vector3){ x, y, z }, c, 14.0f, 8.0f);
            lights[lightCount++] = p;
        }

        /* Two stationary spot lights. */
        VLightData spotA = VLightDataSpot(
            (Vector3){ -10.0f, 9.0f, 0.0f },
            Vector3Normalize(Vector3Subtract((Vector3){ 0, 0, 0 },
                                             (Vector3){ -10.0f, 9.0f, 0.0f })),
            (Color){ 90, 210, 255, 255 }, 260.0f, 22.0f, 0.30f, 0.52f);
        VLightData spotB = VLightDataSpot(
            (Vector3){ 10.0f, 9.0f, 0.0f },
            Vector3Normalize(Vector3Subtract((Vector3){ 0, 0, 0 },
                                             (Vector3){ 10.0f, 9.0f, 0.0f })),
            (Color){ 255, 100, 210, 255 }, 260.0f, 22.0f, 0.30f, 0.52f);
        lights[lightCount++] = spotA;
        lights[lightCount++] = spotB;

        /* Rectangular area light above the grid. */
        VLightData area = VLightDataArea(
            (Vector3){ 0.0f, 8.0f, -6.5f }, (Vector3){ 0.0f, -1.0f, 0.0f },
            6.0f, 3.0f, (Color){ 255, 226, 180, 255 }, 45.0f);
        lights[lightCount++] = area;

        AtmosphereParams atm;
        memset(&atm, 0, sizeof(atm));
        atm.enabled = 1;
        atm.timeOfDay = 15.5f;
        atm.cloudCoverage = 0.30f;
        atm.haze = 0.05f;
        atm.starIntensity = 0.0f;
        atm.sunDirection = sun;
        atm.sunIntensity = 4.0f;
        atm.moonDirection = Vector3Scale(sun, -1.0f);
        atm.moonIntensity = 0.0f;
        atm.sunAngularRadius = 0.009f;
        atm.moonAngularRadius = 0.011f;
        vgraphics_SetAtmosphere(&atm);

        Matrix proj = MatrixPerspective(cam.fovy * (LA_PI / 180.0f),
                                        (float)WIN_W / (float)WIN_H,
                                        cam.nearPlane, cam.farPlane);
        Matrix view = MatrixLookAt(cam.position, cam.target, cam.up);

        BeginDrawing();
        ClearBackground((Color){ 8, 10, 15, 255 });

        /* Upload after the frame fence wait (BeginDrawing) so the previous
           frame can no longer be reading the SSBO. */
        vgraphics_SetLightArray(lights, lightCount);

        BeginMode3D(cam);
        DrawModel(ground, (Vector3){ 0, 0, 0 }, (Vector3){ 0, 0, 0 },
                  (Vector3){ 1, 1, 1 }, WHITE);

        for (int i = 0; i < MAX_POINTS; ++i) {
            int col = i % GRID_COLS;
            int row = i / GRID_COLS;
            float x = ((float)col - 2.0f) * 3.4f;
            float z = ((float)row - 1.5f) * 3.4f;
            DrawModel(sphereModel, (Vector3){ x, 1.05f, z },
                      (Vector3){ 0.0f, animTime * 0.25f + (float)i * 0.4f, 0.0f },
                      (Vector3){ 0.70f, 0.70f, 0.70f }, WHITE);

            if (i < pointCount) {
                float y = 3.1f + sinf(animTime * 1.7f + (float)i * 0.7f) * 0.55f;
                Color c = HueColor((float)i / (float)MAX_POINTS, 0.85f, 1.0f);
                DrawSphere((Vector3){ x, y, z }, 0.075f, c);
            }
        }

        /* Obelisk for a long directional shadow. */
        DrawModel(obelisk, (Vector3){ 0.0f, 2.5f, -7.5f }, (Vector3){ 0, 0, 0 },
                  (Vector3){ 1.4f, 5.0f, 1.4f }, WHITE);
        EndMode3D();

        /* HUD */
        DrawRectangle(0, 0, 720, 112, (Color){ 8, 10, 16, 220 });
        DrawText("EXPLICIT LIGHT ARRAY  (SSBO binding 16)", 18, 14, 22, WHITE);
        char line[256];
        snprintf(line, sizeof(line),
                 "CPU VLightData[%d] -> vkMapMemory -> VkBuffer -> pbr.frag loop",
                 lightCount);
        DrawText(line, 18, 48, 15, (Color){ 180, 200, 225, 255 });
        snprintf(line, sizeof(line),
                 "%d lights: 1 directional, %d point, 2 spot, 1 area   |   %s",
                 lightCount, pointCount, paused ? "PAUSED" : "ANIMATING");
        DrawText(line, 18, 74, 15, (Color){ 200, 214, 235, 255 });

        DrawRectangle(0, WIN_H - 88, WIN_W, 88, (Color){ 8, 10, 16, 228 });
        DrawText("[+] / [-]  point lights     [SPACE] pause     [S] directional shadows     [ESC] exit",
                 18, WIN_H - 66, 15, (Color){ 205, 215, 232, 255 });
        snprintf(line, sizeof(line),
                 "Forward pass: one fragment loop over %d lights; shadows: %s; inverse-square falloff; IBL intensity 0.12",
                 lightCount, shadows ? "ON (2048)" : "OFF");
        DrawText(line, 18, WIN_H - 38, 14, (Color){ 150, 166, 188, 255 });

        EndDrawing();

        ++frame;
        if (testFrames > 0 && frame >= testFrames) break;
    }

    UnloadModel(ground);
    UnloadModel(sphereModel);
    UnloadModel(obelisk);
    if (uiFont.ok) UnloadFont(uiFont);
    vgraphics_UnloadIBL();
    CloseWindow();
    fprintf(stderr, "[light_array] ok (%d lights)\n", pointCount);
    return 0;
}
