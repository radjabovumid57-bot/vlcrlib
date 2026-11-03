#include "vphysics.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionDispatch/btGhostObject.h>
#include <BulletDynamics/Character/btKinematicCharacterController.h>
#include <BulletDynamics/Vehicle/btRaycastVehicle.h>

#include <BulletSoftBody/btSoftBodyRigidBodyCollisionConfiguration.h>
#include <BulletSoftBody/btSoftRigidDynamicsWorld.h>
#include <BulletSoftBody/btSoftBodyHelpers.h>
#include <BulletSoftBody/btSoftBody.h>

/* ================================================================== */
/*  Internal structs                                                  */
/* ================================================================== */
struct PhysicsWorld3D {
    btSoftBodyRigidBodyCollisionConfiguration *config;
    btCollisionDispatcher                     *dispatcher;
    btDbvtBroadphase                          *broadphase;
    btGhostPairCallback                       *ghostPairCallback;
    btSequentialImpulseConstraintSolver       *solver;
    btSoftRigidDynamicsWorld                  *world;
    btSoftBodyWorldInfo                        sbInfo;
};

struct PhysicsBody3D {
    PhysicsWorld3D  *world;
    btRigidBody      *body;
    btCollisionShape *shape;      /* может владеть compound shape */
    btMotionState    *motion;
    bool              isCompound;
};

struct PhysicsJoint3D {
    PhysicsWorld3D *world;
    btTypedConstraint *constraint;
    JointType3D type;
};

struct Character3D {
    PhysicsWorld3D              *world;
    btPairCachingGhostObject    *ghost;
    btKinematicCharacterController *controller;
    btCapsuleShape              *shape;
    Vector3                      gravity;
    float                        radius;
    float                        height;
};

struct Vehicle3D {
    PhysicsWorld3D  *world;
    PhysicsBody3D   *chassis;
    btRaycastVehicle *vehicle;
    btVehicleRaycaster *raycaster;
    int              wheels[4];
    int              wheelCount;
    int              frontWheels[2];
    int              rearWheels[2];
    int              frontCount, rearCount;
};

struct SoftBody3D {
    PhysicsWorld3D *world;
    btSoftBody     *body;
};

/* ================================================================== */
/*  Helpers                                                           */
/* ================================================================== */
static inline btVector3 toBt(Vector3 v)     { return btVector3(v.x, v.y, v.z); }
static inline Vector3   fromBt(btVector3 v) { return Vector3{ v.getX(), v.getY(), v.getZ() }; }
static inline btQuaternion toBtQ(Quaternion q) { return btQuaternion(q.x, q.y, q.z, q.w); }
static inline Quaternion fromBtQ(btQuaternion q) {
    Quaternion r = { q.getX(), q.getY(), q.getZ(), q.getW() };
    return r;
}

static PhysicsBody3D *findBodyByBt(PhysicsWorld3D *w, const btCollisionObject *obj) {
    if (!w || !obj) return NULL;
    int n = w->world->getNumCollisionObjects();
    for (int i = 0; i < n; ++i) {
        btCollisionObject *o = w->world->getCollisionObjectArray()[i];
        if (o == obj) {
            /* Ищем обёртку. У нас нет прямого маппинга, поэтому
               используем userPointer — установим при создании. */
            return (PhysicsBody3D *)o->getUserPointer();
        }
    }
    return NULL;
}

/* ================================================================== */
/*  World                                                             */
/* ================================================================== */
extern "C" PhysicsWorld3D *CreatePhysicsWorld3D(Vector3 gravity) {
    PhysicsWorld3D *w = new PhysicsWorld3D();
    w->config     = new btSoftBodyRigidBodyCollisionConfiguration();
    w->dispatcher = new btCollisionDispatcher(w->config);
    w->broadphase = new btDbvtBroadphase();
    w->ghostPairCallback = new btGhostPairCallback();
    w->broadphase->getOverlappingPairCache()->setInternalGhostPairCallback(
        w->ghostPairCallback);
    w->solver     = new btSequentialImpulseConstraintSolver();
    w->world      = new btSoftRigidDynamicsWorld(w->dispatcher, w->broadphase,
                                                 w->solver, w->config);
    w->world->setGravity(toBt(gravity));
    w->world->getDispatchInfo().m_useContinuous = true;
    w->world->getSolverInfo().m_numIterations = 20;

    w->sbInfo.m_broadphase = w->broadphase;
    w->sbInfo.m_dispatcher = w->dispatcher;
    w->sbInfo.m_gravity    = toBt(gravity);
    w->sbInfo.m_sparsesdf.Initialize();
    return w;
}

extern "C" void DestroyPhysicsWorld3D(PhysicsWorld3D *world) {
    if (!world) return;
    delete world->world;
    delete world->solver;
    world->broadphase->getOverlappingPairCache()->setInternalGhostPairCallback(NULL);
    delete world->ghostPairCallback;
    delete world->broadphase;
    delete world->dispatcher;
    delete world->config;
    delete world;
}

extern "C" void StepPhysicsWorld3D(PhysicsWorld3D *world, float dt) {
    if (!world) return;
    world->world->stepSimulation(dt, 8, 1.0f / 60.0f);
}

extern "C" void SetPhysicsWorld3DGravity(PhysicsWorld3D *w, Vector3 g) {
    if (!w) return;
    w->world->setGravity(toBt(g));
    w->sbInfo.m_gravity = toBt(g);
}

extern "C" Vector3 GetPhysicsWorld3DGravity(const PhysicsWorld3D *w) {
    if (!w) return Vector3{0, -9.81f, 0};
    return fromBt(w->world->getGravity());
}

