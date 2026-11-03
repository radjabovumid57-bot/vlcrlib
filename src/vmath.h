#ifndef VLCRLIB_VMATH_H
#define VLCRLIB_VMATH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <math.h>
#include <stdbool.h>

#ifdef __cplusplus
#define VLCRLIB_COMPOUND_LITERAL(type) type
#else
#define VLCRLIB_COMPOUND_LITERAL(type) (type)
#endif

#define VLCRLIB_PI      3.14159265358979323846f
#define VLCRLIB_DEG2RAD (VLCRLIB_PI / 180.0f)
#define VLCRLIB_RAD2DEG (180.0f / VLCRLIB_PI)

typedef struct Vector2 { float x, y; } Vector2;
typedef struct Vector3 { float x, y, z; } Vector3;
typedef struct Vector4 { float x, y, z, w; } Vector4;
typedef struct Quaternion { float x, y, z, w; } Quaternion;

typedef struct Matrix {
    float m0,  m4,  m8,  m12;
    float m1,  m5,  m9,  m13;
    float m2,  m6,  m10, m14;
    float m3,  m7,  m11, m15;
} Matrix;

/* ---------- Vector2 ---------- */
static inline Vector2 Vector2Add(Vector2 a, Vector2 b)      { return VLCRLIB_COMPOUND_LITERAL(Vector2){a.x+b.x, a.y+b.y}; }
static inline Vector2 Vector2Subtract(Vector2 a, Vector2 b) { return VLCRLIB_COMPOUND_LITERAL(Vector2){a.x-b.x, a.y-b.y}; }
static inline Vector2 Vector2Scale(Vector2 v, float s)      { return VLCRLIB_COMPOUND_LITERAL(Vector2){v.x*s, v.y*s}; }
static inline float   Vector2Length(Vector2 v)              { return sqrtf(v.x*v.x + v.y*v.y); }
static inline float   Vector2Dot(Vector2 a, Vector2 b)      { return a.x*b.x + a.y*b.y; }
static inline Vector2 Vector2Normalize(Vector2 v) {
    float l = Vector2Length(v);
    if (l < 1e-8f) return VLCRLIB_COMPOUND_LITERAL(Vector2){0,0};
    return VLCRLIB_COMPOUND_LITERAL(Vector2){v.x/l, v.y/l};
}
static inline float Vector2Distance(Vector2 a, Vector2 b) {
    float dx = a.x-b.x, dy = a.y-b.y;
    return sqrtf(dx*dx + dy*dy);
}
static inline Vector2 Vector2Lerp(Vector2 a, Vector2 b, float t) {
    return VLCRLIB_COMPOUND_LITERAL(Vector2){a.x + (b.x-a.x)*t, a.y + (b.y-a.y)*t};
}

/* ---------- Vector3 ---------- */
static inline Vector3 Vector3Add(Vector3 a, Vector3 b)      { return VLCRLIB_COMPOUND_LITERAL(Vector3){a.x+b.x, a.y+b.y, a.z+b.z}; }
static inline Vector3 Vector3Subtract(Vector3 a, Vector3 b) { return VLCRLIB_COMPOUND_LITERAL(Vector3){a.x-b.x, a.y-b.y, a.z-b.z}; }
static inline Vector3 Vector3Scale(Vector3 v, float s)      { return VLCRLIB_COMPOUND_LITERAL(Vector3){v.x*s, v.y*s, v.z*s}; }
static inline float   Vector3Length(Vector3 v)              { return sqrtf(v.x*v.x + v.y*v.y + v.z*v.z); }
static inline float   Vector3Dot(Vector3 a, Vector3 b)      { return a.x*b.x + a.y*b.y + a.z*b.z; }
static inline Vector3 Vector3Cross(Vector3 a, Vector3 b) {
    return VLCRLIB_COMPOUND_LITERAL(Vector3){a.y*b.z - a.z*b.y,
                     a.z*b.x - a.x*b.z,
                     a.x*b.y - a.y*b.x};
}
static inline Vector3 Vector3Normalize(Vector3 v) {
    float l = Vector3Length(v);
    if (l < 1e-8f) return VLCRLIB_COMPOUND_LITERAL(Vector3){0,0,0};
    return VLCRLIB_COMPOUND_LITERAL(Vector3){v.x/l, v.y/l, v.z/l};
}
static inline float Vector3Distance(Vector3 a, Vector3 b) {
    float dx=a.x-b.x, dy=a.y-b.y, dz=a.z-b.z;
    return sqrtf(dx*dx + dy*dy + dz*dz);
}
static inline Vector3 Vector3Lerp(Vector3 a, Vector3 b, float t) {
    return VLCRLIB_COMPOUND_LITERAL(Vector3){a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, a.z+(b.z-a.z)*t};
}
/* Analytic two-bone IK. Inputs are joint positions; the pole vector chooses
   the bend plane. Outputs are world-space mid/end positions. */
