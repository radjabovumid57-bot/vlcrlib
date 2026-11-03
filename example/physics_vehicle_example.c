/* Bullet raycast vehicle: engine force, steering, braking and wheel setup. */
#include <vlcrlib.h>
#include <SDL3/SDL_scancode.h>

int main(void) {
    if (!InitWindow(1100, 700, "vlcrlib — Vehicle Physics (Bullet)")) return 1;
    SetTargetFPS(60);
    PhysicsWorld3D *world = CreatePhysicsWorld3D((Vector3){0,-9.81f,0});
    PhysicsBody3D *ground = CreatePhysicsBody3D(world, PHYS_SHAPE_PLANE3D,
        (Vector3){0,0,0}, (Vector3){0,1,0}, PHYS_BODY_STATIC3D, 0, 1.0f, 0.0f);
    Vehicle3D *car = CreateVehicle3D(world, (Vector3){0,1.2f,0}, (Vector3){0.9f,0.35f,1.5f}, 800.0f);
    const float x = 0.82f, z = 1.05f;
    AddVehicle3DWheel(car, true,  (Vector3){-x,-0.2f, z}, (Vector3){0,-1,0}, (Vector3){-1,0,0}, 0.35f, 0.38f, 2.0f);
    AddVehicle3DWheel(car, true,  (Vector3){ x,-0.2f, z}, (Vector3){0,-1,0}, (Vector3){-1,0,0}, 0.35f, 0.38f, 2.0f);
    AddVehicle3DWheel(car, false, (Vector3){-x,-0.2f,-z}, (Vector3){0,-1,0}, (Vector3){-1,0,0}, 0.35f, 0.38f, 2.0f);
    AddVehicle3DWheel(car, false, (Vector3){ x,-0.2f,-z}, (Vector3){0,-1,0}, (Vector3){-1,0,0}, 0.35f, 0.38f, 2.0f);
    Camera3D camera = {0};
    camera.position = (Vector3){8,7,12}; camera.target = (Vector3){0,0.8f,0};
    camera.up = (Vector3){0,1,0}; camera.fovy = 50; camera.nearPlane = 0.1f; camera.farPlane = 150;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime(); if (dt > 1.0f/30.0f) dt = 1.0f/30.0f;
        float engine = 0, steering = 0, brake = 0;
        if (IsKeyDown(SDL_SCANCODE_W)) engine = 2200;
        if (IsKeyDown(SDL_SCANCODE_S)) engine = -1200;
        if (IsKeyDown(SDL_SCANCODE_A)) steering = 0.35f;
        if (IsKeyDown(SDL_SCANCODE_D)) steering = -0.35f;
        if (IsKeyDown(SDL_SCANCODE_SPACE)) brake = 120;
        SetVehicle3DEngineForce(car, engine);
        SetVehicle3DSteering(car, steering);
        SetVehicle3DBrake(car, brake);
        StepPhysicsWorld3D(world, dt);

        Vector3 p = GetVehicle3DPosition(car);
        BeginDrawing(); ClearBackground((Color){23,31,43,255});
        BeginMode3D(camera);
        DrawGrid(40, 1.0f);
        DrawCube((Vector3){0,-0.25f,0}, (Vector3){80,0.5f,80}, (Color){70,82,94,255});
        DrawCube(p, (Vector3){1.8f,0.7f,3.0f}, (Color){45,145,220,255});
        DrawSphere((Vector3){p.x-0.82f,p.y-0.35f,p.z+1.05f},0.38f,DARKGRAY);
        DrawSphere((Vector3){p.x+0.82f,p.y-0.35f,p.z+1.05f},0.38f,DARKGRAY);
        DrawSphere((Vector3){p.x-0.82f,p.y-0.35f,p.z-1.05f},0.38f,DARKGRAY);
        DrawSphere((Vector3){p.x+0.82f,p.y-0.35f,p.z-1.05f},0.38f,DARKGRAY);
        EndMode3D();
        DrawText("Raycast vehicle — Bullet", 20, 20, 26, WHITE);
        DrawText("W/S engine | A/D steering | Space brake", 20, 54, 18, LIGHTGRAY);
        DrawText("Wheel suspension and tire forces are simulated by Bullet", 20, 82, 18, YELLOW);
        EndDrawing();
    }
    DestroyVehicle3D(car); DestroyPhysicsBody3D(ground); DestroyPhysicsWorld3D(world);
    CloseWindow(); return 0;
}