/* ================================================================== */
/*  Rigid body creation                                               */
/* ================================================================== */
static PhysicsBody3D *wrapBody(PhysicsWorld3D *world, btRigidBody *rb,
                               btCollisionShape *shape, btMotionState *motion,
                               bool isCompound) {
    PhysicsBody3D *out = new PhysicsBody3D();
    out->world      = world;
    out->body       = rb;
    out->shape      = shape;
    out->motion     = motion;
    out->isCompound = isCompound;
    rb->setUserPointer(out);
    return out;
}

extern "C" PhysicsBody3D *CreatePhysicsBody3D(PhysicsWorld3D *world,
                                              PhysicsShapeType3D shapeType,
                                              Vector3 position,
                                              Vector3 shapeParam,
                                              PhysicsBodyType3D type,
                                              float mass,
                                              float friction,
                                              float restitution)
{
    if (!world) return NULL;

    btCollisionShape *shape = NULL;
    switch (shapeType) {
        case PHYS_SHAPE_BOX3D:
            shape = new btBoxShape(toBt(shapeParam));
            break;
        case PHYS_SHAPE_SPHERE3D:
            shape = new btSphereShape(shapeParam.x);
            break;
        case PHYS_SHAPE_CAPSULE3D:
            shape = new btCapsuleShape(shapeParam.x, shapeParam.y);
            break;
        case PHYS_SHAPE_CYLINDER3D:
            shape = new btCylinderShape(btVector3(shapeParam.x, shapeParam.y, shapeParam.x));
            break;
        case PHYS_SHAPE_CONE3D:
            shape = new btConeShape(shapeParam.x, shapeParam.y);
            break;
        case PHYS_SHAPE_PLANE3D: {
            btVector3 n = toBt(shapeParam);
            if (n.length2() < 1e-6f) n = btVector3(0, 1, 0);
            n.normalize();
            shape = new btStaticPlaneShape(n, 0.0f);
            break;
        }
        default:
            return NULL;
    }

    btVector3 localInertia(0, 0, 0);
    btScalar  btMass = 0.0f;
    if (type == PHYS_BODY_DYNAMIC3D && mass > 0.0f) {
        shape->calculateLocalInertia(mass, localInertia);
        btMass = mass;
    }

    btTransform start;
    start.setIdentity();
    start.setOrigin(toBt(position));

    btDefaultMotionState *motion = new btDefaultMotionState(start);
    btRigidBody::btRigidBodyConstructionInfo ci(btMass, motion, shape, localInertia);
    btRigidBody *body = new btRigidBody(ci);
    body->setFriction(friction);
    body->setRestitution(restitution);
    if (type == PHYS_BODY_KINEMATIC3D) {
        body->setCollisionFlags(body->getCollisionFlags() |
                                btCollisionObject::CF_KINEMATIC_OBJECT);
        body->setActivationState(DISABLE_DEACTIVATION);
    } else if (type == PHYS_BODY_STATIC3D) {
        body->setCollisionFlags(body->getCollisionFlags() |
                                btCollisionObject::CF_STATIC_OBJECT);
    }

    world->world->addRigidBody(body);

    return wrapBody(world, body, shape, motion, false);
}

extern "C" PhysicsBody3D *CreatePhysicsBody3DConvexHull(PhysicsWorld3D *world,
                                                        const Vector3 *points, int count,
                                                        Vector3 position,
                                                        float mass, float friction, float restitution)
{
    if (!world || !points || count < 4) return NULL;

    btConvexHullShape *shape = new btConvexHullShape();
    for (int i = 0; i < count; ++i)
        shape->addPoint(toBt(points[i]), false);
    shape->recalcLocalAabb();

    btVector3 localInertia(0, 0, 0);
    btScalar btMass = mass;
    if (mass > 0.0f) shape->calculateLocalInertia(mass, localInertia);

    btTransform start;
    start.setIdentity();
    start.setOrigin(toBt(position));

    btDefaultMotionState *motion = new btDefaultMotionState(start);
    btRigidBody::btRigidBodyConstructionInfo ci(btMass, motion, shape, localInertia);
    btRigidBody *rb = new btRigidBody(ci);
    rb->setFriction(friction);
    rb->setRestitution(restitution);
    world->world->addRigidBody(rb);

    return wrapBody(world, rb, shape, motion, false);
}

extern "C" PhysicsBody3D *CreatePhysicsBody3DMesh(PhysicsWorld3D *world,
                                                  const Vector3 *vertices, int vcount,
                                                  const int *indices, int icount,
                                                  Vector3 position,
                                                  PhysicsBodyType3D type,
                                                  float mass, float friction, float restitution)
{
    if (!world || !vertices || vcount < 3 || !indices || icount < 3) return NULL;

    btTriangleMesh *mesh = new btTriangleMesh();
    for (int i = 0; i < icount; i += 3) {
        int i0 = indices[i];
        int i1 = indices[i + 1];
        int i2 = indices[i + 2];
        mesh->addTriangle(toBt(vertices[i0]), toBt(vertices[i1]), toBt(vertices[i2]));
    }

    btBvhTriangleMeshShape *shape = new btBvhTriangleMeshShape(mesh, true);

    btVector3 localInertia(0, 0, 0);
    btScalar btMass = 0.0f;
    if (type == PHYS_BODY_DYNAMIC3D && mass > 0.0f) {
        /* Для dynamic mesh надо использовать btGImpactMeshShape,
           здесь для простоты — static. */
        btMass = 0.0f;
    }

    btTransform start;
    start.setIdentity();
    start.setOrigin(toBt(position));

    btDefaultMotionState *motion = new btDefaultMotionState(start);
    btRigidBody::btRigidBodyConstructionInfo ci(btMass, motion, shape, localInertia);
    btRigidBody *rb = new btRigidBody(ci);
    rb->setFriction(friction);
    rb->setRestitution(restitution);
    rb->setCollisionFlags(rb->getCollisionFlags() | btCollisionObject::CF_STATIC_OBJECT);
    world->world->addRigidBody(rb);

    return wrapBody(world, rb, shape, motion, false);
}

