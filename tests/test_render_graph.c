#include "vrender_graph.h"

#include <stdio.h>
#include <string.h>

static int fails = 0;

static void expect(int condition, const char *name) {
    if (condition) {
        printf("[PASS] %s\n", name);
    } else {
        printf("[FAIL] %s\n", name);
        fails++;
    }
}

int main(void) {
    VrgGraph *g = vrgCreate();
    expect(g != NULL, "create");
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
    expect(colorId == 0 && depthId == 1, "add images");

    int gbuffer = vrgAddPass(g, "gbuffer");
    int lighting = vrgAddPass(g, "lighting");
    expect(gbuffer == 0 && lighting == 1, "add passes");

    expect(vrgPassWriteImage(g, gbuffer, colorId,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT), "write color");
    expect(vrgPassWriteImage(g, gbuffer, depthId,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT), "write depth");
    expect(vrgPassReadImage(g, lighting, colorId,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT), "read color");
    expect(vrgPassReadImage(g, lighting, depthId,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT), "read depth");

    expect(vrgCompile(g), "compile simple graph");
    expect(vrgGetError(g) == NULL, "no compile error");
    expect(vrgGetPassOrder(g, 0) == gbuffer, "pass order 0");
    expect(vrgGetPassOrder(g, 1) == lighting, "pass order 1");

    /* color: UNDEFINED -> ATTACHMENT, ATTACHMENT -> SHADER_READ
       depth: UNDEFINED -> ATTACHMENT, ATTACHMENT -> SHADER_READ */
    expect(vrgGetBarrierCount(g) == 4, "four barriers");

    const VrgImageBarrier *b0 = vrgGetBarrier(g, 0);
    expect(b0 && b0->image == colorId &&
           b0->oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
           b0->newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
           "color initial transition");

    int readBarrier = -1;
    for (int i = 0; i < vrgGetBarrierCount(g); ++i) {
        const VrgImageBarrier *b = vrgGetBarrier(g, i);
        if (b && b->image == colorId &&
            b->newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            readBarrier = i;
            break;
        }
    }
    expect(readBarrier >= 0, "color read barrier exists");
    if (readBarrier >= 0) {
        const VrgImageBarrier *b = vrgGetBarrier(g, readBarrier);
        expect(b->srcAccess == VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
               "read barrier src access");
        expect(b->dstAccess == VK_ACCESS_SHADER_READ_BIT,
               "read barrier dst access");
    }

    /* An imported image may be read without a writer. */
    VrgGraph *imported = vrgCreate();
    VrgImageDesc external = {
        "external", 64, 64, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    int extId = vrgAddImage(imported, &external);
    int sample = vrgAddPass(imported, "sample");
    vrgPassReadImage(imported, sample, extId,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT);
    expect(vrgCompile(imported), "compile imported image");
    expect(vrgGetBarrierCount(imported) == 0, "no barrier for imported image");
    vrgDestroy(imported);

    /* External images execute without vrgAllocateVulkan. */
    VrgGraph *externalGraph = vrgCreate();
    VrgImageDesc target = {
        "swapchain", 64, 64, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };
    int targetId = vrgAddExternalImage(externalGraph, &target,
                                       (VkImage)1, (VkImageView)1);
    int present = vrgAddPass(externalGraph, "composite");
    vrgPassWriteImage(externalGraph, present, targetId,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    expect(targetId == 0, "add external image");
    expect(vrgCompile(externalGraph), "compile external graph");
    expect(vrgGetBarrierCount(externalGraph) == 2,
           "external graph transitions in and out");
    vrgDestroy(externalGraph);

    /* A transient read before write must fail. */
    VrgGraph *bad = vrgCreate();
    int badImage = vrgAddImage(bad, &color);
    int badPass = vrgAddPass(bad, "bad");
    vrgPassReadImage(bad, badPass, badImage,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT);
    expect(!vrgCompile(bad), "read before write rejected");
    expect(vrgGetError(bad) != NULL, "read before write error set");
    vrgDestroy(bad);

    vrgDestroy(g);

    printf("render_graph: %d failure(s)\n", fails);
    return fails ? 1 : 0;
}
