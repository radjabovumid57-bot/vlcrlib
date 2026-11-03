#include <vlcrlib.h>
#include <math.h>

static Color ScaleColor(Color color, float scale) {
    if (scale < 0.0f) scale = 0.0f;
    if (scale > 1.0f) scale = 1.0f;
    return (Color){
        (unsigned char)(color.r * scale),
        (unsigned char)(color.g * scale),
        (unsigned char)(color.b * scale),
        color.a
    };
}

static Material MakeLightMaterial(Color color, float strength) {
    Material material = LoadMaterialDefault();
    SetMaterialColor(&material, color);
    SetMaterialPBR(&material, 0.0f, 0.18f);
    SetMaterialEmission(&material, color, strength);
    return material;
}

static void DrawLightDisc(Vector2 center, float radius, Color color) {
    DrawCircleV(center, radius, (Color){color.r, color.g, color.b, 10});
    DrawCircleV(center, radius * 0.72f, (Color){color.r, color.g, color.b, 16});
    DrawCircleV(center, radius * 0.38f, (Color){color.r, color.g, color.b, 32});
    DrawCircleV(center, 7.0f, (Color){255, 255, 255, 220});
}

int main(void) {
    if (!InitWindow(1280, 720, "vlcrlib light lab")) return 1;
    SetTargetFPS(60);

    Camera3D camera = {0};
    camera.position = (Vector3){8.0f, 5.5f, 9.0f};
    camera.target = (Vector3){0.0f, 1.0f, 0.0f};
    camera.up = (Vector3){0.0f, 1.0f, 0.0f};
    camera.fovy = 55.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;

    Model floor = LoadModelFromMesh(GenMeshPlane(18.0f, 18.0f, 1, 1));
    Model center = LoadModelFromMesh(GenMeshSphere(1.25f, 32, 32));
    Model ring = LoadModelFromMesh(GenMeshTorus(1.1f, 0.3f, 32, 16));
    Model lampWarm = LoadModelFromMesh(GenMeshSphere(0.22f, 16, 16));
    Model lampCool = LoadModelFromMesh(GenMeshSphere(0.22f, 16, 16));
    Model lampKey = LoadModelFromMesh(GenMeshSphere(0.22f, 16, 16));

    vlcrlib_replace_model_material(floor, 0,
        MakeLightMaterial((Color){72, 80, 94, 255}, 0.0f));
    vlcrlib_replace_model_material(center, 0,
        MakeLightMaterial((Color){44, 112, 210, 255}, 0.0f));
    vlcrlib_replace_model_material(ring, 0,
        MakeLightMaterial((Color){184, 190, 202, 255}, 0.0f));
    vlcrlib_replace_model_material(lampWarm, 0,
        MakeLightMaterial((Color){255, 96, 32, 255}, 5.0f));
    vlcrlib_replace_model_material(lampCool, 0,
        MakeLightMaterial((Color){32, 152, 255, 255}, 5.0f));
    vlcrlib_replace_model_material(lampKey, 0,
        MakeLightMaterial((Color){255, 232, 128, 255}, 7.0f));

    bool show3D = true;
    while (!WindowShouldClose()) {
        float time = (float)GetTime();
        if (IsKeyPressed(' ')) show3D = !show3D;

        BeginDrawing();
        ClearBackground((Color){10, 14, 24, 255});

        if (show3D) {
            float orbit = time * 0.32f;
            camera.position.x = 9.0f * cosf(orbit);
            camera.position.z = 9.0f * sinf(orbit);

            BeginMode3D(camera);
            DrawModel(floor, (Vector3){0, 0, 0}, (Vector3){0, 0, 0},
                      (Vector3){1, 1, 1}, WHITE);
            DrawModel(center, (Vector3){0, 1.35f, 0}, (Vector3){0, time * 0.5f, 0},
                      (Vector3){1, 1, 1}, WHITE);
            DrawModel(ring, (Vector3){0, 1.55f, 0}, (Vector3){time * 0.4f, time * 0.8f, 0},
                      (Vector3){1, 1, 1}, WHITE);

            Vector3 warm = {-3.0f, 2.3f + sinf(time * 1.4f) * 0.35f, 0.5f};
            Vector3 cool = {3.0f, 2.0f + cosf(time * 1.1f) * 0.3f, -0.4f};
            Vector3 key = {0.0f, 4.2f, -2.0f};
            DrawModel(lampWarm, warm, (Vector3){0, 0, 0}, (Vector3){1, 1, 1}, WHITE);
            DrawModel(lampCool, cool, (Vector3){0, 0, 0}, (Vector3){1, 1, 1}, WHITE);
            DrawModel(lampKey, key, (Vector3){0, 0, 0}, (Vector3){1, 1, 1}, WHITE);
            DrawGrid(18, 1.0f);
            EndMode3D();

            DrawText("3D LIGHT WORLD", 28, 24, 30, WHITE);
            DrawText("Warm point   Cool point   Key emissive", 30, 64, 18, LIGHTGRAY);
            DrawText("SPACE: switch to 2D light world", 30, 94, 18, SKYBLUE);
        } else {
            DrawRectangle(0, 0, 1280, 720, (Color){22, 28, 42, 255});
            for (int x = 0; x < 1280; x += 64)
                DrawLine(x, 0, x, 720, (Color){44, 54, 72, 180});
            for (int y = 0; y < 720; y += 64)
                DrawLine(0, y, 1280, y, (Color){44, 54, 72, 180});

            float pulse = 1.0f + 0.12f * sinf(time * 3.0f);
            DrawLightDisc((Vector2){270, 330}, 150.0f * pulse, ORANGE);
            DrawLightDisc((Vector2){640, 260}, 190.0f, SKYBLUE);
            DrawLightDisc((Vector2){1010, 430}, 125.0f, GOLD);
            DrawCircleLines(270, 330, 150.0f * pulse, (Color){255, 130, 64, 180});
            DrawCircleLines(640, 260, 190.0f, (Color){64, 180, 255, 180});
            DrawCircleLines(1010, 430, 125.0f, (Color){255, 220, 96, 180});

            DrawRectangle(420, 535, 440, 90, (Color){12, 16, 28, 220});
            DrawText("2D LIGHT WORLD", 28, 24, 30, WHITE);
            DrawText("Point light       Cool area light       Pulsing key light", 30, 64, 18, LIGHTGRAY);
            DrawText("SPACE: switch to 3D light world", 30, 94, 18, SKYBLUE);
            DrawText("Each disc shows light radius and falloff", 448, 570, 18, WHITE);
        }

        EndDrawing();
    }

    UnloadModel(floor);
    UnloadModel(center);
    UnloadModel(ring);
    UnloadModel(lampWarm);
    UnloadModel(lampCool);
    UnloadModel(lampKey);
    CloseWindow();
    return 0;
}
