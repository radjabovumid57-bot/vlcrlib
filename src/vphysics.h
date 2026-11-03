#ifndef VLCRLIB_VPHYSICS_H
#define VLCRLIB_VPHYSICS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "vmath.h"
#include "vgraphics.h"

/* ================================================================== */
/*  Opaque handles                                                    */
/* ================================================================== */
typedef struct PhysicsWorld3D PhysicsWorld3D;
typedef struct PhysicsBody3D  PhysicsBody3D;
typedef struct PhysicsJoint3D PhysicsJoint3D;
typedef struct SoftBody3D     SoftBody3D;
typedef struct Character3D    Character3D;
typedef struct Vehicle3D      Vehicle3D;

/* ================================================================== */
/*  Enums                                                             */
/* ================================================================== */
typedef enum PhysicsShapeType3D {
    PHYS_SHAPE_BOX3D = 0,
    PHYS_SHAPE_SPHERE3D,
    PHYS_SHAPE_CAPSULE3D,
    PHYS_SHAPE_PLANE3D,
    PHYS_SHAPE_CYLINDER3D,
    PHYS_SHAPE_CONE3D,
    PHYS_SHAPE_CONVEX_HULL3D,
    PHYS_SHAPE_MESH3D
} PhysicsShapeType3D;

typedef enum PhysicsBodyType3D {
    PHYS_BODY_STATIC3D = 0,
    PHYS_BODY_DYNAMIC3D,
    PHYS_BODY_KINEMATIC3D
} PhysicsBodyType3D;

typedef enum JointType3D {
    JOINT_FIXED3D = 0,
    JOINT_HINGE3D,
    JOINT_SLIDER3D,
    JOINT_POINT2POINT3D,
    JOINT_CONE_TWIST3D
} JointType3D;

typedef struct PhysicsRayHit3D {
    int     hit;
    float   distance;
    Vector3 point;
    Vector3 normal;
    PhysicsBody3D *body;
} PhysicsRayHit3D;

typedef struct PhysicsContact3D {
    PhysicsBody3D *bodyA;
    PhysicsBody3D *bodyB;
    Vector3        pointOnA;
    Vector3        pointOnB;
    Vector3        normalOnB;
    float          distance;      /* negative = penetrating */
    float          appliedImpulse;
    float          friction;
} PhysicsContact3D;

/* ================================================================== */
/*  World                                                             */
/* ================================================================== */

/* World convention (see the block at the top of vmath.h): right-handed,
   Y up, floor = XZ plane at y = 0. Gravity therefore points along -Y.
   Pass this to CreatePhysicsWorld3D() unless you want a different scale. */
#define VLCRLIB_GRAVITY_X 0.0f
#define VLCRLIB_GRAVITY_Y (-9.81f)
#define VLCRLIB_GRAVITY_Z 0.0f
#define VLCRLIB_DEFAULT_GRAVITY VLCRLIB_COMPOUND_LITERAL(Vector3){ VLCRLIB_GRAVITY_X, VLCRLIB_GRAVITY_Y, VLCRLIB_GRAVITY_Z }

PhysicsWorld3D *CreatePhysicsWorld3D(Vector3 gravity);
void            DestroyPhysicsWorld3D(PhysicsWorld3D *world);
void            StepPhysicsWorld3D(PhysicsWorld3D *world, float dt);
void            SetPhysicsWorld3DGravity(PhysicsWorld3D *w, Vector3 g);
Vector3         GetPhysicsWorld3DGravity(const PhysicsWorld3D *w);

/* ================================================================== */
/*  Rigid body creation                                               */
/* ================================================================== */
PhysicsBody3D *CreatePhysicsBody3D(PhysicsWorld3D *world,
                                   PhysicsShapeType3D shape,
                                   Vector3 position,
                                   Vector3 shapeParam,
                                   PhysicsBodyType3D type,
                                   float mass,
                                   float friction,
                                   float restitution);
void           DestroyPhysicsBody3D(PhysicsBody3D *body);

/* Convex hull из массива точек */
PhysicsBody3D *CreatePhysicsBody3DConvexHull(PhysicsWorld3D *world,
                                             const Vector3 *points, int count,
                                             Vector3 position,
                                             float mass, float friction, float restitution);

/* Tri-mesh из массива вершин и индексов */
PhysicsBody3D *CreatePhysicsBody3DMesh(PhysicsWorld3D *world,
                                       const Vector3 *vertices, int vcount,
                                       const int *indices, int icount,
                                       Vector3 position,
                                       PhysicsBodyType3D type,
                                       float mass, float friction, float restitution);

