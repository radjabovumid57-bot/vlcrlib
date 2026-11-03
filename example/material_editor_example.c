/* Small interactive PBR material editor built with vlcrlib's own VGui. */
#include <vlcrlib.h>
#include <SDL3/SDL_scancode.h>
#include <stdio.h>
#include <string.h>

#define WINDOW_W 1280
#define WINDOW_H 800
#define MATERIAL_COUNT 3
#define MAX_EDITOR_TEXTURES 16

static const char *materialNames[MATERIAL_COUNT] = { "Paint", "Brushed metal", "Plastic" };

static void setPreset(Material *m, int index) {
    if (!m) return;
    if (index == 0) {
        SetMaterialColor(m, (Color){ 190, 48, 38, 255 });
        SetMaterialPBR(m, 0.15f, 0.24f);
        SetMaterialIOR(m, 1.5f);
        SetMaterialEmission(m, BLACK, 0.0f);
    } else if (index == 1) {
        SetMaterialColor(m, (Color){ 175, 185, 198, 255 });
        SetMaterialPBR(m, 0.92f, 0.2f);
        SetMaterialIOR(m, 1.5f);
        SetMaterialEmission(m, BLACK, 0.0f);
    } else {
        SetMaterialColor(m, (Color){ 55, 125, 210, 255 });
        SetMaterialPBR(m, 0.0f, 0.38f);
        SetMaterialIOR(m, 1.46f);
        SetMaterialEmission(m, BLACK, 0.0f);
    }
}

static void labeledSlider(VGuiContext *gui, float x, float y, float width,
                          const char *label, float *value,
                          float minValue, float maxValue, const char *format) {
    char text[96];
    snprintf(text, sizeof(text), "%s  ", label);
    VGuiLabel(gui, (Vector2){ x, y }, text, gui->theme.text);
    snprintf(text, sizeof(text), format, *value);
    VGuiLabel(gui, (Vector2){ x + width - 62.0f, y }, text, gui->theme.accent);
    VGuiSlider(gui, (Rectangle){ x, y + 19.0f, width, 24.0f }, value, minValue, maxValue);
}

