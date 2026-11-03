/* Bullet character controller: move, jump, ground test, and warp. */
#include <vlcrlib.h>
#include <SDL3/SDL_scancode.h>

int main(void) {
    if (!InitWindow(1000, 650, "vlcrlib — Character Controller (Bullet)")) return 1;
    SetTargetFPS(60);
    PhysicsWorld3D *world = CreatePhysicsWorld3D((Vector3){0, -9.81f, 0});
    PhysicsBody3D *ground = CreatePhysicsBody3D(world, PHYS_SHAPE_PLANE3D,
        (Vector3){0,0,0}, (Vector3){0,1,0}, PHYS_BODY_STATIC3D, 0, 0.8f, 0.0f);
    PhysicsBody3D *step = CreatePhysicsBody3D(world, PHYS_SHAPE_BOX3D,
        (Vector3){0,0.35f,-3}, (Vector3){2,0.35f,1}, PHYS_BODY_STATIC3D, 0, 0.8f, 0.0f);
    Character3D *player = CreateCharacter3D(world, (Vector3){0,1.3f,0}, 0.4f, 0.9f, 0.3f);
    Camera3D camera = {0};
    camera.position = (Vector3){7,6,10}; camera.target = (Vector3){0,1,0};
    camera.up = (Vector3){0,1,0}; camera.fovy = 50; camera.nearPlane = 0.1f; camera.farPlane = 100;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 1.0f/30.0f) dt = 1.0f/30.0f;
        Vector3 walk = {0,0,0};
        if (IsKeyDown(SDL_SCANCODE_W)) walk.z -= 4.0f * dt;
        if (IsKeyDown(SDL_SCANCODE_S)) walk.z += 4.0f * dt;
        if (IsKeyDown(SDL_SCANCODE_A)) walk.x -= 4.0f * dt;
        if (IsKeyDown(SDL_SCANCODE_D)) walk.x += 4.0f * dt;
        SetCharacter3DWalkDirection(player, walk);
        if (IsKeyPressed(SDL_SCANCODE_SPACE) && IsCharacter3DOnGround(player))
            JumpCharacter3D(player, 5.0f);
        if (IsKeyPressed(SDL_SCANCODE_R)) WarpCharacter3D(player, (Vector3){0,1.3f,0});
        StepPhysicsWorld3D(world, dt);

        Vector3 p = GetCharacter3DPosition(player);
        BeginDrawing(); ClearBackground((Color){25,32,45,255});
        BeginMode3D(camera);
        DrawGrid(24, 1.0f);
        DrawCube((Vector3){0,-0.2f,0}, (Vector3){24,0.4f,24}, (Color){75,85,100,255});
        DrawCube((Vector3){0,0.35f,-3}, (Vector3){4,0.7f,2}, (Color){150,110,70,255});
        DrawSphere((Vector3){p.x,p.y,p.z}, 0.42f, IsCharacter3DOnGround(player) ? SKYBLUE : ORANGE);
        EndMode3D();
        DrawText("Character controller — Bullet", 20, 20, 26, WHITE);
        DrawText("WASD move | Space jump | R warp to start", 20, 54, 18, LIGHTGRAY);
        DrawText(IsCharacter3DOnGround(player) ? "Grounded" : "Airborne", 20, 82, 18, YELLOW);
        EndDrawing();
    }
    DestroyCharacter3D(player); DestroyPhysicsBody3D(step); DestroyPhysicsBody3D(ground);
    DestroyPhysicsWorld3D(world); CloseWindow(); return 0;
}