extern "C" void DestroyPhysicsBody3D(PhysicsBody3D *b) {
    if (!b) return;
    if (b->world && b->body) {
        b->world->world->removeRigidBody(b->body);
    }
    delete b->body;
    delete b->shape;
    delete b->motion;
    delete b;
}

/* ================================================================== */
/*  Transform / velocity                                              */
/* ================================================================== */
extern "C" void SetPhysicsBody3DPosition(PhysicsBody3D *b, Vector3 p) {
    if (!b || !b->body) return;
    btTransform t = b->body->getWorldTransform();
    t.setOrigin(toBt(p));
    b->body->setWorldTransform(t);
    if (b->motion) b->motion->setWorldTransform(t);
    b->body->activate(true);
}

extern "C" Vector3 GetPhysicsBody3DPosition(const PhysicsBody3D *b) {
    if (!b || !b->body) return Vector3{0,0,0};
    return fromBt(b->body->getWorldTransform().getOrigin());
}

extern "C" void SetPhysicsBody3DRotation(PhysicsBody3D *b, Quaternion q) {
    if (!b || !b->body) return;
    btTransform t = b->body->getWorldTransform();
    t.setRotation(toBtQ(q));
    b->body->setWorldTransform(t);
    if (b->motion) b->motion->setWorldTransform(t);
    b->body->activate(true);
}

extern "C" Quaternion GetPhysicsBody3DRotation(const PhysicsBody3D *b) {
    Quaternion q = {0, 0, 0, 1};
    if (!b || !b->body) return q;
    return fromBtQ(b->body->getWorldTransform().getRotation());
}

extern "C" void SetPhysicsBody3DVelocity(PhysicsBody3D *b, Vector3 v) {
    if (!b || !b->body) return;
    b->body->setLinearVelocity(toBt(v));
    b->body->activate(true);
}

extern "C" Vector3 GetPhysicsBody3DVelocity(const PhysicsBody3D *b) {
    if (!b || !b->body) return Vector3{0,0,0};
    return fromBt(b->body->getLinearVelocity());
}

extern "C" void SetPhysicsBody3DAngularVelocity(PhysicsBody3D *b, Vector3 omega) {
    if (!b || !b->body) return;
    b->body->setAngularVelocity(toBt(omega));
    b->body->activate(true);
}

extern "C" Vector3 GetPhysicsBody3DAngularVelocity(const PhysicsBody3D *b) {
    if (!b || !b->body) return Vector3{0,0,0};
    return fromBt(b->body->getAngularVelocity());
}

/* ================================================================== */
/*  Forces                                                            */
/* ================================================================== */
extern "C" void ApplyPhysicsBody3DForce(PhysicsBody3D *b, Vector3 f) {
    if (!b || !b->body) return;
    b->body->applyCentralForce(toBt(f));
    b->body->activate(true);
}
extern "C" void ApplyPhysicsBody3DForceAtPoint(PhysicsBody3D *b, Vector3 f, Vector3 relPos) {
    if (!b || !b->body) return;
    b->body->applyForce(toBt(f), toBt(relPos));
    b->body->activate(true);
}
extern "C" void ApplyPhysicsBody3DImpulse(PhysicsBody3D *b, Vector3 imp) {
    if (!b || !b->body) return;
    b->body->applyCentralImpulse(toBt(imp));
    b->body->activate(true);
}
extern "C" void ApplyPhysicsBody3DImpulseAtPoint(PhysicsBody3D *b, Vector3 imp, Vector3 relPos) {
    if (!b || !b->body) return;
    b->body->applyImpulse(toBt(imp), toBt(relPos));
    b->body->activate(true);
}
extern "C" void ApplyPhysicsBody3DTorque(PhysicsBody3D *b, Vector3 t) {
    if (!b || !b->body) return;
    b->body->applyTorque(toBt(t));
    b->body->activate(true);
}
extern "C" void ApplyPhysicsBody3DTorqueImpulse(PhysicsBody3D *b, Vector3 ti) {
    if (!b || !b->body) return;
    b->body->applyTorqueImpulse(toBt(ti));
    b->body->activate(true);
}

/* ================================================================== */
/*  Body properties                                                   */
/* ================================================================== */
extern "C" void SetPhysicsBody3DMass(PhysicsBody3D *b, float mass) {
    if (!b || !b->body) return;
    btVector3 inertia(0, 0, 0);
    if (mass > 0.0f && b->shape) b->shape->calculateLocalInertia(mass, inertia);
    b->body->setMassProps(mass, inertia);
    b->body->updateInertiaTensor();
}

extern "C" float GetPhysicsBody3DMass(const PhysicsBody3D *b) {
    if (!b || !b->body) return 0.0f;
    return 1.0f / b->body->getInvMass();
}

