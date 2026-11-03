/* perf_probe.c — deterministic frame-time probe for performance CI. */
#include <vlcrlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmpFloat(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv) {
    int frames = 300;
    if (argc > 1) frames = atoi(argv[1]);
    if (frames < 30) frames = 30;

    if (!InitWindow(1280, 720, "vlcrlib — perf probe")) return 1;
    fprintf(stderr, "[perf] start frames=%d\n", frames);
    SetTargetFPS(0);
    vgraphics_SetDirectionalLight(true, (Vector3){-0.4f, -0.8f, -0.3f},
                                  (Color){255,250,240,255}, 2.0f);
    vgraphics_SetAmbientLight(true, (Color){80,90,110,255}, 0.25f);
    vgraphics_LoadIBL(NULL);
    SetPostProcess(1);
    SetFXAA(1);

    Camera3D cam = {0};
    cam.position = (Vector3){4, 3, 6};
    cam.target = (Vector3){0, 1, 0};
    cam.up = (Vector3){0, 1, 0};
    cam.fovy = 45;
    cam.nearPlane = 0.05f;
    cam.farPlane = 200;

    float *cpu = (float *)malloc((size_t)frames * sizeof(float));
    float *gpu = (float *)malloc((size_t)frames * sizeof(float));
    int count = 0;
    FrameStats last = {0};

    for (int i = 0; i < frames && !WindowShouldClose(); ++i) {
        BeginDrawing();
        ClearBackground((Color){30, 34, 42, 255});
        BeginMode3D(cam);
        DrawGrid(20, 1.0f);
        for (int x = -3; x <= 3; ++x)
            for (int z = -3; z <= 3; ++z)
                DrawCube((Vector3){(float)x, 0.5f, (float)z},
                         (Vector3){0.6f, 1.0f, 0.6f},
                         (Color){90, 120, (unsigned char)(80 + x * 10), 255});
        EndMode3D();
        DrawText("perf probe", 20, 20, 20, WHITE);
        EndDrawing();
        vgraphics_GetFrameStats(&last);
        cpu[count] = last.cpuFrameMs;
        gpu[count] = last.gpuFrameMs;
        count++;
    }

    qsort(cpu, (size_t)count, sizeof(float), cmpFloat);
    qsort(gpu, (size_t)count, sizeof(float), cmpFloat);
    float cpuAvg = 0, gpuAvg = 0;
    for (int i = 0; i < count; ++i) { cpuAvg += cpu[i]; gpuAvg += gpu[i]; }
    cpuAvg /= count > 0 ? count : 1;
    gpuAvg /= count > 0 ? count : 1;

    printf("{\n");
    printf("  \"frames\": %d,\n", count);
    printf("  \"cpu_avg_ms\": %.4f,\n", cpuAvg);
    printf("  \"cpu_p95_ms\": %.4f,\n", cpu[(int)(count * 0.95f)]);
    printf("  \"gpu_avg_ms\": %.4f,\n", gpuAvg);
    printf("  \"gpu_p95_ms\": %.4f,\n", gpu[(int)(count * 0.95f)]);
    printf("  \"draws\": %d,\n", last.drawCalls);
    printf("  \"triangles\": %llu,\n", (unsigned long long)last.triangles);
    printf("  \"textures\": %d,\n", last.textureCount);
    printf("  \"meshes\": %d\n", last.meshCount);
    printf("}\n");
    fflush(stdout);
    fprintf(stderr, "[perf] done count=%d\n", count);

    free(cpu); free(gpu);
    CloseWindow();
    return 0;
}
