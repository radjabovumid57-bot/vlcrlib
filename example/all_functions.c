#include <vlcrlib.h>
#include <stdio.h>
#include <string.h>

static void exercise_math(void) {
    Vector2 a2 = {1.0f, 2.0f};
    Vector2 b2 = {3.0f, 4.0f};
    Vector3 a3 = {1.0f, 2.0f, 3.0f};
    Vector3 b3 = {4.0f, 5.0f, 6.0f};
    Matrix identity = MatrixIdentity();
    Quaternion q = QuaternionIdentity();

    (void)Vector2Add(a2, b2);
    (void)Vector2Subtract(a2, b2);
    (void)Vector2Scale(a2, 2.0f);
    (void)Vector2Length(a2);
    (void)Vector2Dot(a2, b2);
    (void)Vector2Normalize(a2);
    (void)Vector2Distance(a2, b2);
    (void)Vector2Lerp(a2, b2, 0.5f);
    (void)Vector3Add(a3, b3);
    (void)Vector3Subtract(a3, b3);
    (void)Vector3Scale(a3, 2.0f);
    (void)Vector3Length(a3);
    (void)Vector3Dot(a3, b3);
    (void)Vector3Cross(a3, b3);
    (void)Vector3Normalize(a3);
    (void)Vector3Distance(a3, b3);
    (void)Vector3Lerp(a3, b3, 0.5f);
    (void)Vector3Reflect(a3, b3);
    (void)MatrixTranslate(1.0f, 2.0f, 3.0f);
    (void)MatrixScale(1.0f, 2.0f, 3.0f);
    (void)MatrixMultiply(identity, identity);
    (void)MatrixRotateX(0.1f);
    (void)MatrixRotateY(0.1f);
    (void)MatrixRotateZ(0.1f);
    (void)MatrixRotateXYZ(a3);
    (void)MatrixRotate(a3, 0.1f);
    (void)QuaternionNormalize(q);
    (void)QuaternionFromAxisAngle(a3, 0.1f);
    (void)QuaternionMultiply(q, q);
    (void)QuaternionToMatrix(q);
}

static void exercise_files(void) {
    unsigned int bytes = 0;
    unsigned char *data = LoadFileData("all_functions.c", &bytes);
    char *text = LoadFileText("all_functions.c");
    (void)FileExists("all_functions.c");
    (void)DirectoryExists(".");
    (void)GetFileSizeBytes("all_functions.c");
    (void)GetFileModTime("all_functions.c");
    (void)SaveFileData("all_functions.tmp", "ok", 2);
    (void)SaveFileText("all_functions.txt", "vlcrlib");
    (void)CreateDirectoryIfMissing("all_functions_data");
    UnloadFileData(data);
    UnloadFileText(text);
}

static void exercise_bezier(void) {
    Vector2 p0 = {0, 0}, p1 = {1, 2}, p2 = {2, 1}, p3 = {3, 0};
    Vector3 q0 = {0, 0, 0}, q1 = {1, 2, 0}, q2 = {2, 1, 0}, q3 = {3, 0, 0};
    (void)GetBezierQuadPoint(p0, p1, p2, 0.5f);
    (void)GetBezierCubicPoint(p0, p1, p2, p3, 0.5f);
    (void)GetBezierQuadPoint3D(q0, q1, q2, 0.5f);
    (void)GetBezierCubicPoint3D(q0, q1, q2, q3, 0.5f);
    (void)GetBezierCubicTangent(p0, p1, p2, p3, 0.5f);
    (void)GetBezierCubicTangent3D(q0, q1, q2, q3, 0.5f);
    (void)GetBezierCubicLength(p0, p1, p2, p3, 16);
}

static void exercise_network(void) {
    if (!InitNetwork()) return;
    NetSocket tcp = NetCreateTCP();
    NetSocket udp = NetCreateUDP();
    (void)NetSetNonBlocking(&tcp, true);
    (void)NetSetNonBlocking(&udp, true);
    NetClose(&tcp);
    NetClose(&udp);
    CloseNetwork();
}