extern "C" void SetPhysicsBody3DDamping(PhysicsBody3D *b, float linear, float angular) {
    if (!b || !b->body) return;
    b->body->setDamping(linear, angular);
}
extern "C" void GetPhysicsBody3DDamping(const PhysicsBody3D *b, float *linear, float *angular) {
    if (!b || !b->body) return;
    if (linear)  *linear  = b->body->getLinearDamping();
    if (angular) *angular = b->body->getAngularDamping();
}
extern "C" void SetPhysicsBody3DFriction(PhysicsBody3D *b, float f) {
    if (!b || !b->body) return;
    b->body->setFriction(f);
}
extern "C" float GetPhysicsBody3DFriction(const PhysicsBody3D *b) {
    if (!b || !b->body) return 0.0f;
    return b->body->getFriction();
}
extern "C" void SetPhysicsBody3DRestitution(PhysicsBody3D *b, float r) {
    if (!b || !b->body) return;
    b->body->setRestitution(r);
}
extern "C" float GetPhysicsBody3DRestitution(const PhysicsBody3D *b) {
    if (!b || !b->body) return 0.0f;
    return b->body->getRestitution();
}
extern "C" void SetPhysicsBody3DGravity(PhysicsBody3D *b, Vector3 g) {
    if (!b || !b->body) return;
    b->body->setGravity(toBt(g));
}
extern "C" Vector3 GetPhysicsBody3DGravity(const PhysicsBody3D *b) {
    if (!b || !b->body) return Vector3{0,0,0};
    return fromBt(b->body->getGravity());
}
extern "C" void SetPhysicsBody3DLinearFactor(PhysicsBody3D *b, Vector3 f) {
    if (!b || !b->body) return;
    b->body->setLinearFactor(toBt(f));
}
extern "C" void SetPhysicsBody3DAngularFactor(PhysicsBody3D *b, Vector3 f) {
    if (!b || !b->body) return;
    b->body->setAngularFactor(toBt(f));
}
extern "C" void SetPhysicsBody3DCCD(PhysicsBody3D *b, bool enabled) {
    if (!b || !b->body) return;
    if (enabled) {
        b->body->setCcdMotionThreshold(1e-7f);
        b->body->setCcdSweptSphereRadius(0.1f);
    } else {
        b->body->setCcdMotionThreshold(0.0f);
    }
}
extern "C" void ActivatePhysicsBody3D(PhysicsBody3D *b) {
    if (b && b->body) b->body->activate(true);
}
extern "C" void SetPhysicsBody3DSleeping(PhysicsBody3D *b, bool sleeping) {
    if (!b || !b->body) return;
    if (sleeping)
        b->body->setActivationState(ISLAND_SLEEPING);
    else
        b->body->activate(true);
}
extern "C" bool IsPhysicsBody3DSleeping(const PhysicsBody3D *b) {
    if (!b || !b->body) return false;
    return b->body->getActivationState() == ISLAND_SLEEPING;
}
extern "C" bool IsPhysicsBody3DActive(const PhysicsBody3D *b) {
    if (!b || !b->body) return false;
    return b->body->isActive();
}

/* ================================================================== */
/*  Joints                                                            */
/* ================================================================== */
extern "C" PhysicsJoint3D *CreatePhysicsJoint3D(PhysicsWorld3D *world,
                                                JointType3D type,
                                                PhysicsBody3D *bodyA,
                                                PhysicsBody3D *bodyB,
                                                Vector3 pivotInA, Vector3 pivotInB,
                                                Vector3 axisInA, Vector3 axisInB,
                                                bool disableCollisions)
{
    if (!world || !bodyA || !bodyA->body) return NULL;
    btRigidBody *rbA = bodyA->body;
    btRigidBody *rbB = (bodyB && bodyB->body) ? bodyB->body : NULL;

    btTypedConstraint *c = NULL;

    switch (type) {
        case JOINT_HINGE3D: {
            if (rbB) {
                btHingeConstraint *h = new btHingeConstraint(
                    *rbA, *rbB, toBt(pivotInA), toBt(pivotInB),
                    toBt(axisInA), toBt(axisInB), true);
                c = h;
            } else {
                btHingeConstraint *h = new btHingeConstraint(
                    *rbA, toBt(pivotInA), toBt(axisInA), true);
                c = h;
            }
            break;
        }
        case JOINT_SLIDER3D: {
            btTransform fA, fB;
            fA.setIdentity(); fA.setOrigin(toBt(pivotInA));
            fB.setIdentity(); fB.setOrigin(toBt(pivotInB));
            if (rbB)
                c = new btSliderConstraint(*rbA, *rbB, fA, fB, true);
            else
                c = new btSliderConstraint(*rbA, fA, true);
            break;
        }
        case JOINT_POINT2POINT3D: {
            if (rbB)
                c = new btPoint2PointConstraint(*rbA, *rbB, toBt(pivotInA), toBt(pivotInB));
            else
                c = new btPoint2PointConstraint(*rbA, toBt(pivotInA));
            break;
        }
        case JOINT_FIXED3D: {
            if (!rbB) return NULL;
            btTransform fA, fB;
            fA.setIdentity(); fA.setOrigin(toBt(pivotInA));
            fB.setIdentity(); fB.setOrigin(toBt(pivotInB));
            c = new btFixedConstraint(*rbA, *rbB, fA, fB);
            break;
        }
        case JOINT_CONE_TWIST3D: {
            if (!rbB) return NULL;
            btTransform fA, fB;
            fA.setIdentity(); fA.setOrigin(toBt(pivotInA));
            fB.setIdentity(); fB.setOrigin(toBt(pivotInB));
            c = new btConeTwistConstraint(*rbA, *rbB, fA, fB);
            break;
        }
    }

    if (!c) return NULL;

    if (disableCollisions) c->setDbgDrawSize(0.0f);

    world->world->addConstraint(c, disableCollisions);

    PhysicsJoint3D *out = new PhysicsJoint3D();
    out->world = world;
    out->constraint = c;
    out->type = type;
    return out;
}

extern "C" void DestroyPhysicsJoint3D(PhysicsJoint3D *j) {
    if (!j) return;
    if (j->world && j->constraint)
        j->world->world->removeConstraint(j->constraint);
    delete j->constraint;
    delete j;
}

extern "C" void SetJoint3DLimits(PhysicsJoint3D *j, float lower, float upper) {
    if (!j || !j->constraint) return;
    if (j->type == JOINT_HINGE3D) {
        ((btHingeConstraint *)j->constraint)->setLimit(lower, upper);
    } else if (j->type == JOINT_SLIDER3D) {
        btSliderConstraint *s = (btSliderConstraint *)j->constraint;
        s->setLowerLinLimit(lower);
        s->setUpperLinLimit(upper);
    } else if (j->type == JOINT_CONE_TWIST3D) {
        ((btConeTwistConstraint *)j->constraint)->setLimit(upper, upper, lower);
    }
}

