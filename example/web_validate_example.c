/* web_validate_example.c - WebGPU runtime smoke test.
 *
 * Draws several objects with distinct transforms/materials to exercise the
 * per-draw UBO ring and vertex arena, runs a fixed number of frames, then
 * reports the uncaptured WebGPU error count. Intended for headless Chrome/Dawn
 * validation (see tools/web_validate.ps1). */
#include <vlcrlib.h>
#include "vgltf.h"
#include "vplatform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

static Model g_model;
static Model g_ground;
static Model g_propA;
static Model g_propB;
static SkinnedModel g_skinned;
static Animation g_skinnedAnim;
static Model g_ktxModel;
static Model g_bc5NormalModel;
static int g_ktxLoaded;
static Camera3D g_cam;
static int g_frames;
static int g_maxFrames = 150;
static int g_fxaaOn = 1, g_shadowOn = 1, g_bloomOn = 0, g_ssaoOn = 0, g_taaOn = 0;
static float g_iblIntensity = 1.0f;

static void report_and_exit(void) {
    int errors = 0;
#if defined(__EMSCRIPTEN__)
    errors = vgraphics_WebGPUErrorCount();
#endif
    FrameStats stats;
    vgraphics_GetFrameStats(&stats);
    int shot = vgraphics_CaptureScreenshot("/webgpu_screenshot.png") ? 1 : 0;
    char message[256], gpu[32];
    if (stats.gpuFrameMs < 0.0f) snprintf(gpu, sizeof(gpu), "n/a");
    else snprintf(gpu, sizeof(gpu), "%.2fms", (double)stats.gpuFrameMs);
    snprintf(message, sizeof(message),
             "[webgpu-validation] frames=%d errors=%d backend=%s draws=%d shadows=%d tris=%llu gpu=%s shot=%d",
             g_frames, errors, VLCRLIB_GRAPHICS_NAME, stats.drawCalls, stats.shadowDraws,
             (unsigned long long)stats.triangles, gpu, shot);
    fprintf(stdout, "%s\n", message);
    fflush(stdout);
#if defined(__EMSCRIPTEN__)
    EM_ASM({
        var text = UTF8ToString($0);
        console.log(text);
        if (document.getElementById('status'))
            document.getElementById('status').textContent = text;
        document.title = "webgpu-validation errors=" + $1;
    }, message, errors);
    emscripten_cancel_main_loop();
    emscripten_force_exit(errors ? 1 : 0);
#endif
}