static void exercise_physics(void) {
    PhysicsWorld3D *world3d = CreatePhysicsWorld3D((Vector3){0, -9.8f, 0});
    PhysicsBody3D *body3d = CreatePhysicsBody3D(
        world3d, PHYS_SHAPE_BOX3D, (Vector3){0, 1, 0},
        (Vector3){0.5f, 0.5f, 0.5f}, PHYS_BODY_DYNAMIC3D,
        1.0f, 0.5f, 0.2f);
    SetPhysicsBody3DPosition(body3d, (Vector3){0, 2, 0});
    SetPhysicsBody3DVelocity(body3d, (Vector3){1, 0, 0});
    (void)GetPhysicsBody3DPosition(body3d);
    (void)GetPhysicsBody3DVelocity(body3d);
    ApplyPhysicsBody3DImpulse(body3d, (Vector3){0, 1, 0});
    ApplyPhysicsBody3DForce(body3d, (Vector3){0, 1, 0});
    StepPhysicsWorld3D(world3d, 1.0f / 60.0f);
    (void)RaycastPhysics3D(world3d, (Vector3){0, 4, 0}, (Vector3){0, -1, 0}, 10.0f);
    DestroyPhysicsBody3D(body3d);
    DestroyPhysicsWorld3D(world3d);

    PhysicsWorld2D *world2d = CreatePhysicsWorld2D((Vector2){0, 9.8f});
    PhysicsBody2D *body2d = CreatePhysicsBody2D(
        world2d, PHYS_SHAPE_BOX2D, (Vector2){0, 1},
        (Vector2){0.5f, 0.5f}, PHYS_BODY_DYNAMIC2D,
        1.0f, 0.5f, 0.2f);
    SetPhysicsBody2DPosition(body2d, (Vector2){0, 2});
    SetPhysicsBody2DVelocity(body2d, (Vector2){1, 0});
    (void)GetPhysicsBody2DPosition(body2d);
    (void)GetPhysicsBody2DRotation(body2d);
    (void)GetPhysicsBody2DVelocity(body2d);
    ApplyPhysicsBody2DImpulse(body2d, (Vector2){0, 1});
    StepPhysicsWorld2D(world2d, 1.0f / 60.0f);
    (void)RaycastPhysics2D(world2d, (Vector2){0, 4}, (Vector2){0, -1}, 10.0f);
    DestroyPhysicsBody2D(body2d);
    DestroyPhysicsWorld2D(world2d);
}

