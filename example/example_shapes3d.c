#include <vlcrlib.h>

int main(void) {
    if (!InitWindow(1280, 720, "vlcrlib 3D shapes")) return 1;
    SetTargetFPS(60);

    Camera3D camera = {0};
    camera.position = (Vector3){8.0f, 6.0f, 10.0f};
    camera.target = (Vector3){0.0f, 1.0f, 0.0f};
    camera.up = (Vector3){0.0f, 1.0f, 0.0f};
    camera.fovy = 55.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 200.0f;

    Mesh torus = GenMeshTorus(1.0f, 0.25f, 24, 12);
    Mesh cylinder = GenMeshCylinder(0.8f, 2.0f, 24);
    Model torusModel = LoadModelFromMesh(torus);
    Model cylinderModel = LoadModelFromMesh(cylinder);

    while (!WindowShouldClose()) {
        float time = (float)GetTime();
        camera.position.x = 8.0f * cosf(time * 0.35f);
        camera.position.z = 8.0f * sinf(time * 0.35f);

        BeginDrawing();
        ClearBackground((Color){16, 22, 34, 255});
        BeginMode3D(camera);

        DrawGrid(20, 1.0f);
        DrawCube((Vector3){-3.0f, 1.0f, 0.0f}, (Vector3){2, 2, 2}, RED);
        DrawCube((Vector3){0.0f, 1.0f, 0.0f}, (Vector3){2, 2, 2}, BLUE);
        DrawSphere((Vector3){3.0f, 1.2f, 0.0f}, 1.2f, GOLD);
        DrawSphere((Vector3){0.0f, 3.8f, 0.0f}, 0.8f, PINK);
        DrawLine3D((Vector3){-5, 0.02f, 0}, (Vector3){5, 0.02f, 0}, GREEN);
        DrawLine3D((Vector3){0, 0.02f, -5}, (Vector3){0, 0.02f, 5}, SKYBLUE);

        DrawModel(torusModel, (Vector3){-2.5f, 3.0f, -2.0f},
                  (Vector3){0, time, 0}, (Vector3){1, 1, 1}, ORANGE);
        DrawModel(cylinderModel, (Vector3){2.5f, 1.0f, -2.0f},
                  (Vector3){0, time * 0.5f, 0}, (Vector3){1, 1, 1}, LIME);

        DrawBezierCubic3D(
            (Vector3){-4, 0.2f, 3},
            (Vector3){-2, 4, 1},
            (Vector3){2, -1, -1},
            (Vector3){4, 0.2f, 3},
            MAGENTA, 48);

        EndMode3D();
        DrawText("vlcrlib 3D shapes", 28, 24, 28, WHITE);
        DrawText("Cubes  Spheres  Torus  Cylinder  Grid  Bezier", 30, 60, 18, LIGHTGRAY);
        EndDrawing();
    }

    UnloadModel(torusModel);
    UnloadModel(cylinderModel);
    CloseWindow();
    return 0;
}
