/* qa_stress.c - audit harness: N DrawModel calls per frame, real CPU timing,
 * draw/triangle stats and optional Vulkan validation error count.
 * Usage: qa_stress <objectCount> <frames> [csvPath]
 */
#include <vlcrlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    int objects = argc > 1 ? atoi(argv[1]) : 1000;
    int frames = argc > 2 ? atoi(argv[2]) : 60;
    const char *csv = argc > 3 ? argv[3] : NULL;
    if (objects < 1) objects = 1;
    if (frames < 1) frames = 1;

    int qw = getenv("QA_W") ? atoi(getenv("QA_W")) : 640;
    int qh = getenv("QA_H") ? atoi(getenv("QA_H")) : 360;
    if (!InitWindow(qw, qh, "vlcrlib qa_stress")) {
        fprintf(stderr, "[qa_stress] InitWindow failed\n");
        return 1;
    }
    if (InitGraphicsDevice() != VKINIT_OK) {
        fprintf(stderr, "[qa_stress] InitGraphicsDevice failed\n");
        CloseWindow();
        return 1;
    }
    SetTargetFPS(0);
    SetPostProcess(getenv("QA_NO_POST") ? 0 : 1);
    SetFXAA(0);
    SetTAA(0);
    if (getenv("QA_NO_SSAO")) SetSSAO(0, 0.6f, 1.5f);
    vgraphics_SetShadows(getenv("QA_SHADOWS") != NULL, 1024, 0.003f, 0.7f);
    vgraphics_SetAmbientLight(true, (Color){80, 90, 110, 255}, 0.4f);
    vgraphics_SetDirectionalLight(true, (Vector3){-0.4f, -0.8f, -0.3f},
                                  (Color){255, 250, 240, 255}, 2.0f);

    Mesh cube = GenMeshCube(1.0f, 1.0f, 1.0f);
    Model model = LoadModelFromMesh(cube);
    if (!model.id) {
        fprintf(stderr, "[qa_stress] model creation failed\n");
        CloseWindow();
        return 1;
    }

    Camera3D cam = {0};
    cam.position = (Vector3){0.0f, 18.0f, 42.0f};
    cam.target = (Vector3){0.0f, 0.0f, 0.0f};
    cam.up = (Vector3){0.0f, 1.0f, 0.0f};
    cam.fovy = 50.0f;
    cam.nearPlane = 0.1f;
    cam.farPlane = 400.0f;

    int side = 1;
    while (side * side < objects) side++;
    FILE *f = csv ? fopen(csv, "w") : NULL;
    if (f) fprintf(f, "frame,objects,cpu_ms,draws,triangles\n");

    double sum = 0.0, best = 1e30, worst = 0.0, gpu_sum = 0.0;
    int gpu_samples = 0;
    int warmup = frames > 2 ? 2 : 0;
    for (int frame = 0; frame < frames; ++frame) {
        BeginDrawing();
        ClearBackground((Color){20, 24, 32, 255});
        BeginMode3D(cam);
        for (int i = 0; i < objects && !getenv("QA_NO_DRAW"); ++i) {
            int x = i % side, z = i / side;
            float px = ((float)x - (float)side * 0.5f) * 1.2f;
            float pz = ((float)z - (float)side * 0.5f) * 1.2f;
            DrawModel(model, (Vector3){px, 0.5f, pz}, (Vector3){0, 0, 0},
                      (Vector3){0.8f, 0.2f, 0.8f},
                      (Color){180, 150, 120, 255});
        }
        EndMode3D();
        if (!getenv("QA_NO_TEXT")) DrawText("qa_stress", 10, 10, 18, WHITE);
        EndDrawing();

        FrameStats st;
        vgraphics_GetFrameStats(&st);
        if (frame >= warmup) {
            sum += st.cpuFrameMs;
            if (st.cpuFrameMs < best) best = st.cpuFrameMs;
            if (st.cpuFrameMs > worst) worst = st.cpuFrameMs;
            if (st.gpuFrameMs >= 0.0f) { gpu_sum += st.gpuFrameMs; gpu_samples++; }
        }
        if (f) fprintf(f, "%d,%d,%.4f,%d,%llu\n", frame, objects,
                       (double)st.cpuFrameMs, st.drawCalls,
                       (unsigned long long)st.triangles);
    }
    if (f) fclose(f);

    FrameStats st;
    vgraphics_GetFrameStats(&st);
    double avg = frames > warmup ? sum / (double)(frames - warmup) : 0.0;
    int validation = 0;
#if !defined(__EMSCRIPTEN__)
    validation = vgraphics_GetValidationErrorCount();
#endif
    double gpu_avg = gpu_samples ? gpu_sum / (double)gpu_samples : -1.0;
    printf("RESULT objects=%d frames=%d avg_cpu_ms=%.4f min_cpu_ms=%.4f "
           "max_cpu_ms=%.4f avg_gpu_ms=%.4f draws=%d tris=%llu vram_mb=%.2f "
           "validation=%d\n",
           objects, frames, avg, best, worst, gpu_avg, st.drawCalls,
           (unsigned long long)st.triangles,
           (double)st.estimatedGpuBytes / (1024.0 * 1024.0), validation);
    fflush(stdout);

    UnloadModel(model);
    CloseWindow();
    return validation == 0 ? 0 : 2;
}
