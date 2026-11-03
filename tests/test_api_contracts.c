/*
 * API-contract tests: null/empty handles, invalid paths and invalid graph
 * handles must fail without crashing or touching an uninitialized device.
 */
#include "vlcrlib.h"
#include "vrender_graph.h"
#include "vscene.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond, name) do { \
    if (cond) printf("[PASS] %s\n", name); \
    else { printf("[FAIL] %s\n", name); failures++; } \
} while (0)

int main(void) {
    EXPECT(LoadModel(NULL).id == 0, "LoadModel(NULL)");
    EXPECT(LoadModel("").id == 0, "LoadModel(\"\")");
    EXPECT(LoadModel("does-not-exist.obj").id == 0, "LoadModel(missing)");
    EXPECT(LoadSkinnedModel(NULL).id == 0, "LoadSkinnedModel(NULL)");
    EXPECT(LoadTextureKTX2(NULL).id == 0, "LoadTextureKTX2(NULL)");
    EXPECT(LoadTextureFromImage((Image){0}).id == 0,
           "LoadTextureFromImage(empty)");
    EXPECT(vgraphics_RegisterMesh(NULL, 0, NULL, 0).id == 0,
           "RegisterMesh(NULL)");
    EXPECT(LoadRenderTexture(0, 0).id == 0, "LoadRenderTexture(0,0)");
    /* CPU/glTF paths allocate CPU-side material records before a device;
       the Material struct itself is the portable handle. */
    EXPECT(LoadMaterialDefault().id != 0, "LoadMaterialDefault before device (CPU path)");
    EXPECT(vgraphics_CreateModelFromMeshesEx(NULL, NULL, NULL, 1, NULL, 0).id == 0,
           "CreateModelFromMeshesEx(NULL)");
    EXPECT(LoadFileData(NULL, NULL) == NULL, "LoadFileData(NULL)");
    EXPECT(LoadFileText(NULL) == NULL, "LoadFileText(NULL)");
    EXPECT(!SaveFileData(NULL, NULL, 0), "SaveFileData(NULL)");
    EXPECT(!SaveFileText(NULL, NULL), "SaveFileText(NULL)");
    EXPECT(!FileExists(NULL), "FileExists(NULL)");
    EXPECT(!DirectoryExists(NULL), "DirectoryExists(NULL)");
    EXPECT(GetFileSizeBytes(NULL) == 0, "GetFileSizeBytes(NULL)");
    EXPECT(!CreateDirectoryIfMissing(NULL), "CreateDirectoryIfMissing(NULL)");
    EXPECT(!vgraphics_CaptureScreenshot(NULL), "CaptureScreenshot(NULL)");
    EXPECT(!vgraphics_CaptureScreenshot(""), "CaptureScreenshot(\"\")");

    FrameStats stats;
    memset(&stats, 0xAA, sizeof(stats));
    vgraphics_GetFrameStats(NULL);
    EXPECT(1, "GetFrameStats(NULL)");
    int rtW = -1, rtH = -1;
    vgraphics_GetRenderTargetSize(&rtW, &rtH);
    EXPECT(rtW >= 0 && rtH >= 0, "GetRenderTargetSize(before device)");

    Scene scene;
    SceneInit(&scene);
    EXPECT(SceneAddNode(NULL, NULL) == -1, "SceneAddNode(NULL)");
    SceneNode node;
    memset(&node, 0, sizeof(node));
    snprintf(node.model, sizeof(node.model), "m.glb");
    node.parent = 99;
    int idx = SceneAddNode(&scene, &node);
    EXPECT(idx == 0 && scene.count == 1, "SceneAddNode valid");
    EXPECT(scene.nodes[idx].parent == -1, "SceneAddNode invalid parent");
    EXPECT(scene.nodes[idx].scale.x == 1.0f, "SceneAddNode default scale");

    SceneRuntime runtime;
    EXPECT(!SceneLoadModels(NULL, "scene.json", &runtime),
           "SceneLoadModels(NULL scene)");
    EXPECT(!SceneLoadModels(&scene, NULL, &runtime),
           "SceneLoadModels(NULL path)");
    EXPECT(!SceneLoadModels(&scene, "scene.json", NULL),
           "SceneLoadModels(NULL runtime)");
    EXPECT(!SceneLoad(NULL, "x.json"), "SceneLoad(NULL scene)");
    EXPECT(!SceneLoad(&scene, NULL), "SceneLoad(NULL path)");
    EXPECT(!SceneSave(NULL, "x.json"), "SceneSave(NULL scene)");
    EXPECT(!SceneSave(&scene, NULL), "SceneSave(NULL path)");

    EXPECT(!AnimatorAddState(NULL, "x", (Animation){0}, true),
           "AnimatorAddState(NULL)");
    EXPECT(!AnimatorPlay(NULL, "x", 0.0f), "AnimatorPlay(NULL)");
    AnimatorUpdate(NULL, 0.016f);
    EXPECT(strcmp(AnimatorGetState(NULL), "") == 0, "AnimatorGetState(NULL)");
    DestroyAnimator(NULL);
    EXPECT(1, "DestroyAnimator(NULL)");

    EXPECT(!LoadReverbIR(NULL, 0.5f), "LoadReverbIR(NULL)");
    EXPECT(!LoadReverbIR("missing.wav", 0.5f), "LoadReverbIR(missing)");
    SetReverbWet(0.5f);
    EXPECT(1, "SetReverbWet(before init)");

    VrgGraph *g = vrgCreate();
    EXPECT(g != NULL, "vrgCreate");
    EXPECT(vrgAddImage(g, NULL) == -1, "vrgAddImage(NULL)");
    EXPECT(vrgAddPass(g, NULL) == 0, "vrgAddPass(NULL name)");
    EXPECT(!vrgPassReadImage(g, 0, 0, VK_IMAGE_LAYOUT_GENERAL,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_ACCESS_SHADER_READ_BIT),
           "vrgPassReadImage(invalid image)");
    vrgDestroy(g);

    printf("api_contracts: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
