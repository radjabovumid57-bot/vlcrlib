/* First-person game example using vlcrlib's Bullet Character3D controller. */
#include <vlcrlib.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_scancode.h>
#include <math.h>
#include <stdio.h>

static float clampf(float value, float low, float high) {
    return value < low ? low : (value > high ? high : value);
}

int main(void) {
    if (!InitWindow(1280, 720, "vlcrlib — First Person Character (Bullet)")) return 1;
    SetTargetFPS(60);

    SDL_Window *window = (SDL_Window *)GetWindowHandle();
    if (window) SDL_SetWindowRelativeMouseMode(window, true);

    PhysicsWorld3D *world = CreatePhysicsWorld3D((Vector3){ 0.0f, -9.81f, 0.0f });
    if (!world) {
        fprintf(stderr, "Could not create the Bullet physics world.\n");
        CloseWindow();
        return 1;
    }

    PhysicsBody3D *ground = CreatePhysicsBody3D(world, PHYS_SHAPE_BOX3D,
        (Vector3){ 0.0f, -0.15f, -8.0f }, (Vector3){ 15.0f, 0.15f, 21.0f },
        PHYS_BODY_STATIC3D, 0.0f, 0.85f, 0.0f);
    const Vector3 obstacleCenters[] = {
        { 0.0f, 0.55f, -5.0f }, { 3.0f, 1.0f, -9.0f },
        { -3.0f, 0.5f, -12.0f }, { 0.0f, 1.5f, -16.0f },
        { 6.0f, 0.75f, -5.0f }, { -6.0f, 0.75f, -7.0f }
    };
    const Vector3 obstacleHalfExtents[] = {
        { 2.0f, 0.55f, 0.7f }, { 0.8f, 1.0f, 0.8f },
        { 1.4f, 0.5f, 1.0f }, { 2.0f, 1.5f, 0.5f },
        { 0.7f, 0.75f, 2.0f }, { 0.7f, 0.75f, 2.0f }
    };
    PhysicsBody3D *obstacles[6] = { 0 };
    if (!ground) {
        fprintf(stderr, "Could not create the ground collider.\n");
        DestroyPhysicsWorld3D(world);
        CloseWindow();
        return 1;
    }
    for (int i = 0; i < 6; ++i) {
        obstacles[i] = CreatePhysicsBody3D(world, PHYS_SHAPE_BOX3D,
            obstacleCenters[i], obstacleHalfExtents[i],
            PHYS_BODY_STATIC3D, 0.0f, 0.8f, 0.0f);
        if (!obstacles[i]) {
            fprintf(stderr, "Could not create FPS obstacle %d.\n", i);
            for (int j = 0; j < 6; ++j)
                if (obstacles[j]) DestroyPhysicsBody3D(obstacles[j]);
            DestroyPhysicsBody3D(ground);
            DestroyPhysicsWorld3D(world);
            CloseWindow();
            return 1;
        }
    }

    PhysicsBody3D *shootable = CreatePhysicsBody3D(world, PHYS_SHAPE_SPHERE3D,
        (Vector3){ 0.0f, 0.7f, -9.0f }, (Vector3){ 0.7f, 0.0f, 0.0f },
        PHYS_BODY_DYNAMIC3D, 3.0f, 0.65f, 0.05f);
    if (!shootable) {
        fprintf(stderr, "Could not create the ray target.\n");
        for (int i = 0; i < 6; ++i) DestroyPhysicsBody3D(obstacles[i]);
        DestroyPhysicsBody3D(ground);
        DestroyPhysicsWorld3D(world);
        CloseWindow();
        return 1;
    }

    Vector3 spawn = { 0.0f, 1.3f, 2.0f };
    Character3D *player = CreateCharacter3D(world, spawn, 0.38f, 0.9f, 0.30f);
    if (!player) {
        fprintf(stderr, "Could not create the Bullet character controller.\n");
        for (int i = 0; i < 6; ++i) DestroyPhysicsBody3D(obstacles[i]);
        DestroyPhysicsBody3D(ground);
        DestroyPhysicsWorld3D(world);
        CloseWindow();
        return 1;
    }
    SetCharacter3DMaxSlope(player, 0.78539816339f);
    Mesh characterCylinder = GenMeshCylinder(0.38f, 0.9f, 24);
    Mesh characterCaps = GenMeshSphere(0.38f, 12, 20);

    float yaw = 0.0f;
    float pitch = 0.0f;
    Vector3 shotStart = { 0.0f, 0.0f, 0.0f };
    Vector3 shotEnd = { 0.0f, 0.0f, 0.0f };
    float shotTimer = 0.0f;
    const float mouseSensitivity = 0.0025f;
    const float moveSpeed = 5.0f;
    const float jumpSpeed = 5.0f;
    Camera3D camera = { 0 };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 75.0f;
    camera.nearPlane = 0.05f;
    camera.farPlane = 180.0f;

    while (!WindowShouldClose()) {
        float dt = clampf(GetFrameTime(), 0.0f, 1.0f / 30.0f);
        float mouseX = 0.0f, mouseY = 0.0f;
        SDL_GetRelativeMouseState(&mouseX, &mouseY);
        yaw += mouseX * mouseSensitivity;
        pitch = clampf(pitch - mouseY * mouseSensitivity, -1.48f, 1.48f);

        Vector3 forward = {
            sinf(yaw) * cosf(pitch),
            sinf(pitch),
            -cosf(yaw) * cosf(pitch)
        };
        Vector3 right = { cosf(yaw), 0.0f, sinf(yaw) };
        Vector3 playerPosition = GetCharacter3DPosition(player);
        camera.position = (Vector3){
            playerPosition.x - sinf(yaw) * 4.5f + right.x * 0.75f,
            playerPosition.y + 1.8f,
            playerPosition.z + cosf(yaw) * 4.5f + right.z * 0.75f
        };
        camera.target = (Vector3){ playerPosition.x + forward.x * 10.0f,
                                   playerPosition.y + 0.5f + forward.y * 10.0f,
                                   playerPosition.z + forward.z * 10.0f };
        if (shotTimer > 0.0f) shotTimer -= dt;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector3 aimDirection = Vector3Normalize(
                Vector3Subtract(camera.target, camera.position));
            shotStart = camera.position;
            Vector3 rayOrigin = (Vector3){ shotStart.x + aimDirection.x * 0.2f,
                                            shotStart.y + aimDirection.y * 0.2f,
                                            shotStart.z + aimDirection.z * 0.2f };
            PhysicsRayHit3D hit = RaycastPhysics3D(world, rayOrigin, aimDirection, 80.0f);
            shotEnd = hit.hit ? hit.point :
                (Vector3){ rayOrigin.x + aimDirection.x * 80.0f,
                           rayOrigin.y + aimDirection.y * 80.0f,
                           rayOrigin.z + aimDirection.z * 80.0f };
            shotTimer = 0.12f;
            if (hit.hit && hit.body == shootable) {
                ApplyPhysicsBody3DImpulseAtPoint(shootable,
                    (Vector3){ aimDirection.x * 12.0f, aimDirection.y * 12.0f,
                               aimDirection.z * 12.0f },
                    Vector3Subtract(hit.point, GetPhysicsBody3DPosition(shootable)));
            }
        }
        Vector3 walk = { 0.0f, 0.0f, 0.0f };
        if (IsKeyDown(SDL_SCANCODE_W)) walk = (Vector3){ walk.x + forward.x, walk.y, walk.z + forward.z };
        if (IsKeyDown(SDL_SCANCODE_S)) walk = (Vector3){ walk.x - forward.x, walk.y, walk.z - forward.z };
        if (IsKeyDown(SDL_SCANCODE_D)) walk = (Vector3){ walk.x + right.x, walk.y, walk.z + right.z };
        if (IsKeyDown(SDL_SCANCODE_A)) walk = (Vector3){ walk.x - right.x, walk.y, walk.z - right.z };
        walk.y = 0.0f;
        float walkLength = sqrtf(walk.x * walk.x + walk.z * walk.z);
        if (walkLength > 1.0f) {
            walk.x /= walkLength;
            walk.z /= walkLength;
        }
        SetCharacter3DWalkDirection(player,
            (Vector3){ walk.x * moveSpeed * dt, 0.0f, walk.z * moveSpeed * dt });

        if (IsKeyPressed(SDL_SCANCODE_SPACE) && IsCharacter3DOnGround(player))
            JumpCharacter3D(player, jumpSpeed);
        if (IsKeyPressed(SDL_SCANCODE_R)) WarpCharacter3D(player, spawn);

        StepPhysicsWorld3D(world, dt);
        playerPosition = GetCharacter3DPosition(player);
        camera.position = (Vector3){
            playerPosition.x - sinf(yaw) * 4.5f + right.x * 0.75f,
            playerPosition.y + 1.8f,
            playerPosition.z + cosf(yaw) * 4.5f + right.z * 0.75f
        };
        camera.target = (Vector3){ playerPosition.x + forward.x * 10.0f,
                                   playerPosition.y + 0.5f + forward.y * 10.0f,
                                   playerPosition.z + forward.z * 10.0f };

        BeginDrawing();
        ClearBackground((Color){ 106, 155, 205, 255 });
        BeginMode3D(camera);
        DrawCube((Vector3){ 0.0f, -0.15f, -8.0f },
                 (Vector3){ 30.0f, 0.3f, 42.0f }, (Color){ 70, 82, 96, 255 });
        DrawGrid(30, 1.0f);
        for (int i = 0; i < 6; ++i) {
            DrawCube(obstacleCenters[i],
                (Vector3){ obstacleHalfExtents[i].x * 2.0f,
                           obstacleHalfExtents[i].y * 2.0f,
                           obstacleHalfExtents[i].z * 2.0f },
                (Color){ 180, 123, 73, 255 });
        }
        Matrix cylinderTransform = MatrixTranslate(playerPosition.x,
            playerPosition.y, playerPosition.z);
        Matrix capTopTransform = MatrixTranslate(playerPosition.x,
            playerPosition.y + 0.45f, playerPosition.z);
        Matrix capBottomTransform = MatrixTranslate(playerPosition.x,
            playerPosition.y - 0.45f, playerPosition.z);
        DrawMesh(characterCylinder, (Texture2D){ 0 },
                 (Color){ 55, 105, 165, 255 }, cylinderTransform);
        DrawMesh(characterCaps, (Texture2D){ 0 },
                 (Color){ 65, 125, 195, 255 }, capTopTransform);
        DrawMesh(characterCaps, (Texture2D){ 0 },
                 (Color){ 65, 125, 195, 255 }, capBottomTransform);
        Vector3 targetPosition = GetPhysicsBody3DPosition(shootable);
        DrawSphere(targetPosition, 0.7f, (Color){ 210, 145, 70, 255 });
        if (shotTimer > 0.0f) {
            DrawLine3D(shotStart, shotEnd, (Color){ 255, 225, 105, 255 });
            DrawSphere(shotEnd, 0.08f, (Color){ 255, 90, 55, 255 });
        }
        EndMode3D();

        bool grounded = IsCharacter3DOnGround(player);
        DrawText("THIRD-PERSON SHOOTER — BULLET", 20, 18, 25, WHITE);
        DrawText("WASD move | Mouse aim | Click shoot | Space jump | R respawn | Esc quit",
                 20, 52, 17, WHITE);
        DrawText(grounded ? "Grounded" : "Airborne", 20, 78, 17,
                 grounded ? (Color){ 130, 245, 160, 255 }
                          : (Color){ 255, 200, 100, 255 });
        DrawLine(634, 360, 646, 360, WHITE);
        DrawLine(640, 354, 640, 366, WHITE);
        EndDrawing();
    }

    SDL_SetWindowRelativeMouseMode(window, false);
    UnloadMesh(characterCylinder);
    UnloadMesh(characterCaps);
    DestroyCharacter3D(player);
    DestroyPhysicsBody3D(shootable);
    for (int i = 0; i < 6; ++i) DestroyPhysicsBody3D(obstacles[i]);
    DestroyPhysicsBody3D(ground);
    DestroyPhysicsWorld3D(world);
    CloseWindow();
    return 0;
}