int main(void) {
    if (!InitWindow(WINDOW_W, WINDOW_H, "vlcrlib — VGui PBR Material Editor")) return 1;
    SetTargetFPS(60);

    Font font = {0};
#ifdef _WIN32
    font = LoadFontEx("C:/Windows/Fonts/segoeui.ttf", 18, 32, 95);
#endif
    VGuiContext gui = {0};
    gui.theme = VGuiDefaultTheme();

    Camera3D camera = {0};
    camera.position = (Vector3){ 0.0f, 3.4f, 8.2f };
    camera.target = (Vector3){ 0.0f, 0.9f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 48.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;

    Model spheres[MATERIAL_COUNT];
    Material materials[MATERIAL_COUNT];
    for (int i = 0; i < MATERIAL_COUNT; ++i) {
        spheres[i] = LoadModelFromMesh(GenMeshSphere(0.82f, 32, 40));
        materials[i] = LoadMaterialDefault();
        setPreset(&materials[i], i);
        vlcrlib_replace_model_material(spheres[i], 0, materials[i]);
    }
    Model floor = LoadModelFromMesh(GenMeshPlane(14.0f, 14.0f, 1, 1));
    Material floorMat = LoadMaterialDefault();
    SetMaterialColor(&floorMat, (Color){ 95, 105, 118, 255 });
    SetMaterialPBR(&floorMat, 0.0f, 0.82f);
    vlcrlib_replace_model_material(floor, 0, floorMat);

    bool shadows = true;
    bool floorVisible = true;
    int selected = 0;
    int selectedMap = 0;
    int editorTab = 0;
    int effectsTab = 0;
    const char *mapNames[] = { "Albedo", "Normal", "Metal/Rough", "AO", "Emission", "Specular", "Clearcoat" };
    const int mapTypes[] = { MATERIAL_MAP_ALBEDO, MATERIAL_MAP_NORMAL,
                             MATERIAL_MAP_METALLICROUGHNESS, MATERIAL_MAP_AO,
                             MATERIAL_MAP_EMISSION, MATERIAL_MAP_SPECULAR,
                             MATERIAL_MAP_CLEARCOAT };
    const char *editorTabNames[] = { "Surface", "Reflectance", "Effects", "Textures" };
    const char *effectsTabNames[] = { "Emission", "Volume", "Surface FX" };
    const char *alphaModes[] = { "Opaque", "Mask", "Blend" };
    char texturePath[256] = "assets/texture.png";
    char textureStatus[96] = "No texture loaded";
    Texture2D editorTextures[MAX_EDITOR_TEXTURES] = {0};
    int editorTextureCount = 0;
    bool doubleSided[MATERIAL_COUNT] = { false, false, false };
    bool unlit[MATERIAL_COUNT] = { false, false, false };
    bool quit = false;

    vgraphics_SetDirectionalLight(true, (Vector3){ -0.45f, -1.0f, -0.35f },
                                  (Color){ 255, 242, 222, 255 }, 1.8f);
    vgraphics_SetAmbientLight(true, (Color){ 95, 112, 140, 255 }, 0.3f);
    vgraphics_SetShadows(true, 1024, 0.002f, 0.78f);
    vgraphics_SetShadowSoftness(1.4f);
    vgraphics_ComputeDirectionalShadowMatrix(10.0f, 0.5f, 35.0f);

    while (!WindowShouldClose() && !quit) {
        float dt = GetFrameTime();
        Material *material = &materials[selected];
        Color picked = material->baseColor;

        /* Shadow setup can create/refresh descriptors on first use; never do
           that between BeginDrawing and EndDrawing. */
        vgraphics_SetShadows(shadows, 1024, 0.002f, 0.78f);

        BeginDrawing();
        ClearBackground((Color){ 30, 36, 46, 255 });

        BeginMode3D(camera);
        if (floorVisible) {
            DrawModel(floor, (Vector3){ 0, -0.86f, 0 }, (Vector3){0,0,0},
                      (Vector3){1,1,1}, WHITE);
            DrawGrid(14, 1.0f);
        }
        for (int i = 0; i < MATERIAL_COUNT; ++i) {
            float x = ((float)i - 1.0f) * 2.35f;
            DrawModel(spheres[i], (Vector3){ x, 0.02f, 0 },
                      (Vector3){ 0, (float)GetTime() * 0.12f, 0 },
                      (Vector3){1,1,1}, WHITE);
        }
        EndMode3D();

        VGuiBeginFrame(&gui, font, dt);
        VGuiPanel(&gui, (Rectangle){ 820, 14, 446, 772 }, "Material Editor");
        VGuiLabel(&gui, (Vector2){ 838, 52 }, "Select preview material", gui.theme.mutedText);
        for (int i = 0; i < MATERIAL_COUNT; ++i) {
            if (VGuiButton(&gui, (Rectangle){ 838.0f + i * 137.0f, 76, 128, 32 },
                           materialNames[i]))
                selected = i;
        }

        material = &materials[selected];
        const int editorTabCount = 4;
        VGuiTabs(&gui, (Rectangle){ 838, 118, 404, 34 }, editorTabNames,
                 editorTabCount, &editorTab);
        if (editorTab == 0) {
            VGuiLabel(&gui, (Vector2){ 838, 162 }, "Base color", gui.theme.mutedText);
            if (VGuiColorPicker(&gui, (Rectangle){ 838, 184, 190, 356 }, &picked)) {
                SetMaterialColor(material, picked);
                material->baseColorFactor[0] = picked.r / 255.0f;
                material->baseColorFactor[1] = picked.g / 255.0f;
                material->baseColorFactor[2] = picked.b / 255.0f;
                material->baseColorFactor[3] = picked.a / 255.0f;
            }
            /* The picker includes RGBA controls below its hue wheel. Keep the
               linear factors synchronized without duplicating four sliders. */
            material->baseColorFactor[0] = picked.r / 255.0f;
            material->baseColorFactor[1] = picked.g / 255.0f;
            material->baseColorFactor[2] = picked.b / 255.0f;
            material->baseColorFactor[3] = picked.a / 255.0f;
            labeledSlider(&gui, 1040, 166, 202, "Metallic", &material->metallic, 0, 1, "%.2f");
            labeledSlider(&gui, 1040, 211, 202, "Roughness", &material->roughness, 0.02f, 1, "%.2f");
            labeledSlider(&gui, 1040, 256, 202, "IOR", &material->ior, 1, 2.5f, "%.2f");
            labeledSlider(&gui, 1040, 301, 202, "Normal scale", &material->normalScale, 0, 4, "%.2f");
            /* Color picker uses its full 350px height for the hue wheel,
               mode tabs, RGBA/HSV sliders and preview swatch. Keep surface
               controls below it to prevent those controls from overlapping. */
            labeledSlider(&gui, 838, 548, 190, "AO strength", &material->aoStrength, 0, 1, "%.2f");
            VGuiLabel(&gui, (Vector2){ 1040, 548 }, "Alpha mode", gui.theme.mutedText);
            VGuiCombo(&gui, (Rectangle){ 1040, 571, 202, 30 }, "", alphaModes, 3,
                      &material->alphaMode);
            labeledSlider(&gui, 838, 596, 190, "Alpha cutoff", &material->alphaCutoff, 0, 1, "%.2f");
            VGuiCheckBox(&gui, (Rectangle){ 1040, 606, 202, 28 },
                         &doubleSided[selected], "Double-sided");
            VGuiCheckBox(&gui, (Rectangle){ 1040, 636, 202, 28 },
                         &unlit[selected], "Unlit material");
            material->unlit = unlit[selected] ? 1 : 0;
            material->doubleSided = doubleSided[selected] ? 1 : 0;
            SetMaterialBaseColorFactor(material, material->baseColorFactor[0],
                material->baseColorFactor[1], material->baseColorFactor[2], material->baseColorFactor[3]);
        } else if (editorTab == 1) {
            VGuiLabel(&gui, (Vector2){ 838, 164 }, "Specular", gui.theme.mutedText);
            labeledSlider(&gui, 838, 190, 190, "Factor", &material->specularFactor, 0, 1, "%.2f");
            labeledSlider(&gui, 838, 235, 190, "Color R", &material->specularColor[0], 0, 1, "%.2f");
            labeledSlider(&gui, 838, 280, 190, "Color G", &material->specularColor[1], 0, 1, "%.2f");
            labeledSlider(&gui, 838, 325, 190, "Color B", &material->specularColor[2], 0, 1, "%.2f");
            VGuiLabel(&gui, (Vector2){ 1040, 164 }, "Clearcoat", gui.theme.mutedText);
            labeledSlider(&gui, 1040, 190, 202, "Factor", &material->clearcoatFactor, 0, 1, "%.2f");
            labeledSlider(&gui, 1040, 235, 202, "Roughness", &material->clearcoatRoughness, 0, 1, "%.2f");
            VGuiLabel(&gui, (Vector2){ 1040, 298 }, "Sheen", gui.theme.mutedText);
            labeledSlider(&gui, 1040, 325, 202, "Color R", &material->sheenColor[0], 0, 1, "%.2f");
            labeledSlider(&gui, 1040, 370, 202, "Color G", &material->sheenColor[1], 0, 1, "%.2f");
            labeledSlider(&gui, 1040, 415, 202, "Color B", &material->sheenColor[2], 0, 1, "%.2f");
            labeledSlider(&gui, 1040, 460, 202, "Roughness", &material->sheenRoughness, 0, 1, "%.2f");
        } else if (editorTab == 2) {
            VGuiTabs(&gui, (Rectangle){ 838, 164, 404, 32 }, effectsTabNames, 3, &effectsTab);
            if (effectsTab == 0) {
                VGuiLabel(&gui, (Vector2){ 838, 207 }, "Emissive surface color and intensity", gui.theme.mutedText);
                float emissionR = (float)material->emissionColor.r;
                float emissionG = (float)material->emissionColor.g;
                float emissionB = (float)material->emissionColor.b;
                labeledSlider(&gui, 838, 238, 404, "Red", &emissionR, 0, 255, "%.0f");
                labeledSlider(&gui, 838, 283, 404, "Green", &emissionG, 0, 255, "%.0f");
                labeledSlider(&gui, 838, 328, 404, "Blue", &emissionB, 0, 255, "%.0f");
                material->emissionColor.r = (unsigned char)emissionR;
                material->emissionColor.g = (unsigned char)emissionG;
                material->emissionColor.b = (unsigned char)emissionB;
                labeledSlider(&gui, 838, 373, 404, "Emission strength", &material->emissionStrength, 0, 16, "%.2f");
                labeledSlider(&gui, 838, 418, 404, "KHR emissive strength", &material->emissiveStrengthKhr, 0, 16, "%.2f");
            } else if (effectsTab == 1) {
                VGuiLabel(&gui, (Vector2){ 838, 207 }, "Transmission and light absorption", gui.theme.mutedText);
                labeledSlider(&gui, 838, 238, 404, "Transmission", &material->transmissionFactor, 0, 1, "%.2f");
                labeledSlider(&gui, 838, 283, 404, "Thickness", &material->thicknessFactor, 0, 10, "%.2f");
                labeledSlider(&gui, 838, 328, 404, "Attenuation distance", &material->attenuationDistance, 0, 100, "%.2f");
                labeledSlider(&gui, 838, 373, 404, "Attenuation red", &material->attenuationColor[0], 0, 1, "%.2f");
                labeledSlider(&gui, 838, 418, 404, "Attenuation green", &material->attenuationColor[1], 0, 1, "%.2f");
                labeledSlider(&gui, 838, 463, 404, "Attenuation blue", &material->attenuationColor[2], 0, 1, "%.2f");
            } else {
                VGuiLabel(&gui, (Vector2){ 838, 207 }, "Anisotropy", gui.theme.mutedText);
                labeledSlider(&gui, 838, 238, 404, "Strength", &material->anisotropyStrength, 0, 1, "%.2f");
                labeledSlider(&gui, 838, 283, 404, "Rotation (radians)", &material->anisotropyRotation, -3.14159f, 3.14159f, "%.2f");
                VGuiLabel(&gui, (Vector2){ 838, 332 }, "Iridescence", gui.theme.mutedText);
                labeledSlider(&gui, 838, 360, 404, "Factor", &material->iridescenceFactor, 0, 1, "%.2f");
                labeledSlider(&gui, 838, 405, 404, "IOR", &material->iridescenceIor, 1, 2.5f, "%.2f");
                labeledSlider(&gui, 838, 450, 404, "Thickness minimum (nm)", &material->iridescenceThicknessMin, 0, 1200, "%.0f");
                labeledSlider(&gui, 838, 495, 404, "Thickness maximum (nm)", &material->iridescenceThicknessMax, 0, 1200, "%.0f");
                labeledSlider(&gui, 838, 540, 404, "Dispersion", &material->dispersion, 0, 10, "%.2f");
            }
        } else {
            VGuiLabel(&gui, (Vector2){ 838, 164 }, "Material texture maps", gui.theme.mutedText);
            VGuiCombo(&gui, (Rectangle){ 838, 190, 404, 30 }, "Map", mapNames, 7, &selectedMap);
            VGuiInputText(&gui, (Rectangle){ 838, 230, 404, 30 }, texturePath,
                          (int)sizeof(texturePath));
            if (VGuiButton(&gui, (Rectangle){ 838, 270, 194, 32 }, "Load / assign")) {
                if (editorTextureCount < MAX_EDITOR_TEXTURES) {
                    Texture2D texture = LoadTexture(texturePath);
                    if (texture.id) {
                        editorTextures[editorTextureCount++] = texture;
                        SetMaterialTexture(material, mapTypes[selectedMap], texture);
                        snprintf(textureStatus, sizeof(textureStatus), "%s assigned (%dx%d)",
                                 mapNames[selectedMap], texture.width, texture.height);
                    } else snprintf(textureStatus, sizeof(textureStatus), "Could not load texture");
                } else snprintf(textureStatus, sizeof(textureStatus), "Texture slot limit reached");
            }
            if (VGuiButton(&gui, (Rectangle){ 1040, 270, 202, 32 }, "Clear selected map"))
                SetMaterialTexture(material, mapTypes[selectedMap], (Texture2D){0});
            VGuiText(&gui, (Rectangle){ 838, 316, 404, 48 }, textureStatus, 14.0f,
                     gui.theme.mutedText);
            VGuiLabel(&gui, (Vector2){ 838, 380 }, "Maps: albedo, normal, metallic/roughness, AO, emission, specular and clearcoat.",
                      gui.theme.mutedText);
        }

        VGuiCheckBox(&gui, (Rectangle){ 838, 668, 190, 28 }, &shadows, "Shadows");
        VGuiCheckBox(&gui, (Rectangle){ 1048, 668, 194, 28 }, &floorVisible, "Floor + grid");
        VGuiLabel(&gui, (Vector2){ 838, 700 }, "Edit the selected material; previews update live.",
                  gui.theme.mutedText);
        VGuiLabel(&gui, (Vector2){ 838, 720 }, "Texture paths are relative to the example folder.",
                  gui.theme.mutedText);
        if (VGuiButton(&gui, (Rectangle){ 838, 746, 190, 28 }, "Reset preset"))
            setPreset(material, selected);
        if (VGuiButton(&gui, (Rectangle){ 1048, 746, 194, 28 }, "Exit")) quit = true;
        VGuiEndFrame(&gui);

        /* DrawModel stores a material value in the model, so copy edited
           values back each frame. The id is unchanged; ownership stays with
           the corresponding model. */
        vlcrlib_replace_model_material(spheres[selected], 0, *material);
        EndDrawing();
    }

    for (int i = 0; i < MATERIAL_COUNT; ++i) UnloadModel(spheres[i]);
    UnloadModel(floor);
    /* Textures are left owned by the graphics device and released when the
       device closes; this avoids destroying an image referenced by in-flight
       frame commands after a live map replacement. */
    (void)editorTextures;
    (void)editorTextureCount;
    if (font.ok) UnloadFont(font);
    CloseWindow();
    return 0;
}
