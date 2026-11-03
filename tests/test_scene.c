#include "vscene.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Runtime stubs: this test covers the scene file format only. */
Model LoadModelAuto(const char *path) { (void)path; return (Model){0}; }
void  DrawModel(Model m, Vector3 p, Vector3 r, Vector3 s, Color c) {
    (void)m; (void)p; (void)r; (void)s; (void)c;
}
void  UnloadModel(Model m) { (void)m; }

static int fails = 0;
static void expect(int ok, const char *name) {
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) fails++;
}

int main(void) {
    Scene scene;
    SceneInit(&scene);
    expect(scene.count == 0, "scene starts empty");

    SceneNode a;
    memset(&a, 0, sizeof(a));
    snprintf(a.model, sizeof(a.model), "DamagedHelmet.glb");
    a.position = (Vector3){1, 2, 3};
    a.rotation = (Vector3){0, 1.5f, 0};
    a.scale = (Vector3){2, 2, 2};
    a.tint = (Color){200, 100, 50, 255};
    a.visible = 1;
    a.parent = -1;
    expect(SceneAddNode(&scene, &a) == 0, "add first node");

    SceneNode b;
    memset(&b, 0, sizeof(b));
    snprintf(b.model, sizeof(b.model), "skinned_anim.glb");
    b.position = (Vector3){-1, 0, 0};
    b.scale = (Vector3){1, 1, 1};
    b.tint = (Color){255, 255, 255, 255};
    b.visible = 0;
    b.parent = 0;
    expect(SceneAddNode(&scene, &b) == 1, "add second node");

    const char *path = "test_scene_tmp.json";
    expect(SceneSave(&scene, path), "save scene");

    Scene loaded;
    SceneInit(&loaded);
    expect(SceneLoad(&loaded, path), "load scene");
    expect(loaded.count == 2, "node count");
    expect(strcmp(loaded.nodes[0].model, "DamagedHelmet.glb") == 0, "model 0");
    expect(fabsf(loaded.nodes[0].position.x - 1) < 1e-5f, "position 0");
    expect(fabsf(loaded.nodes[0].scale.x - 2) < 1e-5f, "scale 0");
    expect(loaded.nodes[0].tint.r == 200 && loaded.nodes[0].tint.b == 50,
           "tint 0");
    expect(loaded.nodes[1].visible == 0, "visibility");
    expect(loaded.nodes[0].parent == -1, "root parent");
    expect(loaded.nodes[1].parent == 0, "child parent");
    remove(path);

    Scene escaped;
    SceneInit(&escaped);
    SceneNode e;
    memset(&e, 0, sizeof(e));
    snprintf(e.model, sizeof(e.model), "assets\\sub\\\\model \"quoted\".glb");
    e.scale = (Vector3){1, 1, 1};
    e.visible = 1;
    e.parent = -1;
    expect(SceneAddNode(&escaped, &e) == 0, "add escaped node");
    const char *escPath = "test_scene_escape_tmp.json";
    expect(SceneSave(&escaped, escPath), "save escaped scene");
    Scene escapedLoaded;
    SceneInit(&escapedLoaded);
    expect(SceneLoad(&escapedLoaded, escPath), "load escaped scene");
    expect(escapedLoaded.count == 1, "escaped node count");
    expect(strcmp(escapedLoaded.nodes[0].model, e.model) == 0,
           "model path escapes round-trip");
    remove(escPath);

    printf("scene: %d failure(s)\n", fails);
    return fails ? 1 : 0;
}
