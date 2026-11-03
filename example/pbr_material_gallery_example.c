/*
 * Material and ray-tracing study.
 * G switches between vlcrlib's raster PBR material gallery and its separate
 * Vulkan ray-query Cornell-box demo. The two paths are deliberately labeled:
 * the current ray-query demo owns a fixed scene and does not consume these
 * gallery materials.
 */
#include <vlcrlib.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_scancode.h>
#include <stdlib.h>
#include <stdio.h>

#define MATERIAL_COUNT 7

static const char *materialNames[MATERIAL_COUNT] = {
    "Paint + metal map", "Gold", "Rough steel", "Blue glass*",
    "Clear-coated plastic", "Rubber", "Emissive"
};

static Material CreatePreset(int index)
{
    Material material = LoadMaterialDefault();
    switch (index) {
        case 0: /* Paint texture with exposed metal chips; map G=roughness, B=metallic. */
            SetMaterialColor(&material, WHITE);
            SetMaterialPBR(&material, 1.0f, 0.8f);
            break;
        case 1:
            SetMaterialColor(&material, (Color){ 235, 180, 72, 255 });
            SetMaterialPBR(&material, 1.0f, 0.22f);
            break;
        case 2: /* Roughness only; no anisotropic brushed grooves are implemented. */
            SetMaterialColor(&material, (Color){ 175, 188, 202, 255 });
            SetMaterialPBR(&material, 1.0f, 0.58f);
            break;
        case 3: /* Raster transmission tint; this path does not refract scene geometry. */
            SetMaterialColor(&material, (Color){ 120, 190, 230, 255 });
            SetMaterialBaseColorFactor(&material, 0.72f, 0.88f, 0.98f, 0.88f);
            SetMaterialPBR(&material, 0.0f, 0.08f);
            SetMaterialIOR(&material, 1.52f);
            material.transmissionFactor = 0.72f;
            material.alphaMode = 2;
            material.doubleSided = 1;
            break;
        case 4:
            SetMaterialColor(&material, (Color){ 35, 112, 220, 255 });
            SetMaterialPBR(&material, 0.0f, 0.25f);
            material.clearcoatFactor = 0.8f;
            material.clearcoatRoughness = 0.1f;
            break;
        case 5:
            SetMaterialColor(&material, (Color){ 68, 76, 87, 255 });
            SetMaterialPBR(&material, 0.0f, 0.9f);
            break;
        default:
            SetMaterialColor(&material, (Color){ 255, 102, 28, 255 });
            SetMaterialPBR(&material, 0.0f, 0.3f);
            SetMaterialEmission(&material, (Color){ 255, 65, 18, 255 }, 4.0f);
            break;
    }
    return material;
}

static void DrawOverlay(bool rayTracing, bool hardwareRT, bool ibl, int width, int height)
{
    DrawText("MATERIALS + LIGHTING LAB", 22, 12, 23, WHITE);
    if (rayTracing) {
        DrawText(hardwareRT ? "RAY QUERY: hardware Vulkan path" : "RAY QUERY: software fallback",
                 22, 41, 16, (Color){ 175, 220, 255, 255 });
        DrawText("G: PBR gallery   Ray-traced Cornell scene: shadows, reflections, glass",
                 22, height - 27, 15, WHITE);
        return;
    }

    DrawText("RASTER PBR + IBL (this gallery is not ray traced)", 22, 41, 16,
             (Color){ 175, 220, 255, 255 });
    const int labelY = height - 74;
    for (int i = 0; i < MATERIAL_COUNT; ++i) {
        int centerX = width * (2 * i + 1) / (2 * MATERIAL_COUNT);
        int x = centerX - MeasureText(materialNames[i], 14) / 2;
        DrawText(materialNames[i], x, labelY, 14, WHITE);
    }
    DrawText(ibl ? "IBL ON (1 toggles)" : "IBL OFF (1 toggles)", 22, height - 48, 14,
             (Color){ 175, 220, 255, 255 });
    DrawText("G: ray tracing    1: toggle IBL    *glass refraction is approximate",
             22, height - 24, 14, LIGHTGRAY);
}

static Texture2D CreatePaintColorMap(void)
{
    const int size = 128;
    Color *pixels = (Color *)malloc((size_t)size * (size_t)size * sizeof(Color));
    if (!pixels) return (Texture2D){ 0 };

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            unsigned int cellX = (unsigned int)(x / 16);
            unsigned int cellY = (unsigned int)(y / 16);
            unsigned int hash = cellX * 73856093u ^ cellY * 19349663u;
            int edgeX = x % 16;
            int edgeY = y % 16;
            bool chip = (hash % 4u == 0u) && edgeX > 1 && edgeX < 14 && edgeY > 1 && edgeY < 14;
            pixels[y * size + x] = chip
                ? (Color){ 185, 195, 205, 255 }
                : (Color){ 235, 38, 24, 255 };
        }
    }

    Image image = { pixels, size, size, 1, 0 };
    Texture2D texture = LoadTextureFromImageSRGB(image);
    free(pixels);
    return texture;
}

static Texture2D CreatePaintMetalRoughnessMap(void)
{
    const int size = 128;
    Color *pixels = (Color *)malloc((size_t)size * (size_t)size * sizeof(Color));
    if (!pixels) return (Texture2D){ 0 };

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            unsigned int cellX = (unsigned int)(x / 16);
            unsigned int cellY = (unsigned int)(y / 16);
            unsigned int hash = cellX * 73856093u ^ cellY * 19349663u;
            int edgeX = x % 16;
            int edgeY = y % 16;
            bool exposedMetal = (hash % 4u == 0u) && edgeX > 1 && edgeX < 14 && edgeY > 1 && edgeY < 14;
            /* glTF convention: G=roughness, B=metallic; R is unused here. */
            pixels[y * size + x] = exposedMetal
                ? (Color){ 255, 205, 255, 255 }
                : (Color){ 255, 70, 0, 255 };
        }
    }

    Image image = { pixels, size, size, 1, 0 };
    Texture2D texture = LoadTextureFromImage(image); /* linear data, not sRGB */
    free(pixels);
    return texture;
}