static inline bool Vector3SolveTwoBoneIK(Vector3 root, Vector3 mid, Vector3 end,
                                         Vector3 target, Vector3 pole,
                                         Vector3 *outMid, Vector3 *outEnd) {
    float l1 = Vector3Distance(root, mid);
    float l2 = Vector3Distance(mid, end);
    if (l1 < 1e-5f || l2 < 1e-5f) return false;
    Vector3 toTarget = Vector3Subtract(target, root);
    float d = Vector3Length(toTarget);
    if (d < 1e-5f) return false;
    Vector3 dir = Vector3Scale(toTarget, 1.0f / d);
    float minD = fabsf(l1 - l2) + 1e-4f;
    float maxD = l1 + l2 - 1e-4f;
    if (d < minD) d = minD;
    if (d > maxD) d = maxD;
    float cosRoot = (l1*l1 + d*d - l2*l2) / (2.0f * l1 * d);
    if (cosRoot > 1.0f) cosRoot = 1.0f;
    if (cosRoot < -1.0f) cosRoot = -1.0f;
    float sinRoot = sqrtf(1.0f - cosRoot*cosRoot);

    Vector3 bend = Vector3Subtract(pole, Vector3Scale(dir, Vector3Dot(pole, dir)));
    if (Vector3Length(bend) < 1e-5f) {
        Vector3 axis = fabsf(dir.y) > 0.9f ? VLCRLIB_COMPOUND_LITERAL(Vector3){0,0,1}
                                           : VLCRLIB_COMPOUND_LITERAL(Vector3){0,1,0};
        bend = Vector3Cross(dir, axis);
    }
    bend = Vector3Normalize(bend);
    Vector3 midOut = Vector3Add(root,
        Vector3Add(Vector3Scale(dir, l1 * cosRoot), Vector3Scale(bend, l1 * sinRoot)));
    Vector3 endOut = Vector3Add(root, Vector3Scale(dir, d));
    if (outMid) *outMid = midOut;
    if (outEnd) *outEnd = endOut;
    return true;
}

static inline Vector3 Vector3Reflect(Vector3 v, Vector3 n) {
    float d = Vector3Dot(v, n);
    return Vector3Subtract(v, Vector3Scale(n, 2.0f*d));
}