/* ================================================================== */
/*  Rigid body transform / velocity                                   */
/* ================================================================== */
void       SetPhysicsBody3DPosition(PhysicsBody3D *body, Vector3 position);
Vector3    GetPhysicsBody3DPosition(const PhysicsBody3D *body);
void       SetPhysicsBody3DRotation(PhysicsBody3D *body, Quaternion rotation);
Quaternion GetPhysicsBody3DRotation(const PhysicsBody3D *body);

void       SetPhysicsBody3DVelocity(PhysicsBody3D *body, Vector3 velocity);
Vector3    GetPhysicsBody3DVelocity(const PhysicsBody3D *body);
void       SetPhysicsBody3DAngularVelocity(PhysicsBody3D *body, Vector3 omega);
Vector3    GetPhysicsBody3DAngularVelocity(const PhysicsBody3D *body);

/* ================================================================== */
/*  Forces                                                            */
/* ================================================================== */
void       ApplyPhysicsBody3DForce(PhysicsBody3D *body, Vector3 force);
void       ApplyPhysicsBody3DForceAtPoint(PhysicsBody3D *body, Vector3 force, Vector3 relPos);
void       ApplyPhysicsBody3DImpulse(PhysicsBody3D *body, Vector3 impulse);
void       ApplyPhysicsBody3DImpulseAtPoint(PhysicsBody3D *body, Vector3 impulse, Vector3 relPos);
void       ApplyPhysicsBody3DTorque(PhysicsBody3D *body, Vector3 torque);
void       ApplyPhysicsBody3DTorqueImpulse(PhysicsBody3D *body, Vector3 torqueImpulse);

/* ================================================================== */
/*  Body properties                                                   */
/* ================================================================== */
void       SetPhysicsBody3DMass(PhysicsBody3D *body, float mass);
float      GetPhysicsBody3DMass(const PhysicsBody3D *body);
void       SetPhysicsBody3DDamping(PhysicsBody3D *body, float linear, float angular);
void       GetPhysicsBody3DDamping(const PhysicsBody3D *body, float *linear, float *angular);
void       SetPhysicsBody3DFriction(PhysicsBody3D *body, float friction);
float      GetPhysicsBody3DFriction(const PhysicsBody3D *body);
void       SetPhysicsBody3DRestitution(PhysicsBody3D *body, float restitution);
float      GetPhysicsBody3DRestitution(const PhysicsBody3D *body);

void       SetPhysicsBody3DGravity(PhysicsBody3D *body, Vector3 gravity);
Vector3    GetPhysicsBody3DGravity(const PhysicsBody3D *body);
void       SetPhysicsBody3DLinearFactor(PhysicsBody3D *body, Vector3 factor);
void       SetPhysicsBody3DAngularFactor(PhysicsBody3D *body, Vector3 factor);
void       SetPhysicsBody3DCCD(PhysicsBody3D *body, bool enabled);
void       ActivatePhysicsBody3D(PhysicsBody3D *body);
void       SetPhysicsBody3DSleeping(PhysicsBody3D *body, bool sleeping);
bool       IsPhysicsBody3DSleeping(const PhysicsBody3D *body);
bool       IsPhysicsBody3DActive(const PhysicsBody3D *body);

/* ================================================================== */
/*  Joints / Constraints                                              */
/* ================================================================== */
PhysicsJoint3D *CreatePhysicsJoint3D(PhysicsWorld3D *world,
                                     JointType3D type,
                                     PhysicsBody3D *bodyA, PhysicsBody3D *bodyB,
                                     Vector3 pivotInA, Vector3 pivotInB,
                                     Vector3 axisInA, Vector3 axisInB,
                                     bool disableCollisions);

void  DestroyPhysicsJoint3D(PhysicsJoint3D *joint);

/* Limits (для HINGE, SLIDER, CONE_TWIST) */
void  SetJoint3DLimits(PhysicsJoint3D *j, float lower, float upper);

/* Motor (для HINGE, SLIDER) */
void  SetJoint3DMotorVelocity(PhysicsJoint3D *j, float targetVel, float maxForce);
void  SetJoint3DMotorPosition(PhysicsJoint3D *j, float targetPos, float maxForce,
                              float kp, float kd, float dt);
void  DisableJoint3DMotor(PhysicsJoint3D *j);

/* State */
float GetJoint3DPosition(const PhysicsJoint3D *j);
float GetJoint3DVelocity(const PhysicsJoint3D *j);
Vector3 GetJoint3DReactionForce(const PhysicsJoint3D *j);

