#ifndef VLCRLIB_VPARTICLES_H
#define VLCRLIB_VPARTICLES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "vcore.h"
#include "vgraphics.h"

typedef struct Particle {
    Vector2 pos;
    Vector2 vel;
    float   life;
    float   maxLife;
    float   size;
    float   rotation;
    float   angularVel;
    Color   startColor;
    Color   endColor;
} Particle;

typedef struct ParticleSystem {
    Particle *items;
    int       capacity;
    int       count;

    /* эмиттер */
    Vector2   emitterPos;
    Vector2   emitterSpread;      /* ± разброс скорости */
    Vector2   baseVelocity;
    float     gravity;

    float     emissionRate;       /* частиц в секунду, 0 = выкл */
    float     emissionAcc;
    bool      emitting;

    /* диапазоны */
    float     minLife, maxLife;
    float     minSize, maxSize;
    Color     startColor, endColor;

    Texture2D texture;            /* 0 = плоский цвет */
} ParticleSystem;

ParticleSystem CreateParticleSystem(int capacity);
void           DestroyParticleSystem(ParticleSystem *ps);

void           EmitParticle(ParticleSystem *ps, Vector2 pos, Vector2 vel,
                            float life, float size,
                            Color startCol, Color endCol);

void           UpdateParticleSystem(ParticleSystem *ps, float dt);
void           DrawParticleSystem(ParticleSystem *ps);

void           SetParticleEmitter(ParticleSystem *ps, Vector2 pos);
void           SetParticleEmissionRate(ParticleSystem *ps, float rate);
void           SetParticleTexture(ParticleSystem *ps, Texture2D tex);
void           StartParticleEmission(ParticleSystem *ps);
void           StopParticleEmission(ParticleSystem *ps);

#ifdef __cplusplus
}
#endif
#endif