extern "C" void SetJoint3DMotorVelocity(PhysicsJoint3D *j, float targetVel, float maxForce) {
    if (!j || !j->constraint) return;
    if (j->type == JOINT_HINGE3D) {
        ((btHingeConstraint *)j->constraint)->enableAngularMotor(true, targetVel, maxForce);
    } else if (j->type == JOINT_SLIDER3D) {
        btSliderConstraint *s = (btSliderConstraint *)j->constraint;
        s->setPoweredLinMotor(true);
        s->setTargetLinMotorVelocity(targetVel);
        s->setMaxLinMotorForce(maxForce);
    }
}

extern "C" void SetJoint3DMotorPosition(PhysicsJoint3D *j, float targetPos,
                                        float maxForce, float kp, float kd, float dt)
{
    if (!j || !j->constraint) return;
    (void)kp; (void)kd;
    if (j->type == JOINT_HINGE3D) {
        btHingeConstraint *h = (btHingeConstraint *)j->constraint;
        h->enableAngularMotor(true, 0.0f, maxForce);
        h->setMotorTarget(targetPos, dt);
    } else if (j->type == JOINT_SLIDER3D) {
        btSliderConstraint *s = (btSliderConstraint *)j->constraint;
        s->setPoweredLinMotor(true);
        s->setTargetLinMotorVelocity((targetPos - s->getLinearPos()) / dt);
        s->setMaxLinMotorForce(maxForce);
    }
}

extern "C" void DisableJoint3DMotor(PhysicsJoint3D *j) {
    if (!j || !j->constraint) return;
    if (j->type == JOINT_HINGE3D) {
        ((btHingeConstraint *)j->constraint)->enableAngularMotor(false, 0.0f, 0.0f);
    } else if (j->type == JOINT_SLIDER3D) {
        ((btSliderConstraint *)j->constraint)->setPoweredLinMotor(false);
    }
}

extern "C" float GetJoint3DPosition(const PhysicsJoint3D *j) {
    if (!j || !j->constraint) return 0.0f;
    if (j->type == JOINT_HINGE3D)
        return ((btHingeConstraint *)j->constraint)->getHingeAngle();
    if (j->type == JOINT_SLIDER3D)
        return ((btSliderConstraint *)j->constraint)->getLinearPos();
    return 0.0f;
}

extern "C" float GetJoint3DVelocity(const PhysicsJoint3D *j) {
    if (!j || !j->constraint) return 0.0f;
    if (j->type == JOINT_SLIDER3D)
        return ((btSliderConstraint *)j->constraint)->getLinearPos();
    return 0.0f;
}

extern "C" Vector3 GetJoint3DReactionForce(const PhysicsJoint3D *j) {
    (void)j;
    return Vector3{0,0,0};
}

/* ================================================================== */
/*  Contacts                                                          */
/* ================================================================== */
extern "C" int GetPhysicsContactCount(const PhysicsWorld3D *world) {
    if (!world) return 0;
    btDispatcher *d = ((PhysicsWorld3D *)world)->world->getDispatcher();
    int n = d->getNumManifolds();
    int total = 0;
    for (int i = 0; i < n; ++i) {
        total += d->getManifoldByIndexInternal(i)->getNumContacts();
    }
    return total;
}

extern "C" bool GetPhysicsContact(const PhysicsWorld3D *world, int index,
                                  PhysicsContact3D *out)
{
    if (!world || !out) return false;
    PhysicsWorld3D *w = (PhysicsWorld3D *)world;
    btDispatcher *d = w->world->getDispatcher();
    int n = d->getNumManifolds();

    int k = 0;
    for (int i = 0; i < n; ++i) {
        btPersistentManifold *m = d->getManifoldByIndexInternal(i);
        int nc = m->getNumContacts();
        for (int j = 0; j < nc; ++j) {
            if (k == index) {
                const btManifoldPoint &pt = m->getContactPoint(j);
                const btCollisionObject *obA = m->getBody0();
                const btCollisionObject *obB = m->getBody1();
                out->bodyA = (PhysicsBody3D *)obA->getUserPointer();
                out->bodyB = (PhysicsBody3D *)obB->getUserPointer();
                out->pointOnA = fromBt(pt.m_positionWorldOnA);
                out->pointOnB = fromBt(pt.m_positionWorldOnB);
                out->normalOnB = fromBt(pt.m_normalWorldOnB);
                out->distance = pt.getDistance();
                out->appliedImpulse = pt.getAppliedImpulse();
                out->friction = pt.m_combinedFriction;
                return true;
            }
            k++;
        }
    }
    return false;
}

extern "C" bool CheckCollisionPhysics3D(PhysicsBody3D *a, PhysicsBody3D *b,
                                        PhysicsContact3D *out)
{
    if (!a || !b || !a->body || !b->body) return false;

    btVector3 minA, maxA, minB, maxB;
    a->body->getAabb(minA, maxA);
    b->body->getAabb(minB, maxB);

    bool aabbHit = (minA.x() <= maxB.x() && maxA.x() >= minB.x() &&
                    minA.y() <= maxB.y() && maxA.y() >= minB.y() &&
                    minA.z() <= maxB.z() && maxA.z() >= minB.z());

    if (aabbHit && out) {
        out->bodyA = a;
        out->bodyB = b;
        out->distance = 0.0f;
        out->appliedImpulse = 0.0f;
    }
    return aabbHit;
}

