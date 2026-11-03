#ifndef VLCRLIB_VSCENE_H
#define VLCRLIB_VSCENE_H

#include "vgraphics.h"
#include "vloaders.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCENE_MAX_NODES 256
#define SCENE_MODEL_PATH 512

typedef struct SceneNode {
    char    model[SCENE_MODEL_PATH];
    Vector3 position;
    Vector3 rotation;
    Vector3 scale;
    Color   tint;
    int     visible;
    int     parent;   /* -1 = root */
} SceneNode;

typedef struct Scene {
    SceneNode nodes[SCENE_MAX_NODES];
    int       count;
} Scene;

/* Runtime ownership of models referenced by a Scene. */
typedef struct SceneRuntime {
    Model models[SCENE_MAX_NODES];
    int   loaded[SCENE_MAX_NODES];
} SceneRuntime;

void SceneInit(Scene *scene);
int  SceneAddNode(Scene *scene, const SceneNode *node);
bool SceneSave(const Scene *scene, const char *path);
bool SceneLoad(Scene *scene, const char *path);

/* Loads model paths relative to the scene file directory. */
bool SceneLoadModels(Scene *scene, const char *scenePath, SceneRuntime *runtime);
void SceneDraw(const Scene *scene, const SceneRuntime *runtime);
void SceneUnloadModels(SceneRuntime *runtime);

#ifdef __cplusplus
}
#endif

#endif /* VLCRLIB_VSCENE_H */