/* ---------- Matrix ---------- */
static inline Matrix MatrixIdentity(void) {
    return VLCRLIB_COMPOUND_LITERAL(Matrix){ 1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0,0,1 };
}
static inline Matrix MatrixTranslate(float x, float y, float z) {
    Matrix m = MatrixIdentity();
    m.m12 = x; m.m13 = y; m.m14 = z;
    return m;
}
static inline Matrix MatrixScale(float x, float y, float z) {
    Matrix m = MatrixIdentity();
    m.m0 = x; m.m5 = y; m.m10 = z;
    return m;
}
static inline Matrix MatrixMultiply(Matrix a, Matrix b) {
    const float av[16] = {
        a.m0, a.m1, a.m2, a.m3, a.m4, a.m5, a.m6, a.m7,
        a.m8, a.m9, a.m10, a.m11, a.m12, a.m13, a.m14, a.m15
    };
    const float bv[16] = {
        b.m0, b.m1, b.m2, b.m3, b.m4, b.m5, b.m6, b.m7,
        b.m8, b.m9, b.m10, b.m11, b.m12, b.m13, b.m14, b.m15
    };
    float rv[16] = {0};
    for (int c = 0; c < 4; ++c)
        for (int rw = 0; rw < 4; ++rw) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k)
                s += av[rw + k*4] * bv[k + c*4];
            rv[rw + c*4] = s;
        }
    return VLCRLIB_COMPOUND_LITERAL(Matrix){
        rv[0], rv[4], rv[8], rv[12],
        rv[1], rv[5], rv[9], rv[13],
        rv[2], rv[6], rv[10], rv[14],
        rv[3], rv[7], rv[11], rv[15]
    };
}
static inline Matrix MatrixRotateX(float a) {
    Matrix m = MatrixIdentity();
    float c = cosf(a), s = sinf(a);
    m.m5 = c; m.m6 = s;
    m.m9 = -s; m.m10 = c;
    return m;
}
static inline Matrix MatrixRotateY(float a) {
    Matrix m = MatrixIdentity();
    float c = cosf(a), s = sinf(a);
    m.m0 = c; m.m2 = -s;
    m.m8 = s; m.m10 = c;
    return m;
}
static inline Matrix MatrixRotateZ(float a) {
    Matrix m = MatrixIdentity();
    float c = cosf(a), s = sinf(a);
    m.m0 = c; m.m1 = s;
    m.m4 = -s; m.m5 = c;
    return m;
}
static inline Matrix MatrixRotateXYZ(Vector3 a) {
    return MatrixMultiply(MatrixMultiply(MatrixRotateZ(a.z), MatrixRotateY(a.y)), MatrixRotateX(a.x));
}
static inline Matrix MatrixRotate(Vector3 axis, float angle) {
    Vector3 n = Vector3Normalize(axis);
    float c = cosf(angle), s = sinf(angle), t = 1.0f - c;
    Matrix m = MatrixIdentity();
    m.m0 = t*n.x*n.x + c;
    m.m1 = t*n.x*n.y + s*n.z;
    m.m2 = t*n.x*n.z - s*n.y;
    m.m4 = t*n.x*n.y - s*n.z;
    m.m5 = t*n.y*n.y + c;
    m.m6 = t*n.y*n.z + s*n.x;
    m.m8 = t*n.x*n.z + s*n.y;
    m.m9 = t*n.y*n.z - s*n.x;
    m.m10 = t*n.z*n.z + c;
    return m;
}
/* ==================================================================
 *  Projection conventions — vlcrlib world/NDC contract
 * ==================================================================
 *
 *  WORLD
 *    Right-handed, Y up. The floor is the XZ plane at y = 0.
 *    Gravity is (0, -9.81, 0). Units are metres by convention.
 *
 *  VIEW
 *    MatrixLookAt(eye, target, up) is right-handed: the camera looks
 *    down its own -Z, so z = normalize(eye - target). With up = (0,1,0)
 *    world +Y maps to view +Y.
 *
 *  CLIP / NDC  (Vulkan)
 *    x, y in [-1,1]; z in [0,1] — near plane at z=0, far plane at z=1.
 *    Depth is the "zero-to-one" (ZO) convention. GL's [-1,1] depth must
 *    NOT be used: Vulkan clips z < 0, so an OpenGL-style matrix silently
 *    discards everything closer than 2*near/(near+far) and halves the
 *    usable depth range.
 *
 *  SCREEN
 *    Vulkan framebuffer coordinates have their origin at the TOP-LEFT
 *    with y growing downwards. Two different, deliberately opposite
 *    strategies are used so that both render with a conventional origin:
 *
 *      3D geometry  -> MatrixPerspective() negates Y (m5 = -f) and is
 *                      drawn with a POSITIVE-height viewport, so world +Y
 *                      appears up on screen.
 *      2D / HUD     -> MatrixOrtho(0, w, h, 0, ...) with y = 0 at the top,
 *                      drawn with a NEGATIVE-height viewport (y + h, h = -h).
 *
 *    Because the 3D projection mirrors Y, triangle winding is also
 *    mirrored: pipelines therefore keep frontFace = COUNTER_CLOCKWISE,
 *    which is what glTF (CCW in a Y-up, right-handed world) expects.
 *
 *  Keep these two paths consistent. Mixing them up is what produces a
 *  scene that appears rotated by 180 degrees (Y up becomes Y down).
 * ================================================================== */

/* Perspective projection, Vulkan ZO depth. Y is negated so that a Y-up
 * world renders Y-up on a y-down framebuffer (see conventions above). */