extern "C" int GetClosestPointsPhysics3D(PhysicsWorld3D *world,
                                         PhysicsBody3D *a, PhysicsBody3D *b,
                                         float maxDist,
                                         PhysicsContact3D *out, int maxCount)
{
    (void)world;
    if (!a || !b || !a->body || !b->body || !out) return 0;

    struct Callback : public btCollisionWorld::ClosestConvexResultCallback {
        PhysicsBody3D *aWrap, *bWrap;
        PhysicsContact3D *out;
        int maxOut, count;
        float maxD;
        Callback(PhysicsBody3D *aa, PhysicsBody3D *bb, PhysicsContact3D *o, int m, float md)
            : btCollisionWorld::ClosestConvexResultCallback(btVector3(0,0,0), btVector3(0,0,0)),
              aWrap(aa), bWrap(bb), out(o), maxOut(m), count(0), maxD(md) {}

        virtual btScalar addSingleResult(btCollisionWorld::LocalConvexResult &r, bool /*normalInWorldSpace*/) {
            if (count >= maxOut) return 1.0f;
            out[count].bodyA = aWrap;
            out[count].bodyB = bWrap;
            out[count].pointOnA = fromBt(r.m_hitPointLocal);
            out[count].pointOnB = fromBt(r.m_hitPointLocal);
            out[count].normalOnB = fromBt(r.m_hitNormalLocal);
            out[count].distance = r.m_hitFraction;
            count++;
            return 1.0f;
        }
    } cb(a, b, out, maxCount, maxDist);

    btTransform t;
    a->body->getMotionState()->getWorldTransform(t);

    struct ShapeCast : public btConvexShape {
        virtual btVector3 localGetSupportingVertex(const btVector3 &) const override { return btVector3(0,0,0); }
        virtual btVector3 localGetSupportingVertexWithoutMargin(const btVector3 &) const override { return btVector3(0,0,0); }
        virtual void batchedUnitVectorGetSupportingVertexWithoutMargin(const btVector3*, btVector3*, int) const override {}
        virtual void getAabb(const btTransform &, btVector3 &, btVector3 &) const override {}
        virtual void calculateLocalInertia(btScalar, btVector3 &) const override {}
        virtual const char *getName() const override { return "dummy"; }
    };
    (void)cb;
    (void)t;
    (void)maxDist;
    return 0;
}

extern "C" int QueryPhysicsOverlapSphere(PhysicsWorld3D *world,
                                         Vector3 center, float radius,
                                         PhysicsBody3D **out, int maxCount)
{
    if (!world || !out) return 0;
    int count = 0;
    btVector3 c = toBt(center);
    int n = world->world->getNumCollisionObjects();
    for (int i = 0; i < n; ++i) {
        btCollisionObject *o = world->world->getCollisionObjectArray()[i];
        btVector3 mn, mx;
        o->getCollisionShape()->getAabb(o->getWorldTransform(), mn, mx);
        btVector3 closest(
            btMax(mn.x(), btMin(mx.x(), c.x())),
            btMax(mn.y(), btMin(mx.y(), c.y())),
            btMax(mn.z(), btMin(mx.z(), c.z())));
        if ((closest - c).length2() <= radius * radius) {
            PhysicsBody3D *wrap = (PhysicsBody3D *)o->getUserPointer();
            if (wrap) {
                out[count++] = wrap;
                if (count >= maxCount) break;
            }
        }
    }
    return count;
}

/* ================================================================== */
/*  Character                                                         */
/* ================================================================== */
extern "C" Character3D *CreateCharacter3D(PhysicsWorld3D *world,
                                          Vector3 position,
                                          float radius, float height,
                                          float stepHeight)
{
    if (!world) return NULL;

    Character3D *c = new Character3D();
    c->world = world;
    c->radius = radius;
    c->height = height;
    c->gravity = Vector3{0, -9.81f, 0};

    c->ghost = new btPairCachingGhostObject();
    c->ghost->setWorldTransform(
        btTransform(btQuaternion::getIdentity(), toBt(position)));

    c->shape = new btCapsuleShape(radius, height);
    c->ghost->setCollisionShape(c->shape);
    c->ghost->setCollisionFlags(btCollisionObject::CF_CHARACTER_OBJECT);

    /* 4-й аргумент — btVector3(0,1,0) вместо 1 */
    c->controller = new btKinematicCharacterController(
        c->ghost, c->shape, stepHeight, btVector3(0.0f, 1.0f, 0.0f));

    world->world->addCollisionObject(c->ghost,
        btBroadphaseProxy::CharacterFilter,
        btBroadphaseProxy::StaticFilter | btBroadphaseProxy::DefaultFilter);
    world->world->addAction(c->controller);

    return c;
}

extern "C" void DestroyCharacter3D(Character3D *c) {
    if (!c) return;
    if (c->world && c->controller) c->world->world->removeAction(c->controller);
    if (c->world && c->ghost)      c->world->world->removeCollisionObject(c->ghost);
    delete c->controller;
    delete c->shape;
    delete c->ghost;
    delete c;
}

extern "C" void SetCharacter3DWalkDirection(Character3D *c, Vector3 dir) {
    if (!c || !c->controller) return;
    c->controller->setWalkDirection(toBt(dir));
}

extern "C" void JumpCharacter3D(Character3D *c, float speed) {
    if (!c || !c->controller) return;
    c->controller->jump(toBt(Vector3{0, speed, 0}));
}

extern "C" void WarpCharacter3D(Character3D *c, Vector3 pos) {
    if (!c || !c->ghost) return;
    btTransform t;
    t.setIdentity();
    t.setOrigin(toBt(pos));
    c->ghost->setWorldTransform(t);
}

extern "C" Vector3 GetCharacter3DPosition(const Character3D *c) {
    if (!c || !c->ghost) return Vector3{0,0,0};
    return fromBt(c->ghost->getWorldTransform().getOrigin());
}

extern "C" bool IsCharacter3DOnGround(const Character3D *c) {
    if (!c || !c->controller) return false;
    return c->controller->onGround();
}

extern "C" void SetCharacter3DGravity(Character3D *c, Vector3 g) {
    if (!c || !c->controller) return;
    c->gravity = g;
    c->controller->setGravity(toBt(g));
}

