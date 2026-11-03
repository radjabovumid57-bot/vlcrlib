#ifndef VLCRLIB_VSHAPES_H
#define VLCRLIB_VSHAPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "vcore.h"

void DrawPixel(int x, int y, Color color);
void DrawLine(int x0, int y0, int x1, int y1, Color color);
void DrawLineEx(Vector2 start, Vector2 end, float thick, Color color);
void DrawCircle(int cx, int cy, float radius, Color color);
void DrawCircleV(Vector2 center, float radius, Color color);
void DrawCircleLines(int cx, int cy, float radius, Color color);   /* ← добавь */
void DrawRectangle(int x, int y, int w, int h, Color color);
void DrawRectangleRec(Rectangle rec, Color color);
void DrawRectanglePro(Rectangle rec, Vector2 origin, float rotation, Color color);
void DrawRectangleLines(int x, int y, int w, int h, Color color);
void DrawTriangle(Vector2 v1, Vector2 v2, Vector2 v3, Color color);
void DrawPoly(Vector2 center, int sides, float radius, float rotation, Color color);


void vshapes_FlushBatch(void);

#ifdef __cplusplus
}
#endif
#endif /* VLCRLIB_VSHAPES_H */