#include "vbezier.h"
#include "vshapes.h"
#include "vgraphics.h"
#include <math.h>

/* ------------------------------------------------------------------ */
/*  Точки                                                             */
/* ------------------------------------------------------------------ */

Vector2 GetBezierQuadPoint(Vector2 p0, Vector2 p1, Vector2 p2, float t) {
    float u = 1.0f - t;
    return (Vector2){
        u*u*p0.x + 2*u*t*p1.x + t*t*p2.x,
        u*u*p0.y + 2*u*t*p1.y + t*t*p2.y
    };
}

Vector2 GetBezierCubicPoint(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3, float t) {
    float u = 1.0f - t;
    float uu = u*u, tt = t*t;
    float uuu = uu*u, ttt = tt*t;
    return (Vector2){
        uuu*p0.x + 3*uu*t*p1.x + 3*u*tt*p2.x + ttt*p3.x,
        uuu*p0.y + 3*uu*t*p1.y + 3*u*tt*p2.y + ttt*p3.y
    };
}

Vector3 GetBezierQuadPoint3D(Vector3 p0, Vector3 p1, Vector3 p2, float t) {
    float u = 1.0f - t;
    return (Vector3){
        u*u*p0.x + 2*u*t*p1.x + t*t*p2.x,
        u*u*p0.y + 2*u*t*p1.y + t*t*p2.y,
        u*u*p0.z + 2*u*t*p1.z + t*t*p2.z
    };
}

Vector3 GetBezierCubicPoint3D(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
    float u = 1.0f - t;
    float uu = u*u, tt = t*t;
    float uuu = uu*u, ttt = tt*t;
    return (Vector3){
        uuu*p0.x + 3*uu*t*p1.x + 3*u*tt*p2.x + ttt*p3.x,
        uuu*p0.y + 3*uu*t*p1.y + 3*u*tt*p2.y + ttt*p3.y,
        uuu*p0.z + 3*uu*t*p1.z + 3*u*tt*p2.z + ttt*p3.z
    };
}

/* ------------------------------------------------------------------ */
/*  Касательные                                                       */
/* ------------------------------------------------------------------ */

Vector2 GetBezierCubicTangent(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3, float t) {
    float u = 1.0f - t;
    return (Vector2){
        3*u*u*(p1.x - p0.x) + 6*u*t*(p2.x - p1.x) + 3*t*t*(p3.x - p2.x),
        3*u*u*(p1.y - p0.y) + 6*u*t*(p2.y - p1.y) + 3*t*t*(p3.y - p2.y)
    };
}

Vector3 GetBezierCubicTangent3D(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
    float u = 1.0f - t;
    return (Vector3){
        3*u*u*(p1.x - p0.x) + 6*u*t*(p2.x - p1.x) + 3*t*t*(p3.x - p2.x),
        3*u*u*(p1.y - p0.y) + 6*u*t*(p2.y - p1.y) + 3*t*t*(p3.y - p2.y),
        3*u*u*(p1.z - p0.z) + 6*u*t*(p2.z - p1.z) + 3*t*t*(p3.z - p2.z)
    };
}

/* ------------------------------------------------------------------ */
/*  Длина                                                             */
/* ------------------------------------------------------------------ */

float GetBezierCubicLength(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3, int samples) {
    if (samples < 4) samples = 4;
    float len = 0.0f;
    Vector2 prev = p0;
    for (int i = 1; i <= samples; ++i) {
        float t = (float)i / samples;
        Vector2 cur = GetBezierCubicPoint(p0, p1, p2, p3, t);
        float dx = cur.x - prev.x;
        float dy = cur.y - prev.y;
        len += sqrtf(dx*dx + dy*dy);
        prev = cur;
    }
    return len;
}

/* ------------------------------------------------------------------ */
/*  Рисование                                                         */
/* ------------------------------------------------------------------ */

void DrawBezierQuad(Vector2 p0, Vector2 p1, Vector2 p2,
                    float thick, Color color, int segments)
{
    if (segments < 2) segments = 2;
    Vector2 prev = p0;
    for (int i = 1; i <= segments; ++i) {
        float t = (float)i / segments;
        Vector2 cur = GetBezierQuadPoint(p0, p1, p2, t);
        DrawLineEx(prev, cur, thick, color);
        prev = cur;
    }
}

void DrawBezierCubic(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3,
                     float thick, Color color, int segments)
{
    if (segments < 2) segments = 2;
    Vector2 prev = p0;
    for (int i = 1; i <= segments; ++i) {
        float t = (float)i / segments;
        Vector2 cur = GetBezierCubicPoint(p0, p1, p2, p3, t);
        DrawLineEx(prev, cur, thick, color);
        prev = cur;
    }
}

void DrawBezierQuad3D(Vector3 p0, Vector3 p1, Vector3 p2,
                      Color color, int segments)
{
    if (segments < 2) segments = 2;
    Vector3 prev = p0;
    for (int i = 1; i <= segments; ++i) {
        float t = (float)i / segments;
        Vector3 cur = GetBezierQuadPoint3D(p0, p1, p2, t);
        DrawLine3D(prev, cur, color);
        prev = cur;
    }
}

void DrawBezierCubic3D(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3,
                       Color color, int segments)
{
    if (segments < 2) segments = 2;
    Vector3 prev = p0;
    for (int i = 1; i <= segments; ++i) {
        float t = (float)i / segments;
        Vector3 cur = GetBezierCubicPoint3D(p0, p1, p2, p3, t);
        DrawLine3D(prev, cur, color);
        prev = cur;
    }
}