static inline Matrix MatrixPerspective(float fovyRad, float aspect, float zn, float zf) {
    /* Guard degenerate inputs: aspect 0 would divide by zero, and a
       zero/negative far plane would produce a singular matrix. */
    if (!(aspect > 0.0f)) aspect = 1.0f;
    if (!(fovyRad > 0.0f) || fovyRad >= VLCRLIB_PI) fovyRad = 45.0f * VLCRLIB_DEG2RAD;
    if (!(zn > 0.0f)) zn = 0.01f;
    if (!(zf > zn)) zf = zn + 1000.0f;

    float f = 1.0f / tanf(fovyRad * 0.5f);
    Matrix m = {0};
    m.m0  = f / aspect;
    m.m5  = -f;                        /* Vulkan Y flip (Y-down framebuffer) */
    m.m10 = zf / (zn - zf);            /* ZO: near -> 0, far -> 1 */
    m.m11 = -1.0f;
    m.m14 = (zf * zn) / (zn - zf);
    return m;
}

/* Orthographic projection, Vulkan ZO depth (near -> 0, far -> 1). */
static inline Matrix MatrixOrtho(float l, float r, float b, float t, float zn, float zf) {
    /* Guard zero-width ranges so the matrix never becomes singular. */
    if (r == l) r = l + 1.0f;
    if (t == b) t = b + 1.0f;
    if (zf == zn) zf = zn + 1.0f;

    Matrix m = MatrixIdentity();
    m.m0  = 2.0f / (r - l);
    m.m5  = 2.0f / (t - b);
    m.m10 = -1.0f / (zf - zn);         /* ZO: near -> 0, far -> 1 */
    m.m12 = -(r + l) / (r - l);
    m.m13 = -(t + b) / (t - b);
    m.m14 = -zn / (zf - zn);
    return m;
}

static inline Matrix MatrixLookAt(Vector3 eye, Vector3 target, Vector3 up) {
    Vector3 z = Vector3Normalize(Vector3Subtract(eye, target));
    /* Looking straight along the up axis makes cross(up, z) zero, which would
       propagate NaN through the whole view matrix (black screen). Fall back to
       a perpendicular up vector, the same way the shadow code does. */
    Vector3 u = up;
    if (fabsf(Vector3Dot(Vector3Normalize(u), z)) > 0.999f)
        u = VLCRLIB_COMPOUND_LITERAL(Vector3){ 0.0f, 0.0f, 1.0f };
    Vector3 x = Vector3Normalize(Vector3Cross(u, z));
    Vector3 y = Vector3Cross(z, x);
    Matrix m = MatrixIdentity();
    m.m0 = x.x; m.m1 = y.x; m.m2 = z.x;
    m.m4 = x.y; m.m5 = y.y; m.m6 = z.y;
    m.m8 = x.z; m.m9 = y.z; m.m10 = z.z;
    m.m12 = -Vector3Dot(x, eye);
    m.m13 = -Vector3Dot(y, eye);
    m.m14 = -Vector3Dot(z, eye);
    return m;
}

