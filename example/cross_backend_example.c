/* cross_backend_example.c — deterministic frame shared by the Vulkan and
 * WebGPU backends for the cross-backend golden-image check.
 *
 * The scene is static (no time-based animation) and all post-processing is off,
 * so the rendered frame depends only on geometry, lighting and shadows. The
 * native run writes a PNG through vgraphics_CaptureScreenshot(); the web run is
 * captured from the canvas by tools/cross_backend_golden.ps1. */
#include <vlcrlib.h>
#include "vgltf.h"
#include "vmath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

static Model g_ground, g_cube, g_sphere, g_helmet;
static Camera3D g_cam;
static int g_frames;
static int g_maxFrames = 10;
static const char *g_out = "cross_backend.png";

static void finish(void) {
    int shot = vgraphics_CaptureScreenshot(g_out) ? 1 : 0;
    int errors = 0;
#if defined(__EMSCRIPTEN__)
    errors = vgraphics_WebGPUErrorCount();
#endif
    FrameStats stats = {0};
    vgraphics_GetFrameStats(&stats);
    printf("[cross-golden] frames=%d shot=%d errors=%d backend=%s"
           " draws=%d culled=%d shadow=%d tris=%llu\n",
           g_frames, shot, errors, VLCRLIB_GRAPHICS_NAME,
           stats.drawCalls, stats.culledDraws, stats.shadowDraws,
           (unsigned long long)stats.triangles);
    fflush(stdout);
#if defined(__EMSCRIPTEN__)
    EM_ASM({ console.log(UTF8ToString($0)); }, "cross-golden done");
    emscripten_cancel_main_loop();
    emscripten_force_exit((shot && !errors) ? 0 : 1);
#endif
}

static void frame(void) {
    BeginDrawing();
    /* Black clear keeps the background identical on both backends: Vulkan
       hardware-encodes the clear to sRGB while WebGPU applies the display
       transform in the composite, so only 0/0 survives both paths exactly. */
    ClearBackground((Color){0, 0, 0, 255});
    BeginMode3D(g_cam);
    DrawModel(g_ground, (Vector3){0, -1.2f, 0}, (Vector3){0, 0, 0},
              (Vector3){1, 1, 1}, (Color){70, 78, 92, 255});
    DrawModel(g_cube, (Vector3){-1.8f, -0.8f, 0.2f}, (Vector3){0, 25, 0},
              (Vector3){1, 1, 1}, (Color){220, 140, 80, 255});
    DrawModel(g_cube, (Vector3){1.7f, -0.75f, -0.2f}, (Vector3){0, -18, 0},
              (Vector3){1.1f, 1.1f, 1.1f}, (Color){180, 100, 210, 255});
    DrawModel(g_sphere, (Vector3){0, -0.5f, 0.9f}, (Vector3){0, 0, 0},
              (Vector3){1, 1, 1}, (Color){90, 160, 230, 255});
    if (g_helmet.id)
        DrawModel(g_helmet, (Vector3){0, -0.1f, -1.5f}, (Vector3){0, 0, 0},
                  (Vector3){1.2f, 1.2f, 1.2f}, WHITE);
    EndMode3D();
    EndDrawing();

    g_frames++;
    if (g_frames >= g_maxFrames) finish();
}

int main(int argc, char **argv) {
    if (argc > 1) g_out = argv[1];
    if (argc > 2) { int n = atoi(argv[2]); if (n > 0) g_maxFrames = n; }
    int skipHelmet = argc > 3 && strcmp(argv[3], "nohelmet") == 0;
    if (!InitWindow(960, 540, "vlcrlib - cross-backend golden")) return 1;
    SetTargetFPS(60);

    vgraphics_SetDirectionalLight(true, (Vector3){-0.4f, -0.7f, -0.3f},
                                  (Color){255, 250, 240, 255}, 1.5f);
    vgraphics_SetAmbientLight(true, (Color){50, 55, 70, 255}, 0.35f);
    vgraphics_SetShadows(true, 1024, 0.003f, 0.8f);
    /* Both backends light the scene from the same baked split-sum maps. The
       Vulkan-only skybox stays off so the background is the clear color on
       both sides. */
    if (!vgraphics_LoadIBL(NULL))
        fprintf(stderr, "[cross-golden] IBL bake failed; using backend defaults\n");
    vgraphics_SetIBLIntensity(1.0f);
    SetGraphicsExposure(0.5f);
    vgraphics_SetSkybox(0);
    SetPostProcess(1);
    SetFXAA(0);
    SetTAA(0);
    SetBloom(0.65f, 0.0f);
    SetSSAO(0, 12.0f, 1.5f);

    g_ground = LoadModelFromMesh(GenMeshCube(20.0f, 0.1f, 20.0f));
    g_cube = LoadModelFromMesh(GenMeshCube(0.8f, 0.8f, 0.8f));
    g_sphere = LoadModelFromMesh(GenMeshSphere(0.65f, 16, 24));

    const char *helmetPaths[] = {
        "/assets/DamagedHelmet.glb",
        "assets/DamagedHelmet.glb",
        "DamagedHelmet.glb",
        "example/DamagedHelmet.glb",
        NULL
    };
    for (int i = 0; !skipHelmet && helmetPaths[i]; ++i) {
        g_helmet = LoadModelGLTFTiny(helmetPaths[i]);
        if (g_helmet.id) break;
    }

    g_cam = (Camera3D){0};
    g_cam.position = (Vector3){0, 1.3f, 6.0f};
    g_cam.target = (Vector3){0, -0.3f, 0};
    g_cam.up = (Vector3){0, 1, 0};
    g_cam.fovy = 50.0f;
    g_cam.nearPlane = 0.1f;
    g_cam.farPlane = 100.0f;

    fprintf(stderr, "[cross-golden] start backend=%s frames=%d helmet=%d"
            " models=%u/%u/%u/%u\n",
            VLCRLIB_GRAPHICS_NAME, g_maxFrames, g_helmet.id ? 1 : 0,
            g_ground.id, g_cube.id, g_sphere.id, g_helmet.id);
#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop(frame, 60, 1);
#else
    while (g_frames < g_maxFrames) frame();
#endif
    return 0;
}