int main(void) {
    exercise_math();
    exercise_files();
    exercise_bezier();
    exercise_network();

    if (!InitWindow(1280, 720, "vlcrlib all functions")) return 1;
    SetTargetFPS(60);
    SetWindowTitle("vlcrlib API example");
    SetWindowPosition(100, 100);
    SetWindowSize(1280, 720);
    SetExitKey(27);
    (void)IsWindowResized();
    (void)GetTime();
    (void)GetMousePosition();
    (void)GetMouseWheelMove();
    (void)IsGamepadAvailable(0);
    (void)IsGamepadButtonPressed(0, 0);
    (void)IsGamepadButtonDown(0, 0);
    (void)GetGamepadAxisMovement(0, 0);
    SetMousePosition(640, 360);

    unsigned char pixel[4] = {255, 255, 255, 255};
    Image image = {pixel, 1, 1, 1, 0};
    Image memoryImage = {0};
    ImageFlipVertical(&image);
    ImageFlipHorizontal(&image);
    ExportImage(image, "all_functions.png");
    Texture2D texture = LoadTextureFromImage(image);
    UpdateTexture(texture, pixel);

    Mesh cube = GenMeshCube(1, 1, 1);
    Mesh sphere = GenMeshSphere(1, 16, 16);
    Mesh plane = GenMeshPlane(2, 2, 2, 2);
    Mesh cylinder = GenMeshCylinder(1, 2, 16);
    Mesh torus = GenMeshTorus(1, 0.25f, 16, 8);
    Material material = LoadMaterialDefault();
    SetMaterialColor(&material, WHITE);
    SetMaterialPBR(&material, 0.2f, 0.8f);
    SetMaterialTexture(&material, MATERIAL_MAP_ALBEDO, texture);
    Model model = LoadModelFromMesh(cube);
    (void)LoadModel("missing.obj");
    (void)LoadSkinnedModel("missing.gltf");
    (void)GetSkinnedModelAnimationCount((SkinnedModel){0});
    (void)GetSkinnedModelAnimationDuration((SkinnedModel){0}, 0);
    UnloadSkinnedModel((SkinnedModel){0});

    RenderTexture2D target = LoadRenderTexture(320, 180);
    BeginTextureMode(target);
    EndTextureMode();
    UnloadRenderTexture(target);

    Shader shader = LoadShader("shaders/2d.vert.spv", "shaders/2d.frag.spv");
    UnloadShader(shader);

    ParticleSystem particles = CreateParticleSystem(32);
    SetParticleEmitter(&particles, (Vector2){640, 360});
    SetParticleEmissionRate(&particles, 10);
    SetParticleTexture(&particles, texture);
    EmitParticle(&particles, (Vector2){640, 360}, (Vector2){1, 0}, 1, 4, RED, BLUE);
    StartParticleEmission(&particles);
    UpdateParticleSystem(&particles, 1.0f / 60.0f);
    StopParticleEmission(&particles);

    exercise_physics();

    while (!WindowShouldClose()) {
        Camera2D camera2d = {0};
        Camera3D camera3d = {0};
        camera2d.zoom = 1;
        camera3d.position = (Vector3){4, 4, 4};
        camera3d.target = (Vector3){0, 0, 0};
        camera3d.up = (Vector3){0, 1, 0};
        camera3d.fovy = 45;
        camera3d.nearPlane = 0.1f;
        camera3d.farPlane = 100;
        UpdateCamera2D(&camera2d, 0);
        UpdateCamera3D(&camera3d, 0);
        UpdateCamera(&camera2d, 0);

        BeginDrawing();
        ClearBackground(DARKGRAY);
        BeginMode3D(camera3d);
        DrawGrid(10, 1);
        DrawMesh(cube, texture, WHITE, MatrixIdentity());
        DrawModel(model, (Vector3){0, 0, 0}, (Vector3){0, 0, 0}, (Vector3){1, 1, 1}, WHITE);
        DrawCube((Vector3){0, 0.5f, 0}, (Vector3){1, 1, 1}, RED);
        DrawSphere((Vector3){2, 1, 0}, 1, BLUE);
        DrawLine3D((Vector3){-2, 0, 0}, (Vector3){2, 0, 0}, WHITE);
        DrawBezierCubic3D((Vector3){-2, 0, 0}, (Vector3){-1, 2, 0},
                          (Vector3){1, -1, 0}, (Vector3){2, 0, 0}, GOLD, 16);
        EndMode3D();

        BeginMode2D(camera2d);
        DrawPixel(10, 10, WHITE);
        DrawLine(20, 20, 100, 20, GREEN);
        DrawLineEx((Vector2){20, 30}, (Vector2){100, 30}, 2, GREEN);
        DrawCircle(150, 80, 30, BLUE);
        DrawCircleV((Vector2){220, 80}, 30, RED);
        DrawRectangle(260, 50, 80, 50, GOLD);
        DrawRectangleRec((Rectangle){350, 50, 80, 50}, GREEN);
        DrawRectanglePro((Rectangle){440, 75, 80, 50}, (Vector2){40, 25}, 15, BLUE);
        DrawRectangleLines(530, 50, 80, 50, WHITE);
        DrawTriangle((Vector2){650, 50}, (Vector2){620, 110}, (Vector2){680, 110}, PINK);
        DrawPoly((Vector2){760, 80}, 6, 30, 0, ORANGE);
        DrawBezierQuad((Vector2){800, 120}, (Vector2){850, 20}, (Vector2){900, 120}, 2, WHITE, 16);
        DrawBezierCubic((Vector2){920, 120}, (Vector2){950, 20}, (Vector2){980, 20}, (Vector2){1010, 120}, 2, GOLD, 16);
        DrawTexture(texture, 1040, 20, WHITE);
        DrawTextureV(texture, (Vector2){1040, 30}, WHITE);
        DrawTextureRec(texture, (Rectangle){0, 0, 1, 1}, (Vector2){1040, 40}, WHITE);
        DrawTextureEx(texture, (Vector2){1040, 50}, 0, 1, WHITE);
        DrawTexturePro(texture, (Rectangle){0, 0, 1, 1},
                   (Rectangle){1040, 60, 1, 1}, (Vector2){0, 0}, 0, WHITE);
        DrawText("vlcrlib all functions", 20, 150, 24, WHITE);
        (void)MeasureText("vlcrlib", 24);
        (void)MeasureTextEx(GetFontDefault(), "vlcrlib", 24, 1);
        EndMode2D();
        DrawParticleSystem(&particles);
        EndDrawing();
    }

    DestroyParticleSystem(&particles);
    UnloadModel(model);
    UnloadMaterial(material);
    UnloadMesh(cube);
    UnloadMesh(sphere);
    UnloadMesh(plane);
    UnloadMesh(cylinder);
    UnloadMesh(torus);
    UnloadFont(GetFontDefault());
    UnloadTexture(texture);
    UnloadImage(memoryImage);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
