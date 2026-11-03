#include "vmodels.h"
#include <stdio.h>

/* ------------------------------------------------------------------
   В этой сборке реализация загрузки моделей (OBJ, glTF) уже находится
   в vgraphics.c — потому что там же лежит боковая таблица мешей,
   дескрипторов и Vulkan-ресурсов.

   vmodels.cpp оставлен как C++ TU для:
     - будущих расширений (assimp / custom loaders),
     - поддержки скелетных анимаций, если их логика будет вынесена
       из vgraphics.c.

   Сейчас он компилируется, чтобы CMake линковал C++ runtime (нужен
   для Bullet/Box2D) и чтобы структура проекта соответствовала
   запланированной архитектуре.
   ------------------------------------------------------------------ */

extern "C" {

Animation LoadAnimation(const char *path, int animIndex) {
    (void)path; (void)animIndex;
    Animation a = {0};
    return a;
}

void UnloadAnimation(Animation anim) {
    (void)anim;
}

} /* extern "C" */