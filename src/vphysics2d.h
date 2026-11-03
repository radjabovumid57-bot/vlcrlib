#ifndef VLCRLIB_VPHYSICS2D_H
#define VLCRLIB_VPHYSICS2D_H

#ifdef __cplusplus
extern "C" {
#endif

#include "vmath.h"
#include "vcore.h"

/* ================================================================== */
/*  Opaque handles                                                    */
/* ================================================================== */
typedef struct PhysicsWorld2D PhysicsWorld2D;
typedef struct PhysicsBody2D  PhysicsBody2D;
typedef struct PhysicsJoint2D PhysicsJoint2D;
typedef struct Character2D    Character2D;

/* ================================================================== */
/*  Enums                                                             */
/* ================================================================== */
typedef enum PhysicsBodyType2D {
    PHYS_BODY_STATIC2D = 0,
    PHYS_BODY_DYNAMIC2D,
    PHYS_BODY_KINEMATIC2D
} PhysicsBodyType2D;

typedef enum PhysicsShapeType2D {
    PHYS_SHAPE_BOX2D = 0,
    PHYS_SHAPE_CIRCLE2D,
    PHYS_SHAPE_POLYGON2D,
    PHYS_SHAPE_EDGE2D,
    PHYS_SHAPE_CHAIN2D
} PhysicsShapeType2D;

typedef enum JointType2D {
    JOINT2D_REVOLUTE = 0,
    JOINT2D_PRISMATIC,
    JOINT2D_DISTANCE,
    JOINT2D_PULLEY,
    JOINT2D_GEAR,
    JOINT2D_WHEEL,
    JOINT2D_WELD,
    JOINT2D_FRICTION,
    JOINT2D_MOTOR,
    JOINT2D_MOUSE
} JointType2D;

typedef struct PhysicsRayHit2D {
    int     hit;
    float   distance;
    Vector2 point;
    Vector2 normal;
    float   fraction;
    PhysicsBody2D *body;
} PhysicsRayHit2D;

typedef struct PhysicsContact2D {
    PhysicsBody2D *bodyA;
    PhysicsBody2D *bodyB;
    Vector2        point;
    Vector2        normal;
    float          separation;
    float          normalImpulse;
    float          tangentImpulse;
    int            isTouching;
    int            isSensor;
} PhysicsContact2D;

typedef struct PhysicsAABB2D {
    Vector2 lowerBound;
    Vector2 upperBound;
} PhysicsAABB2D;

/* ================================================================== */
/*  World                                                             */
/* ================================================================== */
PhysicsWorld2D *CreatePhysicsWorld2D(Vector2 gravity);
void            DestroyPhysicsWorld2D(PhysicsWorld2D *world);
void            StepPhysicsWorld2D(PhysicsWorld2D *world, float dt);

void            SetPhysicsWorld2DGravity(PhysicsWorld2D *w, Vector2 g);
Vector2         GetPhysicsWorld2DGravity(const PhysicsWorld2D *w);

void            SetPhysicsWorld2DSubSteps(PhysicsWorld2D *w,
                                          int velocityIterations,
                                          int positionIterations);

/* Enable/disable sleeping */
void            SetPhysicsWorld2DAllowSleep(PhysicsWorld2D *w, bool allow);
bool            GetPhysicsWorld2DAllowSleep(const PhysicsWorld2D *w);

/* ================================================================== */
/*  Body creation                                                     */
/* ================================================================== */
PhysicsBody2D *CreatePhysicsBody2D(PhysicsWorld2D *world,
                                   PhysicsShapeType2D shape,
                                   Vector2 position,
                                   Vector2 sizeOrRadius,
                                   PhysicsBodyType2D type,
                                   float density,
                                   float friction,
                                   float restitution);

/* Box из полуразмеров (half-extents) */
PhysicsBody2D *CreatePhysicsBody2DBox(PhysicsWorld2D *world,
                                      Vector2 position, Vector2 halfSize,
                                      PhysicsBodyType2D type,
                                      float density, float friction, float restitution);

/* Круг */
PhysicsBody2D *CreatePhysicsBody2DCircle(PhysicsWorld2D *world,
                                         Vector2 position, float radius,
                                         PhysicsBodyType2D type,
                                         float density, float friction, float restitution);

/* Выпуклый многоугольник (до 8 вершин, CCW) */
PhysicsBody2D *CreatePhysicsBody2DPolygon(PhysicsWorld2D *world,
                                          Vector2 position,
                                          const Vector2 *points, int count,
                                          PhysicsBodyType2D type,
                                          float density, float friction, float restitution);

/* Отрезок — только static */
PhysicsBody2D *CreatePhysicsBody2DEdge(PhysicsWorld2D *world,
                                       Vector2 a, Vector2 b,
                                       float friction, float restitution);

/* Цепь из сегментов — только static (для земли) */
PhysicsBody2D *CreatePhysicsBody2DChain(PhysicsWorld2D *world,
                                        const Vector2 *points, int count,
                                        bool loop,
                                        float friction, float restitution);

void           DestroyPhysicsBody2D(PhysicsBody2D *body);

/* ================================================================== */
/*  Transform / velocity                                              */
/* ================================================================== */
void    SetPhysicsBody2DPosition(PhysicsBody2D *body, Vector2 position);
Vector2 GetPhysicsBody2DPosition(const PhysicsBody2D *body);
void    SetPhysicsBody2DRotation(PhysicsBody2D *body, float radians);
float   GetPhysicsBody2DRotation(const PhysicsBody2D *body);

void    SetPhysicsBody2DVelocity(PhysicsBody2D *body, Vector2 v);
Vector2 GetPhysicsBody2DVelocity(const PhysicsBody2D *body);
void    SetPhysicsBody2DAngularVelocity(PhysicsBody2D *body, float omega);
float   GetPhysicsBody2DAngularVelocity(const PhysicsBody2D *body);

/* Linear/angular velocity at a world point */
Vector2 GetPhysicsBody2DVelocityAtPoint(const PhysicsBody2D *body, Vector2 worldPoint);

/* ================================================================== */
/*  Forces / impulses                                                 */
/* ================================================================== */
void ApplyPhysicsBody2DForce(PhysicsBody2D *body, Vector2 force);
void ApplyPhysicsBody2DForceAtPoint(PhysicsBody2D *body, Vector2 force, Vector2 worldPoint);
void ApplyPhysicsBody2DImpulse(PhysicsBody2D *body, Vector2 impulse);
void ApplyPhysicsBody2DImpulseAtPoint(PhysicsBody2D *body, Vector2 impulse, Vector2 worldPoint);
void ApplyPhysicsBody2DTorque(PhysicsBody2D *body, float torque);
void ApplyPhysicsBody2DAngularImpulse(PhysicsBody2D *body, float angularImpulse);
void ClearPhysicsBody2DForces(PhysicsBody2D *body);

/* ================================================================== */
/*  Body properties                                                   */
/* ================================================================== */
void       SetPhysicsBody2DType(PhysicsBody2D *body, PhysicsBodyType2D type);
PhysicsBodyType2D GetPhysicsBody2DType(const PhysicsBody2D *body);

void       SetPhysicsBody2DMass(PhysicsBody2D *body, float mass);
float      GetPhysicsBody2DMass(const PhysicsBody2D *body);
float      GetPhysicsBody2DInertia(const PhysicsBody2D *body);

void       SetPhysicsBody2DLinearDamping(PhysicsBody2D *body, float d);
float      GetPhysicsBody2DLinearDamping(const PhysicsBody2D *body);
void       SetPhysicsBody2DAngularDamping(PhysicsBody2D *body, float d);
float      GetPhysicsBody2DAngularDamping(const PhysicsBody2D *body);

void       SetPhysicsBody2DGravityScale(PhysicsBody2D *body, float scale);
float      GetPhysicsBody2DGravityScale(const PhysicsBody2D *body);
void       SetPhysicsBody2DFixedRotation(PhysicsBody2D *body, bool fixed);
void       SetPhysicsBody2DBullet(PhysicsBody2D *body, bool bullet);
void       SetPhysicsBody2DEnabled(PhysicsBody2D *body, bool enabled);
bool       IsPhysicsBody2DEnabled(const PhysicsBody2D *body);
void       SetPhysicsBody2DSleepingAllowed(PhysicsBody2D *body, bool allowed);
void       SetPhysicsBody2DAwake(PhysicsBody2D *body, bool awake);
bool       IsPhysicsBody2DAwake(const PhysicsBody2D *body);

/* Fixture-level properties (на все фикстуры тела) */
void       SetPhysicsBody2DFriction(PhysicsBody2D *body, float friction);
float      GetPhysicsBody2DFriction(const PhysicsBody2D *body);
void       SetPhysicsBody2DRestitution(PhysicsBody2D *body, float restitution);
float      GetPhysicsBody2DRestitution(const PhysicsBody2D *body);
void       SetPhysicsBody2DDensity(PhysicsBody2D *body, float density);

/* Sensor */
void       SetPhysicsBody2DSensor(PhysicsBody2D *body, bool sensor);
bool       IsPhysicsBody2DSensor(const PhysicsBody2D *body);

/* Collision filter (group + mask) */
void       SetPhysicsBody2DFilter(PhysicsBody2D *body,
                                  unsigned short categoryBits,
                                  unsigned short maskBits,
                                  int groupIndex);

/* User data — произвольный указатель */
void       SetPhysicsBody2DUserData(PhysicsBody2D *body, void *userData);
void      *GetPhysicsBody2DUserData(const PhysicsBody2D *body);

/* Bounding box */
PhysicsAABB2D GetPhysicsBody2DAABB(const PhysicsBody2D *body);

/* ================================================================== */
/*  Joints                                                            */
/* ================================================================== */
PhysicsJoint2D *CreatePhysicsJoint2D(PhysicsWorld2D *world,
                                     JointType2D type,
                                     PhysicsBody2D *bodyA,
                                     PhysicsBody2D *bodyB,
                                     Vector2 anchorA,
                                     Vector2 anchorB,
                                     bool collideConnected);

void  DestroyPhysicsJoint2D(PhysicsJoint2D *joint);

/* Limits */
void  SetJoint2DLimits(PhysicsJoint2D *j, float lower, float upper);
void  EnableJoint2DLimit(PhysicsJoint2D *j, bool enable);

/* Motors (для REVOLUTE, PRISMATIC, WHEEL) */
void  EnableJoint2DMotor(PhysicsJoint2D *j, bool enable);
void  SetJoint2DMotorSpeed(PhysicsJoint2D *j, float speed);
void  SetJoint2DMotorMaxTorque(PhysicsJoint2D *j, float torque);
void  SetJoint2DMotorMaxForce(PhysicsJoint2D *j, float force);

/* State */
float GetJoint2DAngle(const PhysicsJoint2D *j);
float GetJoint2DSpeed(const PhysicsJoint2D *j);
float GetJoint2DLength(const PhysicsJoint2D *j);
Vector2 GetJoint2DReactionForce(const PhysicsJoint2D *j, float inv_dt);

/* Pulley / gear — коэффициенты */
void  SetJoint2DPulleyRatio(PhysicsJoint2D *j, float ratio);
void  SetJoint2DGearRatio(PhysicsJoint2D *j, float ratio);

/* Weld — мягкость */
void  SetJoint2DWeldStiffness(PhysicsJoint2D *j, float stiffness);

/* Mouse joint — цель и параметры */
void  SetJoint2DMouseTarget(PhysicsJoint2D *j, Vector2 target);
void  SetJoint2DMouseMaxForce(PhysicsJoint2D *j, float maxForce);
void  SetJoint2DMouseFrequency(PhysicsJoint2D *j, float hz, float dampingRatio);

/* ================================================================== */
/*  Contacts / queries                                                */
/* ================================================================== */
int  GetPhysics2DContactCount(const PhysicsWorld2D *world);
bool GetPhysics2DContact(const PhysicsWorld2D *world, int index,
                         PhysicsContact2D *out);

/* Проверка столкновения между двумя телами (AABB) */
bool CheckCollisionPhysics2D(PhysicsBody2D *a, PhysicsBody2D *b,
                             PhysicsContact2D *out);

/* Raycast */
PhysicsRayHit2D RaycastPhysics2D(PhysicsWorld2D *world,
                                 Vector2 origin, Vector2 direction,
                                 float maxDist);

/* Все хиты по лучу — до maxCount */
int RaycastPhysics2DAll(PhysicsWorld2D *world,
                        Vector2 origin, Vector2 direction, float maxDist,
                        PhysicsRayHit2D *out, int maxCount);

/* Overlap-запрос: тела, чьи AABB пересекаются с данным AABB */
int QueryPhysics2DOverlapAABB(PhysicsWorld2D *world,
                              PhysicsAABB2D aabb,
                              PhysicsBody2D **out, int maxCount);

/* Тела в круге */
int QueryPhysics2DOverlapCircle(PhysicsWorld2D *world,
                                Vector2 center, float radius,
                                PhysicsBody2D **out, int maxCount);

/* Тела в точке */
int QueryPhysics2DPoint(PhysicsWorld2D *world,
                        Vector2 point,
                        PhysicsBody2D **out, int maxCount);

/* ================================================================== */
/*  Character controller                                              */
/* ================================================================== */
Character2D *CreateCharacter2D(PhysicsWorld2D *world,
                               Vector2 position,
                               float radius,
                               float stepHeight,
                               float slopeMaxRadians);
void  DestroyCharacter2D(Character2D *c);

void  SetCharacter2DVelocity(Character2D *c, Vector2 v);
Vector2 GetCharacter2DVelocity(const Character2D *c);
void  JumpCharacter2D(Character2D *c, float height);
Vector2 GetCharacter2DPosition(const Character2D *c);
void  WarpCharacter2D(Character2D *c, Vector2 position);
bool  IsCharacter2DGrounded(const Character2D *c);

/* ================================================================== */
/*  Debug draw (для отладки)                                          */
/* ================================================================== */
void VPhysics2DDrawBodies(PhysicsWorld2D *world, bool drawAABB);

#ifdef __cplusplus
}
#endif
#endif /* VLCRLIB_VPHYSICS2D_H */