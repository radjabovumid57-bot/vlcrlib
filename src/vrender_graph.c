#include "vrender_graph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct VrgUse {
    int                   image;
    int                   pass;
    int                   write;
    VkImageLayout         layout;
    VkPipelineStageFlags  stage;
    VkAccessFlags         access;
} VrgUse;

struct VrgGraph {
    VrgImageDesc images[VRG_MAX_IMAGES];
    int          imageCount;

    VrgPassInfo  passes[VRG_MAX_PASSES];
    int          passCount;

    VrgUse       uses[VRG_MAX_USES];
    int          useCount;

    int          passOrder[VRG_MAX_PASSES];
    int          passOrderCount;

    VrgImageBarrier barriers[VRG_MAX_BARRIERS];
    int          barrierCount;

    VrgVulkanImage vkImages[VRG_MAX_IMAGES];
    bool           external[VRG_MAX_IMAGES];
    int            externalCount;
    bool           compiled;
    bool           allocated;

    char         error[256];
};

static uint32_t vrgFindMemoryType(VkPhysicalDevice physical, uint32_t bits,
                                  VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(physical, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    return UINT32_MAX;
}

static VkImageAspectFlags vrgAspectForFormat(VkFormat format) {
    switch (format) {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_D32_SFLOAT:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        case VK_FORMAT_S8_UINT:
            return VK_IMAGE_ASPECT_STENCIL_BIT;
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

static void vrgSetError(VrgGraph *g, const char *text) {
    if (!g) return;
    snprintf(g->error, sizeof(g->error), "%s", text ? text : "error");
}

static bool vrgValidImage(const VrgGraph *g, int image) {
    return g && image >= 0 && image < g->imageCount;
}
static bool vrgValidPass(const VrgGraph *g, int pass) {
    return g && pass >= 0 && pass < g->passCount;
}

VrgGraph *vrgCreate(void) {
    VrgGraph *g = (VrgGraph *)calloc(1, sizeof(*g));
    return g;
}

void vrgDestroy(VrgGraph *g) {
    free(g);
}

int vrgAddImage(VrgGraph *g, const VrgImageDesc *desc) {
    if (!g || !desc || g->imageCount >= VRG_MAX_IMAGES) return -1;
    if (desc->width == 0 || desc->height == 0) return -1;
    if (g->allocated) return -1;
    int idx = g->imageCount++;
    g->images[idx] = *desc;
    if (!g->images[idx].name) g->images[idx].name = "image";
    g->compiled = false;
    return idx;
}

int vrgAddExternalImage(VrgGraph *g, const VrgImageDesc *desc,
                        VkImage image, VkImageView view) {
    if (!g || !desc || !image || !view || g->allocated) return -1;
    int idx = vrgAddImage(g, desc);
    if (idx < 0) return -1;
    g->external[idx] = true;
    g->externalCount++;
    g->vkImages[idx].image = image;
    g->vkImages[idx].view = view;
    return idx;
}

int vrgAddPass(VrgGraph *g, const char *name) {
    if (!g || g->passCount >= VRG_MAX_PASSES) return -1;
    if (g->allocated) return -1;
    int idx = g->passCount++;
    g->passes[idx].name = name ? name : "pass";
    g->compiled = false;
    return idx;
}

static bool vrgAddUse(VrgGraph *g, int pass, int image, bool write,
                      VkImageLayout layout, VkPipelineStageFlags stage,
                      VkAccessFlags access) {
    if (!vrgValidPass(g, pass) || !vrgValidImage(g, image)) return false;
    if (g->useCount >= VRG_MAX_USES || g->allocated) return false;
    g->compiled = false;
    VrgUse *u = &g->uses[g->useCount++];
    u->pass = pass;
    u->image = image;
    u->write = write ? 1 : 0;
    u->layout = layout;
    u->stage = stage;
    u->access = access;
    if (write) g->passes[pass].writeCount++;
    else       g->passes[pass].readCount++;
    return true;
}

bool vrgPassReadImage(VrgGraph *g, int pass, int image,
                      VkImageLayout layout,
                      VkPipelineStageFlags stage,
                      VkAccessFlags access) {
    return vrgAddUse(g, pass, image, false, layout, stage, access);
}

bool vrgPassWriteImage(VrgGraph *g, int pass, int image,
                       VkImageLayout layout,
                       VkPipelineStageFlags stage,
                       VkAccessFlags access) {
    return vrgAddUse(g, pass, image, true, layout, stage, access);
}

static bool vrgAddBarrier(VrgGraph *g, int image, int pass,
                          VkImageLayout oldLayout, VkImageLayout newLayout,
                          VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                          VkPipelineStageFlags srcStage,
                          VkPipelineStageFlags dstStage) {
    if (g->barrierCount >= VRG_MAX_BARRIERS) return false;
    VrgImageBarrier *b = &g->barriers[g->barrierCount++];
    b->image = image;
    b->pass = pass;
    b->oldLayout = oldLayout;
    b->newLayout = newLayout;
    b->srcAccess = srcAccess;
    b->dstAccess = dstAccess;
    b->srcStage = srcStage;
    b->dstStage = dstStage;
    return true;
}

bool vrgCompile(VrgGraph *g) {
    if (!g) return false;
    if (g->allocated) {
        vrgSetError(g, "cannot compile an allocated graph");
        return false;
    }
    g->error[0] = '\0';
    g->passOrderCount = 0;
    g->barrierCount = 0;
    g->compiled = false;

    if (g->passCount == 0) {
        g->compiled = true;
        g->allocated = (g->externalCount == g->imageCount);
        return true;
    }

    /* Edges: pass -> pass. A read after a writer and a write after readers
       both create dependencies. */
    unsigned char edges[VRG_MAX_PASSES][VRG_MAX_PASSES];
    memset(edges, 0, sizeof(edges));

    int lastWriter[VRG_MAX_IMAGES];
    uint64_t readers[VRG_MAX_IMAGES][2];
    for (int i = 0; i < VRG_MAX_IMAGES; ++i) {
        lastWriter[i] = -1;
        readers[i][0] = readers[i][1] = 0;
    }

    for (int u = 0; u < g->useCount; ++u) {
        const VrgUse *use = &g->uses[u];
        int img = use->image;
        int pass = use->pass;

        if (!use->write) {
            if (lastWriter[img] >= 0 && lastWriter[img] != pass)
                edges[lastWriter[img]][pass] = 1;
            else if (lastWriter[img] < 0 &&
                     g->images[img].initialLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
                vrgSetError(g, "read before write on a transient image");
                return false;
            }
            if (pass < 128) readers[img][pass >> 6] |= (uint64_t)1 << (pass & 63);
        } else {
            if (lastWriter[img] >= 0 && lastWriter[img] != pass)
                edges[lastWriter[img]][pass] = 1;
            for (int r = 0; r < g->passCount; ++r) {
                if (r == pass) continue;
                if (readers[img][r >> 6] & ((uint64_t)1 << (r & 63)))
                    edges[r][pass] = 1;
            }
            readers[img][0] = readers[img][1] = 0;
            lastWriter[img] = pass;
        }
    }

    int indegree[VRG_MAX_PASSES] = {0};
    for (int a = 0; a < g->passCount; ++a)
        for (int b = 0; b < g->passCount; ++b)
            if (edges[a][b]) indegree[b]++;

    /* Kahn, always choosing the lowest pass index for deterministic output. */
    bool emitted[VRG_MAX_PASSES] = {false};
    while (g->passOrderCount < g->passCount) {
        int next = -1;
        for (int p = 0; p < g->passCount; ++p) {
            if (!emitted[p] && indegree[p] == 0) { next = p; break; }
        }
        if (next < 0) {
            vrgSetError(g, "render graph cycle");
            return false;
        }
        emitted[next] = true;
        g->passOrder[g->passOrderCount++] = next;
        for (int b = 0; b < g->passCount; ++b)
            if (edges[next][b]) indegree[b]--;
    }

    /* Barrier plan in execution order. */
    VkImageLayout layout[VRG_MAX_IMAGES];
    VkAccessFlags access[VRG_MAX_IMAGES];
    VkPipelineStageFlags stage[VRG_MAX_IMAGES];
    for (int i = 0; i < g->imageCount; ++i) {
        layout[i] = g->images[i].initialLayout;
        access[i] = 0;
        stage[i] = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }

    for (int oi = 0; oi < g->passOrderCount; ++oi) {
        int pass = g->passOrder[oi];
        for (int u = 0; u < g->useCount; ++u) {
            const VrgUse *use = &g->uses[u];
            if (use->pass != pass) continue;
            int img = use->image;
            bool hazard = (layout[img] != use->layout) ||
                          (use->write != 0) ||
                          ((access[img] & (VK_ACCESS_SHADER_WRITE_BIT |
                                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                           VK_ACCESS_TRANSFER_WRITE_BIT)) != 0) ||
                          ((use->access & (VK_ACCESS_SHADER_WRITE_BIT |
                                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                           VK_ACCESS_TRANSFER_WRITE_BIT)) != 0);
            if (hazard) {
                if (!vrgAddBarrier(g, img, pass, layout[img], use->layout,
                                   access[img], use->access,
                                   stage[img], use->stage))
                    return false;
            }
            layout[img] = use->layout;
            access[img] = use->access;
            stage[img] = use->stage;
        }
    }

    for (int i = 0; i < g->imageCount; ++i) {
        VkImageLayout finalLayout = g->images[i].finalLayout;
        if (finalLayout == VK_IMAGE_LAYOUT_UNDEFINED) continue;
        if (layout[i] != finalLayout) {
            if (!vrgAddBarrier(g, i, -1, layout[i], finalLayout,
                               access[i], 0, stage[i],
                               VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT))
                return false;
        }
    }
    g->compiled = true;
    g->allocated = (g->externalCount == g->imageCount);
    return true;
}

int vrgGetImageCount(const VrgGraph *g) { return g ? g->imageCount : 0; }
int vrgGetPassCount(const VrgGraph *g)  { return g ? g->passCount : 0; }

const VrgImageDesc *vrgGetImage(const VrgGraph *g, int image) {
    return vrgValidImage(g, image) ? &g->images[image] : NULL;
}
const VrgPassInfo *vrgGetPass(const VrgGraph *g, int pass) {
    return vrgValidPass(g, pass) ? &g->passes[pass] : NULL;
}
int vrgGetPassOrder(const VrgGraph *g, int index) {
    if (!g || index < 0 || index >= g->passOrderCount) return -1;
    return g->passOrder[index];
}
int vrgGetBarrierCount(const VrgGraph *g) {
    return g ? g->barrierCount : 0;
}
const VrgImageBarrier *vrgGetBarrier(const VrgGraph *g, int index) {
    if (!g || index < 0 || index >= g->barrierCount) return NULL;
    return &g->barriers[index];
}
const char *vrgGetError(const VrgGraph *g) {
    return (g && g->error[0]) ? g->error : NULL;
}

bool vrgAllocateVulkan(VrgGraph *g, VkDevice device, VkPhysicalDevice physical) {
    if (!g || !device || !physical) return false;
    if (!g->compiled || g->allocated) return false;
    if (g->externalCount > 0) return false; /* mixed external/transient unsupported */

    for (int i = 0; i < g->imageCount; ++i) {
        const VrgImageDesc *desc = &g->images[i];
        VkImageCreateInfo ici = {0};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = desc->format;
        ici.extent.width = desc->width;
        ici.extent.height = desc->height;
        ici.extent.depth = 1;
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = desc->usage;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VrgVulkanImage *dst = &g->vkImages[i];
        if (vkCreateImage(device, &ici, NULL, &dst->image) != VK_SUCCESS) {
            vrgFreeVulkan(g, device);
            return false;
        }

        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(device, dst->image, &req);
        uint32_t type = vrgFindMemoryType(physical, req.memoryTypeBits,
                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (type == UINT32_MAX) {
            vrgFreeVulkan(g, device);
            return false;
        }
        VkMemoryAllocateInfo mai = {0};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = type;
        if (vkAllocateMemory(device, &mai, NULL, &dst->memory) != VK_SUCCESS) {
            vrgFreeVulkan(g, device);
            return false;
        }
        if (vkBindImageMemory(device, dst->image, dst->memory, 0) != VK_SUCCESS) {
            vrgFreeVulkan(g, device);
            return false;
        }

        VkImageViewCreateInfo vci = {0};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = dst->image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = desc->format;
        vci.subresourceRange.aspectMask = vrgAspectForFormat(desc->format);
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &vci, NULL, &dst->view) != VK_SUCCESS) {
            vrgFreeVulkan(g, device);
            return false;
        }
    }
    g->allocated = true;
    return true;
}

void vrgFreeVulkan(VrgGraph *g, VkDevice device) {
    if (!g) return;
    for (int i = 0; i < g->imageCount; ++i) {
        if (g->external[i]) continue;
        VrgVulkanImage *img = &g->vkImages[i];
        if (img->view && device) vkDestroyImageView(device, img->view, NULL);
        if (img->image && device) vkDestroyImage(device, img->image, NULL);
        if (img->memory && device) vkFreeMemory(device, img->memory, NULL);
        *img = (VrgVulkanImage){0};
    }
    g->allocated = false;
}

VkImage vrgGetVkImage(const VrgGraph *g, int image) {
    return (g && image >= 0 && image < g->imageCount && g->allocated)
        ? g->vkImages[image].image : VK_NULL_HANDLE;
}

VkImageView vrgGetVkImageView(const VrgGraph *g, int image) {
    return (g && image >= 0 && image < g->imageCount && g->allocated)
        ? g->vkImages[image].view : VK_NULL_HANDLE;
}

static void vrgEmitBarriers(VrgGraph *g, VkCommandBuffer cb, int pass) {
    for (int i = 0; i < g->barrierCount; ++i) {
        const VrgImageBarrier *b = &g->barriers[i];
        if (b->pass != pass) continue;
        VrgVulkanImage *img = &g->vkImages[b->image];
        if (!img->image) continue;
        VkImageMemoryBarrier barrier = {0};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = b->oldLayout;
        barrier.newLayout = b->newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = img->image;
        barrier.subresourceRange.aspectMask =
            vrgAspectForFormat(g->images[b->image].format);
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = b->srcAccess;
        barrier.dstAccessMask = b->dstAccess;
        vkCmdPipelineBarrier(cb, b->srcStage, b->dstStage, 0,
                             0, NULL, 0, NULL, 1, &barrier);
    }
}

bool vrgExecute(VrgGraph *g, VkCommandBuffer cb,
                VrgPassCallback callback, void *user) {
    if (!g || !cb || !g->compiled || !g->allocated) return false;
    for (int i = 0; i < g->passOrderCount; ++i) {
        int pass = g->passOrder[i];
        vrgEmitBarriers(g, cb, pass);
        if (callback) callback(cb, pass, user);
    }
    vrgEmitBarriers(g, cb, -1);
    return true;
}
