#ifndef VLCRLIB_VBEZIER_H
#define VLCRLIB_VBEZIER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "vcore.h"

/* --- Точки на кривой --- */
Vector2 GetBezierQuadPoint(Vector2 p0, Vector2 p1, Vector2 p2, float t);
Vector2 GetBezierCubicPoint(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3, float t);
Vector3 GetBezierQuadPoint3D(Vector3 p0, Vector3 p1, Vector3 p2, float t);
Vector3 GetBezierCubicPoint3D(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t);

/* --- Производные (касательные) --- */
Vector2 GetBezierCubicTangent(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3, float t);
Vector3 GetBezierCubicTangent3D(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t);

/* --- Длина --- */
float GetBezierCubicLength(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3, int samples);

/* --- Рисование (2D) --- */
void DrawBezierQuad(Vector2 p0, Vector2 p1, Vector2 p2,
                    float thick, Color color, int segments);
void DrawBezierCubic(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3,
                     float thick, Color color, int segments);

/* --- Рисование (3D) --- */
void DrawBezierQuad3D(Vector3 p0, Vector3 p1, Vector3 p2,
                      Color color, int segments);
void DrawBezierCubic3D(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3,
                       Color color, int segments);

#ifdef __cplusplus
}
#endif
#endif