/* Box2D overview: body types, shapes, character helper, joints, contacts, raycast. */
#include <vlcrlib.h>
#include <SDL3/SDL_scancode.h>
#include <math.h>
#include <stdio.h>

#define SCALE 48.0f
static Vector2 toScreen(Vector2 p) { return (Vector2){640.0f + p.x*SCALE, 610.0f - p.y*SCALE}; }

static void drawBox(Vector2 center, Vector2 half, float angle, Color color) {
    Vector2 local[4] = {{-half.x,-half.y},{half.x,-half.y},{half.x,half.y},{-half.x,half.y}};
    Vector2 v[4]; float c=cosf(angle), s=sinf(angle);
    for (int i=0;i<4;++i) {
        Vector2 p=toScreen((Vector2){center.x+local[i].x*c-local[i].y*s,
                                     center.y+local[i].x*s+local[i].y*c});
        v[i]=p;
    }
    for (int i=0;i<4;++i) {
        int j=(i+1)%4;
        DrawTriangle(v[0],v[i],v[(i+1)%4],color);
        DrawLineEx(v[i],v[j],2.0f,WHITE);
    }
}

int main(void) {
    if (!InitWindow(1280, 720, "vlcrlib — 2D Physics Types (Box2D)")) return 1;
    SetTargetFPS(60);
    PhysicsWorld2D *world=CreatePhysicsWorld2D((Vector2){0,-9.8f});
    SetPhysicsWorld2DSubSteps(world,8,3);
    PhysicsBody2D *ground=CreatePhysicsBody2DBox(world,(Vector2){0,-0.5f},(Vector2){11,0.5f},
        PHYS_BODY_STATIC2D,0.0f,0.8f,0.05f);

    PhysicsBody2D *boxes[6]={0};
    for (int i=0;i<6;++i)
        boxes[i]=CreatePhysicsBody2DBox(world,(Vector2){3.0f+(i%2)*1.2f,1.0f+(i/2)*1.3f},
            (Vector2){0.5f,0.5f},PHYS_BODY_DYNAMIC2D,1.0f,0.45f,0.2f);
    PhysicsBody2D *ball=CreatePhysicsBody2DCircle(world,(Vector2){-3,5},0.65f,
        PHYS_BODY_DYNAMIC2D,1.0f,0.25f,0.8f);
    PhysicsBody2D *platform=CreatePhysicsBody2DBox(world,(Vector2){0,2.0f},(Vector2){1.5f,0.18f},
        PHYS_BODY_KINEMATIC2D,0.0f,0.7f,0.0f);
    SetPhysicsBody2DVelocity(platform,(Vector2){1.6f,0});
    PhysicsBody2D *pendulum=CreatePhysicsBody2DBox(world,(Vector2){-6,3.5f},(Vector2){0.22f,1.0f},
        PHYS_BODY_DYNAMIC2D,1.0f,0.5f,0.0f);
    PhysicsJoint2D *hinge=CreatePhysicsJoint2D(world,JOINT2D_REVOLUTE,pendulum,NULL,
        (Vector2){-6,4.5f},(Vector2){0,0},false);
    EnableJoint2DLimit(hinge,true); SetJoint2DLimits(hinge,-0.8f,0.8f);
    Character2D *character=CreateCharacter2D(world,(Vector2){-8,1.0f},0.38f,0.3f,0.7f);

    bool motor=false; float platformDir=1.0f;
    while (!WindowShouldClose()) {
        float dt=GetFrameTime(); if(dt>1.0f/30.0f)dt=1.0f/30.0f;
        Vector2 cp=GetCharacter2DPosition(character), cv={0,0};
        if(IsKeyDown(SDL_SCANCODE_A))cv.x=-4.0f;
        if(IsKeyDown(SDL_SCANCODE_D))cv.x=4.0f;
        SetCharacter2DVelocity(character,cv);
        if(IsKeyPressed(SDL_SCANCODE_SPACE)&&IsCharacter2DGrounded(character))JumpCharacter2D(character,1.8f);
        if(IsKeyPressed(SDL_SCANCODE_M)){motor=!motor;EnableJoint2DMotor(hinge,motor);SetJoint2DMotorMaxTorque(hinge,15.0f);}
        SetJoint2DMotorSpeed(hinge,motor?1.8f:0.0f);
        Vector2 pp=GetPhysicsBody2DPosition(platform);
        if(pp.x>2.5f)platformDir=-1.0f; if(pp.x< -2.5f)platformDir=1.0f;
        SetPhysicsBody2DVelocity(platform,(Vector2){platformDir*1.6f,0});
        if(IsKeyPressed(SDL_SCANCODE_R)){WarpCharacter2D(character,(Vector2){-8,1});}
        StepPhysicsWorld2D(world,dt);

        PhysicsRayHit2D ray=RaycastPhysics2D(world,(Vector2){-10,6},(Vector2){1,-0.35f},22.0f);
        BeginDrawing();ClearBackground((Color){22,28,40,255});
        DrawText("2D Physics Types — Box2D",24,18,27,WHITE);
        DrawText("Static + dynamic + kinematic | box + circle | character + revolute joint",24,52,17,LIGHTGRAY);
        DrawText("A/D move character | Space jump | M toggle hinge motor | R reset character",24,78,16,YELLOW);

        Vector2 g=toScreen((Vector2){0,-0.5f});
        DrawRectangle((int)(g.x-11*SCALE),(int)(g.y-0.5f*SCALE),(int)(22*SCALE),(int)SCALE,(Color){65,78,98,255});
        for(int i=0;i<6;++i)if(boxes[i])drawBox(GetPhysicsBody2DPosition(boxes[i]),(Vector2){0.5f,0.5f},
            -GetPhysicsBody2DRotation(boxes[i]),(Color){85+(unsigned char)(i*20),150,220,255});
        if(ball){Vector2 b=toScreen(GetPhysicsBody2DPosition(ball));DrawCircleV(b,0.65f*SCALE,(Color){235,130,75,255});DrawCircleLines((int)b.x,(int)b.y,0.65f*SCALE,WHITE);}
        drawBox(GetPhysicsBody2DPosition(platform),(Vector2){1.5f,0.18f},0,(Color){110,190,125,255});
        drawBox(GetPhysicsBody2DPosition(pendulum),(Vector2){0.22f,1.0f},-GetPhysicsBody2DRotation(pendulum),(Color){205,165,75,255});
        Vector2 anchor=toScreen((Vector2){-6,4.5f});DrawCircleV(anchor,5,YELLOW);
        Vector2 ch=toScreen(cp);DrawCircleV(ch,0.38f*SCALE,SKYBLUE);DrawCircleLines((int)ch.x,(int)ch.y,0.38f*SCALE,WHITE);
        Vector2 rs=toScreen((Vector2){-10,6});
        Vector2 re=toScreen(Vector2Add((Vector2){-10,6},Vector2Scale(Vector2Normalize((Vector2){1,-0.35f}),22.0f)));
        DrawLineEx(rs,re,2,ray.hit?GOLD:DARKGRAY);if(ray.hit)DrawCircleV(toScreen(ray.point),6,GOLD);
        char hud[100];snprintf(hud,sizeof(hud),"Contacts: %d | Hinge angle: %.2f",GetPhysics2DContactCount(world),GetJoint2DAngle(hinge));
        DrawText(hud,24,110,17,WHITE);
        EndDrawing();
    }
    DestroyCharacter2D(character);DestroyPhysicsJoint2D(hinge);
    DestroyPhysicsBody2D(pendulum);DestroyPhysicsBody2D(platform);DestroyPhysicsBody2D(ball);
    for(int i=0;i<6;++i)DestroyPhysicsBody2D(boxes[i]);
    DestroyPhysicsBody2D(ground);DestroyPhysicsWorld2D(world);CloseWindow();return 0;
}
