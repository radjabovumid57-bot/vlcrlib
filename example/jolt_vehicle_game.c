/*
 * Bullet raycast vehicle sample using vlcrlib's VPhysics Vehicle3D API.
 *
 * Bullet simulates the chassis and four raycast wheels/suspension. The custom
 * tire meshes are drawn from the wheel transforms returned by VPhysics.
 * Controls: W/S throttle and reverse, A/D steer, Space handbrake, R reset.
 */
#include <vlcrlib.h>
#include <SDL3/SDL_scancode.h>
#include <math.h>
#include <stdio.h>

#ifdef _WIN32
__declspec(dllimport) short __stdcall GetAsyncKeyState(int virtualKey);
#endif

#define CHECKPOINT_COUNT 8
#define RACE_LAPS 3

static const Vector3 checkpointPositions[CHECKPOINT_COUNT] = {
    { -24.0f, 0.0f, 18.0f }, { 0.0f, 0.0f, 18.0f },
    {  24.0f, 0.0f, 18.0f }, { 24.0f, 0.0f, 0.0f },
    {  24.0f, 0.0f,-18.0f }, { 0.0f, 0.0f,-18.0f },
    { -24.0f, 0.0f,-18.0f }, {-24.0f, 0.0f, 0.0f }
};

static float clampf(float value, float low, float high) {
    return value < low ? low : (value > high ? high : value);
}