static void frame(void) {
    /* Deterministic across runs so the validation screenshot can act as a
     * golden image. */
    float t = (float)g_frames / 60.0f;
    FrameStats stats;
    vgraphics_GetFrameStats(&stats);

    /* Toggle state must not introduce WebGPU validation errors. */
    if (g_frames == 30) {
        g_fxaaOn = 0;
        g_taaOn = 1;
        SetFXAA(0);
        SetTAA(1);
    } else if (g_frames == 60) {
        g_shadowOn = 0;
        g_ssaoOn = 1;
        g_iblIntensity = 0.0f;
        SetSSAO(1, 12.0f, 1.5f);
        vgraphics_SetShadows(false, 1024, 0.003f, 0.8f);
        vgraphics_SetIBLIntensity(0.0f);
    } else if (g_frames == 90) {
        g_fxaaOn = 1;
        g_shadowOn = 1;
        g_bloomOn = 1;
        g_ssaoOn = 0;
        g_iblIntensity = 0.5f;
        SetFXAA(1);
        SetBloom(0.65f, 0.8f);
        SetSSAO(0, 12.0f, 1.5f);
        vgraphics_SetShadows(true, 1024, 0.003f, 0.8f);
        vgraphics_SetIBLIntensity(g_iblIntensity);
    } else if (g_frames == 120) {
        g_taaOn = 1;
        g_bloomOn = 0;
        SetBloom(0.65f, 0.0f);
    }

    BeginDrawing();
    ClearBackground((Color){24, 27, 35, 255});
    BeginMode3D(g_cam);
    DrawGrid(20, 1.0f);
    DrawModel(g_ground, (Vector3){0, -1.2f, 0}, (Vector3){0, 0, 0},
              (Vector3){1, 1, 1}, (Color){70, 78, 92, 255});
    DrawModel(g_propA, (Vector3){-2.2f, -0.8f, 0.4f},
              (Vector3){0, t * 0.5f, 0}, (Vector3){1, 1, 1},
              (Color){220, 140, 80, 255});
    /* Same mesh, second instance: exercises per-command TAA velocity. */
    DrawModel(g_propA, (Vector3){-4.1f, -0.8f, -1.4f},
              (Vector3){0, -t * 0.7f, 0}, (Vector3){1, 1, 1},
              (Color){180, 100, 210, 255});
    DrawModel(g_propB, (Vector3){2.2f, -0.8f, -0.3f},
              (Vector3){0, -t * 0.4f, 0}, (Vector3){1, 1, 1},
              (Color){90, 160, 230, 255});
    if (g_model.id)
        DrawModel(g_model, (Vector3){0, 0, 0}, (Vector3){0, t * 0.3f, 0},
                  (Vector3){1, 1, 1}, WHITE);
    if (g_skinned.id) {
        DrawSkinnedModel(g_skinned, g_skinnedAnim, t * 0.5f, (Vector3){3.1f, -1.1f, 1.8f},
                         (Vector3){0, t * 0.2f, 0}, (Vector3){1.2f, 1.2f, 1.2f}, WHITE);
    }
    if (g_ktxModel.id)
        DrawModel(g_ktxModel, (Vector3){4.6f, 0.2f, -3.6f}, (Vector3){0, 0, 0},
                  (Vector3){0.8f, 0.8f, 0.8f}, WHITE);
    if (g_bc5NormalModel.id)
        DrawModel(g_bc5NormalModel, (Vector3){6.0f, 0.2f, -3.6f}, (Vector3){0, 0, 0},
                  (Vector3){0.8f, 0.8f, 0.8f}, WHITE);
    EndMode3D();

    DrawText("WebGPU runtime validation", 20, 18, 26, WHITE);
    DrawText(VLCRLIB_GRAPHICS_NAME, 20, 52, 18, LIGHTGRAY);
    char status[256];
    snprintf(status, sizeof(status),
             "frame %d / %d  fxaa=%d taa=%d shadow=%d bloom=%d ssao=%d spot=4 point=4 skin=%d ktx=%d bc5n=%d ibl=%.2f  draws=%d tris=%llu",
             g_frames, g_maxFrames, g_fxaaOn, g_taaOn, g_shadowOn,
             g_bloomOn, g_ssaoOn, g_skinned.id ? 1 : 0, g_ktxLoaded,
            g_bc5NormalModel.id ? 1 : 0, g_iblIntensity, stats.drawCalls,
             (unsigned long long)stats.triangles);
    DrawText(status, 20, 78, 12, (Color){180, 210, 255, 255});
    EndDrawing();

    g_frames++;
    if (g_frames >= g_maxFrames) report_and_exit();
}