extern "C" void SetCharacter3DMaxSlope(Character3D *c, float radians) {
    if (!c || !c->controller) return;
    c->controller->setMaxSlope(radians);
}

/* ================================================================== */
/*  Vehicle                                                           */
/* ================================================================== */
extern "C" Vehicle3D *CreateVehicle3D(PhysicsWorld3D *world,
                                      Vector3 position,
                                      Vector3 chassisHalfExtents,
                                      float mass)
{
    if (!world) return NULL;

    Vehicle3D *v = new Vehicle3D();
    v->world = world;
    v->chassis = CreatePhysicsBody3D(world,
        PHYS_SHAPE_BOX3D, position, chassisHalfExtents,
        PHYS_BODY_DYNAMIC3D, mass, 0.5f, 0.1f);
    if (!v->chassis) {
        delete v;
        return NULL;
    }
    v->chassis->body->setActivationState(DISABLE_DEACTIVATION);

    btVehicleRaycaster *raycaster = new btDefaultVehicleRaycaster(world->world);
    btRaycastVehicle::btVehicleTuning tuning;
    v->vehicle = new btRaycastVehicle(tuning, v->chassis->body, raycaster);
    v->raycaster = raycaster;
    v->vehicle->setCoordinateSystem(0, 1, 2);
    world->world->addAction(v->vehicle);

    v->wheelCount = 0;
    v->frontCount = 0;
    v->rearCount = 0;
    return v;
}

extern "C" void DestroyVehicle3D(Vehicle3D *v) {
    if (!v) return;
    if (v->world && v->vehicle) v->world->world->removeAction(v->vehicle);
    delete v->vehicle;
    delete v->raycaster;
    DestroyPhysicsBody3D(v->chassis);
    delete v;
}

extern "C" void AddVehicle3DWheel(Vehicle3D *v, bool isFront,
                                  Vector3 connectionPoint,
                                  Vector3 direction,
                                  Vector3 axle,
                                  float suspensionRestLength,
                                  float radius,
                                  float friction)
{
    if (!v || !v->vehicle || v->wheelCount >= 4) return;

    btVector3 conn = toBt(connectionPoint);
    btVector3 dir  = toBt(direction);
    btVector3 axl  = toBt(axle);

    btRaycastVehicle::btVehicleTuning tuning;

    /* Не присваиваем результат addWheel — вызываем как процедуру */
    v->vehicle->addWheel(conn, dir, axl,
                         suspensionRestLength, radius,
                         tuning, isFront);

    /* Индекс нового колеса = последний */
    int idx = v->vehicle->getNumWheels() - 1;

    /* getWheelInfo возвращает ССЫЛКУ — берём её один раз */
    btWheelInfo &wi = v->vehicle->getWheelInfo(idx);
    wi.m_suspensionStiffness      = 20.0f;
    wi.m_wheelsDampingRelaxation  = 2.3f;
    wi.m_wheelsDampingCompression = 4.4f;
    wi.m_frictionSlip             = friction;
    wi.m_rollInfluence            = 0.1f;

    v->wheels[v->wheelCount++] = idx;
    if (isFront) {
        if (v->frontCount < 2) v->frontWheels[v->frontCount++] = idx;
    } else {
        if (v->rearCount < 2)  v->rearWheels[v->rearCount++] = idx;
    }
}

extern "C" void SetVehicle3DEngineForce(Vehicle3D *v, float force) {
    if (!v || !v->vehicle) return;
    for (int i = 0; i < v->rearCount; ++i)
        v->vehicle->applyEngineForce(force, v->rearWheels[i]);
}

extern "C" void SetVehicle3DSteering(Vehicle3D *v, float angle) {
    if (!v || !v->vehicle) return;
    for (int i = 0; i < v->frontCount; ++i)
        v->vehicle->setSteeringValue(angle, v->frontWheels[i]);
}

extern "C" void SetVehicle3DBrake(Vehicle3D *v, float force) {
    if (!v || !v->vehicle) return;
    for (int i = 0; i < v->wheelCount; ++i)
        v->vehicle->setBrake(force, v->wheels[i]);
}

extern "C" void ResetVehicle3D(Vehicle3D *v, Vector3 position,
                                 Quaternion rotation) {
    if (!v || !v->chassis || !v->chassis->body) return;
    btTransform transform;
    transform.setIdentity();
    transform.setOrigin(toBt(position));
    transform.setRotation(toBtQ(rotation));

    btRigidBody *chassis = v->chassis->body;
    chassis->setWorldTransform(transform);
    if (chassis->getMotionState())
        chassis->getMotionState()->setWorldTransform(transform);
    chassis->setLinearVelocity(btVector3(0, 0, 0));
    chassis->setAngularVelocity(btVector3(0, 0, 0));
    chassis->clearForces();
    chassis->activate(true);

    v->vehicle->setCoordinateSystem(0, 1, 2);
    v->vehicle->resetSuspension();
    SetVehicle3DEngineForce(v, 0.0f);
    SetVehicle3DSteering(v, 0.0f);
    SetVehicle3DBrake(v, 0.0f);
    for (int i = 0; i < v->wheelCount; ++i) {
        btWheelInfo &wheel = v->vehicle->getWheelInfo(v->wheels[i]);
        wheel.m_rotation = 0.0f;
        wheel.m_deltaRotation = 0.0f;
        wheel.m_raycastInfo.m_isInContact = false;
        v->vehicle->updateWheelTransform(v->wheels[i], true);
    }
    v->world->world->updateSingleAabb(chassis);
}

extern "C" bool GetVehicle3DWheelTransform(Vehicle3D *v, int wheelIndex,
                                             Vector3 *position,
                                             Quaternion *rotation) {
    if (!v || !v->vehicle || wheelIndex < 0 || wheelIndex >= v->wheelCount)
        return false;
    int bulletWheel = v->wheels[wheelIndex];
    v->vehicle->updateWheelTransform(bulletWheel, true);
    const btTransform &transform =
        v->vehicle->getWheelInfo(bulletWheel).m_worldTransform;
    if (position) *position = fromBt(transform.getOrigin());
    if (rotation) *rotation = fromBtQ(transform.getRotation());
    return true;
}