static bool gameKeyDown(int scancode, int virtualKey) {
#ifdef _WIN32
    /* Prefer the library's SDL event state, while keeping the Win32 poll as a
       fallback for windows where the event-state cache misses a held key. */
    bool sdlDown = IsKeyDown(scancode);
    bool win32Down = virtualKey > 0 &&
                     (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
    return sdlDown || win32Down;
#else
    (void)virtualKey;
    return IsKeyDown(scancode);
#endif
}

static bool gameKeyPressed(int scancode, int virtualKey) {
#ifdef _WIN32
    static bool wasDown[256] = { false };
    if (virtualKey < 0 || virtualKey >= 256) return false;
    bool isDown = gameKeyDown(scancode, virtualKey);
    bool pressed = isDown && !wasDown[virtualKey];
    wasDown[virtualKey] = isDown;
    return pressed;
#else
    (void)virtualKey;
    return IsKeyPressed(scancode);
#endif
}

static Vector3 add3(Vector3 a, Vector3 b) {
    return (Vector3){ a.x + b.x, a.y + b.y, a.z + b.z };
}

static Vector3 scale3(Vector3 value, float scale) {
    return (Vector3){ value.x * scale, value.y * scale, value.z * scale };
}

static float dot3(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static Vector3 rotate3(Quaternion rotation, Vector3 value) {
    Matrix matrix = QuaternionToMatrix(rotation);
    return (Vector3){
        matrix.m0 * value.x + matrix.m4 * value.y + matrix.m8  * value.z,
        matrix.m1 * value.x + matrix.m5 * value.y + matrix.m9  * value.z,
        matrix.m2 * value.x + matrix.m6 * value.y + matrix.m10 * value.z
    };
}

static PhysicsBody3D *makeStaticBox(PhysicsWorld3D *world, Vector3 center,
                                    Vector3 halfExtents) {
    return CreatePhysicsBody3D(world, PHYS_SHAPE_BOX3D, center, halfExtents,
                               PHYS_BODY_STATIC3D, 0.0f, 0.9f, 0.0f);
}

static bool createTrackColliders(PhysicsWorld3D *world,
                                 PhysicsBody3D *bodies[9]) {
    /* Plane plus outer bounds and the island walls inside the loop. */
    bodies[0] = CreatePhysicsBody3D(world, PHYS_SHAPE_PLANE3D,
        (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 1.0f, 0.0f },
        PHYS_BODY_STATIC3D, 0.0f, 1.0f, 0.0f);
    const Vector3 centers[8] = {
        { -32.0f, 1.0f, 0.0f }, { 32.0f, 1.0f, 0.0f },
        { 0.0f, 1.0f, -26.0f }, { 0.0f, 1.0f, 26.0f },
        { -18.0f, 1.0f, 0.0f }, { 18.0f, 1.0f, 0.0f },
        { 0.0f, 1.0f, -12.0f }, { 0.0f, 1.0f, 12.0f }
    };
    const Vector3 halfExtents[8] = {
        { 0.5f, 1.0f, 26.0f }, { 0.5f, 1.0f, 26.0f },
        { 32.5f, 1.0f, 0.5f }, { 32.5f, 1.0f, 0.5f },
        { 0.5f, 1.0f, 12.0f }, { 0.5f, 1.0f, 12.0f },
        { 18.5f, 1.0f, 0.5f }, { 18.5f, 1.0f, 0.5f }
    };
    if (!bodies[0]) return false;
    for (int i = 0; i < 8; ++i) {
        bodies[i + 1] = makeStaticBox(world, centers[i], halfExtents[i]);
        if (!bodies[i + 1]) return false;
    }
    return true;
}

static void destroyTrackColliders(PhysicsBody3D *bodies[9]) {
    for (int i = 0; i < 9; ++i) {
        if (bodies[i]) DestroyPhysicsBody3D(bodies[i]);
    }
}

static void drawCar(Mesh cube, Mesh tireMesh, Mesh hubMesh,
                    Mesh treadMesh, Mesh sidewallMesh,
                    Vector3 position, Quaternion rotation,
                    const Vector3 wheelPositions[4],
                    const Quaternion wheelRotations[4]) {
    Matrix pose = MatrixMultiply(MatrixTranslate(position.x, position.y, position.z),
                                 QuaternionToMatrix(rotation));
    DrawMesh(cube, (Texture2D){ 0 }, (Color){ 47, 167, 239, 255 },
             MatrixMultiply(pose, MatrixScale(1.8f, 0.7f, 2.8f)));

    Matrix axleRotation = MatrixRotateZ(1.57079632679f);
    for (int i = 0; i < 4; ++i) {
        Matrix wheelPose = MatrixMultiply(
            MatrixMultiply(
                MatrixTranslate(wheelPositions[i].x, wheelPositions[i].y,
                                wheelPositions[i].z),
                QuaternionToMatrix(wheelRotations[i])),
            axleRotation);
        DrawMesh(tireMesh, (Texture2D){ 0 }, (Color){ 28, 33, 41, 255 }, wheelPose);
        DrawMesh(hubMesh, (Texture2D){ 0 }, (Color){ 160, 171, 180, 255 }, wheelPose);

        /* Repeated raised tread blocks make the tire read clearly at distance. */
        const int treadSegments = 20;
        const float treadRadius = 0.322f;
        const float twoPi = 6.28318530718f;
        const float treadBands[3] = { -0.10f, 0.0f, 0.10f };
        for (int band = 0; band < 3; ++band) {
            for (int segment = 0; segment < treadSegments; ++segment) {
                float angle = (float)segment * twoPi / (float)treadSegments;
                Vector3 treadPosition = {
                    sinf(angle) * treadRadius,
                    treadBands[band],
                    cosf(angle) * treadRadius
                };
                Matrix treadLocal = MatrixMultiply(
                    MatrixMultiply(
                        MatrixTranslate(treadPosition.x, treadPosition.y,
                                        treadPosition.z),
                        MatrixRotateY(angle)),
                    MatrixScale(0.070f, 0.082f, 0.040f));
                Color treadColor = ((segment + band) % 2 == 0)
                    ? (Color){ 39, 44, 50, 255 }
                    : (Color){ 32, 37, 43, 255 };
                DrawMesh(treadMesh, (Texture2D){ 0 }, treadColor,
                         MatrixMultiply(wheelPose, treadLocal));
            }
        }

        /* Sidewall bead rings add molded detail and rotate with the tire. */
        for (int side = -1; side <= 1; side += 2) {
            Matrix sidewallPose = MatrixMultiply(
                wheelPose, MatrixTranslate(0.0f, side * 0.118f, 0.0f));
            DrawMesh(sidewallMesh, (Texture2D){ 0 },
                     (Color){ 72, 78, 84, 255 }, sidewallPose);
        }
    }

    Vector3 hoodPosition = add3(position,
        rotate3(rotation, (Vector3){ 0.0f, 0.32f, 0.68f }));
    Matrix hoodPose = MatrixMultiply(
        MatrixTranslate(hoodPosition.x, hoodPosition.y, hoodPosition.z),
        QuaternionToMatrix(rotation));
    DrawMesh(cube, (Texture2D){ 0 }, (Color){ 94, 198, 248, 255 },
             MatrixMultiply(hoodPose, MatrixScale(1.28f, 0.16f, 0.72f)));
}

static void drawCheckpointGate(Vector3 center, bool active, bool horizontal) {
    Color color = active ? (Color){ 255, 197, 55, 255 }
                         : (Color){ 180, 198, 208, 255 };
    if (horizontal) {
        DrawCube((Vector3){ center.x - 4.3f, 1.4f, center.z }, (Vector3){ 0.18f, 2.8f, 0.18f }, color);
        DrawCube((Vector3){ center.x + 4.3f, 1.4f, center.z }, (Vector3){ 0.18f, 2.8f, 0.18f }, color);
        DrawCube((Vector3){ center.x, 2.75f, center.z }, (Vector3){ 8.8f, 0.18f, 0.18f }, color);
    } else {
        DrawCube((Vector3){ center.x, 1.4f, center.z - 4.3f }, (Vector3){ 0.18f, 2.8f, 0.18f }, color);
        DrawCube((Vector3){ center.x, 1.4f, center.z + 4.3f }, (Vector3){ 0.18f, 2.8f, 0.18f }, color);
        DrawCube((Vector3){ center.x, 2.75f, center.z }, (Vector3){ 0.18f, 0.18f, 8.8f }, color);
    }
}

static void drawCourse(int activeCheckpoint) {
    DrawCube((Vector3){ 0.0f, -0.65f, 0.0f }, (Vector3){ 76.0f, 1.0f, 64.0f },
             (Color){ 45, 112, 67, 255 });

    Color asphalt = (Color){ 59, 66, 74, 255 };
    DrawCube((Vector3){ -24.0f, -0.08f, 0.0f }, (Vector3){ 12.0f, 0.12f, 36.0f }, asphalt);
    DrawCube((Vector3){  24.0f, -0.08f, 0.0f }, (Vector3){ 12.0f, 0.12f, 36.0f }, asphalt);
    DrawCube((Vector3){ 0.0f, -0.08f,  18.0f }, (Vector3){ 36.0f, 0.12f, 12.0f }, asphalt);
    DrawCube((Vector3){ 0.0f, -0.08f, -18.0f }, (Vector3){ 36.0f, 0.12f, 12.0f }, asphalt);
    DrawCube((Vector3){ 0.0f, -0.01f, 0.0f }, (Vector3){ 35.0f, 0.1f, 23.0f },
             (Color){ 45, 112, 67, 255 });

    Color line = (Color){ 224, 216, 178, 255 };
    for (int z = -16; z <= 16; z += 8) {
        DrawCube((Vector3){ -24.0f, 0.0f, (float)z }, (Vector3){ 0.12f, 0.025f, 3.0f }, line);
        DrawCube((Vector3){  24.0f, 0.0f, (float)z }, (Vector3){ 0.12f, 0.025f, 3.0f }, line);
    }
    for (int x = -16; x <= 16; x += 8) {
        DrawCube((Vector3){ (float)x, 0.0f,  18.0f }, (Vector3){ 3.0f, 0.025f, 0.12f }, line);
        DrawCube((Vector3){ (float)x, 0.0f, -18.0f }, (Vector3){ 3.0f, 0.025f, 0.12f }, line);
    }

    for (int i = 0; i < CHECKPOINT_COUNT; ++i) {
        bool horizontalGate = i == 0 || i == 1 || i == 2 ||
                              i == 4 || i == 5 || i == 6;
        drawCheckpointGate(checkpointPositions[i], i == activeCheckpoint, horizontalGate);
    }
}

int main(void) {
    if (!InitWindow(1280, 720, "vlcrlib — Bullet Vehicle Challenge")) return 1;
    SetTargetFPS(60);

    PhysicsWorld3D *world = CreatePhysicsWorld3D((Vector3){ 0.0f, -9.81f, 0.0f });
    if (!world) {
        fprintf(stderr, "Could not create the VPhysics/Bullet world.\n");
        CloseWindow();
        return 1;
    }

    PhysicsBody3D *trackBodies[9] = { 0 };
    if (!createTrackColliders(world, trackBodies)) {
        fprintf(stderr, "Could not create all track colliders.\n");
        destroyTrackColliders(trackBodies);
        DestroyPhysicsWorld3D(world);
        CloseWindow();
        return 1;
    }

    Vehicle3D *car = CreateVehicle3D(world,
        (Vector3){ -24.0f, 0.95f, 0.0f },
        (Vector3){ 0.9f, 0.35f, 1.4f }, 1050.0f);
    if (!car) {
        fprintf(stderr, "Could not create the VPhysics vehicle body.\n");
        destroyTrackColliders(trackBodies);
        DestroyPhysicsWorld3D(world);
        CloseWindow();
        return 1;
    }
    const float wheelX = 0.88f;
    const float wheelZ = 1.02f;
    const Vector3 wheelDown = { 0.0f, -1.0f, 0.0f };
    const Vector3 wheelAxle = { -1.0f, 0.0f, 0.0f };
    AddVehicle3DWheel(car, true,  (Vector3){ -wheelX, -0.18f,  wheelZ },
                      wheelDown, wheelAxle, 0.35f, 0.34f, 3.0f);
    AddVehicle3DWheel(car, true,  (Vector3){  wheelX, -0.18f,  wheelZ },
                      wheelDown, wheelAxle, 0.35f, 0.34f, 3.0f);
    AddVehicle3DWheel(car, false, (Vector3){ -wheelX, -0.18f, -wheelZ },
                      wheelDown, wheelAxle, 0.35f, 0.34f, 3.0f);
    AddVehicle3DWheel(car, false, (Vector3){  wheelX, -0.18f, -wheelZ },
                      wheelDown, wheelAxle, 0.35f, 0.34f, 3.0f);

    Mesh carMesh = GenMeshCube(1.0f, 1.0f, 1.0f);
    Mesh tireMesh = GenMeshCylinder(0.34f, 0.30f, 24);
    Mesh hubMesh = GenMeshCylinder(0.17f, 0.32f, 24);
    Mesh treadMesh = GenMeshCube(1.0f, 1.0f, 1.0f);
    Mesh sidewallMesh = GenMeshTorus(0.27f, 0.012f, 24, 6);
    VInputState input;
    VInputInit(&input);
    VInputBindAxisKeys(&input, "throttle", SDL_SCANCODE_S, SDL_SCANCODE_W);
    /* Positive steering is left (positive Y yaw with this camera/model setup). */
    VInputBindAxisKeys(&input, "steer", SDL_SCANCODE_D, SDL_SCANCODE_A);
    VInputSetAxisSmoothing(&input, "throttle", 0.0f);
    VInputSetAxisSmoothing(&input, "steer", 0.0f);
    VInputBindKey(&input, "handbrake", SDL_SCANCODE_SPACE);
    VInputBindKey(&input, "reset", SDL_SCANCODE_R);

    Camera3D camera = { 0 };
    camera.position = (Vector3){ -31.0f, 8.5f, -10.0f };
    camera.target = (Vector3){ -24.0f, 0.95f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 55.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 220.0f;

    int checkpoint = 0;
    int lap = 0;
    bool finished = false;
    float raceTime = 0.0f;
    Vector3 previousPosition = GetVehicle3DPosition(car);

    while (!WindowShouldClose()) {
        float dt = clampf(GetFrameTime(), 0.0f, 1.0f / 30.0f);
        VInputUpdate(&input, dt);

        if (gameKeyPressed(SDL_SCANCODE_R, 'R') ||
            VInputIsActionPressed(&input, "reset")) {
            ResetVehicle3D(car, (Vector3){ -24.0f, 0.95f, 0.0f },
                           QuaternionIdentity());
            previousPosition = GetVehicle3DPosition(car);
            checkpoint = 0;
            lap = 0;
            raceTime = 0.0f;
            finished = false;
        }

        float keyboardThrottle = (gameKeyDown(SDL_SCANCODE_W, 'W') ? 1.0f : 0.0f) -
                                 (gameKeyDown(SDL_SCANCODE_S, 'S') ? 1.0f : 0.0f);
        float keyboardSteer = (gameKeyDown(SDL_SCANCODE_A, 'A') ? 1.0f : 0.0f) -
                              (gameKeyDown(SDL_SCANCODE_D, 'D') ? 1.0f : 0.0f);
        float throttle = keyboardThrottle != 0.0f
            ? keyboardThrottle : VInputGetAxis(&input, "throttle");
        float steering = keyboardSteer != 0.0f
            ? keyboardSteer : VInputGetAxis(&input, "steer");
        bool handbrake = gameKeyDown(SDL_SCANCODE_SPACE, 0x20) ||
                         VInputIsActionDown(&input, "handbrake");
        if (finished) {
            throttle = 0.0f;
            steering = 0.0f;
        }
        float engineForce = throttle >= 0.0f ? throttle * 3200.0f
                                              : throttle * 2400.0f;
        SetVehicle3DEngineForce(car, finished ? 0.0f : engineForce);
        SetVehicle3DSteering(car, finished ? 0.0f : steering * 0.38f);
        SetVehicle3DBrake(car, handbrake ? 180.0f : (finished ? 180.0f : 0.0f));
        if (!finished) raceTime += dt;

        StepPhysicsWorld3D(world, dt);

        Vector3 position = GetVehicle3DPosition(car);
        Quaternion rotation = GetVehicle3DRotation(car);
        Vector3 velocity = dt > 0.0001f
            ? scale3((Vector3){ position.x - previousPosition.x,
                                position.y - previousPosition.y,
                                position.z - previousPosition.z }, 1.0f / dt)
            : (Vector3){ 0.0f, 0.0f, 0.0f };
        previousPosition = position;
        Vector3 forward = rotate3(rotation, (Vector3){ 0.0f, 0.0f, 1.0f });
        float signedSpeed = dot3(velocity, forward);
        Vector3 wheelPositions[4] = { 0 };
        Quaternion wheelRotations[4] = { 0 };
        for (int wheel = 0; wheel < 4; ++wheel) {
            GetVehicle3DWheelTransform(car, wheel,
                                       &wheelPositions[wheel],
                                       &wheelRotations[wheel]);
        }
        if (!finished && Vector3Distance(position, checkpointPositions[checkpoint]) < 4.8f) {
            ++checkpoint;
            if (checkpoint == CHECKPOINT_COUNT) {
                checkpoint = 0;
                ++lap;
                if (lap >= RACE_LAPS) finished = true;
            }
        }

        Vector3 cameraGoal = add3(position,
            add3(scale3(forward, -9.5f), (Vector3){ 0.0f, 6.0f, 0.0f }));
        camera.position = Vector3Lerp(camera.position, cameraGoal,
                                      clampf(dt * 4.5f, 0.0f, 1.0f));
        camera.target = Vector3Lerp(camera.target,
            add3(position, (Vector3){ 0.0f, 0.8f, 0.0f }),
            clampf(dt * 7.0f, 0.0f, 1.0f));

        BeginDrawing();
        ClearBackground((Color){ 128, 184, 225, 255 });
        BeginMode3D(camera);
        drawCourse(checkpoint);
        drawCar(carMesh, tireMesh, hubMesh, treadMesh, sidewallMesh,
                position, rotation, wheelPositions, wheelRotations);
        EndMode3D();

        float speedKmh = sqrtf(velocity.x * velocity.x + velocity.z * velocity.z) * 3.6f;
        char hud[128];
        snprintf(hud, sizeof(hud), "SPEED %03.0f km/h   LAP %d/%d   TIME %05.1f",
                 speedKmh, lap + 1, RACE_LAPS, raceTime);
        char inputHud[112];
        snprintf(inputHud, sizeof(inputHud),
                 "KEYS W%d S%d A%d D%d  throttle %+.0f steer %+.2f rad  speed %+.1f",
                 gameKeyDown(SDL_SCANCODE_W, 'W'),
                 gameKeyDown(SDL_SCANCODE_S, 'S'),
                 gameKeyDown(SDL_SCANCODE_A, 'A'),
                 gameKeyDown(SDL_SCANCODE_D, 'D'), throttle,
                 steering * 0.38f, signedSpeed);
        DrawText("VPHYSICS BULLET VEHICLE", 24, 20, 26, WHITE);
        DrawText(hud, 24, 54, 19, (Color){ 240, 245, 250, 255 });
        DrawText("W/S throttle   A/D steer   SPACE handbrake   R reset",
                 24, 86, 17, WHITE);
        DrawText(inputHud, 24, 110, 15, (Color){ 235, 240, 245, 255 });
        if (finished) {
            DrawText("FINISH!  Press R to race again", 400, 120, 26,
                     (Color){ 255, 218, 77, 255 });
        }
        EndDrawing();
    }

    UnloadMesh(carMesh);
    UnloadMesh(tireMesh);
    UnloadMesh(hubMesh);
    UnloadMesh(treadMesh);
    UnloadMesh(sidewallMesh);
    DestroyVehicle3D(car);
    destroyTrackColliders(trackBodies);
    DestroyPhysicsWorld3D(world);
    CloseWindow();
    return 0;
}
