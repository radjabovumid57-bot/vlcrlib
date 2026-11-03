/* render_graph_example.c — prints a compiled g-buffer/lighting graph. */
#include "vrender_graph.h"
#include <stdio.h>

int main(void) {
    VrgGraph *g = vrgCreate();
    if (!g) return 1;

    VrgImageDesc color = {
        "color", 1280, 720, VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    VrgImageDesc depth = {
        "depth", 1280, 720, VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    int colorId = vrgAddImage(g, &color);
    int depthId = vrgAddImage(g, &depth);
    int gbuffer = vrgAddPass(g, "gbuffer");
    int lighting = vrgAddPass(g, "lighting");

    vrgPassWriteImage(g, gbuffer, colorId,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    vrgPassWriteImage(g, gbuffer, depthId,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    vrgPassReadImage(g, lighting, colorId,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT);
    vrgPassReadImage(g, lighting, depthId,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT);

    if (!vrgCompile(g)) {
        fprintf(stderr, "compile failed: %s\n", vrgGetError(g));
        vrgDestroy(g);
        return 1;
    }

    printf("passes:\n");
    for (int i = 0; i < vrgGetPassCount(g); ++i) {
        int pass = vrgGetPassOrder(g, i);
        const VrgPassInfo *info = vrgGetPass(g, pass);
        printf("  %d: %s (r=%d w=%d)\n", i, info->name,
               info->readCount, info->writeCount);
    }
    printf("barriers:\n");
    for (int i = 0; i < vrgGetBarrierCount(g); ++i) {
        const VrgImageBarrier *b = vrgGetBarrier(g, i);
        printf("  image=%d pass=%d %d -> %d\n", b->image, b->pass,
               (int)b->oldLayout, (int)b->newLayout);
    }

    vrgDestroy(g);
    return 0;
}