int main(void)
{
    const int windowWidth = 1280;
    const int windowHeight = 720;
    if (!InitWindow(windowWidth, windowHeight, "vlcrlib — Materials and Ray Tracing Lab")) return 1;
    SetTargetFPS(60);

    Font uiFont = { 0 };
#ifdef _WIN32
    uiFont = LoadFontEx("C:/Windows/Fonts/segoeui.ttf", 32, 32, 96);
#endif
    if (!uiFont.ok) fprintf(stderr, "Could not load the UI font; overlay text will be unavailable.\n");

    Camera3D camera = { 0 };
    camera.position = (Vector3){ 0.0f, 4.9f, 18.0f };
    camera.target = (Vector3){ 0.0f, 1.15f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 39.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;

    Model spheres[MATERIAL_COUNT];
    Texture2D paintColorMap = CreatePaintColorMap();
    Texture2D paintMetalRoughnessMap = CreatePaintMetalRoughnessMap();
    for (int i = 0; i < MATERIAL_COUNT; ++i) {
        spheres[i] = LoadModelFromMesh(GenMeshSphere(0.82f, 48, 64));
        Material material = CreatePreset(i);
        if (i == 0) {
            SetMaterialTexture(&material, MATERIAL_MAP_ALBEDO, paintColorMap);
            SetMaterialTexture(&material, MATERIAL_MAP_METALLICROUGHNESS, paintMetalRoughnessMap);
        }
        vlcrlib_replace_model_material(spheres[i], 0, material);
    }

    Model floor = LoadModelFromMesh(GenMeshPlane(28.0f, 12.0f, 1, 1));
    Material floorMaterial = LoadMaterialDefault();
    SetMaterialColor(&floorMaterial, (Color){ 28, 34, 44, 255 });
    SetMaterialPBR(&floorMaterial, 0.0f, 0.92f);
    vlcrlib_replace_model_material(floor, 0, floorMaterial);

    vgraphics_SetDirectionalLight(true, (Vector3){ -0.45f, -1.0f, -0.35f },
                                  (Color){ 255, 239, 220, 255 }, 0.95f);
    vgraphics_SetAmbientLight(true, (Color){ 90, 105, 130, 255 }, 0.08f);
    vgraphics_SetPointLight(true, (Vector3){ -5.0f, 5.5f, 3.0f },
                            (Color){ 175, 205, 255, 255 }, 1.6f, 28.0f);
    vgraphics_SetAreaLight(true, (Vector3){ 0.0f, 6.0f, -2.0f },
                           (Vector3){ 0.0f, -1.0f, 0.0f }, 5.0f, 2.0f,
                           (Color){ 255, 220, 175, 255 }, 1.2f);
    vgraphics_SetShadows(true, 1536, 0.0015f, 0.82f);
    vgraphics_SetShadowSoftness(1.5f);
    vgraphics_ComputeDirectionalShadowMatrix(14.0f, 0.5f, 45.0f);

    bool iblLoaded = vgraphics_LoadIBL(NULL);
    bool showIBL = iblLoaded;
    bool showRayTracing = false;
    vgraphics_SetIBLIntensity(showIBL ? 0.85f : 0.0f);
    if (!iblLoaded) fprintf(stderr, "IBL setup failed; using the shader's procedural studio environment.\n");

    while (!WindowShouldClose()) {
        if (IsKeyPressed(SDL_SCANCODE_G)) showRayTracing = !showRayTracing;
        if (IsKeyPressed(SDL_SCANCODE_1) && iblLoaded) showIBL = !showIBL;
        vgraphics_SetIBLIntensity(showIBL ? 0.85f : 0.0f);
        vgraphics_SetCameraPos(camera.position);
        vgraphics_ComputeDirectionalShadowMatrix(14.0f, 0.5f, 45.0f);

        int screenWidth = windowWidth;
        int screenHeight = windowHeight;
        SDL_GetWindowSizeInPixels((SDL_Window *)GetWindowHandle(), &screenWidth, &screenHeight);

        BeginDrawing();
        ClearBackground((Color){ 31, 40, 54, 255 });
        if (showRayTracing) {
            DrawRayTracedDemo((float)GetTime());
        } else {
            BeginMode3D(camera);
            DrawModel(floor, (Vector3){ 0.0f, -0.04f, 0.0f },
                      (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 1.0f, 1.0f, 1.0f }, WHITE);
            for (int i = 0; i < MATERIAL_COUNT; ++i) {
                float x = ((float)i - 3.0f) * 2.75f;
                DrawModel(spheres[i], (Vector3){ x, 0.83f, 0.0f },
                          (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 1.0f, 1.0f, 1.0f }, WHITE);
            }
            EndMode3D();
        }
        DrawOverlay(showRayTracing, vgraphics_IsHardwareRayTracingAvailable(),
                    showIBL, screenWidth, screenHeight);
        EndDrawing();
    }

    for (int i = 0; i < MATERIAL_COUNT; ++i) UnloadModel(spheres[i]);
    UnloadModel(floor);
    UnloadTexture(paintColorMap);
    UnloadTexture(paintMetalRoughnessMap);
    vgraphics_UnloadIBL();
    if (uiFont.ok) UnloadFont(uiFont);
    CloseWindow();
    return 0;
}
