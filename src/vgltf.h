#ifndef VLCRLIB_VGLTF_H
#define VLCRLIB_VGLTF_H

#include <stdint.h>  

#ifdef __cplusplus
extern "C" {
#endif

#include "vgraphics.h"

/* Загружает glTF/GLB модель через tinygltf.
   Возвращает Model с заполненными мешами, материалами и текстурами.
   При ошибке возвращает Model с id = 0. */
Model LoadModelGLTFTiny(const char *path);

/* Загружает скелетную модель (skinning). Пока не реализовано. */
SkinnedModel vgltf_LoadSkinnedModel(const char *path);

#ifdef __cplusplus
}
#endif
#endif /* VLCRLIB_VGLTF_H */