#include "vshapes.h"
#include "vgraphics.h"
#include <stdlib.h>
#include <math.h>

static Vertex2D *g_batch = NULL;
static int       g_count = 0;
static int       g_cap   = 0;

static void push(Vertex2D v) {
    if (g_count >= g_cap) {
        g_cap = g_cap ? g_cap * 2 : 4096;
        g_batch = realloc(g_batch, (size_t)g_cap * sizeof(Vertex2D));
    }
    g_batch[g_count++] = v;
}

static Vertex2D vtx(float x, float y, float u, float v, Color c) {
    Vertex2D out = { x, y, u, v, c.r, c.g, c.b, c.a };
    return out;
}

void vshapes_FlushBatch(void) {
    if (g_count == 0) return;
    vgraphics_DrawVertexBuffer(g_batch, g_count, (Texture2D){0});
    g_count = 0;
}

static void quad(Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color col) {
    push(vtx(a.x, a.y, 0, 0, col));
    push(vtx(b.x, b.y, 0, 0, col));
    push(vtx(c.x, c.y, 0, 0, col));
    push(vtx(a.x, a.y, 0, 0, col));
    push(vtx(c.x, c.y, 0, 0, col));
    push(vtx(d.x, d.y, 0, 0, col));
}

void DrawPixel(int x, int y, Color color) {
    quad((Vector2){x, y}, (Vector2){x+1, y}, (Vector2){x+1, y+1}, (Vector2){x, y+1}, color);
}

void DrawLine(int x0, int y0, int x1, int y1, Color color) {
    DrawLineEx((Vector2){x0, y0}, (Vector2){x1, y1}, 1.0f, color);
}

void DrawLineEx(Vector2 start, Vector2 end, float thick, Color color) {
    Vector2 d = Vector2Subtract(end, start);
    float len = Vector2Length(d);
    if (len < 1e-6f) return;
    Vector2 dir = { d.x / len, d.y / len };
    Vector2 nrm = { -dir.y * thick * 0.5f, dir.x * thick * 0.5f };
    Vector2 a = { start.x + nrm.x, start.y + nrm.y };
    Vector2 b = { end.x   + nrm.x, end.y   + nrm.y };
    Vector2 c = { end.x   - nrm.x, end.y   - nrm.y };
    Vector2 dd= { start.x - nrm.x, start.y - nrm.y };
    quad(a, b, c, dd, color);
}

void DrawCircleV(Vector2 center, float radius, Color color) {
    const int N = 48;
    for (int i = 0; i < N; ++i) {
        float a0 = (float)i / N * 2.0f * VLCRLIB_PI;
        float a1 = (float)(i + 1) / N * 2.0f * VLCRLIB_PI;
        Vector2 p0 = { center.x + cosf(a0) * radius, center.y + sinf(a0) * radius };
        Vector2 p1 = { center.x + cosf(a1) * radius, center.y + sinf(a1) * radius };
        push(vtx(center.x, center.y, 0, 0, color));
        push(vtx(p0.x, p0.y, 0, 0, color));
        push(vtx(p1.x, p1.y, 0, 0, color));
    }
}

void DrawCircle(int cx, int cy, float radius, Color color) {
    DrawCircleV((Vector2){ (float)cx, (float)cy }, radius, color);
}

void DrawCircleLines(int cx, int cy, float radius, Color color) {
    const int N = 48;
    Vector2 prev = { cx + radius, (float)cy };
    for (int i = 1; i <= N; ++i) {
        float a = (float)i / N * 2.0f * VLCRLIB_PI;
        Vector2 cur = { cx + cosf(a) * radius, cy + sinf(a) * radius };
        DrawLineEx(prev, cur, 1.5f, color);
        prev = cur;
    }
}

void DrawRectangle(int x, int y, int w, int h, Color color) {
    DrawRectangleRec((Rectangle){ (float)x, (float)y, (float)w, (float)h }, color);
}

void DrawRectangleRec(Rectangle r, Color color) {
    quad((Vector2){r.x, r.y},
         (Vector2){r.x + r.width, r.y},
         (Vector2){r.x + r.width, r.y + r.height},
         (Vector2){r.x, r.y + r.height}, color);
}

void DrawRectangleLines(int x, int y, int w, int h, Color color) {
    DrawRectangle(x, y, w, 1, color);
    DrawRectangle(x, y + h - 1, w, 1, color);
    DrawRectangle(x, y, 1, h, color);
    DrawRectangle(x + w - 1, y, 1, h, color);
}

void DrawRectanglePro(Rectangle rec, Vector2 origin, float rotation, Color color) {
    float c = cosf(rotation), s = sinf(rotation);
    Vector2 pts[4] = {
        { rec.x,                rec.y },
        { rec.x + rec.width,    rec.y },
        { rec.x + rec.width,    rec.y + rec.height },
        { rec.x,                rec.y + rec.height }
    };
    for (int i = 0; i < 4; ++i) {
        float dx = pts[i].x - origin.x;
        float dy = pts[i].y - origin.y;
        pts[i].x = origin.x + dx * c - dy * s;
        pts[i].y = origin.y + dx * s + dy * c;
    }
    push(vtx(pts[0].x, pts[0].y, 0, 0, color));
    push(vtx(pts[1].x, pts[1].y, 0, 0, color));
    push(vtx(pts[2].x, pts[2].y, 0, 0, color));
    push(vtx(pts[0].x, pts[0].y, 0, 0, color));
    push(vtx(pts[2].x, pts[2].y, 0, 0, color));
    push(vtx(pts[3].x, pts[3].y, 0, 0, color));
}

void DrawTriangle(Vector2 v1, Vector2 v2, Vector2 v3, Color color) {
    push(vtx(v1.x, v1.y, 0, 0, color));
    push(vtx(v2.x, v2.y, 0, 0, color));
    push(vtx(v3.x, v3.y, 0, 0, color));
}

void DrawPoly(Vector2 center, int sides, float radius, float rotation, Color color) {
    if (sides < 3) return;
    for (int i = 0; i < sides; ++i) {
        float a0 = rotation + (float)i / sides * 2.0f * VLCRLIB_PI;
        float a1 = rotation + (float)(i + 1) / sides * 2.0f * VLCRLIB_PI;
        Vector2 p0 = { center.x + cosf(a0) * radius, center.y + sinf(a0) * radius };
        Vector2 p1 = { center.x + cosf(a1) * radius, center.y + sinf(a1) * radius };
        push(vtx(center.x, center.y, 0, 0, color));
        push(vtx(p0.x, p0.y, 0, 0, color));
        push(vtx(p1.x, p1.y, 0, 0, color));
    }
}