/* ---------- Quaternion ---------- */
static inline Quaternion QuaternionIdentity(void) { return VLCRLIB_COMPOUND_LITERAL(Quaternion){0,0,0,1}; }
static inline Quaternion QuaternionNormalize(Quaternion q) {
    float l = sqrtf(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (l < 1e-8f) return QuaternionIdentity();
    return VLCRLIB_COMPOUND_LITERAL(Quaternion){q.x/l, q.y/l, q.z/l, q.w/l};
}
static inline Quaternion QuaternionMultiply(Quaternion a, Quaternion b) {
    return VLCRLIB_COMPOUND_LITERAL(Quaternion){
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z
    };
}
static inline Quaternion QuaternionFromEuler(float pitch, float yaw, float roll) {
    float cy = cosf(yaw*0.5f),  sy = sinf(yaw*0.5f);
    float cp = cosf(pitch*0.5f), sp = sinf(pitch*0.5f);
    float cr = cosf(roll*0.5f), sr = sinf(roll*0.5f);
    return VLCRLIB_COMPOUND_LITERAL(Quaternion){
        sr*cp*cy - cr*sp*sy,
        cr*sp*cy + sr*cp*sy,
        cr*cp*sy - sr*sp*cy,
        cr*cp*cy + sr*sp*sy
    };
}
static inline Quaternion QuaternionFromAxisAngle(Vector3 axis, float angle) {
    Vector3 a = Vector3Normalize(axis);
    float h = angle * 0.5f, s = sinf(h);
    return VLCRLIB_COMPOUND_LITERAL(Quaternion){a.x*s, a.y*s, a.z*s, cosf(h)};
}
static inline Quaternion QuaternionSlerp(Quaternion a, Quaternion b, float t) {
    float d = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
    if (d < 0.0f) { b.x=-b.x; b.y=-b.y; b.z=-b.z; b.w=-b.w; d = -d; }
    if (d > 1.0f) d = 1.0f;
    if (d > 0.9995f) {
        return QuaternionNormalize(VLCRLIB_COMPOUND_LITERAL(Quaternion){
            a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t,
            a.z+(b.z-a.z)*t, a.w+(b.w-a.w)*t
        });
    }
    float th0 = acosf(d), th = th0 * t;
    float s0 = cosf(th) - d * sinf(th) / sinf(th0);
    float s1 = sinf(th) / sinf(th0);
    return VLCRLIB_COMPOUND_LITERAL(Quaternion){
        a.x*s0 + b.x*s1, a.y*s0 + b.y*s1,
        a.z*s0 + b.z*s1, a.w*s0 + b.w*s1
    };
}
static inline Quaternion QuaternionFromMatrix(Matrix m) {
    float trace = m.m0 + m.m5 + m.m10;
    Quaternion q;
    if (trace > 0.0f) {
        float s = sqrtf(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m.m6 - m.m9) / s;
        q.y = (m.m8 - m.m2) / s;
        q.z = (m.m1 - m.m4) / s;
    } else if (m.m0 > m.m5 && m.m0 > m.m10) {
        float s = sqrtf(1.0f + m.m0 - m.m5 - m.m10) * 2.0f;
        q.w = (m.m6 - m.m9) / s;
        q.x = 0.25f * s;
        q.y = (m.m4 + m.m1) / s;
        q.z = (m.m8 + m.m2) / s;
    } else if (m.m5 > m.m10) {
        float s = sqrtf(1.0f + m.m5 - m.m0 - m.m10) * 2.0f;
        q.w = (m.m8 - m.m2) / s;
        q.x = (m.m4 + m.m1) / s;
        q.y = 0.25f * s;
        q.z = (m.m9 + m.m6) / s;
    } else {
        float s = sqrtf(1.0f + m.m10 - m.m0 - m.m5) * 2.0f;
        q.w = (m.m1 - m.m4) / s;
        q.x = (m.m8 + m.m2) / s;
        q.y = (m.m9 + m.m6) / s;
        q.z = 0.25f * s;
    }
    return QuaternionNormalize(q);
}

static inline Vector3 QuaternionToEuler(Quaternion q) {
    /* Inverse of MatrixRotateXYZ = Rz * Ry * Rx. */
    float sinY = 2.0f * (q.w*q.y - q.z*q.x);
    if (sinY > 1.0f) sinY = 1.0f;
    if (sinY < -1.0f) sinY = -1.0f;
    float y = asinf(sinY);
    float x = atan2f(2.0f*(q.w*q.x + q.y*q.z),
                     1.0f - 2.0f*(q.x*q.x + q.y*q.y));
    float z = atan2f(2.0f*(q.w*q.z + q.x*q.y),
                     1.0f - 2.0f*(q.y*q.y + q.z*q.z));
    return VLCRLIB_COMPOUND_LITERAL(Vector3){x, y, z};
}

static inline bool MatrixDecomposeTRS(Matrix m, Vector3 *outT,
                                      Vector3 *outR, Vector3 *outS) {
    if (outT) *outT = VLCRLIB_COMPOUND_LITERAL(Vector3){m.m12, m.m13, m.m14};
    float sx = sqrtf(m.m0*m.m0 + m.m1*m.m1 + m.m2*m.m2);
    float sy = sqrtf(m.m4*m.m4 + m.m5*m.m5 + m.m6*m.m6);
    float sz = sqrtf(m.m8*m.m8 + m.m9*m.m9 + m.m10*m.m10);
    if (sx < 1e-6f || sy < 1e-6f || sz < 1e-6f) return false;
    Matrix r = m;
    r.m0 /= sx; r.m1 /= sx; r.m2 /= sx;
    r.m4 /= sy; r.m5 /= sy; r.m6 /= sy;
    r.m8 /= sz; r.m9 /= sz; r.m10 /= sz;
    /* A negative determinant means one axis is mirrored. Fold the mirror
       into X so a -X scale round-trips instead of becoming a rotation. */
    float det = r.m0 * (r.m5 * r.m10 - r.m6 * r.m9)
              - r.m4 * (r.m1 * r.m10 - r.m2 * r.m9)
              + r.m8 * (r.m1 * r.m6 - r.m2 * r.m5);
    if (det < 0.0f) {
        sx = -sx;
        r.m0 = -r.m0; r.m1 = -r.m1; r.m2 = -r.m2;
    }
    Quaternion q = QuaternionFromMatrix(r);
    if (outR) *outR = QuaternionToEuler(q);
    if (outS) *outS = VLCRLIB_COMPOUND_LITERAL(Vector3){sx, sy, sz};
    return true;
}

static inline Matrix QuaternionToMatrix(Quaternion q) {
    Matrix m = MatrixIdentity();
    float x=q.x, y=q.y, z=q.z, w=q.w;
    m.m0  = 1 - 2*y*y - 2*z*z;
    m.m1  = 2*x*y + 2*w*z;
    m.m2  = 2*x*z - 2*w*y;
    m.m4  = 2*x*y - 2*w*z;
    m.m5  = 1 - 2*x*x - 2*z*z;
    m.m6  = 2*y*z + 2*w*x;
    m.m8  = 2*x*z + 2*w*y;
    m.m9  = 2*y*z - 2*w*x;
    m.m10 = 1 - 2*x*x - 2*y*y;
    return m;
}
static inline Matrix MatrixTranspose(Matrix m) {
    /* Transpose в layout struct = переставить m1<->m4, m2<->m8, m3<->m12,
       m6<->m9, m7<->m13, m11<->m14. Проще всего через initializer:
       поля идут m0,m4,m8,m12, m1,m5,m9,m13, m2,m6,m10,m14, m3,m7,m11,m15. */
    return VLCRLIB_COMPOUND_LITERAL(Matrix){
        m.m0,  m.m1,  m.m2,  m.m3,
        m.m4,  m.m5,  m.m6,  m.m7,
        m.m8,  m.m9,  m.m10, m.m11,
        m.m12, m.m13, m.m14, m.m15
    };
}

static inline Matrix MatrixInvert(Matrix m) {
    /* Affine inverse: предполагается, что нижняя строка == [0 0 0 1].
       Для трансформов model/view/translation/rotation/scale — всегда так. */
    float a00 = m.m0, a01 = m.m4, a02 = m.m8;
    float a10 = m.m1, a11 = m.m5, a12 = m.m9;
    float a20 = m.m2, a21 = m.m6, a22 = m.m10;

    float det = a00 * (a11*a22 - a12*a21)
              - a01 * (a10*a22 - a12*a20)
              + a02 * (a10*a21 - a11*a20);

    if (fabsf(det) < 1e-12f) return MatrixIdentity();
    float invDet = 1.0f / det;

    float b00 = (a11*a22 - a12*a21) * invDet;
    float b01 = (a02*a21 - a01*a22) * invDet;
    float b02 = (a01*a12 - a02*a11) * invDet;
    float b10 = (a12*a20 - a10*a22) * invDet;
    float b11 = (a00*a22 - a02*a20) * invDet;
    float b12 = (a02*a10 - a00*a12) * invDet;
    float b20 = (a10*a21 - a11*a20) * invDet;
    float b21 = (a01*a20 - a00*a21) * invDet;
    float b22 = (a00*a11 - a01*a10) * invDet;

    float tx = m.m12, ty = m.m13, tz = m.m14;

    Matrix r = MatrixIdentity();
    r.m0  = b00;  r.m4  = b01;  r.m8  = b02;
    r.m1  = b10;  r.m5  = b11;  r.m9  = b12;
    r.m2  = b20;  r.m6  = b21;  r.m10 = b22;
    r.m12 = -(b00*tx + b01*ty + b02*tz);
    r.m13 = -(b10*tx + b11*ty + b12*tz);
    r.m14 = -(b20*tx + b21*ty + b22*tz);
    return r;
}

/* Нормальная матрица = transpose(inverse(model)) для неравномерного scale. */
static inline Matrix MatrixNormalFromModel(Matrix model) {
    return MatrixTranspose(MatrixInvert(model));
}

/* ---------- Utility ---------- */
static inline float Clamp(float v, float mn, float mx) { return v<mn?mn:(v>mx?mx:v); }
static inline float Lerp(float a, float b, float t)    { return a + (b-a)*t; }

#ifdef __cplusplus
}
#endif
#endif /* VLCRLIB_VMATH_H */