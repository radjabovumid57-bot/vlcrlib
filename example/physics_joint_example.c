/* Bullet hinge joint with limits and a keyboard-controlled motor. */
#include <vlcrlib.h>
#include <SDL3/SDL_scancode.h>

int main(void) {
    if (!InitWindow(1000, 650, "vlcrlib — Joints and Constraints (Bullet)")) return 1;
    SetTargetFPS(60);
    PhysicsWorld3D *world = CreatePhysicsWorld3D((Vector3){0,-9.81f,0});
    PhysicsBody3D *ground = CreatePhysicsBody3D(world, PHYS_SHAPE_PLANE3D,
        (Vector3){0,0,0}, (Vector3){0,1,0}, PHYS_BODY_STATIC3D, 0, 0.8f, 0.0f);
    PhysicsBody3D *anchor = CreatePhysicsBody3D(world, PHYS_SHAPE_BOX3D,
        (Vector3){0,4,0}, (Vector3){0.25f,0.25f,0.25f}, PHYS_BODY_STATIC3D, 0, 0.5f, 0.0f);
    PhysicsBody3D *door = CreatePhysicsBody3D(world, PHYS_SHAPE_BOX3D,
        (Vector3){0,3,0}, (Vector3){0.12f,1.0f,0.8f}, PHYS_BODY_DYNAMIC3D, 8, 0.6f, 0.0f);
    PhysicsJoint3D *hinge = CreatePhysicsJoint3D(world, JOINT_HINGE3D, anchor, door,
        (Vector3){0,0,0}, (Vector3){0,1,0}, (Vector3){0,1,0}, (Vector3){0,1,0}, true);
    SetJoint3DLimits(hinge, -1.2f, 1.2f);
    Camera3D camera = {0};
    camera.position = (Vector3){6,5,9}; camera.target = (Vector3){0,2,0};
    camera.up = (Vector3){0,1,0}; camera.fovy = 50; camera.nearPlane = 0.1f; camera.farPlane = 100;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime(); if (dt > 1.0f/30.0f) dt = 1.0f/30.0f;
        if (IsKeyDown(SDL_SCANCODE_A)) SetJoint3DMotorVelocity(hinge, -1.5f, 40.0f);
        else if (IsKeyDown(SDL_SCANCODE_D)) SetJoint3DMotorVelocity(hinge, 1.5f, 40.0f);
        else DisableJoint3DMotor(hinge);
        StepPhysicsWorld3D(world, dt);
        Vector3 p = GetPhysicsBody3DPosition(door);
        BeginDrawing(); ClearBackground((Color){25,32,45,255});
        BeginMode3D(camera);
        DrawGrid(20,1.0f);
        DrawCube((Vector3){0,-0.2f,0},(Vector3){20,0.4f,20},(Color){75,85,100,255});
        DrawCube((Vector3){0,4,0},(Vector3){0.5f,0.5f,0.5f},YELLOW);
        DrawCube(p,(Vector3){0.24f,2.0f,1.6f},(Color){190,120,70,255});
        DrawLine3D((Vector3){0,4,0},p,WHITE);
        EndMode3D();
        DrawText("Hinge joint: local pivots, axis, angular limits and motor",20,20,24,WHITE);
        DrawText("Hold A/D to drive motor | release to disable motor",20,56,18,LIGHTGRAY);
        EndDrawing();
    }
    DestroyPhysicsJoint3D(hinge); DestroyPhysicsBody3D(door);
    DestroyPhysicsBody3D(anchor); DestroyPhysicsBody3D(ground);
    DestroyPhysicsWorld3D(world); CloseWindow(); return 0;
}