extern "C" Vector3 GetVehicle3DPosition(const Vehicle3D *v) {
    if (!v || !v->chassis) return Vector3{0,0,0};
    return GetPhysicsBody3DPosition(v->chassis);
}

extern "C" Quaternion GetVehicle3DRotation(const Vehicle3D *v) {
    if (!v || !v->chassis) return Quaternion{0,0,0,1};
    return GetPhysicsBody3DRotation(v->chassis);
}

/* ================================================================== */
/*  Soft body                                                         */
/* ================================================================== */
extern "C" SoftBody3D *CreateSoftBodyCloth(PhysicsWorld3D *world,
                                           Vector3 c00, Vector3 c10,
                                           Vector3 c01, Vector3 c11,
                                           int resX, int resZ,
                                           float mass)
{
    if (!world) return NULL;
    if (resX < 2) resX = 2;
    if (resZ < 2) resZ = 2;

    btSoftBody *psb = btSoftBodyHelpers::CreatePatch(world->sbInfo,
        toBt(c00), toBt(c10), toBt(c01), toBt(c11),
        resX, resZ, 0, true);

    psb->m_cfg.kDF = 0.35f;
    psb->m_cfg.kDP = 0.001f;
    psb->m_cfg.kLF = 0.05f;
    psb->m_cfg.kMT = 0.05f;
    psb->m_cfg.collisions = btSoftBody::fCollision::SDF_RS;

    if (mass > 0.0f) psb->setTotalMass(mass, true);
    world->world->addSoftBody(psb);

    SoftBody3D *out = new SoftBody3D();
    out->world = world;
    out->body  = psb;
    return out;
}

extern "C" SoftBody3D *CreateSoftBodySphere(PhysicsWorld3D *world,
                                            Vector3 center, float radius,
                                            int resolution, float mass)
{
    if (!world) return NULL;
    if (resolution < 4) resolution = 4;

    btSoftBody *psb = btSoftBodyHelpers::CreateEllipsoid(
        world->sbInfo,
        toBt(center),
        btVector3(radius, radius, radius),
        resolution);

    /* kPR (pressure coefficient), а не pressure */
    psb->m_cfg.kPR = 1.3f;
    psb->m_cfg.kDF = 0.15f;
    psb->m_cfg.kDP = 0.001f;
    psb->m_cfg.kMT = 0.05f;

    psb->generateBendingConstraints(2);
    psb->setTotalMass(mass, true);

    world->world->addSoftBody(psb);

    SoftBody3D *out = new SoftBody3D();
    out->world = world;
    out->body  = psb;
    return out;
}

extern "C" void DestroySoftBody3D(SoftBody3D *sb) {
    if (!sb) return;
    if (sb->world && sb->body) sb->world->world->removeSoftBody(sb->body);
    delete sb->body;
    delete sb;
}

extern "C" int GetSoftBody3DVertexCount(const SoftBody3D *sb) {
    if (!sb || !sb->body) return 0;
    return sb->body->m_nodes.size();
}

extern "C" Vector3 GetSoftBody3DVertex(const SoftBody3D *sb, int index) {
    if (!sb || !sb->body) return Vector3{0,0,0};
    if (index < 0 || index >= sb->body->m_nodes.size()) return Vector3{0,0,0};
    return fromBt(sb->body->m_nodes[index].m_x);
}

extern "C" int GetSoftBody3DFaceCount(const SoftBody3D *sb) {
    if (!sb || !sb->body) return 0;
    return sb->body->m_faces.size();
}

extern "C" void GetSoftBody3DFace(const SoftBody3D *sb, int index,
                                  Vector3 *v0, Vector3 *v1, Vector3 *v2)
{
    if (!sb || !sb->body || !v0 || !v1 || !v2) return;
    if (index < 0 || index >= sb->body->m_faces.size()) return;
    const btSoftBody::Face &f = sb->body->m_faces[index];
    *v0 = fromBt(f.m_n[0]->m_x);
    *v1 = fromBt(f.m_n[1]->m_x);
    *v2 = fromBt(f.m_n[2]->m_x);
}

extern "C" void PinSoftBody3DVertex(SoftBody3D *sb, int index) {
    if (!sb || !sb->body) return;
    if (index < 0 || index >= sb->body->m_nodes.size()) return;
    sb->body->setMass(index, 0.0f);
}

/* ================================================================== */
/*  Raycast                                                           */
/* ================================================================== */
extern "C" PhysicsRayHit3D RaycastPhysics3D(PhysicsWorld3D *world,
                                            Vector3 origin, Vector3 direction,
                                            float maxDist)
{
    PhysicsRayHit3D out = {0};
    if (!world || !world->world) return out;

    btVector3 from = toBt(origin);
    btVector3 dir  = toBt(direction);
    if (dir.length2() < 1e-6f) return out;
    dir.normalize();
    btVector3 to = from + dir * maxDist;

    btCollisionWorld::ClosestRayResultCallback cb(from, to);
    world->world->rayTest(from, to, cb);

    if (cb.hasHit()) {
        out.hit      = 1;
        out.distance = cb.m_closestHitFraction * maxDist;
        out.point    = fromBt(cb.m_hitPointWorld);
        out.normal   = fromBt(cb.m_hitNormalWorld);
        out.body     = (PhysicsBody3D *)cb.m_collisionObject->getUserPointer();
    }
    return out;
}

/* ================================================================== */
/*  Model sync (legacy)                                               */
/* ================================================================== */
extern "C" void SyncPhysicsBodyToModel3D(PhysicsBody3D *b, Model *model) {
    (void)b; (void)model;
}
