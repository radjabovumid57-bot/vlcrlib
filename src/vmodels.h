#ifndef VLCRLIB_VMODELS_H
#define VLCRLIB_VMODELS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "vgraphics.h"

/* Re-export: publичный API моделей уже определён в vgraphics.h
   (LoadModel, LoadModelFromMesh, UnloadModel, DrawModel, Material и т.д.).
   Здесь — только расширения для скелетных анимаций. */

typedef struct Bone {
    char       name[64];
    int        parent;
    Matrix     inverseBind;
    Matrix     local;
    Vector3    t, s;
    Quaternion r;
} Bone;

typedef struct KeyVec3 { float t; Vector3 v; } KeyVec3;
typedef struct KeyQuat { float t; Quaternion q; } KeyQuat;

typedef struct Track {
    KeyVec3 *pos;   int posCount;
    KeyQuat *rot;   int rotCount;
    KeyVec3 *scale; int scaleCount;
} Track;

/* ---------- Skeleton / animation public API ---------- */
Animation LoadAnimation(const char *path, int animIndex);
void      UnloadAnimation(Animation anim);

/* Эти функции уже реализованы в vgraphics.c и объявлены там.
   Здесь — псевдонимы для удобства. */

#ifdef __cplusplus
}
#endif
#endif /* VLCRLIB_VMODELS_H */