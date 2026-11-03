#include "vparticles.h"
#include "vshapes.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

ParticleSystem CreateParticleSystem(int capacity) {
    ParticleSystem ps = {0};
    if (capacity < 1) capacity = 1;
    ps.items    = calloc((size_t)capacity, sizeof(Particle));
    ps.capacity = capacity;
    ps.count    = 0;

    ps.emitterPos      = (Vector2){0,0};
    ps.emitterSpread   = (Vector2){40, 40};
    ps.baseVelocity    = (Vector2){0, -80};
    ps.gravity         = 120.0f;
    ps.emissionRate    = 0.0f;
    ps.emissionAcc     = 0.0f;
    ps.emitting        = false;

    ps.minLife = 0.8f; ps.maxLife = 1.8f;
    ps.minSize = 6.0f; ps.maxSize = 14.0f;
    ps.startColor = (Color){ 255, 220, 120, 255 };
    ps.endColor   = (Color){ 255,  40,  20, 0 };
    return ps;
}

void DestroyParticleSystem(ParticleSystem *ps) {
    if (!ps) return;
    if (ps->items) free(ps->items);
    memset(ps, 0, sizeof(*ps));
}

static float frand(float a, float b) {
    return a + ((float)rand() / (float)RAND_MAX) * (b - a);
}

void EmitParticle(ParticleSystem *ps, Vector2 pos, Vector2 vel,
                  float life, float size, Color sc, Color ec)
{
    if (!ps || ps->count >= ps->capacity) return;
    Particle *p = &ps->items[ps->count++];
    p->pos          = pos;
    p->vel          = vel;
    p->life         = life;
    p->maxLife      = life;
    p->size         = size;
    p->rotation     = frand(0, 6.28f);
    p->angularVel   = frand(-3, 3);
    p->startColor   = sc;
    p->endColor     = ec;
}

static Color lerpColor(Color a, Color b, float t) {
    Color c;
    c.r = (unsigned char)(a.r + (b.r - a.r) * t);
    c.g = (unsigned char)(a.g + (b.g - a.g) * t);
    c.b = (unsigned char)(a.b + (b.b - a.b) * t);
    c.a = (unsigned char)(a.a + (b.a - a.a) * t);
    return c;
}

void UpdateParticleSystem(ParticleSystem *ps, float dt) {
    if (!ps) return;

    /* эмиссия */
    if (ps->emitting && ps->emissionRate > 0.0f) {
        ps->emissionAcc += ps->emissionRate * dt;
        while (ps->emissionAcc >= 1.0f) {
            ps->emissionAcc -= 1.0f;
            Vector2 pos = ps->emitterPos;
            Vector2 vel = {
                ps->baseVelocity.x + frand(-ps->emitterSpread.x, ps->emitterSpread.x),
                ps->baseVelocity.y + frand(-ps->emitterSpread.y, ps->emitterSpread.y)
            };
            EmitParticle(ps, pos, vel,
                         frand(ps->minLife, ps->maxLife),
                         frand(ps->minSize, ps->maxSize),
                         ps->startColor, ps->endColor);
        }
    }

    /* симуляция */
    int w = 0;
    for (int i = 0; i < ps->count; ++i) {
        Particle *p = &ps->items[i];
        p->life -= dt;
        if (p->life <= 0.0f) continue;

        p->vel.y += ps->gravity * dt;
        p->pos.x += p->vel.x * dt;
        p->pos.y += p->vel.y * dt;
        p->rotation += p->angularVel * dt;

        if (w != i) ps->items[w] = *p;
        ++w;
    }
    ps->count = w;
}

void DrawParticleSystem(ParticleSystem *ps) {
    if (!ps || ps->count == 0) return;

    /* сбрасываем 2D-батч, чтобы частицы шли поверх/до примитивов корректно */
    vshapes_FlushBatch();

    Vertex2D *verts = malloc(sizeof(Vertex2D) * 6 * ps->count);
    int vc = 0;
    Texture2D tex = ps->texture;

    for (int i = 0; i < ps->count; ++i) {
        Particle *p = &ps->items[i];
        float t = 1.0f - (p->life / p->maxLife);
        Color c = lerpColor(p->startColor, p->endColor, t);
        float hs = p->size * 0.5f;
        float cs = cosf(p->rotation), sn = sinf(p->rotation);

        /* 4 угла quad (вращение вокруг центра) */
        Vector2 P[4] = {
            { -hs, -hs }, { hs, -hs }, { hs, hs }, { -hs, hs }
        };
        Vector2 S[4];
        for (int k = 0; k < 4; ++k) {
            S[k].x = p->pos.x + P[k].x * cs - P[k].y * sn;
            S[k].y = p->pos.y + P[k].x * sn + P[k].y * cs;
        }

        Vertex2D v0 = { S[0].x, S[0].y, 0, 0, c.r, c.g, c.b, c.a };
        Vertex2D v1 = { S[1].x, S[1].y, 1, 0, c.r, c.g, c.b, c.a };
        Vertex2D v2 = { S[2].x, S[2].y, 1, 1, c.r, c.g, c.b, c.a };
        Vertex2D v3 = { S[3].x, S[3].y, 0, 1, c.r, c.g, c.b, c.a };

        verts[vc++] = v0; verts[vc++] = v1; verts[vc++] = v2;
        verts[vc++] = v0; verts[vc++] = v2; verts[vc++] = v3;
    }

    if (vc > 0) {
        vgraphics_DrawVertexBuffer(verts, vc, tex);
    }
    free(verts);
}

void SetParticleEmitter(ParticleSystem *ps, Vector2 pos) {
    if (ps) ps->emitterPos = pos;
}

void SetParticleEmissionRate(ParticleSystem *ps, float rate) {
    if (ps) ps->emissionRate = rate;
}

void SetParticleTexture(ParticleSystem *ps, Texture2D tex) {
    if (ps) ps->texture = tex;
}

void StartParticleEmission(ParticleSystem *ps) { if (ps) ps->emitting = true; }
void StopParticleEmission(ParticleSystem *ps)  { if (ps) ps->emitting = false; }