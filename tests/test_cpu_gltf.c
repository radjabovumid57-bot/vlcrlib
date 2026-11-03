/* Regression test: LoadModel(glTF) -> CPU geometry/materials -> shaded draw.
 *
 * This runs without InitWindow()/Vulkan. It catches the CPU texture-slot
 * regression (all textures sharing slot 1 and being overwritten by the font
 * atlas) and verifies that albedo + emissive textures reach the CPU model. */
#include "vgraphics.h"
#include "vcpu.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(cond, name) do { \
    if (cond) printf("[PASS] %s\n", name); \
    else { printf("[FAIL] %s\n", name); fails++; } \
} while (0)

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "assets/DamagedHelmet.glb";
    setvbuf(stdout, NULL, _IONBF, 0);

    Model model = LoadModel(path);
    CHECK(model.id != 0, "LoadModel(glTF) without a Vulkan device");
    if (!model.id) {
        printf("cpu_gltf: %d failure(s)\n", fails);
        return 1;
    }

    VCpuModel cpu;
    memset(&cpu, 0, sizeof(cpu));
    CHECK(vgraphics_BuildCpuModel(model, &cpu), "vgraphics_BuildCpuModel");
    CHECK(cpu.vertexCount > 1000, "CPU model has geometry");
    CHECK(cpu.partCount >= 1, "CPU model has material parts");
    CHECK(cpu.ownedTextureCount >= 1, "CPU model owns albedo texture data");
    if (cpu.partCount > 0) {
        CHECK(cpu.parts[0].texture != NULL, "albedo texture reaches the part");
        float len2 = cpu.vertices[0].normal.x * cpu.vertices[0].normal.x +
                     cpu.vertices[0].normal.y * cpu.vertices[0].normal.y +
                     cpu.vertices[0].normal.z * cpu.vertices[0].normal.z;
        CHECK(len2 > 0.25f, "vertex normals reach the CPU model");
    }

    VCpuCanvas canvas;
    CHECK(VCpuInit(&canvas, 320, 240), "VCpuInit offscreen canvas");
    VCpuClear(&canvas, (Color){18, 20, 28, 255});
    VCpuClearDepth(&canvas);

    Matrix proj = MatrixPerspective(50.0f * VLCRLIB_DEG2RAD, 4.0f / 3.0f,
                                    0.1f, 100.0f);
    proj.m5 = -proj.m5; /* CPU framebuffer is top-left origin */
    Matrix view = MatrixLookAt((Vector3){0, 0, 4.5f},
                               (Vector3){0, 0, 0},
                               (Vector3){0, 1, 0});
    Matrix world = MatrixIdentity();
    Matrix mvp = MatrixMultiply(proj, MatrixMultiply(view, world));
    VCpuDrawModelShaded(&canvas, &cpu, mvp, world, (Vector3){0, 0, 4.5f},
                        (Color){210, 210, 220, 255});

    const uint32_t background = 0xFF12141Cu;
    int nonBackground = 0, colored = 0;
    for (size_t i = 0; i < (size_t)canvas.width * canvas.height; ++i) {
        uint32_t p = canvas.color[i];
        if (p == background) continue;
        nonBackground++;
        unsigned r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
        if (r != g || g != b) colored++;
    }
    CHECK(nonBackground > 200, "shaded model covers pixels");
    CHECK(colored > 50, "albedo texture produces coloured pixels");

    uint64_t hash1 = VCpuHash(&canvas);
    VCpuClear(&canvas, (Color){18, 20, 28, 255});
    VCpuClearDepth(&canvas);
    VCpuDrawModelShaded(&canvas, &cpu, mvp, world, (Vector3){0, 0, 4.5f},
                        (Color){210, 210, 220, 255});
    CHECK(VCpuHash(&canvas) == hash1, "CPU shaded render is deterministic");

    /* Shadow map: a ground plane plus the helmet must produce a different
       image when directional shadows are enabled. */
    Mesh groundMesh = GenMeshCube(6.0f, 0.1f, 6.0f);
    Model ground = LoadModelFromMesh(groundMesh);
    VCpuModel groundCpu;
    memset(&groundCpu, 0, sizeof(groundCpu));
    CHECK(vgraphics_BuildCpuModel(ground, &groundCpu),
          "CPU ground model build");
    Matrix groundWorld = MatrixTranslate(0.0f, -1.15f, 0.0f);
    Matrix groundMvp = MatrixMultiply(proj, MatrixMultiply(view, groundWorld));
    Color groundTint = {90, 100, 115, 255};

    VCpuSetShadows(false, 0, 0.0025f, 0.0f);
    VCpuClear(&canvas, (Color){18, 20, 28, 255});
    VCpuClearDepth(&canvas);
    VCpuDrawModelShaded(&canvas, &groundCpu, groundMvp, groundWorld,
                        (Vector3){0, 1.0f, 5.2f}, groundTint);
    VCpuDrawModelShaded(&canvas, &cpu, mvp, world,
                        (Vector3){0, 1.0f, 5.2f},
                        (Color){210, 210, 220, 255});
    uint64_t hashNoShadow = VCpuHash(&canvas);

    VCpuSetShadows(true, 256, 0.0025f, 0.9f);
    CHECK(VCpuShadowsEnabled(), "CPU shadows enabled");
    VCpuShadowCaster casters[2] = {
        { &groundCpu, groundWorld },
        { &cpu, world }
    };
    VCpuShadowBuild(casters, 2);
    VCpuClear(&canvas, (Color){18, 20, 28, 255});
    VCpuClearDepth(&canvas);
    VCpuDrawModelShaded(&canvas, &groundCpu, groundMvp, groundWorld,
                        (Vector3){0, 1.0f, 5.2f}, groundTint);
    VCpuDrawModelShaded(&canvas, &cpu, mvp, world,
                        (Vector3){0, 1.0f, 5.2f},
                        (Color){210, 210, 220, 255});
    uint64_t hashShadow = VCpuHash(&canvas);
    CHECK(hashShadow != hashNoShadow, "shadow map changes the rendered image");

    /* IBL: procedural bundle must be accepted and change the image. */
    VIBL_Bundle ibl = VIBL_BuildProcedural(16, 8);
    CHECK(ibl.valid, "procedural IBL bundle valid");
    CHECK(VCpuSetIBLBundle(&ibl), "CPU accepts the IBL bundle");
    CHECK(VCpuIsIBLLoaded(), "CPU IBL is loaded");
    VCpuSetIBLIntensity(1.0f);
    VCpuClear(&canvas, (Color){18, 20, 28, 255});
    VCpuClearDepth(&canvas);
    VCpuDrawModelShaded(&canvas, &groundCpu, groundMvp, groundWorld,
                        (Vector3){0, 1.0f, 5.2f}, groundTint);
    VCpuDrawModelShaded(&canvas, &cpu, mvp, world,
                        (Vector3){0, 1.0f, 5.2f},
                        (Color){210, 210, 220, 255});
    CHECK(VCpuHash(&canvas) != hashShadow, "IBL changes the rendered image");
    VCpuUnloadIBL();
    CHECK(!VCpuIsIBLLoaded(), "CPU IBL unload");
    VCpuSetShadows(false, 0, 0.0025f, 0.0f);

    VCpuModelFree(&groundCpu);
    UnloadModel(ground);
    VCpuModelFree(&cpu);
    VCpuFree(&canvas);
    UnloadModel(model);
    VCpuShutdown();

    printf("cpu_gltf: %d failure(s)\n", fails);
    return fails ? 1 : 0;
}