int main(int argc, char **argv) {
    if (argc > 1) {
        int requested = atoi(argv[1]);
        if (requested > 0) g_maxFrames = requested;
    }
    if (!InitWindow(960, 540, "vlcrlib — WebGPU validation")) {
        fprintf(stderr, "[webgpu-validation] init-failed backend=%s\n",
                VLCRLIB_GRAPHICS_NAME);
        fflush(stderr);
#if defined(__EMSCRIPTEN__)
        EM_ASM({
            var text = "[webgpu-validation] init-failed: no WebGPU adapter";
            console.error(text);
            if (document.getElementById('status'))
                document.getElementById('status').textContent = text;
            document.title = "webgpu-validation init-failed";
        });
        emscripten_force_exit(2);
#endif
        return 1;
    }
    SetTargetFPS(60);

    vgraphics_SetDirectionalLight(true, (Vector3){-0.4f, -0.7f, -0.3f},
                                  (Color){255, 250, 240, 255}, 1.5f);
    vgraphics_SetAmbientLight(true, (Color){50, 55, 70, 255}, 0.35f);
    vgraphics_SetSpotLight(true, (Vector3){3, 4, 3}, (Vector3){-0.5f, -0.7f, -0.5f},
                           (Color){255, 220, 180, 255}, 2.0f, 25.0f, 0.3f, 0.5f);
    VLightData spot2 = VLightDataSpot((Vector3){-3, 3, -2}, (Vector3){0.5f, -0.6f, 0.3f},
                                      (Color){120, 190, 255, 255}, 1.5f, 20.0f, 0.3f, 0.5f);
    vgraphics_AddLight(&spot2);
    VLightData spot3 = VLightDataSpot((Vector3){-2, 2, 3}, (Vector3){0.3f, -0.6f, -0.5f},
                                      (Color){255, 170, 90, 255}, 1.2f, 18.0f, 0.3f, 0.5f);
    vgraphics_AddLight(&spot3);
    VLightData spot4 = VLightDataSpot((Vector3){2, 1.5, 3}, (Vector3){-0.2f, -0.5f, -0.7f},
                                      (Color){180, 220, 255, 255}, 1.0f, 16.0f, 0.3f, 0.5f);
    vgraphics_AddLight(&spot4);
    VLightData point2 = VLightDataPoint((Vector3){3.5f, 2.0f, -1.5f},
                                        (Color){255, 140, 90, 255}, 2.0f, 16.0f);
    vgraphics_AddLight(&point2);
    VLightData point3 = VLightDataPoint((Vector3){-3.0f, 1.8f, -2.5f},
                                        (Color){140, 220, 255, 255}, 1.8f, 15.0f);
    vgraphics_AddLight(&point3);
    VLightData point4 = VLightDataPoint((Vector3){2.5f, 1.2f, -3.0f},
                                        (Color){255, 200, 120, 255}, 1.5f, 14.0f);
    vgraphics_AddLight(&point4);
    vgraphics_SetPointLight(true, (Vector3){-3.5f, 1.5f, 2.0f},
                            (Color){100, 170, 255, 255}, 2.5f, 18.0f);
    if (!vgraphics_LoadIBL(NULL))
        fprintf(stderr, "[webgpu-validation] IBL load failed\n");
    vgraphics_SetIBLIntensity(1.0f);
    vgraphics_SetShadows(true, 1024, 0.003f, 0.8f);
    vgraphics_SetShadowSoftness(1.5f);

    g_ground = LoadModelFromMesh(GenMeshCube(20.0f, 0.1f, 20.0f));
    g_propA = LoadModelFromMesh(GenMeshCube(0.8f, 0.8f, 0.8f));
    g_propB = LoadModelFromMesh(GenMeshSphere(0.6f, 12, 18));

    const char *paths[] = {
        "/assets/DamagedHelmet.glb",
        "assets/DamagedHelmet.glb",
        "DamagedHelmet.glb",
        "example/DamagedHelmet.glb",
        NULL
    };
    for (int i = 0; paths[i]; ++i) {
        g_model = LoadModelGLTFTiny(paths[i]);
        if (g_model.id) {
            fprintf(stderr, "[webgpu-validation] loaded %s id=%u\n",
                    paths[i], g_model.id);
            break;
        }
    }

    const char *skinPaths[] = {
        "/assets/skinned_anim.glb",
        "assets/skinned_anim.glb",
        "skinned_anim.glb",
        NULL
    };
    for (int i = 0; skinPaths[i]; ++i) {
        g_skinned = LoadSkinnedModel(skinPaths[i]);
        if (g_skinned.id) {
            int anims = GetSkinnedModelAnimationCount(g_skinned);
            int chosen = 0;
            for (int a = 0; a < anims; ++a) {
                const char *nm = GetSkinnedModelAnimationName(g_skinned, a);
                if (nm && strcmp(nm, "bind") != 0 &&
                    GetSkinnedModelAnimationDuration(g_skinned, a) > 0.0f)
                    chosen = a;
            }
            g_skinnedAnim = GetSkinnedModelAnimation(g_skinned, chosen);
            fprintf(stderr,
                    "[webgpu-validation] loaded %s id=%u bones=%d anims=%d clip=%s\n",
                    skinPaths[i], g_skinned.id,
                    GetSkinnedModelBoneCount(g_skinned), anims,
                    GetSkinnedModelAnimationName(g_skinned, chosen));
            break;
        }
    }
    Texture2D ktx = LoadTextureKTX2("/assets/ktx2_test.ktx2");
    g_ktxLoaded = ktx.id ? 1 : 0;
    fprintf(stderr, "[webgpu-validation] ktx2 id=%u size=%dx%d\n",
            ktx.id, ktx.width, ktx.height);
    Texture2D etc2 = LoadTextureKTX2("/assets/etc2_test.ktx2");
    fprintf(stderr, "[webgpu-validation] etc2 id=%u size=%dx%d\n",
            etc2.id, etc2.width, etc2.height);
    Texture2D astc = LoadTextureKTX2("/assets/astc_test.ktx2");
    fprintf(stderr, "[webgpu-validation] astc id=%u size=%dx%d\n",
            astc.id, astc.width, astc.height);
    if (ktx.id) {
        Vertex3D qv[4] = {
            {-0.7f, -0.7f, 0, 0, 0, 1, 0, 1, 1, 0, 0, 1, 255, 255, 255, 255},
            { 0.7f, -0.7f, 0, 0, 0, 1, 1, 1, 1, 0, 0, 1, 255, 255, 255, 255},
            { 0.7f,  0.7f, 0, 0, 0, 1, 1, 0, 1, 0, 0, 1, 255, 255, 255, 255},
            {-0.7f,  0.7f, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 255, 255, 255, 255},
        };
        uint32_t qi[6] = {0, 1, 2, 0, 2, 3};
        Mesh qm = vgraphics_RegisterMesh(qv, 4, qi, 6);
        Material qmat = LoadMaterialDefault();
        SetMaterialTexture(&qmat, MATERIAL_MAP_AO, ktx);
        int mi = 0;
        g_ktxModel = vgraphics_CreateModelFromMeshes(&qm, &mi, 1, &qmat, 1);
        Material nmat = LoadMaterialDefault();
        SetMaterialTexture(&nmat, MATERIAL_MAP_NORMAL, ktx);
        int nmi = 0;
        g_bc5NormalModel = vgraphics_CreateModelFromMeshes(&qm, &nmi, 1, &nmat, 1);
        fprintf(stderr, "[webgpu-validation] ktx2 model=%u normal=%u\n",
                g_ktxModel.id, g_bc5NormalModel.id);
    }

    g_cam = (Camera3D){0};
    g_cam.position = (Vector3){0, 1.2f, 6.0f};
    g_cam.target = (Vector3){0, -0.2f, 0};
    g_cam.up = (Vector3){0, 1, 0};
    g_cam.fovy = 50.0f;
    g_cam.nearPlane = 0.1f;
    g_cam.farPlane = 100.0f;

    fprintf(stderr, "[webgpu-validation] start backend=%s frames=%d\n",
            VLCRLIB_GRAPHICS_NAME, g_maxFrames);
#if defined(__EMSCRIPTEN__)
    /* A timer-driven loop is advanced by Chrome's virtual time in headless
     * mode; requestAnimationFrame (fps=0) is not pumped without a compositor. */
    emscripten_set_main_loop(frame, 60, 1);
#else
    while (g_frames < g_maxFrames) frame();
    report_and_exit();
#endif
    return 0;
}
