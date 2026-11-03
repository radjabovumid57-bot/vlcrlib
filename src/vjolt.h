#ifndef VLCRLIB_VJOLT_H
#define VLCRLIB_VJOLT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "vmath.h"
#include <stdbool.h>

/* ==================================================================
 *  Jolt Physics backend for vlcrlib (raylib-style C API)
 *  Parallel to Bullet (vphysics.h) — use one or the other per world.
 * ================================================================== */

typedef struct JoltWorld  JoltWorld;
typedef struct JoltBody   JoltBody;

typedef enum JoltShapeType {
    JOLT_SHAPE_BOX = 0,
    JOLT_SHAPE_SPHERE,
    JOLT_SHAPE_CAPSULE,
    JOLT_SHAPE_CYLINDER
} JoltShapeType;

typedef enum JoltBodyType {
    JOLT_BODY_STATIC = 0,
    JOLT_BODY_DYNAMIC,
    JOLT_BODY_KINEMATIC
} JoltBodyType;

typedef struct JoltRayHit {
    int       hit;
    float     distance;
    Vector3   point;
    Vector3   normal;
    JoltBody *body;
} JoltRayHit;

/* ---- Library lifetime (call once per process) ---- */
bool Jolt_Init(void);
void Jolt_Shutdown(void);
bool Jolt_IsInitialized(void);

/* ---- World ---- */
JoltWorld *Jolt_CreateWorld(Vector3 gravity);
void       Jolt_DestroyWorld(JoltWorld *world);
void       Jolt_Step(JoltWorld *world, float dt);
void       Jolt_SetGravity(JoltWorld *world, Vector3 gravity);
Vector3    Jolt_GetGravity(const JoltWorld *world);

/* shapeParam:
 *   BOX:      half-extents (x,y,z)
 *   SPHERE:   radius in x (y,z ignored)
 *   CAPSULE:  radius in x, half-height of cylinder in y
 *   CYLINDER: radius in x, half-height in y
 */
JoltBody *Jolt_CreateBody(JoltWorld *world,
                          JoltShapeType shape,
                          Vector3 position,
                          Vector3 shapeParam,
                          JoltBodyType type,
                          float mass,
                          float friction,
                          float restitution);

void Jolt_DestroyBody(JoltBody *body);

/* ---- Transform / velocity ---- */
void       Jolt_SetPosition(JoltBody *body, Vector3 position);
Vector3    Jolt_GetPosition(const JoltBody *body);
void       Jolt_SetRotation(JoltBody *body, Quaternion rotation);
Quaternion Jolt_GetRotation(const JoltBody *body);
void       Jolt_SetLinearVelocity(JoltBody *body, Vector3 v);
Vector3    Jolt_GetLinearVelocity(const JoltBody *body);
void       Jolt_SetAngularVelocity(JoltBody *body, Vector3 w);
Vector3    Jolt_GetAngularVelocity(const JoltBody *body);

/* ---- Forces ---- */
void Jolt_AddForce(JoltBody *body, Vector3 force);
void Jolt_AddImpulse(JoltBody *body, Vector3 impulse);
void Jolt_AddTorque(JoltBody *body, Vector3 torque);

/* ---- Raycast ---- */
JoltRayHit Jolt_Raycast(JoltWorld *world, Vector3 origin, Vector3 direction, float maxDist);

/* ---- Activate ---- */
void Jolt_ActivateBody(JoltBody *body);

#ifdef __cplusplus
}
#endif
#endif /* VLCRLIB_VJOLT_H */
