/* Interactive overview of the 3D environment, light, shadow and fog controls. */
#include <vlcrlib.h>
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>

static void sliderControl(VGuiContext *gui, float y, const char *name,
                          float *value, float minValue, float maxValue) {
    char label[80];
    snprintf(label, sizeof(label), "%s: %.2f", name, *value);
    VGuiLabel(gui, (Vector2){986, y}, label, gui->theme.text);
    VGuiSlider(gui, (Rectangle){986, y + 20, 260, 24}, value, minValue, maxValue);
}

int main(void) {
    if (!InitWindow(1280, 720, "vlcrlib — Environment, Lighting and Fog")) return 1;
    SetTargetFPS(60);

    Camera3D camera = {0};
    camera.position = (Vector3){ 7.0f, 5.0f, 9.0f };
    camera.target = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 50.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;

    bool directional = true, point = true, spot = true, area = true;
    bool ambient = true, shadows = true, spotShadows = false, pointShadows = false;
    bool fog = false, ibl = false;
    float dirIntensity = 1.5f, pointIntensity = 5.0f, spotIntensity = 7.0f;
    float areaIntensity = 5.0f, ambientIntensity = 0.28f;
    float fogStart = 5.0f, fogEnd = 24.0f, fogDensity = 0.0f;
    float iblIntensity = 1.0f;
    bool iblLoaded = false;
    bool iblFailed = false;
    VGuiContext gui = {0};
    gui.theme = VGuiDefaultTheme();
    Font uiFont = {0};
#ifdef _WIN32
    uiFont = LoadFontEx("C:/Windows/Fonts/segoeui.ttf", 16, 16, 96);
#endif
    Color sky = (Color){ 105, 160, 205, 255 };

    Model floor = LoadModelFromMesh(GenMeshPlane(24.0f, 24.0f, 1, 1));
    Model sphere = LoadModelFromMesh(GenMeshSphere(0.8f, 32, 32));
    Model cube = LoadModelFromMesh(GenMeshCube(1.4f, 2.0f, 1.4f));
    Model smallCube = LoadModelFromMesh(GenMeshCube(1.2f, 1.4f, 1.2f));
    Material floorMat = LoadMaterialDefault();
    SetMaterialColor(&floorMat, (Color){75, 82, 88, 255});
    SetMaterialPBR(&floorMat, 0.05f, 0.9f);
    vlcrlib_replace_model_material(floor, 0, floorMat);
    Material sphereMat = LoadMaterialDefault();
    SetMaterialColor(&sphereMat, (Color){200, 145, 60, 255});
    SetMaterialPBR(&sphereMat, 0.75f, 0.22f);
    vlcrlib_replace_model_material(sphere, 0, sphereMat);
    Material cubeMat = LoadMaterialDefault();
    SetMaterialColor(&cubeMat, (Color){190, 125, 80, 255});
    SetMaterialPBR(&cubeMat, 0.05f, 0.55f);
    vlcrlib_replace_model_material(cube, 0, cubeMat);
    Material smallMat = LoadMaterialDefault();
    SetMaterialColor(&smallMat, (Color){95, 150, 205, 255});
    SetMaterialPBR(&smallMat, 0.15f, 0.28f);
    vlcrlib_replace_model_material(smallCube, 0, smallMat);
    Model lightMarker = LoadModelFromMesh(GenMeshSphere(0.16f, 16, 16));
    Material markerMat = LoadMaterialDefault();
    SetMaterialColor(&markerMat, YELLOW);
    SetMaterialEmission(&markerMat, YELLOW, 6.0f);
    vlcrlib_replace_model_material(lightMarker, 0, markerMat);

    vgraphics_SetDirectionalLight(directional, (Vector3){-0.5f, -1.0f, -0.35f},
                                  (Color){255, 238, 210, 255}, dirIntensity);
    vgraphics_SetAmbientLight(ambient, (Color){100, 125, 155, 255}, ambientIntensity);
    vgraphics_SetPointLight(point, (Vector3){-2, 2, 0}, ORANGE, pointIntensity, 9.0f);
    vgraphics_SetSpotLight(spot, (Vector3){2, 5, 2}, (Vector3){-0.3f, -1, -0.2f},
                           SKYBLUE, spotIntensity, 12.0f, 0.35f, 0.8f);
    vgraphics_SetAreaLight(area, (Vector3){0, 5, -2}, (Vector3){0, -1, 0},
                           3.0f, 2.0f, (Color){255, 220, 170, 255}, areaIntensity);
    vgraphics_SetShadows(shadows, 1024, 0.002f, 0.8f);
    vgraphics_SetShadowSoftness(1.5f);
    vgraphics_ComputeDirectionalShadowMatrix(12.0f, 0.5f, 40.0f);

    while (!WindowShouldClose()) {
        float time = (float)GetTime();
        if (IsKeyPressed(SDL_SCANCODE_1)) directional = !directional;
        if (IsKeyPressed(SDL_SCANCODE_2)) point = !point;
        if (IsKeyPressed(SDL_SCANCODE_3)) spot = !spot;
        if (IsKeyPressed(SDL_SCANCODE_4)) area = !area;
        if (IsKeyPressed(SDL_SCANCODE_F)) fog = !fog;
        if (IsKeyPressed(SDL_SCANCODE_S)) shadows = !shadows;
        if (IsKeyPressed(SDL_SCANCODE_5)) spotShadows = !spotShadows;
        if (IsKeyPressed(SDL_SCANCODE_6)) pointShadows = !pointShadows;
        if (IsKeyPressed(SDL_SCANCODE_E)) {
            ibl = !ibl;
            if (ibl) iblFailed = false;
        }

        /* IBL rebuilds GPU images/descriptors: do it between frames, never
           inside BeginDrawing/EndDrawing (that would invalidate the command
           buffer that already references the old descriptor sets). */
        if (ibl && !iblLoaded && !iblFailed) {
            iblLoaded = vgraphics_LoadIBL(NULL);
            if (!iblLoaded) iblFailed = true; /* don't retry every frame */
        }

        Vector3 pointPos = { -2.0f + cosf(time) * 1.2f, 2.0f, sinf(time) * 1.2f };
        vgraphics_SetDirectionalLight(directional, (Vector3){-0.5f, -1.0f, -0.35f},
                                      (Color){255, 238, 210, 255}, dirIntensity);
        vgraphics_SetAmbientLight(ambient, (Color){100, 125, 155, 255}, ambientIntensity);
        vgraphics_SetPointLight(point, pointPos, ORANGE, pointIntensity, 9.0f);
        vgraphics_SetSpotLight(spot, (Vector3){2, 5, 2}, (Vector3){-0.3f, -1, -0.2f},
                               SKYBLUE, spotIntensity, 12.0f, 0.35f, 0.8f);
        vgraphics_SetAreaLight(area, (Vector3){0, 5, -2}, (Vector3){0, -1, 0},
                               3.0f, 2.0f, (Color){255, 220, 170, 255}, areaIntensity);
        vgraphics_SetShadows(shadows, 1024, 0.002f, 0.8f);
        vgraphics_SetSpotShadows(spotShadows, 1024, 0.0025f, 0.85f);
        vgraphics_SetPointShadows(pointShadows, 512, 0.004f, 0.85f);
        vgraphics_ComputeDirectionalShadowMatrix(12.0f, 0.5f, 40.0f);
        vgraphics_SetFog(fog, sky, fogStart, fogEnd, fogDensity);
        vgraphics_SetCameraPos(camera.position);

        BeginDrawing();
        ClearBackground(sky);
        BeginMode3D(camera);
        DrawModel(floor, (Vector3){0, -0.02f, 0}, (Vector3){0, 0, 0}, (Vector3){1, 1, 1}, WHITE);
        DrawGrid(24, 1.0f);
        DrawModel(cube, (Vector3){0, 1, 0}, (Vector3){0, time * 0.2f, 0}, (Vector3){1, 1, 1}, WHITE);
        DrawModel(sphere, (Vector3){-2, 0.8f, 0}, (Vector3){0, 0, 0}, (Vector3){1, 1, 1}, WHITE);
        DrawModel(smallCube, (Vector3){2, 0.7f, -1}, (Vector3){0, 0.3f, 0}, (Vector3){1, 1, 1}, WHITE);
        DrawModel(lightMarker, pointPos, (Vector3){0, 0, 0}, (Vector3){1, 1, 1}, WHITE);
        EndMode3D();

        DrawText("Environment and lighting lab", 24, 18, 26, WHITE);
        DrawText("Environment / Lighting / Fog", 24, 18, 26, WHITE);
        VGuiBeginFrame(&gui, uiFont, GetFrameTime());
        VGuiPanel(&gui, (Rectangle){970, 18, 292, 698}, "Render controls");
        VGuiCheckBox(&gui, (Rectangle){986, 56, 260, 28}, &directional, "Directional / sun");
        VGuiCheckBox(&gui, (Rectangle){986, 88, 260, 28}, &point, "Point light");
        VGuiCheckBox(&gui, (Rectangle){986, 120, 260, 28}, &spot, "Spot light");
        VGuiCheckBox(&gui, (Rectangle){986, 152, 260, 28}, &area, "Area light");
        VGuiCheckBox(&gui, (Rectangle){986, 184, 260, 28}, &ambient, "Ambient fill");
        VGuiCheckBox(&gui, (Rectangle){986, 216, 260, 28}, &shadows, "Directional shadows");
        VGuiCheckBox(&gui, (Rectangle){986, 248, 260, 28}, &spotShadows, "Spot shadows (key 5)");
        VGuiCheckBox(&gui, (Rectangle){986, 280, 260, 28}, &pointShadows, "Point shadows (key 6)");
        VGuiCheckBox(&gui, (Rectangle){986, 312, 260, 28}, &fog, "Fog");
        VGuiCheckBox(&gui, (Rectangle){986, 344, 260, 28}, &ibl, "IBL reflections");
        sliderControl(&gui, 366, "Sun intensity", &dirIntensity, 0.0f, 5.0f);
        sliderControl(&gui, 408, "Point intensity", &pointIntensity, 0.0f, 12.0f);
        sliderControl(&gui, 450, "Spot intensity", &spotIntensity, 0.0f, 12.0f);
        sliderControl(&gui, 492, "Area intensity", &areaIntensity, 0.0f, 12.0f);
        sliderControl(&gui, 534, "Ambient intensity", &ambientIntensity, 0.0f, 1.0f);
        sliderControl(&gui, 576, "Fog start", &fogStart, 0.0f, 30.0f);
        sliderControl(&gui, 618, "Fog end", &fogEnd, 5.0f, 60.0f);
        if (ibl && iblLoaded) sliderControl(&gui, 660, "IBL intensity", &iblIntensity, 0.0f, 2.0f);
        VGuiEndFrame(&gui);
        vgraphics_SetIBLIntensity(ibl && iblLoaded ? iblIntensity : 0.0f);
        EndDrawing();
    }

    UnloadModel(floor);
    UnloadModel(sphere);
    UnloadModel(cube);
    UnloadModel(smallCube);
    UnloadModel(lightMarker);
    vgraphics_UnloadIBL();
    if (uiFont.ok) UnloadFont(uiFont);
    CloseWindow();
    return 0;
}
