#include <vlcrlib.h>
#include "vlight.h"
#include <math.h>

static Material MakeEmitter(Color color, float strength) {
    Material material = LoadMaterialDefault();
    SetMaterialColor(&material, color);
    SetMaterialEmission(&material, color, strength);
    return material;
}

int main(void) {
    if (!InitWindow(1280, 720, "vlcrlib 3D lights")) return 1;
    SetTargetFPS(60);

    VGuiContext gui = {0};
    gui.theme = VGuiDefaultTheme();
    Font uiFont = {0};
#ifdef _WIN32
    uiFont = LoadFontEx("C:/Windows/Fonts/segoeui.ttf", 18, 18, 96);
#endif

    bool showDirectional = true;
    bool showPoint = true;
    bool showSpot = true;
    bool showAmbient = true;

    Camera3D camera = {0};
    camera.position = (Vector3){8, 5, 9};
    camera.target = (Vector3){0, 1, 0};
    camera.up = (Vector3){0, 1, 0};
    camera.fovy = 55.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;

    VLight ambient = VLightAmbient((Color){70, 80, 110, 255}, 0.25f);
    VLight sun = VLightDirectional((Vector3){-0.4f, -1.0f, -0.3f}, WHITE, 1.0f);
    VLight point = VLightPoint((Vector3){-3, 2, 0}, ORANGE, 1.0f, 7.0f);
    VLight spot = VLightSpot((Vector3){3, 4, 2}, (Vector3){-0.5f, -1, -0.4f},
                             SKYBLUE, 1.0f, 10.0f,
                             22.0f * VLCRLIB_DEG2RAD,
                             55.0f * VLCRLIB_DEG2RAD);

    Model floor = LoadModelFromMesh(GenMeshPlane(18, 18, 1, 1));
    Model sphere = LoadModelFromMesh(GenMeshSphere(1.2f, 32, 32));
    Model emitterA = LoadModelFromMesh(GenMeshSphere(0.22f, 16, 16));
    Model emitterB = LoadModelFromMesh(GenMeshSphere(0.22f, 16, 16));
    Model sunMarker = LoadModelFromMesh(GenMeshSphere(0.32f, 16, 16));

    Material floorMaterial = LoadMaterialDefault();
    SetMaterialColor(&floorMaterial, (Color){70, 78, 94, 255});
    SetMaterialPBR(&floorMaterial, 0.05f, 0.85f);
    vlcrlib_replace_model_material(floor, 0, floorMaterial);

    Material sphereMaterial = LoadMaterialDefault();
    SetMaterialColor(&sphereMaterial, (Color){42, 112, 210, 255});
    SetMaterialPBR(&sphereMaterial, 0.15f, 0.28f);
    vlcrlib_replace_model_material(sphere, 0, sphereMaterial);
    vlcrlib_replace_model_material(emitterA, 0, MakeEmitter(ORANGE, 5.0f));
    vlcrlib_replace_model_material(emitterB, 0, MakeEmitter(SKYBLUE, 5.0f));
    vlcrlib_replace_model_material(sunMarker, 0, MakeEmitter(YELLOW, 7.0f));

    while (!WindowShouldClose()) {
        float time = (float)GetTime();
        float orbit = time * 0.32f;
        camera.position.x = 9.0f * cosf(orbit);
        camera.position.z = 9.0f * sinf(orbit);
        point.position = (Vector3){-3.0f + cosf(time) * 1.4f, 2.2f, sinf(time) * 1.5f};
        spot.position = (Vector3){3.0f, 4.0f + sinf(time * 1.4f) * 0.4f, 2.0f};
        Vector3 sunPosition = {0.0f, 5.5f, -3.5f};

        vgraphics_SetDirectionalLight(showDirectional, sun.direction, sun.color, sun.intensity);
        vgraphics_SetPointLight(showPoint, point.position, point.color,
                    point.intensity, point.range);
        vgraphics_SetSpotLight(showSpot, spot.position, spot.direction, spot.color,
                       spot.intensity, spot.range, spot.innerCone, spot.outerCone);
        vgraphics_SetAmbientLight(showAmbient, ambient.color, ambient.intensity);

        BeginDrawing();
        ClearBackground((Color){10, 14, 24, 255});
        BeginMode3D(camera);
        if (showAmbient) {
            DrawModel(floor, (Vector3){0, 0, 0}, (Vector3){0, 0, 0}, (Vector3){1, 1, 1}, WHITE);
        }
        DrawModel(sphere, (Vector3){0, 1.3f, 0}, (Vector3){0, time * 0.4f, 0},
                  (Vector3){1, 1, 1}, WHITE);
        if (showPoint)
            DrawModel(emitterA, point.position, (Vector3){0, 0, 0}, (Vector3){1, 1, 1}, WHITE);
        if (showSpot)
            DrawModel(emitterB, spot.position, (Vector3){0, 0, 0}, (Vector3){1, 1, 1}, WHITE);
        if (showDirectional)
            DrawModel(sunMarker, sunPosition, (Vector3){0, 0, 0}, (Vector3){1, 1, 1}, WHITE);
        DrawGrid(18, 1.0f);
        EndMode3D();

        Color ambientPreview = VLightColorAt3D(ambient, (Vector3){0, 1, 0}, (Vector3){0, 1, 0});
        Color sunPreview = VLightColorAt3D(sun, (Vector3){0, 1, 0}, (Vector3){0, 1, 0});
        VGuiBeginFrame(&gui, uiFont, GetFrameTime());
        VGuiPanel(&gui, (Rectangle){980, 24, 270, 220}, "Show light types");
        VGuiCheckBox(&gui, (Rectangle){1000, 68, 230, 30}, &showDirectional, "Directional / Sun");
        VGuiCheckBox(&gui, (Rectangle){1000, 104, 230, 30}, &showPoint, "Point light");
        VGuiCheckBox(&gui, (Rectangle){1000, 140, 230, 30}, &showSpot, "Spot light");
        VGuiCheckBox(&gui, (Rectangle){1000, 176, 230, 30}, &showAmbient, "Ambient light");
        VGuiEndFrame(&gui);
        DrawText("3D LIGHTS", 28, 24, 30, WHITE);
        if (showDirectional) DrawText("Sun / directional light", 30, 64, 18, YELLOW);
        if (showPoint) DrawText("Point light", 30, 92, 18, ORANGE);
        if (showSpot) DrawText("Spot light", 30, 120, 18, SKYBLUE);
        if (showAmbient) DrawText("Ambient light", 30, 148, 18, ambientPreview);
        DrawText("Light contribution preview", 30, 650, 18, LIGHTGRAY);
        DrawText("Sun", 30, 680, 18, sunPreview);
        EndDrawing();
    }

    UnloadModel(floor);
    UnloadModel(sphere);
    UnloadModel(emitterA);
    UnloadModel(emitterB);
    UnloadModel(sunMarker);
    if (uiFont.ok) UnloadFont(uiFont);
    CloseWindow();
    return 0;
}
