/* Minimal PBR emissive-material example. */
#include <vlcrlib.h>
#include <math.h>

int main(void)
{
    if (!InitWindow(1000, 650, "vlcrlib — PBR Emission")) return 1;
    SetTargetFPS(60);

    Camera3D camera = {0};
    camera.position = (Vector3){ 5.0f, 3.5f, 7.0f };
    camera.target = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;

    Model floor = LoadModelFromMesh(GenMeshPlane(12.0f, 12.0f, 1, 1));
    Model emitter = LoadModelFromMesh(GenMeshSphere(0.75f, 32, 32));
    Model comparison = LoadModelFromMesh(GenMeshSphere(0.75f, 32, 32));

    Material floorMaterial = LoadMaterialDefault();
    SetMaterialColor(&floorMaterial, (Color){ 95, 105, 120, 255 });
    SetMaterialPBR(&floorMaterial, 0.0f, 0.82f);
    vlcrlib_replace_model_material(floor, 0, floorMaterial);

    Material emissiveMaterial = LoadMaterialDefault();
    SetMaterialColor(&emissiveMaterial, (Color){ 255, 100, 24, 255 });
    SetMaterialPBR(&emissiveMaterial, 0.0f, 0.3f);
    SetMaterialEmission(&emissiveMaterial, (Color){ 255, 55, 8, 255 }, 5.0f);
    vlcrlib_replace_model_material(emitter, 0, emissiveMaterial);

    Material regularMaterial = LoadMaterialDefault();
    SetMaterialColor(&regularMaterial, (Color){ 255, 100, 24, 255 });
    SetMaterialPBR(&regularMaterial, 0.0f, 0.3f);
    vlcrlib_replace_model_material(comparison, 0, regularMaterial);

    vgraphics_SetDirectionalLight(true, (Vector3){ -0.4f, -1.0f, -0.5f },
                                  (Color){ 230, 235, 255, 255 }, 1.2f);
    vgraphics_SetAmbientLight(true, (Color){ 100, 115, 140, 255 }, 0.12f);

    while (!WindowShouldClose()) {
        float time = (float)GetTime();
        BeginDrawing();
        ClearBackground((Color){ 24, 29, 40, 255 });
        BeginMode3D(camera);
        DrawModel(floor, (Vector3){ 0.0f, 0.0f, 0.0f },
                  (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 1.0f, 1.0f, 1.0f }, WHITE);
        DrawModel(emitter, (Vector3){ -1.25f, 1.1f, 0.0f },
                  (Vector3){ 0.0f, time * 0.35f, 0.0f },
                  (Vector3){ 1.0f, 1.0f, 1.0f }, WHITE);
        DrawModel(comparison, (Vector3){ 1.25f, 1.1f, 0.0f },
                  (Vector3){ 0.0f, time * 0.35f, 0.0f },
                  (Vector3){ 1.0f, 1.0f, 1.0f }, WHITE);
        EndMode3D();

        DrawText("PBR emissive material", 24, 20, 28, WHITE);
        DrawText("Emission: warm orange, strength 5.0", 24, 58, 18,
                 (Color){ 255, 190, 145, 255 });
        DrawText("LEFT: emissive   RIGHT: same base color, no emission", 24, 90, 16, LIGHTGRAY);
        DrawText("Emission makes a surface appear bright; it does not cast light onto other objects.",
                 24, 612, 15, LIGHTGRAY);
        EndDrawing();
    }

    UnloadModel(floor);
    UnloadModel(emitter);
    UnloadModel(comparison);
    CloseWindow();
    return 0;
}