/* ================================================================== */
/*  Contact manifold                                                  */
/* ================================================================== */
int  GetPhysicsContactCount(const PhysicsWorld3D *world);
bool GetPhysicsContact(const PhysicsWorld3D *world, int index,
                       PhysicsContact3D *out);

/* Проверка коллизии между двумя телами (по AABB) */
bool CheckCollisionPhysics3D(PhysicsBody3D *a, PhysicsBody3D *b,
                             PhysicsContact3D *out);

/* Closest points между двумя телами */
int  GetClosestPointsPhysics3D(PhysicsWorld3D *world,
                               PhysicsBody3D *a, PhysicsBody3D *b,
                               float maxDist,
                               PhysicsContact3D *out, int maxCount);

/* Overlap: какие тела внутри сферы */
int  QueryPhysicsOverlapSphere(PhysicsWorld3D *world,
                               Vector3 center, float radius,
                               PhysicsBody3D **out, int maxCount);

/* ================================================================== */
/*  Character controller                                              */
/* ================================================================== */
Character3D *CreateCharacter3D(PhysicsWorld3D *world,
                               Vector3 position,
                               float radius, float height,
                               float stepHeight);
void  DestroyCharacter3D(Character3D *c);

void  SetCharacter3DWalkDirection(Character3D *c, Vector3 dir);
void  JumpCharacter3D(Character3D *c, float speed);
void  WarpCharacter3D(Character3D *c, Vector3 position);
Vector3 GetCharacter3DPosition(const Character3D *c);
bool  IsCharacter3DOnGround(const Character3D *c);
void  SetCharacter3DGravity(Character3D *c, Vector3 g);
void  SetCharacter3DMaxSlope(Character3D *c, float radians);

/* ================================================================== */
/*  Vehicle                                                           */
/* ================================================================== */
Vehicle3D *CreateVehicle3D(PhysicsWorld3D *world,
                           Vector3 position, Vector3 chassisHalfExtents,
                           float mass);
void  DestroyVehicle3D(Vehicle3D *v);

/* Колёса: 4 максимум — 2 передних (isFront=true), 2 задних */
void  AddVehicle3DWheel(Vehicle3D *v, bool isFront,
                        Vector3 connectionPoint,      /* в системе chassis */
                        Vector3 direction,            /* 0,-1,0 обычно */
                        Vector3 axle,                 /* -1,0,0 обычно */
                        float suspensionRestLength,
                        float radius,
                        float friction);

void  SetVehicle3DEngineForce(Vehicle3D *v, float force);      /* на задние */
void  SetVehicle3DSteering(Vehicle3D *v, float angle);         /* на передние */
void  SetVehicle3DBrake(Vehicle3D *v, float force);
void  ResetVehicle3D(Vehicle3D *v, Vector3 position, Quaternion rotation);
bool  GetVehicle3DWheelTransform(Vehicle3D *v, int wheelIndex,
                                 Vector3 *position, Quaternion *rotation);
Vector3 GetVehicle3DPosition(const Vehicle3D *v);
Quaternion GetVehicle3DRotation(const Vehicle3D *v);

/* ================================================================== */
/*  Soft body                                                         */
/* ================================================================== */
SoftBody3D *CreateSoftBodyCloth(PhysicsWorld3D *world,
                                Vector3 corner00, Vector3 corner10,
                                Vector3 corner01, Vector3 corner11,
                                int resX, int resZ,
                                float mass);
SoftBody3D *CreateSoftBodySphere(PhysicsWorld3D *world,
                                 Vector3 center, float radius,
                                 int resolution, float mass);
void  DestroySoftBody3D(SoftBody3D *sb);
int   GetSoftBody3DVertexCount(const SoftBody3D *sb);
Vector3 GetSoftBody3DVertex(const SoftBody3D *sb, int index);
int   GetSoftBody3DFaceCount(const SoftBody3D *sb);
void  GetSoftBody3DFace(const SoftBody3D *sb, int index,
                        Vector3 *v0, Vector3 *v1, Vector3 *v2);
void  PinSoftBody3DVertex(SoftBody3D *sb, int index);

/* ================================================================== */
/*  Raycast                                                           */
/* ================================================================== */
PhysicsRayHit3D RaycastPhysics3D(PhysicsWorld3D *world,
                                 Vector3 origin, Vector3 direction, float maxDist);

/* ================================================================== */
/*  Model sync (legacy)                                               */
/* ================================================================== */
void SyncPhysicsBodyToModel3D(PhysicsBody3D *body, Model *model);

#ifdef __cplusplus
}
#endif
#endif /* VLCRLIB_VPHYSICS_H */
