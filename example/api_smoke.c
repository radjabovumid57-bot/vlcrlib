/*
 * api_smoke.c — automated API smoke test (no IBL, no shadows)
 * Exit 0 = pass, 1 = fail. Prints [PASS]/[FAIL] lines.
 */
#include <vlcrlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vgltf.h"

static int fails = 0;
static void expect(int cond, const char *name) {
    if (cond) fprintf(stderr, "[PASS] %s\n", name);
    else { fprintf(stderr, "[FAIL] %s\n", name); fails++; }
    fflush(stderr);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    fprintf(stderr, "[smoke] start\n");

    if (!InitWindow(800, 600, "vlcrlib api_smoke")) {
        fprintf(stderr, "[FAIL] InitWindow\n");
        return 1;
    }
    expect(1, "InitWindow");
    expect(InitGraphicsDevice() == VKINIT_OK, "InitGraphicsDevice idempotent");
    SetTargetFPS(60);

    vgraphics_SetDirectionalLight(true,
        (Vector3){ -0.4f, -0.9f, -0.3f }, WHITE, 3.0f);
    vgraphics_SetAmbientLight(true, (Color){ 100, 110, 130, 255 }, 0.5f);
    vgraphics_SetShadows(true, 1024, 0.0025f, 0.9f);
    vgraphics_SetShadowSoftness(1.5f);
    SetFXAA(0);
    SetTAA(1);
    SetPostProcess(1);
    expect(1, "lights + shadows + FXAA");

    Model m = LoadModelGLTFTiny("DamagedHelmet.glb");
    if (!m.id) m = LoadModelGLTFTiny("example/DamagedHelmet.glb");
    expect(m.id != 0, "LoadModelGLTFTiny DamagedHelmet");

    Camera3D cam = {0};
    cam.position = (Vector3){ 0, 1.4f, 4.5f };
    cam.target   = (Vector3){ 0, 1.0f, 0 };
    cam.up       = (Vector3){ 0, 1, 0 };
    cam.fovy = 45.f;
    cam.nearPlane = 0.05f;
    cam.farPlane = 100.f;

    Model floor = LoadModelFromMesh(GenMeshPlane(20, 20, 1, 1));
    const char *ktxPath = getenv("VLCR_KTX2");
    if (ktxPath) {
        Texture2D t = LoadTextureKTX2(ktxPath);
        fprintf(stderr, "[smoke] ktx2 %s -> id=%u %dx%d\n",
                ktxPath, t.id, t.width, t.height);
        expect(t.id != 0, "KTX2 BC7/BC5 texture loaded");
        if (t.id) UnloadTexture(t);
    }

    int frames = 0;
    while (!WindowShouldClose() && frames < 90) {
        BeginDrawing();
        ClearBackground((Color){ 40, 44, 52, 255 });
        if (m.id) {
            BeginMode3D(cam);
            DrawGrid(8, 1.0f);
            if (floor.id)
                DrawModel(floor, (Vector3){0, 0, 0}, (Vector3){0, 0, 0},
                          (Vector3){1, 1, 1}, (Color){90, 95, 105, 255});
            DrawModel(m, (Vector3){0, 1, 0}, (Vector3){0, 0, 0},
                      (Vector3){1.2f, 1.2f, 1.2f}, WHITE);
            /* Camera is at z=4.5 looking toward -Z; z=+10 is behind it. */
            DrawModel(m, (Vector3){0, 1, 10}, (Vector3){0, 0, 0},
                      (Vector3){1.2f, 1.2f, 1.2f}, WHITE);
            EndMode3D();
        }
        DrawText(fails ? "SMOKE FAIL" : "SMOKE OK", 16, 16, 22, WHITE);
        EndDrawing();
        frames++;
        if (getenv("VLCR_RESIZE") && frames == 20)
            SetWindowSize(640, 480);
        if (getenv("VLCR_SHADOW_RESIZE") && frames == 20)
            vgraphics_SetShadows(true, 512, 0.002f, 0.9f);
        const char *shot = getenv("VLCR_SCREENSHOT");
        if (shot && frames == 40) vgraphics_CaptureScreenshot(shot);
    }
    expect(frames >= 30, "rendered >= 30 frames without crash");

    FrameStats fs = {0};
    vgraphics_GetFrameStats(&fs);
    expect(fs.frameIndex > 0, "FrameStats frameIndex");
    expect(fs.width > 0 && fs.height > 0, "FrameStats extent");
    expect(fs.drawCalls > 0, "FrameStats drawCalls");
    expect(fs.triangles > 0, "FrameStats triangles");
    expect(fs.cpuFrameMs > 0.0f, "FrameStats cpuFrameMs");
    expect(fs.culledDraws > 0, "frustum culling rejects behind-camera mesh");
    fprintf(stderr,
            "[stats] draws=%d tris=%llu culled=%d cpu=%.3fms gpu=%.3fms "
            "tex=%d mesh=%d vram=%.1fMB\n",
            fs.drawCalls, (unsigned long long)fs.triangles, fs.culledDraws,
            fs.cpuFrameMs, fs.gpuFrameMs, fs.textureCount, fs.meshCount,
            (double)fs.estimatedGpuBytes / (1024.0 * 1024.0));
    fprintf(stderr, "[stats] gpu pass ms: shadow=%.3f scene=%.3f post=%.3f\n",
            fs.shadowMs, fs.sceneMs, fs.postMs);

    const char *validation = getenv("VLCRLIB_VALIDATION");
    if (validation && strcmp(validation, "0") != 0) {
        int verr = vgraphics_GetValidationErrorCount();
        fprintf(stderr, "[stats] validation errors=%d\n", verr);
        expect(verr == 0, "no Vulkan validation errors");
    }

    if (m.id) UnloadModel(m);
    if (floor.id) UnloadModel(floor);
    CloseWindow();

    const char *validationAfter = getenv("VLCRLIB_VALIDATION");
    if (validationAfter && strcmp(validationAfter, "0") != 0) {
        int verr = vgraphics_GetValidationErrorCount();
        fprintf(stderr, "[stats] validation errors after shutdown=%d\n", verr);
        expect(verr == 0, "no Vulkan validation errors after shutdown");
    }

    fprintf(stderr, "[smoke] done fails=%d frames=%d\n", fails, frames);
    return fails ? 1 : 0;
}
