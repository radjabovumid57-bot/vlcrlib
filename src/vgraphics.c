#include "vgraphics.h"
#include "vlcrlib.h"
#include "vgraphics_internal.h"
#include "vcpu.h"

/* Optional debug names: only effective with VK_EXT_debug_utils enabled. */
static void vlcrNameImage(VkImage image, const char *name) {
    if (!image || !name || !VK.instance || !VK.device) return;
    PFN_vkSetDebugUtilsObjectNameEXT set_name =
        (PFN_vkSetDebugUtilsObjectNameEXT)vkGetInstanceProcAddr(
            VK.instance, "vkSetDebugUtilsObjectNameEXT");
    if (!set_name) return;
    VkDebugUtilsObjectNameInfoEXT info = {0};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType = VK_OBJECT_TYPE_IMAGE;
    info.objectHandle = (uint64_t)image;
    info.pObjectName = name;
    set_name(VK.device, &info);
}

static VkCommandBuffer ibl_begin_cmd(void);
static void ibl_end_cmd(VkCommandBuffer cb);
static void vlcrInitSampledImage(VkImage image, VkImageAspectFlags aspect) {
    if (!image) return;
    VkCommandBuffer cb = ibl_begin_cmd();
    VkImageMemoryBarrier b = {0};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = (VkImageSubresourceRange){ aspect, 0, 1, 0, 1 };
    b.srcAccessMask = 0;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &b);
    ibl_end_cmd(cb);
}

/* post-process (defined later) */
static void recordBloomPass(VkCommandBuffer cb);
static void recordComposite(VkCommandBuffer cb);
static bool recordBloomGraph(VkCommandBuffer cb);
static bool recordSSAOGraph(VkCommandBuffer cb);
static bool recordCompositeGraph(VkCommandBuffer cb);
static void updateTaaParams(void);
static bool createVelocityResources(void);
static void recordVelocityPass(VkCommandBuffer cb);
static bool createCompositePipeline(void);
/* device module (vgraphics_device.c) */
bool createInstance(void);
bool pickPhysicalDevice(void);
bool createDevice(void);
bool createSwapchain(uint32_t w, uint32_t h);
bool createDepthResources(void);
bool createRenderPass(void);
bool createFramebuffers(void);
bool createCommands(void);
bool createSync(void);
bool createRenderPassRT(void);
bool vgraphics_internal_recreate(void);

/* pipelines module */
VkShaderModule createShaderModule(const char *path);
bool createSampler2D(void);
bool createDescriptorSet2D(void);
bool createPipeline2D(void);
bool createRayTraceDemoPipeline(void);
bool createUBO3D(void);
bool createDescriptorSet3D(void);
bool createPipeline3D(bool isLine);
bool createUBOPBR(void);
bool createLightBuffer(void);
bool createDescriptorSetLayoutPBR(void);
bool createPipelinePBR(void);
bool createBoneBuffer(void);
bool createDescriptorSetLayoutSkin(void);
bool createPipelineSkin(void);
#include "vibl.h"
#include "vtextures.h"
#include "vrender_graph.h"
#include <stb_image_write.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#include "vgltf.h"

typedef struct TaaParams {
    float curVP[16];
    float prevVP[16];
    float params[4]; /* x=enabled, y=feedback, z=reset, w=unused */
} TaaParams;

/* ================================================================== */
/*  Constants                                                         */
/* ================================================================== */
/* MAX_FRAMES_IN_FLIGHT in vgraphics_internal.h */

#ifndef VLCRLIB_MAX_TEXTURES
#define VLCRLIB_MAX_TEXTURES    256
#endif
#define VLCRLIB_MAX_MESHES      512
#ifndef VLCRLIB_MAX_MATERIALS
#define VLCRLIB_MAX_MATERIALS   128
#endif
#define VLCRLIB_MAX_RT          32
#ifndef VLCRLIB_MAX_MODELS
#define VLCRLIB_MAX_MODELS      64
#endif
#define VLCRLIB_MAX_SKELETONS   32
#define VLCRLIB_MAX_ANIMATIONS  128
#ifndef VLCRLIB_MAX_BONES
#define VLCRLIB_MAX_BONES       256
#endif
#ifndef VLCRLIB_MAX_SKMODELS
#define VLCRLIB_MAX_SKMODELS    32
#endif
#ifndef VLCRLIB_MAX_2D_DESCRIPTORS
#define VLCRLIB_MAX_2D_DESCRIPTORS 512
#endif

/* ================================================================== */
/*  Vulkan state                                                      */
/* ================================================================== */
/* VkState: see vgraphics_internal.h */


VkState VK;

/* ================================================================== */
/*  Side tables                                                       */
/* ================================================================== */
TextureEntry g_textureTable[VLCRLIB_MAX_TEXTURES];
int g_textureCount = 0;
static unsigned int g_textureGenerations[VLCRLIB_MAX_TEXTURES];

/* Lightweight frame instrumentation (see FrameStats in vgraphics.h). */
static FrameStats g_frameStats;
static Uint64     g_frameStatsStart = 0;
static uint32_t   g_timerSlotValid = 0;

static void statsCountDraw(int indexCount, int transparent) {
    g_frameStats.drawCalls++;
    g_frameStats.triangles += (uint64_t)(indexCount > 0 ? indexCount / 3 : 0);
    if (transparent) g_frameStats.transparentDraws++;
}
static void statsCountShadowDraw(int indexCount) {
    g_frameStats.shadowDraws++;
    g_frameStats.triangles += (uint64_t)(indexCount > 0 ? indexCount / 3 : 0);
}

static unsigned int textureGeneration(unsigned int slot) {
    if (slot == 0 || slot >= VLCRLIB_MAX_TEXTURES) return 0;
    if (g_textureGenerations[slot] == 0) g_textureGenerations[slot] = 1;
    return g_textureGenerations[slot];
}

static void invalidateTextureGeneration(unsigned int slot) {
    if (slot == 0 || slot >= VLCRLIB_MAX_TEXTURES) return;
    unsigned int next = textureGeneration(slot) + 1;
    g_textureGenerations[slot] = next ? next : 1;
}

static int allocateTextureSlot(void) {
    for (int i = 1; i < VLCRLIB_MAX_TEXTURES; ++i) {
        if (!g_textureTable[i].view && !g_textureTable[i].img &&
            !g_textureTable[i].cpuPixels) {
            if (i > g_textureCount) g_textureCount = i;
            (void)textureGeneration((unsigned int)i);
            return i;
        }
    }
    return 0;
}

typedef struct DeferredTextureRelease {
    TextureEntry entry;
    uint32_t frameSlot;
} DeferredTextureRelease;
static DeferredTextureRelease g_deferredTextureReleases[VLCRLIB_MAX_TEXTURES];
static int g_deferredTextureReleaseCount = 0;

static void destroyTextureEntry(TextureEntry *entry) {
    if (!entry) return;
    free(entry->cpuPixels);
    entry->cpuPixels = NULL;
    if (!VK.device) { *entry = (TextureEntry){0}; return; }
    if (entry->view) vkDestroyImageView(VK.device, entry->view, NULL);
    if (entry->img) vkDestroyImage(VK.device, entry->img, NULL);
    if (entry->mem) vkFreeMemory(VK.device, entry->mem, NULL);
    if (entry->ownSamp && entry->samp)
        vkDestroySampler(VK.device, entry->samp, NULL);
    *entry = (TextureEntry){0};
}

static void releaseCompletedTextures(uint32_t frameSlot, bool waitAll) {
    if (waitAll && VK.device) vkDeviceWaitIdle(VK.device);
    for (int i = 0; i < g_deferredTextureReleaseCount;) {
        DeferredTextureRelease *pending = &g_deferredTextureReleases[i];
        if (!waitAll && pending->frameSlot != frameSlot) { ++i; continue; }
        destroyTextureEntry(&pending->entry);
        g_deferredTextureReleases[i] =
            g_deferredTextureReleases[--g_deferredTextureReleaseCount];
    }
}

typedef struct MeshEntry {
    VkBuffer       vbo, ibo;
    VkDeviceMemory vboMem, iboMem;
    int            vertexCount, indexCount;
    int            isSkinned;
    int            hasBounds;
    Vector3        boundsMin, boundsMax;
    unsigned int   generation;
    /* CPU-side copy for the software backend bridge. */
    Vertex3D      *cpuVertices;
    uint32_t      *cpuIndices;
    int            cpuVertexCount;
} MeshEntry;
static MeshEntry g_meshTable[VLCRLIB_MAX_MESHES];
static int       g_meshCount = 0;
static unsigned int g_meshGenerations[VLCRLIB_MAX_MESHES];

static unsigned int meshGeneration(unsigned int slot) {
    if (!slot || slot >= VLCRLIB_MAX_MESHES) return 0;
    if (!g_meshGenerations[slot]) g_meshGenerations[slot] = 1;
    return g_meshGenerations[slot];
}

static void invalidateMeshGeneration(unsigned int slot) {
    if (!slot || slot >= VLCRLIB_MAX_MESHES) return;
    unsigned int next = meshGeneration(slot) + 1;
    g_meshGenerations[slot] = next ? next : 1;
}

static int allocateMeshSlot(void) {
    for (int i = 1; i < VLCRLIB_MAX_MESHES; ++i) {
        if (!g_meshTable[i].vbo && !g_meshTable[i].ibo &&
            !g_meshTable[i].cpuVertices) {
            if (i > g_meshCount) g_meshCount = i;
            (void)meshGeneration((unsigned int)i);
            return i;
        }
    }
    return 0;
}

static bool meshHandleValid(Mesh mesh) {
    return mesh.id > 0 && mesh.id < VLCRLIB_MAX_MESHES &&
           mesh.generation != 0 &&
           mesh.generation == g_meshTable[mesh.id].generation &&
           (g_meshTable[mesh.id].vbo != VK_NULL_HANDLE ||
            g_meshTable[mesh.id].cpuVertices != NULL);
}

typedef struct DeferredMeshRelease {
    MeshEntry entry;
    uint32_t frameSlot;
} DeferredMeshRelease;
static DeferredMeshRelease g_deferredMeshReleases[VLCRLIB_MAX_MESHES];
static int g_deferredMeshReleaseCount = 0;

static void destroyMeshEntry(MeshEntry *entry) {
    if (!entry) return;
    free(entry->cpuVertices);
    free(entry->cpuIndices);
    entry->cpuVertices = NULL;
    entry->cpuIndices = NULL;
    if (!VK.device) { *entry = (MeshEntry){0}; return; }
    if (entry->vbo) vkDestroyBuffer(VK.device, entry->vbo, NULL);
    if (entry->vboMem) vkFreeMemory(VK.device, entry->vboMem, NULL);
    if (entry->ibo) vkDestroyBuffer(VK.device, entry->ibo, NULL);
    if (entry->iboMem) vkFreeMemory(VK.device, entry->iboMem, NULL);
    *entry = (MeshEntry){0};
}

static void releaseCompletedMeshes(uint32_t frameSlot, bool waitAll) {
    if (waitAll && VK.device) vkDeviceWaitIdle(VK.device);
    for (int i = 0; i < g_deferredMeshReleaseCount;) {
        DeferredMeshRelease *pending = &g_deferredMeshReleases[i];
        if (!waitAll && pending->frameSlot != frameSlot) { ++i; continue; }
        destroyMeshEntry(&pending->entry);
        g_deferredMeshReleases[i] =
            g_deferredMeshReleases[--g_deferredMeshReleaseCount];
    }
}

PBRMaterialEntry g_matTable[VLCRLIB_MAX_MATERIALS];
int g_matCount = 0;

typedef struct RenderTargetEntry {
    VkFramebuffer  fb;
    VkImage        colorImg; VkDeviceMemory colorMem; VkImageView colorView;
    VkImage        depthImg; VkDeviceMemory depthMem; VkImageView depthView;
    int            w, h;
    unsigned int   textureId;
    unsigned int   generation;
} RenderTargetEntry;
static RenderTargetEntry g_rtTable[VLCRLIB_MAX_RT];
static int               g_rtCount = 0;
static unsigned int g_rtGenerations[VLCRLIB_MAX_RT];

typedef struct DeferredRenderTargetRelease {
    RenderTargetEntry entry;
    uint32_t frameSlot;
} DeferredRenderTargetRelease;
static DeferredRenderTargetRelease g_deferredRTReleases[VLCRLIB_MAX_RT];
static int g_deferredRTReleaseCount = 0;

static unsigned int rtGeneration(unsigned int slot) {
    if (!slot || slot >= VLCRLIB_MAX_RT) return 0;
    if (!g_rtGenerations[slot]) g_rtGenerations[slot] = 1;
    return g_rtGenerations[slot];
}

static void invalidateRTGeneration(unsigned int slot) {
    if (!slot || slot >= VLCRLIB_MAX_RT) return;
    unsigned int next = rtGeneration(slot) + 1;
    g_rtGenerations[slot] = next ? next : 1;
}

static int allocateRTSlot(void) {
    for (int i = 1; i < VLCRLIB_MAX_RT; ++i) {
        if (!g_rtTable[i].fb && !g_rtTable[i].colorImg) {
            if (i > g_rtCount) g_rtCount = i;
            (void)rtGeneration((unsigned int)i);
            return i;
        }
    }
    return 0;
}

static void destroyRenderTargetEntry(RenderTargetEntry *entry) {
    if (!entry || !VK.device) return;
    if (entry->fb) vkDestroyFramebuffer(VK.device, entry->fb, NULL);
    if (entry->colorView) vkDestroyImageView(VK.device, entry->colorView, NULL);
    if (entry->colorImg) vkDestroyImage(VK.device, entry->colorImg, NULL);
    if (entry->colorMem) vkFreeMemory(VK.device, entry->colorMem, NULL);
    if (entry->depthView) vkDestroyImageView(VK.device, entry->depthView, NULL);
    if (entry->depthImg) vkDestroyImage(VK.device, entry->depthImg, NULL);
    if (entry->depthMem) vkFreeMemory(VK.device, entry->depthMem, NULL);
    *entry = (RenderTargetEntry){0};
}

static void releaseCompletedRenderTargets(uint32_t frameSlot, bool waitAll) {
    if (waitAll && VK.device) vkDeviceWaitIdle(VK.device);
    for (int i = 0; i < g_deferredRTReleaseCount;) {
        DeferredRenderTargetRelease *pending = &g_deferredRTReleases[i];
        if (!waitAll && pending->frameSlot != frameSlot) { ++i; continue; }
        destroyRenderTargetEntry(&pending->entry);
        g_deferredRTReleases[i] = g_deferredRTReleases[--g_deferredRTReleaseCount];
    }
}

typedef struct ModelEntry {
    Mesh    *meshes;
    int     *meshMaterials;
    Matrix  *meshTransforms; /* per-draw world matrix from glTF nodes (vkguide-style) */
    int      meshCount;
    Material*materials;
    int      materialCount;
    Texture2D *ownedTextures;
    int        ownedTextureCount;
    Matrix     prevTransform;
    int        prevTransformValid;
} ModelEntry;
static ModelEntry g_modelTable[VLCRLIB_MAX_MODELS];
static int        g_modelCount = 0;

/* Skeleton / animation */
typedef struct Bone {
    char     name[64];
    int      parent;
    Matrix   inverseBind;
    Matrix   local;
    Vector3  t, s;
    Quaternion r;
} Bone;

typedef struct KeyVec3 { float t; Vector3 v, inTangent, outTangent; } KeyVec3;
typedef struct KeyQuat { float t; Quaternion q, inTangent, outTangent; } KeyQuat;

typedef struct Track {
    KeyVec3 *pos;   int posCount;
    KeyQuat *rot;   int rotCount;
    KeyVec3 *scale; int scaleCount;
    int posInterpolation, rotInterpolation, scaleInterpolation;
} Track;

typedef struct SkeletonEntry {
    Bone    bones[VLCRLIB_MAX_BONES];
    Vector3 restT[VLCRLIB_MAX_BONES];
    Vector3 restS[VLCRLIB_MAX_BONES];
    Quaternion restR[VLCRLIB_MAX_BONES];
    Matrix  preTransform[VLCRLIB_MAX_BONES];
    int     boneCount;
} SkeletonEntry;
static SkeletonEntry g_skelTable[VLCRLIB_MAX_SKELETONS];
static int           g_skelCount = 0;

typedef struct AnimationEntry {
    char           name[64];
    float          duration;
    int            boneCount;
    Track         *tracks;
    SkeletonEntry *skeleton;
} AnimationEntry;
static AnimationEntry g_animTable[VLCRLIB_MAX_ANIMATIONS];
static int            g_animCount = 0;

typedef struct SkinnedModelEntry {
    VkBuffer       *vbos;
    VkDeviceMemory *vboMems;
    VkBuffer       *ibos;
    VkDeviceMemory *iboMems;
    int            *indexCounts;
    int             meshCount;

    Material       *materials;
    int            *meshMaterials;
    int             materialCount;

    int             skeletonId;
    int            *animationIds;
    int             animationCount;
    Vector3         boundsMin, boundsMax;
    int             hasBounds;
    VkBuffer        velocityBoneBuffer;
    VkDeviceMemory  velocityBoneMem;
    void           *velocityBoneMapped;
    VkDescriptorSet velocitySkinDesc;
    Matrix          prevBones[VLCRLIB_MAX_BONES];
    Matrix          prevTransform;
    int             prevBonesValid;
} SkinnedModelEntry;
static SkinnedModelEntry g_skModelTable[VLCRLIB_MAX_SKMODELS];
static int               g_skModelCount = 0;

/* line VBO */
static VkBuffer       g_lineVbo;
static VkDeviceMemory g_lineVboMem;
static VkDeviceSize   g_lineVboCap;

/* UBO3D / UBO_PBR: see vgraphics_ubo.h (Sprint D single source of truth) */

/* ================================================================== */
/*  Helpers                                                           */
/* ================================================================== */
uint32_t vlcr_findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(VK.phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    }
    return 0xFFFFFFFFu;
}

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags props,
                         VkBuffer *outBuf, VkDeviceMemory *outMem) {
    VkBufferCreateInfo ci = {0};
    ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size        = size;
    ci.usage       = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(VK.device, &ci, NULL, outBuf) != VK_SUCCESS) return false;

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(VK.device, *outBuf, &mr);

    VkMemoryAllocateInfo ai = {0};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = vlcr_findMemoryType(mr.memoryTypeBits, props);
    if (vkAllocateMemory(VK.device, &ai, NULL, outMem) != VK_SUCCESS) return false;
    return vkBindBufferMemory(VK.device, *outBuf, *outMem, 0) == VK_SUCCESS;
}

static bool copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) {
    VkCommandBufferAllocateInfo ai = {0};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = VK.cmdPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cb;
    if (vkAllocateCommandBuffers(VK.device, &ai, &cb) != VK_SUCCESS) return false;

    VkCommandBufferBeginInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    VkBufferCopy region = {0, 0, size};
    vkCmdCopyBuffer(cb, src, dst, 1, &region);

    vkEndCommandBuffer(cb);

    VkSubmitInfo si = {0};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cb;
    vkQueueSubmit(VK.gfxQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(VK.gfxQueue);
    vkFreeCommandBuffers(VK.device, VK.cmdPool, 1, &cb);
    return true;
}

static bool createImageMips(uint32_t w, uint32_t h, uint32_t mipLevels, VkFormat fmt,
                            VkImageTiling tiling, VkImageUsageFlags usage,
                            VkMemoryPropertyFlags props,
                            VkImage *outImg, VkDeviceMemory *outMem);

bool vlcr_createImage(uint32_t w, uint32_t h, VkFormat fmt,
                      VkImageTiling tiling, VkImageUsageFlags usage,
                      VkMemoryPropertyFlags props,
                      VkImage *outImg, VkDeviceMemory *outMem) {
    return createImageMips(w, h, 1, fmt, tiling, usage, props, outImg, outMem);
}
static bool createImage(uint32_t w, uint32_t h, VkFormat fmt,
                        VkImageTiling tiling, VkImageUsageFlags usage,
                        VkMemoryPropertyFlags props,
                        VkImage *outImg, VkDeviceMemory *outMem) {
    return vlcr_createImage(w, h, fmt, tiling, usage, props, outImg, outMem);
}

static bool createImageMips(uint32_t w, uint32_t h, uint32_t mipLevels, VkFormat fmt,
                            VkImageTiling tiling, VkImageUsageFlags usage,
                            VkMemoryPropertyFlags props,
                            VkImage *outImg, VkDeviceMemory *outMem) {
    if (mipLevels < 1) mipLevels = 1;
    VkImageCreateInfo ci = {0};
    ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.extent        = (VkExtent3D){ w, h, 1 };
    ci.mipLevels     = mipLevels;
    ci.arrayLayers   = 1;
    ci.format        = fmt;
    ci.tiling        = tiling;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ci.usage         = usage;
    ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(VK.device, &ci, NULL, outImg) != VK_SUCCESS) return false;

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(VK.device, *outImg, &mr);
    VkMemoryAllocateInfo ai = {0};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = vlcr_findMemoryType(mr.memoryTypeBits, props);
    if (vkAllocateMemory(VK.device, &ai, NULL, outMem) != VK_SUCCESS) return false;
    return vkBindImageMemory(VK.device, *outImg, *outMem, 0) == VK_SUCCESS;
}

static void transitionImageLayoutMips(VkImage img, VkFormat fmt,
                                      VkImageLayout oldL, VkImageLayout newL,
                                      uint32_t baseMip, uint32_t mipCount) {
    (void)fmt;
    VkCommandBufferAllocateInfo ai = {0};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = VK.cmdPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(VK.device, &ai, &cb);

    VkCommandBufferBeginInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    VkImageMemoryBarrier b = {0};
    b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout           = oldL;
    b.newLayout           = newL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = img;
    b.subresourceRange    = (VkImageSubresourceRange){
        VK_IMAGE_ASPECT_COLOR_BIT, baseMip, mipCount, 0, 1
    };

    VkPipelineStageFlags srcStage, dstStage;
    if (oldL == VK_IMAGE_LAYOUT_UNDEFINED &&
        newL == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldL == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newL == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    vkCmdPipelineBarrier(cb, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1, &b);
    vkEndCommandBuffer(cb);

    VkSubmitInfo si = {0};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cb;
    vkQueueSubmit(VK.gfxQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(VK.gfxQueue);
    vkFreeCommandBuffers(VK.device, VK.cmdPool, 1, &cb);
}

static void transitionImageLayout(VkImage img, VkFormat fmt,
                                  VkImageLayout oldL, VkImageLayout newL) {
    transitionImageLayoutMips(img, fmt, oldL, newL, 0, 1);
}

/* static VkShaderModule createShaderModule -> vgraphics_pipeline.c */


/* ================================================================== */
/*  Instance / device / swapchain                                     */
/* ================================================================== */
/* static bool createInstance -> vgraphics_device.c */


int deviceTypeScore(VkPhysicalDeviceType t) {
    /* Higher = better. Prefer real GPU; allow CPU (lavapipe/SwiftShader) as last resort. */
    switch (t) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return 400;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 300;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return 200;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            return 100; /* software Vulkan */
        default:                                    return 10;
    }
}

const char *deviceTypeName(VkPhysicalDeviceType t) {
    switch (t) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "DISCRETE_GPU";
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "INTEGRATED_GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "VIRTUAL_GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "CPU";
        default:                                    return "OTHER";
    }
}

/* static bool pickPhysicalDevice -> vgraphics_device.c */


/* static bool createDevice -> vgraphics_device.c */


/* static bool createSwapchain -> vgraphics_device.c */


/* static bool createDepthResources -> vgraphics_device.c */


/* static bool createRenderPass -> vgraphics_device.c */


/* static bool createFramebuffers -> vgraphics_device.c */


/* static bool createCommands -> vgraphics_device.c */


/* static bool createSync -> vgraphics_device.c */


/* ================================================================== */
/*  2D pipeline                                                       */
/* ================================================================== */
/* static bool createSampler2D -> vgraphics_pipeline.c */


/* static bool createDescriptorSet2D -> vgraphics_pipeline.c */


/* static bool createPipeline2D -> vgraphics_pipeline.c */


/* ================================================================== */
/*  3D pipeline (unlit, per-object color)                             */
/* ================================================================== */
/* static bool createUBO3D -> vgraphics_pipeline.c */


/* static bool createDescriptorSet3D -> vgraphics_pipeline.c */


/* static bool createPipeline3D -> vgraphics_pipeline.c */


/* ================================================================== */
/*  PBR pipeline                                                      */
/* ================================================================== */
/* static bool createUBOPBR -> vgraphics_pipeline.c */


/* static bool createDescriptorSetLayoutPBR -> vgraphics_pipeline.c */


/* static bool createPipelinePBR -> vgraphics_pipeline.c */


/* ================================================================== */
/*  Skin pipeline                                                     */
/* ================================================================== */
/* static bool createBoneBuffer -> vgraphics_pipeline.c */


/* static bool createDescriptorSetLayoutSkin -> vgraphics_pipeline.c */


/* static bool createPipelineSkin -> vgraphics_pipeline.c */


/* ================================================================== */
/*  RT render pass                                                    */
/* ================================================================== */
/* static bool createRenderPassRT -> vgraphics_device.c */


/* ================================================================== */
/*  Texture (staging ??? image)                                         */
/* ================================================================== */
/* Build a VkSampler from glTF-style TextureOptions. Returns VK_NULL_HANDLE on
 * failure so the caller can fall back to the shared sampler. */
static VkSampler createSamplerFromOptions(const TextureOptions *o) {
    static const VkFilter magOf[2]  = { VK_FILTER_NEAREST, VK_FILTER_LINEAR };
    static const VkFilter minOf[6]  = {
        VK_FILTER_NEAREST, VK_FILTER_LINEAR, VK_FILTER_NEAREST,
        VK_FILTER_LINEAR,  VK_FILTER_NEAREST, VK_FILTER_LINEAR
    };
    static const VkSamplerMipmapMode mipOf[6] = {
        VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST,
        VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST,
        VK_SAMPLER_MIPMAP_MODE_LINEAR,  VK_SAMPLER_MIPMAP_MODE_LINEAR
    };
    static const VkSamplerAddressMode addrOf[3] = {
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT
    };

    int minF = o->minFilter, magF = o->magFilter;
    int ws = o->wrapS, wt = o->wrapT;
    if (minF < 0 || minF > 5) minF = 5;
    if (magF < 0 || magF > 1) magF = 1;
    if (ws < 0 || ws > 2) ws = 0;
    if (wt < 0 || wt > 2) wt = 0;

    VkSamplerCreateInfo ci = {0};
    ci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ci.magFilter    = magOf[magF];
    ci.minFilter    = minOf[minF];
    ci.mipmapMode   = mipOf[minF];
    ci.addressModeU = addrOf[ws];
    ci.addressModeV = addrOf[wt];
    ci.addressModeW = addrOf[ws];
    ci.minLod       = 0.0f;
    ci.maxLod       = (o->mipmaps ? 16.0f : 0.0f);
    ci.maxAnisotropy = 1.0f;

    VkSampler s = VK_NULL_HANDLE;
    if (vkCreateSampler(VK.device, &ci, NULL, &s) != VK_SUCCESS) return VK_NULL_HANDLE;
    return s;
}

/* stb_image gives RGBA bytes; the CPU sampler stores packed 0xAARRGGBB. */
static void packCpuPixels(uint32_t *dst, const unsigned char *src, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        dst[i] = ((uint32_t)src[i * 4 + 3] << 24) |
                 ((uint32_t)src[i * 4 + 0] << 16) |
                 ((uint32_t)src[i * 4 + 1] << 8) |
                 (uint32_t)src[i * 4 + 2];
    }
}

static Texture2D loadTextureFromImageOpts(Image image, VkFormat format,
                                          const TextureOptions *opts) {
    Texture2D tex = {0};
    (void)format; (void)opts;
    if (!image.data || image.width <= 0 || image.height <= 0) return tex;
    if (!VK.device || !VK.phys) {
        int idx = allocateTextureSlot();
        if (!idx) return tex;
        size_t n = (size_t)image.width * (size_t)image.height * 4u;
        uint32_t *cpu = (uint32_t *)malloc(n);
        if (!cpu) return tex;
        packCpuPixels(cpu, (const unsigned char *)image.data, n / 4);
        g_textureTable[idx].width = image.width;
        g_textureTable[idx].height = image.height;
        g_textureTable[idx].cpuPixels = cpu;
        g_textureTable[idx].generation = textureGeneration((unsigned int)idx);
        tex.id = (unsigned int)idx;
        tex.generation = g_textureTable[idx].generation;
        tex.width = image.width;
        tex.height = image.height;
        return tex;
    }

    VkDeviceSize size = (VkDeviceSize)image.width * image.height * 4;

    /* mip count */
    uint32_t mipLevels = 1;
    {
        uint32_t mw = (uint32_t)image.width, mh = (uint32_t)image.height;
        while (mw > 1 || mh > 1) {
            mw = mw > 1 ? mw / 2 : 1;
            mh = mh > 1 ? mh / 2 : 1;
            mipLevels++;
        }
    }
    /* A loader may explicitly ask for a single level (glTF sampler without
       mip filtering), which skips the blit chain entirely. */
    if (opts && !opts->mipmaps) mipLevels = 1;

    /* Blit support for this format? */
    VkFormatProperties fprops;
    vkGetPhysicalDeviceFormatProperties(VK.phys, format, &fprops);
    VkFormatFeatureFlags feat = fprops.optimalTilingFeatures;
    bool canBlit = (feat & VK_FORMAT_FEATURE_BLIT_SRC_BIT) &&
                   (feat & VK_FORMAT_FEATURE_BLIT_DST_BIT) &&
                   (feat & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
    if (!canBlit || (image.width <= 1 && image.height <= 1))
        mipLevels = 1;

    VkBuffer staging;
    VkDeviceMemory stagingMem;
    if (!createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &staging, &stagingMem))
        return tex;

    void *mapped = NULL;
    if (vkMapMemory(VK.device, stagingMem, 0, size, 0, &mapped) != VK_SUCCESS) {
        vkDestroyBuffer(VK.device, staging, NULL);
        vkFreeMemory(VK.device, stagingMem, NULL);
        return tex;
    }
    memcpy(mapped, image.data, (size_t)size);
    vkUnmapMemory(VK.device, stagingMem);

    VkImage vkImg;
    VkDeviceMemory imgMem;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (mipLevels > 1)
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    if (!createImageMips((uint32_t)image.width, (uint32_t)image.height, mipLevels, format,
                         VK_IMAGE_TILING_OPTIMAL, usage,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &vkImg, &imgMem)) {
        vkDestroyBuffer(VK.device, staging, NULL);
        vkFreeMemory(VK.device, stagingMem, NULL);
        return tex;
    }
    vlcrNameImage(vkImg, "uploaded_texture");

    /* base level: UNDEFINED -> TRANSFER_DST */
    transitionImageLayoutMips(vkImg, format,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              0, mipLevels);

    VkCommandBufferAllocateInfo cbai = {0};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = VK.cmdPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(VK.device, &cbai, &cb);

    VkCommandBufferBeginInfo cbbi = {0};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &cbbi);

    VkBufferImageCopy region = {0};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = (VkExtent3D){ (uint32_t)image.width, (uint32_t)image.height, 1 };
    vkCmdCopyBufferToImage(cb, staging, vkImg,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    /* Generate mip chain with blit (Sprint A point 6) */
    if (mipLevels > 1) {
        int32_t mipW = image.width;
        int32_t mipH = image.height;
        for (uint32_t i = 1; i < mipLevels; ++i) {
            VkImageMemoryBarrier barrier = {0};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.image = vkImg;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.subresourceRange.levelCount = 1;

            /* previous level: TRANSFER_DST -> TRANSFER_SRC */
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cb,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, NULL, 0, NULL, 1, &barrier);

            int32_t nextW = mipW > 1 ? mipW / 2 : 1;
            int32_t nextH = mipH > 1 ? mipH / 2 : 1;

            VkImageBlit blit = {0};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = i - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = 1;
            blit.srcOffsets[0] = (VkOffset3D){ 0, 0, 0 };
            blit.srcOffsets[1] = (VkOffset3D){ mipW, mipH, 1 };
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = i;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = 1;
            blit.dstOffsets[0] = (VkOffset3D){ 0, 0, 0 };
            blit.dstOffsets[1] = (VkOffset3D){ nextW, nextH, 1 };

            vkCmdBlitImage(cb,
                vkImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                vkImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &blit, VK_FILTER_LINEAR);

            /* previous level: TRANSFER_SRC -> SHADER_READ */
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cb,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, NULL, 0, NULL, 1, &barrier);

            mipW = nextW;
            mipH = nextH;
        }
        /* last mip: TRANSFER_DST -> SHADER_READ */
        VkImageMemoryBarrier last = {0};
        last.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        last.image = vkImg;
        last.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        last.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        last.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        last.subresourceRange.baseMipLevel = mipLevels - 1;
        last.subresourceRange.levelCount = 1;
        last.subresourceRange.baseArrayLayer = 0;
        last.subresourceRange.layerCount = 1;
        last.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        last.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        last.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        last.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cb,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, NULL, 0, NULL, 1, &last);
    }

    vkEndCommandBuffer(cb);
    VkSubmitInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    vkQueueSubmit(VK.gfxQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(VK.gfxQueue);
    vkFreeCommandBuffers(VK.device, VK.cmdPool, 1, &cb);

    if (mipLevels == 1) {
        transitionImageLayoutMips(vkImg, format,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                  0, 1);
    }

    vkDestroyBuffer(VK.device, staging, NULL);
    vkFreeMemory(VK.device, stagingMem, NULL);

    VkImageViewCreateInfo vi = {0};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = vkImg;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = (VkImageSubresourceRange){
        VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1 };
    VkImageView view;
    if (vkCreateImageView(VK.device, &vi, NULL, &view) != VK_SUCCESS) {
        vkDestroyImage(VK.device, vkImg, NULL);
        vkFreeMemory(VK.device, imgMem, NULL);
        return tex;
    }

    int idx = allocateTextureSlot();
    if (!idx) {
        vkDestroyImageView(VK.device, view, NULL);
        vkDestroyImage(VK.device, vkImg, NULL);
        vkFreeMemory(VK.device, imgMem, NULL);
        return tex;
    }

    /* Per-texture sampler when the caller supplied sampling state. glTF's
       default wrap is REPEAT while the library's shared sampler clamps, so
       tiling textures need their own sampler to look right. */
    VkSampler samp = VK.sampler2D;
    int ownSamp = 0;
    if (opts) {
        VkSampler custom = createSamplerFromOptions(opts);
        if (custom != VK_NULL_HANDLE) { samp = custom; ownSamp = 1; }
    }

    g_textureTable[idx] = (TextureEntry){ vkImg, imgMem, view, samp,
                                          image.width, image.height, ownSamp };
    g_textureTable[idx].generation = textureGeneration((unsigned int)idx);
    if (image.data && image.width > 0 && image.height > 0) {
        size_t n = (size_t)image.width * (size_t)image.height * 4u;
        uint32_t *cpu = (uint32_t *)malloc(n);
        if (cpu) {
            packCpuPixels(cpu, (const unsigned char *)image.data, n / 4);
            g_textureTable[idx].cpuPixels = cpu;
        }
    }

    tex.id     = (unsigned int)idx;
    tex.generation = g_textureTable[idx].generation;
    tex.width  = image.width;
    tex.height = image.height;
    tex.format = 0;
    tex.mipmaps = (int)mipLevels;
    return tex;
}

/* Shared implementation for the plain / sRGB entry points. */
static Texture2D loadTextureFromImageFormat(Image image, VkFormat format) {
    return loadTextureFromImageOpts(image, format, NULL);
}

/* Minimal KTX2 reader for the formats emitted by `vlcrc --ktx`: raw BC7/BC5,
 * supercompression scheme 0. Mip levels are accepted but only level 0 is
 * uploaded in this milestone. */
Texture2D LoadTextureKTX2(const char *path) {
    Texture2D tex = {0};
    if (!VK.device || !path || !path[0]) return tex;
    FILE *f = fopen(path, "rb");
    if (!f) return tex;
    unsigned char hdr[80];
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr) ||
        memcmp(hdr, "\xABKTX 20\xBB\r\n\x1A\n", 12) != 0) {
        fclose(f);
        return tex;
    }
    uint32_t vkFormat, w, h, levels, supercomp;
    memcpy(&vkFormat, hdr + 12, 4);
    memcpy(&w, hdr + 20, 4);
    memcpy(&h, hdr + 24, 4);
    memcpy(&levels, hdr + 40, 4);
    memcpy(&supercomp, hdr + 44, 4);
    if (supercomp != 0 || levels == 0 || w == 0 || h == 0) {
        fclose(f);
        return tex;
    }
    if (levels > 16) levels = 16;
    uint64_t offsets[16] = {0}, lengths[16] = {0};
    uint64_t totalLen = 0;
    fseek(f, 80, SEEK_SET);
    for (uint32_t l = 0; l < levels; ++l) {
        uint64_t uncompressed = 0;
        if (fread(&offsets[l], 8, 1, f) != 1 ||
            fread(&lengths[l], 8, 1, f) != 1 ||
            fread(&uncompressed, 8, 1, f) != 1) {
            fclose(f);
            return tex;
        }
        (void)uncompressed;
        if (lengths[l] == 0 || offsets[l] > 64ull * 1024 * 1024) {
            fclose(f);
            return tex;
        }
        totalLen += lengths[l];
    }
    if (totalLen == 0 || totalLen > 128ull * 1024 * 1024) {
        fclose(f);
        return tex;
    }
    unsigned char *data = (unsigned char *)malloc((size_t)totalLen);
    if (!data) { fclose(f); return tex; }
    uint64_t cursor = 0;
    for (uint32_t l = 0; l < levels; ++l) {
        fseek(f, (long)offsets[l], SEEK_SET);
        if (fread(data + cursor, 1, (size_t)lengths[l], f) != (size_t)lengths[l]) {
            free(data); fclose(f); return tex;
        }
        cursor += lengths[l];
    }
    fclose(f);
    uint64_t dataLen = totalLen;

    VkFormat format = (VkFormat)vkFormat;
    if (format != VK_FORMAT_BC7_UNORM_BLOCK && format != VK_FORMAT_BC7_SRGB_BLOCK &&
        format != VK_FORMAT_BC5_UNORM_BLOCK && format != VK_FORMAT_BC5_SNORM_BLOCK) {
        free(data);
        return tex;
    }

    VkBuffer staging;
    VkDeviceMemory stagingMem;
    if (!createBuffer((VkDeviceSize)dataLen, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &staging, &stagingMem)) {
        free(data);
        return tex;
    }
    void *mapped = NULL;
    if (vkMapMemory(VK.device, stagingMem, 0, dataLen, 0, &mapped) != VK_SUCCESS) {
        vkDestroyBuffer(VK.device, staging, NULL);
        vkFreeMemory(VK.device, stagingMem, NULL);
        free(data);
        return tex;
    }
    memcpy(mapped, data, (size_t)dataLen);
    vkUnmapMemory(VK.device, stagingMem);
    free(data);

    VkImage img;
    VkDeviceMemory imgMem;
    if (!createImageMips(w, h, levels, format, VK_IMAGE_TILING_OPTIMAL,
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &img, &imgMem)) {
        vkDestroyBuffer(VK.device, staging, NULL);
        vkFreeMemory(VK.device, stagingMem, NULL);
        return tex;
    }
    transitionImageLayoutMips(img, format, VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, levels);

    VkCommandBufferAllocateInfo cbai = {0};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = VK.cmdPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(VK.device, &cbai, &cb);
    VkCommandBufferBeginInfo cbbi = {0};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &cbbi);
    uint64_t levelOffset = 0;
    for (uint32_t l = 0; l < levels; ++l) {
        VkBufferImageCopy region = {0};
        region.bufferOffset = levelOffset;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = l;
        region.imageSubresource.layerCount = 1;
        region.imageExtent.width = w >> l > 0 ? w >> l : 1;
        region.imageExtent.height = h >> l > 0 ? h >> l : 1;
        region.imageExtent.depth = 1;
        vkCmdCopyBufferToImage(cb, staging, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        levelOffset += lengths[l];
    }
    VkImageMemoryBarrier b = {0};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange=(VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT,0,levels,0,1};
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0,NULL,0,NULL,1,&b);
    vkEndCommandBuffer(cb);
    VkSubmitInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    vkQueueSubmit(VK.gfxQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(VK.gfxQueue);
    vkFreeCommandBuffers(VK.device, VK.cmdPool, 1, &cb);
    vkDestroyBuffer(VK.device, staging, NULL);
    vkFreeMemory(VK.device, stagingMem, NULL);

    VkImageViewCreateInfo vi = {0};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = img;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange=(VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT,0,levels,0,1};
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(VK.device, &vi, NULL, &view) != VK_SUCCESS) {
        vkDestroyImage(VK.device, img, NULL);
        vkFreeMemory(VK.device, imgMem, NULL);
        return tex;
    }
    int idx = allocateTextureSlot();
    if (!idx) {
        vkDestroyImageView(VK.device, view, NULL);
        vkDestroyImage(VK.device, img, NULL);
        vkFreeMemory(VK.device, imgMem, NULL);
        return tex;
    }
    TextureOptions opts = {0};
    opts.srgb = (format == VK_FORMAT_BC7_SRGB_BLOCK) ? 1 : 0;
    opts.wrapS = 0; /* REPEAT: cooked glTF UVs are frequently outside [0,1] */
    opts.wrapT = 0;
    opts.minFilter = 5;
    opts.magFilter = 1;
    opts.mipmaps = 1;
    VkSampler samp = createSamplerFromOptions(&opts);
    int ownSamp = 0;
    if (samp == VK_NULL_HANDLE) samp = VK.sampler2D;
    else ownSamp = 1;
    g_textureTable[idx] = (TextureEntry){ img, imgMem, view, samp, (int)w, (int)h, ownSamp };
    g_textureTable[idx].generation = textureGeneration((unsigned int)idx);
    tex.id = (unsigned int)idx;
    tex.generation = g_textureTable[idx].generation;
    tex.width = (int)w;
    tex.height = (int)h;
    tex.mipmaps = (int)levels;
    return tex;
}

Texture2D LoadTextureFromImage(Image image) {
    return loadTextureFromImageFormat(image, VK_FORMAT_R8G8B8A8_UNORM);
}

Texture2D LoadTextureFromImageSRGB(Image image) {
    return loadTextureFromImageFormat(image, VK_FORMAT_R8G8B8A8_SRGB);
}

Texture2D LoadTextureFromImageEx(Image image, const TextureOptions *options) {
    VkFormat format = (options && options->srgb) ? VK_FORMAT_R8G8B8A8_SRGB
                                                 : VK_FORMAT_R8G8B8A8_UNORM;
    return loadTextureFromImageOpts(image, format, options);
}

void UnloadTexture(Texture2D texture) {
    if (texture.id == 0 || texture.id >= VLCRLIB_MAX_TEXTURES) return;
    TextureEntry *e = &g_textureTable[texture.id];
    if ((!e->view && !e->cpuPixels) || !texture.generation ||
        texture.generation != e->generation) return;
    /* A render target owns its exposed color texture and framebuffer as one
       resource. It must be retired through UnloadRenderTexture(). */
    for (int i = 1; i <= g_rtCount; ++i)
        if (g_rtTable[i].textureId == texture.id &&
            g_rtTable[i].generation != 0)
            return;
    if (VK.inFrame && g_deferredTextureReleaseCount >= VLCRLIB_MAX_TEXTURES)
        return; /* Keep the live resource if the frame's retirement queue is full. */

    /* Descriptors may still name this view from previous submissions. Wait
       for those submissions, detach every material reference, then replace
       their descriptors before destroying the image view. */
    if (VK.device) vkDeviceWaitIdle(VK.device);
    material_detach_texture(texture);
    if (VK.inFrame && g_deferredTextureReleaseCount < VLCRLIB_MAX_TEXTURES) {
        g_deferredTextureReleases[g_deferredTextureReleaseCount++] =
            (DeferredTextureRelease){ *e, VK.currentFrame };
    } else {
        /* Outside a frame the device is idle; on queue overflow, wait for all
           frames before releasing so no submitted command can retain a view. */
        if (VK.inFrame) releaseCompletedTextures(0, true);
        destroyTextureEntry(e);
    }
    *e = (TextureEntry){0};
    invalidateTextureGeneration(texture.id);
}

bool vgraphics_GetCpuTexturePixels(Texture2D texture, const uint32_t **pixels,
                                   int *width, int *height) {
    if (texture.id == 0 || texture.id >= VLCRLIB_MAX_TEXTURES) return false;
    TextureEntry *e = &g_textureTable[texture.id];
    if (!e->cpuPixels || !texture.generation || texture.generation != e->generation)
        return false;
    if (pixels) *pixels = e->cpuPixels;
    if (width) *width = e->width;
    if (height) *height = e->height;
    return true;
}

void UpdateTexture(Texture2D texture, const void *pixels) {
    if (!pixels || texture.id == 0 || texture.id >= VLCRLIB_MAX_TEXTURES ||
        !VK.device || !VK.gfxQueue || !VK.cmdPool)
        return;

    TextureEntry *entry = &g_textureTable[texture.id];
    if (!entry->img || !texture.generation || texture.generation != entry->generation ||
        entry->width <= 0 || entry->height <= 0) return;

    const uint32_t mipLevels = texture.mipmaps > 0 ? (uint32_t)texture.mipmaps : 1u;
    const VkDeviceSize dataSize = (VkDeviceSize)entry->width *
                                  (VkDeviceSize)entry->height * 4u;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    if (!createBuffer(dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &staging, &stagingMem))
        return;

    void *mapped = NULL;
    if (vkMapMemory(VK.device, stagingMem, 0, dataSize, 0, &mapped) != VK_SUCCESS) {
        vkDestroyBuffer(VK.device, staging, NULL);
        vkFreeMemory(VK.device, stagingMem, NULL);
        return;
    }
    memcpy(mapped, pixels, (size_t)dataSize);
    vkUnmapMemory(VK.device, stagingMem);

    /* Texture creation and normal frame submissions use this queue. Wait
       before changing image layout so no pending draw still samples the image. */
    if (vkQueueWaitIdle(VK.gfxQueue) != VK_SUCCESS) {
        vkDestroyBuffer(VK.device, staging, NULL);
        vkFreeMemory(VK.device, stagingMem, NULL);
        return;
    }

    VkCommandBufferAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = VK.cmdPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(VK.device, &allocInfo, &cb) != VK_SUCCESS) {
        vkDestroyBuffer(VK.device, staging, NULL);
        vkFreeMemory(VK.device, stagingMem, NULL);
        return;
    }

    VkCommandBufferBeginInfo beginInfo = {0};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VkResult result = vkBeginCommandBuffer(cb, &beginInfo);
    if (result == VK_SUCCESS) {
        VkImageMemoryBarrier toTransfer = {0};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransfer.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = entry->img;
        toTransfer.subresourceRange = (VkImageSubresourceRange){
            VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1 };
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, NULL, 0, NULL, 1, &toTransfer);

        VkBufferImageCopy region = {0};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = (VkExtent3D){ (uint32_t)entry->width,
                                           (uint32_t)entry->height, 1 };
        vkCmdCopyBufferToImage(cb, staging, entry->img,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        int32_t mipW = entry->width, mipH = entry->height;
        for (uint32_t level = 1; level < mipLevels; ++level) {
            VkImageMemoryBarrier toSource = {0};
            toSource.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toSource.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toSource.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            toSource.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toSource.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            toSource.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toSource.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toSource.image = entry->img;
            toSource.subresourceRange = (VkImageSubresourceRange){
                VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 1, 0, 1 };
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                 0, NULL, 0, NULL, 1, &toSource);

            int32_t nextW = mipW > 1 ? mipW / 2 : 1;
            int32_t nextH = mipH > 1 ? mipH / 2 : 1;
            VkImageBlit blit = {0};
            blit.srcSubresource = (VkImageSubresourceLayers){
                VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1 };
            blit.srcOffsets[1] = (VkOffset3D){ mipW, mipH, 1 };
            blit.dstSubresource = (VkImageSubresourceLayers){
                VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 };
            blit.dstOffsets[1] = (VkOffset3D){ nextW, nextH, 1 };
            vkCmdBlitImage(cb, entry->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           entry->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &blit, VK_FILTER_LINEAR);

            VkImageMemoryBarrier toShader = {0};
            toShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toShader.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toShader.image = entry->img;
            toShader.subresourceRange = (VkImageSubresourceRange){
                VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 1, 0, 1 };
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                 0, NULL, 0, NULL, 1, &toShader);
            mipW = nextW;
            mipH = nextH;
        }

        VkImageMemoryBarrier lastToShader = {0};
        lastToShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        lastToShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        lastToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        lastToShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        lastToShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        lastToShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        lastToShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        lastToShader.image = entry->img;
        lastToShader.subresourceRange = (VkImageSubresourceRange){
            VK_IMAGE_ASPECT_COLOR_BIT, mipLevels - 1, 1, 0, 1 };
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                             0, NULL, 0, NULL, 1, &lastToShader);
        result = vkEndCommandBuffer(cb);
    }

    if (result == VK_SUCCESS) {
        VkSubmitInfo submit = {0};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cb;
        result = vkQueueSubmit(VK.gfxQueue, 1, &submit, VK_NULL_HANDLE);
        if (result == VK_SUCCESS) result = vkQueueWaitIdle(VK.gfxQueue);
    }
    vkFreeCommandBuffers(VK.device, VK.cmdPool, 1, &cb);
    vkDestroyBuffer(VK.device, staging, NULL);
    vkFreeMemory(VK.device, stagingMem, NULL);
    if (result != VK_SUCCESS)
        fprintf(stderr, "[vlcrlib] UpdateTexture failed (VkResult %d)\n", (int)result);
}

static bool createWhiteTexture(void) {
    unsigned char white[4] = {255, 255, 255, 255};
    Image img = { .data = white, .width = 1, .height = 1, .mipmaps = 1, .format = 0 };
    VK.whiteTexture = LoadTextureFromImage(img);
    return VK.whiteTexture.id != 0;
}

/* ================================================================== */
/*  Mesh / OBJ / Generators                                           */
/* ================================================================== */
static bool uploadMeshBuffers(VkBuffer *vbo, VkDeviceMemory *vboMem,
                              VkBuffer *ibo, VkDeviceMemory *iboMem,
                              const void *verts, int vcount, size_t vstride,
                              const uint32_t *indices, int icount) {
    VkDeviceSize vsize = (VkDeviceSize)vcount * vstride;
    VkDeviceSize isize = (VkDeviceSize)icount * sizeof(uint32_t);

    VkBuffer vstg; VkDeviceMemory vstgMem;
    VkBuffer istg; VkDeviceMemory istgMem;

    if (!createBuffer(vsize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &vstg, &vstgMem)) return false;
    if (!createBuffer(isize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &istg, &istgMem)) return false;

    void *m = NULL;
    if (vkMapMemory(VK.device, vstgMem, 0, vsize, 0, &m) != VK_SUCCESS) {
        vkDestroyBuffer(VK.device, vstg, NULL); vkFreeMemory(VK.device, vstgMem, NULL);
        vkDestroyBuffer(VK.device, istg, NULL); vkFreeMemory(VK.device, istgMem, NULL);
        return false;
    }
    memcpy(m, verts, vsize); vkUnmapMemory(VK.device, vstgMem);
    if (vkMapMemory(VK.device, istgMem, 0, isize, 0, &m) != VK_SUCCESS) {
        vkDestroyBuffer(VK.device, vstg, NULL); vkFreeMemory(VK.device, vstgMem, NULL);
        vkDestroyBuffer(VK.device, istg, NULL); vkFreeMemory(VK.device, istgMem, NULL);
        return false;
    }
    memcpy(m, indices, isize); vkUnmapMemory(VK.device, istgMem);

    if (!createBuffer(vsize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vbo, vboMem)) return false;
    if (!createBuffer(isize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ibo, iboMem)) return false;

    copyBuffer(vstg, *vbo, vsize);
    copyBuffer(istg, *ibo, isize);

    vkDestroyBuffer(VK.device, vstg, NULL); vkFreeMemory(VK.device, vstgMem, NULL);
    vkDestroyBuffer(VK.device, istg, NULL); vkFreeMemory(VK.device, istgMem, NULL);
    return true;
}

static Mesh registerMesh(const Vertex3D *verts, int vcount,
                         const uint32_t *indices, int icount) {
    Mesh m = {0};
    if (!verts || !indices || vcount <= 0 || icount <= 0) return m;
    int idx = allocateMeshSlot();
    if (!idx) return m;
    MeshEntry *e = &g_meshTable[idx];
    if (!VK.device) {
        e->cpuVertices = (Vertex3D *)malloc(sizeof(Vertex3D) * (size_t)vcount);
        e->cpuIndices = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)icount);
        if (!e->cpuVertices || !e->cpuIndices) {
            destroyMeshEntry(e);
            return m;
        }
        memcpy(e->cpuVertices, verts, sizeof(Vertex3D) * (size_t)vcount);
        memcpy(e->cpuIndices, indices, sizeof(uint32_t) * (size_t)icount);
        e->cpuVertexCount = vcount;
        e->vertexCount = vcount;
        e->indexCount = icount;
        e->boundsMin = (Vector3){verts[0].x, verts[0].y, verts[0].z};
        e->boundsMax = e->boundsMin;
        for (int i = 1; i < vcount; ++i) {
            if (verts[i].x < e->boundsMin.x) e->boundsMin.x = verts[i].x;
            if (verts[i].y < e->boundsMin.y) e->boundsMin.y = verts[i].y;
            if (verts[i].z < e->boundsMin.z) e->boundsMin.z = verts[i].z;
            if (verts[i].x > e->boundsMax.x) e->boundsMax.x = verts[i].x;
            if (verts[i].y > e->boundsMax.y) e->boundsMax.y = verts[i].y;
            if (verts[i].z > e->boundsMax.z) e->boundsMax.z = verts[i].z;
        }
        e->hasBounds = 1;
        e->generation = meshGeneration((unsigned int)idx);
        m.id = (unsigned int)idx;
        m.generation = e->generation;
        m.vertexCount = vcount;
        m.indexCount = icount;
        return m;
    }
    if (!uploadMeshBuffers(&e->vbo, &e->vboMem, &e->ibo, &e->iboMem,
                           verts, vcount, sizeof(Vertex3D), indices, icount)) {
        *e = (MeshEntry){0};
        return m;
    }
    e->vertexCount = vcount;
    e->indexCount  = icount;
    e->isSkinned   = 0;
    if (vcount > 0) {
        e->boundsMin = (Vector3){verts[0].x, verts[0].y, verts[0].z};
        e->boundsMax = e->boundsMin;
        for (int i = 1; i < vcount; ++i) {
            if (verts[i].x < e->boundsMin.x) e->boundsMin.x = verts[i].x;
            if (verts[i].y < e->boundsMin.y) e->boundsMin.y = verts[i].y;
            if (verts[i].z < e->boundsMin.z) e->boundsMin.z = verts[i].z;
            if (verts[i].x > e->boundsMax.x) e->boundsMax.x = verts[i].x;
            if (verts[i].y > e->boundsMax.y) e->boundsMax.y = verts[i].y;
            if (verts[i].z > e->boundsMax.z) e->boundsMax.z = verts[i].z;
        }
        e->hasBounds = 1;
    }
    e->cpuVertices = (Vertex3D *)malloc(sizeof(Vertex3D) * (size_t)vcount);
    e->cpuIndices = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)icount);
    if (e->cpuVertices && e->cpuIndices) {
        memcpy(e->cpuVertices, verts, sizeof(Vertex3D) * (size_t)vcount);
        memcpy(e->cpuIndices, indices, sizeof(uint32_t) * (size_t)icount);
        e->cpuVertexCount = vcount;
    } else {
        free(e->cpuVertices); e->cpuVertices = NULL;
        free(e->cpuIndices); e->cpuIndices = NULL;
    }
    e->generation = meshGeneration((unsigned int)idx);
    m.id           = (unsigned int)idx;
    m.generation   = e->generation;
    m.vertexCount  = vcount;
    m.indexCount   = icount;
    return m;
}

Mesh GenMeshCube(float w, float h, float d) {
    float hx = w * 0.5f, hy = h * 0.5f, hz = d * 0.5f;
    Vertex3D v[24] = {
        {-hx,-hy, hz, 0,0,1, 0,1}, { hx,-hy, hz, 0,0,1, 1,1},
        { hx, hy, hz, 0,0,1, 1,0}, {-hx, hy, hz, 0,0,1, 0,0},
        { hx,-hy,-hz, 0,0,-1, 0,1}, {-hx,-hy,-hz, 0,0,-1, 1,1},
        {-hx, hy,-hz, 0,0,-1, 1,0}, { hx, hy,-hz, 0,0,-1, 0,0},
        {-hx,-hy,-hz, -1,0,0, 0,1}, {-hx,-hy, hz, -1,0,0, 1,1},
        {-hx, hy, hz, -1,0,0, 1,0}, {-hx, hy,-hz, -1,0,0, 0,0},
        { hx,-hy, hz, 1,0,0, 0,1}, { hx,-hy,-hz, 1,0,0, 1,1},
        { hx, hy,-hz, 1,0,0, 1,0}, { hx, hy, hz, 1,0,0, 0,0},
        {-hx, hy, hz, 0,1,0, 0,1}, { hx, hy, hz, 0,1,0, 1,1},
        { hx, hy,-hz, 0,1,0, 1,0}, {-hx, hy,-hz, 0,1,0, 0,0},
        {-hx,-hy,-hz, 0,-1,0, 0,1}, { hx,-hy,-hz, 0,-1,0, 1,1},
        { hx,-hy, hz, 0,-1,0, 1,0}, {-hx,-hy, hz, 0,-1,0, 0,0},
    };
    uint32_t idx[36] = {
         0,1,2,  0,2,3,  4,5,6,  4,6,7,
         8,9,10, 8,10,11, 12,13,14, 12,14,15,
        16,17,18,16,18,19, 20,21,22, 20,22,23
    };
    return registerMesh(v, 24, idx, 36);
}

Mesh GenMeshSphere(float radius, int rings, int slices) {
    if (rings < 3)  rings = 3;
    if (slices < 3) slices = 3;
    int vcount = (rings + 1) * (slices + 1);
    Vertex3D *verts = calloc(vcount, sizeof(Vertex3D));

    for (int r = 0; r <= rings; ++r) {
        float phi = (float)r / rings * VLCRLIB_PI;
        float sp = sinf(phi), cp = cosf(phi);
        for (int s = 0; s <= slices; ++s) {
            float theta = (float)s / slices * 2.0f * VLCRLIB_PI;
            float st = sinf(theta), ct = cosf(theta);
            Vertex3D *v = &verts[r * (slices + 1) + s];
            v->x  = radius * sp * ct;
            v->y  = radius * cp;
            v->z  = radius * sp * st;
            v->nx = sp * ct; v->ny = cp; v->nz = sp * st;
            v->u  = (float)s / slices;
            v->v  = (float)r / rings;
        }
    }
    int icount = rings * slices * 6;
    uint32_t *idx = malloc(icount * sizeof(uint32_t));
    int k = 0;
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < slices; ++s) {
            uint32_t a = r * (slices + 1) + s;
            uint32_t b = a + slices + 1;
            idx[k++] = a;     idx[k++] = b;     idx[k++] = a + 1;
            idx[k++] = a + 1; idx[k++] = b;     idx[k++] = b + 1;
        }
    Mesh m = registerMesh(verts, vcount, idx, icount);
    free(verts); free(idx);
    return m;
}

Mesh GenMeshPlane(float w, float d, int resX, int resZ) {
    if (resX < 1) resX = 1;
    if (resZ < 1) resZ = 1;
    int vc = (resX + 1) * (resZ + 1);
    int ic = resX * resZ * 6;
    Vertex3D *v = malloc(vc * sizeof(Vertex3D));
    uint32_t *idx = malloc(ic * sizeof(uint32_t));
    int k = 0;
    for (int z = 0; z <= resZ; ++z)
        for (int x = 0; x <= resX; ++x) {
            float fx = (float)x / resX;
            float fz = (float)z / resZ;
            v[k++] = (Vertex3D){ (fx - 0.5f) * w, 0.0f, (fz - 0.5f) * d,
                                 0, 1, 0, fx, fz };
        }
    k = 0;
    for (int z = 0; z < resZ; ++z)
        for (int x = 0; x < resX; ++x) {
            uint32_t a = z * (resX + 1) + x;
            uint32_t b = a + resX + 1;
            idx[k++] = a;     idx[k++] = b;     idx[k++] = a + 1;
            idx[k++] = a + 1; idx[k++] = b;     idx[k++] = b + 1;
        }
    Mesh m = registerMesh(v, vc, idx, ic);
    free(v); free(idx);
    return m;
}

Mesh GenMeshCylinder(float radius, float height, int slices) {
    if (slices < 3) slices = 3;
    int vc = (slices + 1) * 2 + 2 + (slices + 1) * 2;
    int ic = slices * 6 + slices * 3 + slices * 3;
    Vertex3D *v = malloc(vc * sizeof(Vertex3D));
    uint32_t *idx = malloc(ic * sizeof(uint32_t));
    int vi = 0, ii = 0;
    float hy = height * 0.5f;

    for (int s = 0; s <= slices; ++s) {
        float a = (float)s / slices * 2.0f * VLCRLIB_PI;
        float x = cosf(a) * radius, z = sinf(a) * radius;
        v[vi++] = (Vertex3D){ x, -hy, z, cosf(a), 0, sinf(a), (float)s/slices, 1 };
        v[vi++] = (Vertex3D){ x,  hy, z, cosf(a), 0, sinf(a), (float)s/slices, 0 };
    }
    for (int s = 0; s < slices; ++s) {
        uint32_t a = s*2, b = a+1, c = a+2, d = a+3;
        idx[ii++] = a; idx[ii++] = b; idx[ii++] = c;
        idx[ii++] = c; idx[ii++] = b; idx[ii++] = d;
    }
    uint32_t topCenter = (uint32_t)vi;
    v[vi++] = (Vertex3D){ 0, hy, 0, 0, 1, 0, 0.5f, 0.5f };
    for (int s = 0; s <= slices; ++s) {
        float a = (float)s / slices * 2.0f * VLCRLIB_PI;
        v[vi++] = (Vertex3D){ cosf(a)*radius, hy, sinf(a)*radius,
                              0, 1, 0, cosf(a)*0.5f+0.5f, sinf(a)*0.5f+0.5f };
    }
    for (int s = 0; s < slices; ++s) {
        idx[ii++] = topCenter; idx[ii++] = topCenter + 1 + s; idx[ii++] = topCenter + 2 + s;
    }
    uint32_t botCenter = (uint32_t)vi;
    v[vi++] = (Vertex3D){ 0, -hy, 0, 0, -1, 0, 0.5f, 0.5f };
    for (int s = 0; s <= slices; ++s) {
        float a = (float)s / slices * 2.0f * VLCRLIB_PI;
        v[vi++] = (Vertex3D){ cosf(a)*radius, -hy, sinf(a)*radius,
                              0, -1, 0, cosf(a)*0.5f+0.5f, sinf(a)*0.5f+0.5f };
    }
    for (int s = 0; s < slices; ++s) {
        idx[ii++] = botCenter; idx[ii++] = botCenter + 2 + s; idx[ii++] = botCenter + 1 + s;
    }
    Mesh m = registerMesh(v, vi, idx, ii);
    free(v); free(idx);
    return m;
}

Mesh GenMeshTorus(float r1, float r2, int ringSeg, int tubeSeg) {
    if (ringSeg < 3) ringSeg = 3;
    if (tubeSeg < 3) tubeSeg = 3;
    int vc = (ringSeg + 1) * (tubeSeg + 1);
    int ic = ringSeg * tubeSeg * 6;
    Vertex3D *v = malloc(vc * sizeof(Vertex3D));
    uint32_t *idx = malloc(ic * sizeof(uint32_t));
    int vi = 0, ii = 0;

    for (int r = 0; r <= ringSeg; ++r) {
        float phi = (float)r / ringSeg * 2.0f * VLCRLIB_PI;
        float cp = cosf(phi), sp = sinf(phi);
        for (int t = 0; t <= tubeSeg; ++t) {
            float theta = (float)t / tubeSeg * 2.0f * VLCRLIB_PI;
            float ct = cosf(theta), st = sinf(theta);
            v[vi++] = (Vertex3D){
                (r1 + r2 * ct) * cp, r2 * st, (r1 + r2 * ct) * sp,
                ct * cp, st, ct * sp,
                (float)t/tubeSeg, (float)r/ringSeg
            };
        }
    }
    for (int r = 0; r < ringSeg; ++r)
        for (int t = 0; t < tubeSeg; ++t) {
            uint32_t a = r * (tubeSeg + 1) + t;
            uint32_t b = a + tubeSeg + 1;
            idx[ii++] = a;     idx[ii++] = b;     idx[ii++] = a + 1;
            idx[ii++] = a + 1; idx[ii++] = b;     idx[ii++] = b + 1;
        }
    Mesh m = registerMesh(v, vi, idx, ii);
    free(v); free(idx);
    return m;
}

void UnloadMesh(Mesh mesh) {
    if (!meshHandleValid(mesh)) return;
    if (VK.inFrame && g_deferredMeshReleaseCount >= VLCRLIB_MAX_MESHES)
        return;
    MeshEntry *e = &g_meshTable[mesh.id];
    if (VK.device) vkDeviceWaitIdle(VK.device);
    if (VK.inFrame && g_deferredMeshReleaseCount < VLCRLIB_MAX_MESHES) {
        g_deferredMeshReleases[g_deferredMeshReleaseCount++] =
            (DeferredMeshRelease){ *e, VK.currentFrame };
    } else {
        if (VK.inFrame) releaseCompletedMeshes(0, true);
        destroyMeshEntry(e);
    }
    *e = (MeshEntry){0};
    invalidateMeshGeneration(mesh.id);
}

/* ================================================================== */
/*  OBJ loader                                                        */
/* ================================================================== */
static Mesh loadMeshOBJ(const char *path) {
    Mesh empty = {0};
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "[vlcrlib] OBJ open failed: %s\n", path); return empty; }

    float *positions = NULL; size_t posCount = 0, posCap = 0;
    float *uvs       = NULL; size_t uvCount  = 0, uvCap  = 0;
    float *normals   = NULL; size_t normCount= 0, normCap= 0;
    Vertex3D *verts  = NULL; size_t vCount   = 0, vCap   = 0;
    uint32_t *indices= NULL; size_t iCount   = 0, iCap   = 0;

    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'v' && line[1] == ' ') {
            float x, y, z;
            if (sscanf(line + 2, "%f %f %f", &x, &y, &z) == 3) {
                if (posCount + 3 > posCap) { posCap = posCap ? posCap*2 : 512; positions = realloc(positions, posCap*sizeof(float)); }
                positions[posCount++] = x; positions[posCount++] = y; positions[posCount++] = z;
            }
        } else if (line[0] == 'v' && line[1] == 't') {
            float u, v;
            if (sscanf(line + 3, "%f %f", &u, &v) == 2) {
                if (uvCount + 2 > uvCap) { uvCap = uvCap ? uvCap*2 : 512; uvs = realloc(uvs, uvCap*sizeof(float)); }
                uvs[uvCount++] = u; uvs[uvCount++] = v;
            }
        } else if (line[0] == 'v' && line[1] == 'n') {
            float x, y, z;
            if (sscanf(line + 3, "%f %f %f", &x, &y, &z) == 3) {
                if (normCount + 3 > normCap) { normCap = normCap ? normCap*2 : 512; normals = realloc(normals, normCap*sizeof(float)); }
                normals[normCount++] = x; normals[normCount++] = y; normals[normCount++] = z;
            }
        } else if (line[0] == 'f' && line[1] == ' ') {
            int face[32][3]; int fc = 0;
            char *p = line + 2;
            while (*p && fc < 32) {
                while (*p == ' ' || *p == '\t') p++;
                if (!*p || *p == '\n' || *p == '\r') break;
                int pi = 0, ti = 0, ni = 0, field = 0;
                while (*p && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') {
                    if (*p == '/') { field++; p++; continue; }
                    int sign = 1;
                    if (*p == '-') { sign = -1; p++; }
                    int val = 0;
                    while (*p >= '0' && *p <= '9') { val = val*10 + (*p - '0'); p++; }
                    val *= sign;
                    if (field == 0) pi = val;
                    else if (field == 1) ti = val;
                    else if (field == 2) ni = val;
                    if (*p != '/') break;
                }
                face[fc][0] = pi; face[fc][1] = ti; face[fc][2] = ni; fc++;
            }
            for (int t = 1; t < fc - 1; t++) {
                int tri[3][3] = {
                    { face[0][0], face[0][1], face[0][2] },
                    { face[t][0], face[t][1], face[t][2] },
                    { face[t+1][0], face[t+1][1], face[t+1][2] },
                };
                for (int k = 0; k < 3; ++k) {
                    Vertex3D vv = {0};
                    int pi = tri[k][0], ti = tri[k][1], ni = tri[k][2];
                    long long posIndex = pi > 0 ? (long long)pi - 1 : (long long)(posCount / 3) + pi;
                    long long uvIndex = ti > 0 ? (long long)ti - 1 : (long long)(uvCount / 2) + ti;
                    long long normIndex = ni > 0 ? (long long)ni - 1 : (long long)(normCount / 3) + ni;
                    if (posIndex >= 0 && (size_t)posIndex < posCount / 3) {
                        vv.x = positions[(size_t)posIndex*3+0];
                        vv.y = positions[(size_t)posIndex*3+1];
                        vv.z = positions[(size_t)posIndex*3+2];
                    }
                    if (uvIndex >= 0 && (size_t)uvIndex < uvCount / 2) {
                        vv.u = uvs[(size_t)uvIndex*2+0];
                        vv.v = 1.0f - uvs[(size_t)uvIndex*2+1];
                    }
                    if (normIndex >= 0 && (size_t)normIndex < normCount / 3) {
                        vv.nx = normals[(size_t)normIndex*3+0];
                        vv.ny = normals[(size_t)normIndex*3+1];
                        vv.nz = normals[(size_t)normIndex*3+2];
                    }
                    if (vv.nx == 0 && vv.ny == 0 && vv.nz == 0) vv.ny = 1.0f;

                    int found = -1;
                    for (size_t j = 0; j < vCount; ++j)
                        if (verts[j].x == vv.x && verts[j].y == vv.y && verts[j].z == vv.z &&
                            verts[j].u == vv.u && verts[j].v == vv.v &&
                            verts[j].nx == vv.nx && verts[j].ny == vv.ny && verts[j].nz == vv.nz)
                            { found = (int)j; break; }

                    if (found < 0) {
                        if (vCount + 1 > vCap) { vCap = vCap ? vCap*2 : 1024; verts = realloc(verts, vCap*sizeof(Vertex3D)); }
                        verts[vCount] = vv; found = (int)vCount++;
                    }
                    if (iCount + 1 > iCap) { iCap = iCap ? iCap*2 : 2048; indices = realloc(indices, iCap*sizeof(uint32_t)); }
                    indices[iCount++] = (uint32_t)found;
                }
            }
        }
    }
    fclose(f);

    Mesh m = {0};
    if (vCount > 0 && iCount > 0) m = registerMesh(verts, (int)vCount, indices, (int)iCount);
    free(positions); free(uvs); free(normals);
    free(verts); free(indices);
    return m;
}

/* ================================================================== */
/*  Material                                                          */
/* ================================================================== */
void vlcr_matrixToGpu(float out[16], Matrix m) {
    out[0] = m.m0;  out[1] = m.m1;  out[2] = m.m2;  out[3] = m.m3;
    out[4] = m.m4;  out[5] = m.m5;  out[6] = m.m6;  out[7] = m.m7;
    out[8] = m.m8;  out[9] = m.m9;  out[10] = m.m10; out[11] = m.m11;
    out[12] = m.m12; out[13] = m.m13; out[14] = m.m14; out[15] = m.m15;
}

static void writeUBO3D(Matrix mvp, Matrix model, Color color) {
    if (VK.ubo3DSlot >= VK.ubo3DMaxSlots) return;
    uint32_t offset = VK.ubo3DSlot * VK.ubo3DStride;
    UBO3D *u = (UBO3D *)((char *)VK.ubo3DMapped + offset);

    vlcr_matrixToGpu(u->mvp, mvp);
    vlcr_matrixToGpu(u->model, model);
    vlcr_matrixToGpu(u->normalMat, MatrixNormalFromModel(model));   /* ????????: ???? ???????? */

    u->lightDir[0] = -0.5f; u->lightDir[1] = -1.0f; u->lightDir[2] = -0.3f; u->lightDir[3] = 0;
    u->lightColor[0] = 1; u->lightColor[1] = 1; u->lightColor[2] = 1; u->lightColor[3] = 1;
    u->baseColor[0] = color.r / 255.0f;
    u->baseColor[1] = color.g / 255.0f;
    u->baseColor[2] = color.b / 255.0f;
    u->baseColor[3] = color.a / 255.0f;
    u->_pad[0] = u->_pad[1] = u->_pad[2] = u->_pad[3] = 0;
    VK.ubo3DSlot++;
}

/* mvp must be proj*view*model (model applied once in VS as gl_Position = mvp * localPos).
 * model is still uploaded for worldPos / lighting. */
/* writeUBOPBR -> vgraphics_material.c */



/* ================================================================== */
/*  IBL GPU upload (cubemap + BRDF LUT ??? VkImage)                     */
/* ================================================================== */

static void ibl_destroy_gpu(void) {
    if (VK.iblIrrView) { vkDestroyImageView(VK.device, VK.iblIrrView, NULL); VK.iblIrrView = VK_NULL_HANDLE; }
    if (VK.iblIrrImage) { vkDestroyImage(VK.device, VK.iblIrrImage, NULL); VK.iblIrrImage = VK_NULL_HANDLE; }
    if (VK.iblIrrMem) { vkFreeMemory(VK.device, VK.iblIrrMem, NULL); VK.iblIrrMem = VK_NULL_HANDLE; }
    if (VK.iblPreView) { vkDestroyImageView(VK.device, VK.iblPreView, NULL); VK.iblPreView = VK_NULL_HANDLE; }
    if (VK.iblPreImage) { vkDestroyImage(VK.device, VK.iblPreImage, NULL); VK.iblPreImage = VK_NULL_HANDLE; }
    if (VK.iblPreMem) { vkFreeMemory(VK.device, VK.iblPreMem, NULL); VK.iblPreMem = VK_NULL_HANDLE; }
    if (VK.iblBrdfView) { vkDestroyImageView(VK.device, VK.iblBrdfView, NULL); VK.iblBrdfView = VK_NULL_HANDLE; }
    if (VK.iblBrdfImage) { vkDestroyImage(VK.device, VK.iblBrdfImage, NULL); VK.iblBrdfImage = VK_NULL_HANDLE; }
    if (VK.iblBrdfMem) { vkFreeMemory(VK.device, VK.iblBrdfMem, NULL); VK.iblBrdfMem = VK_NULL_HANDLE; }
    if (VK.iblEnvView) { vkDestroyImageView(VK.device, VK.iblEnvView, NULL); VK.iblEnvView = VK_NULL_HANDLE; }
    if (VK.iblEnvImage) { vkDestroyImage(VK.device, VK.iblEnvImage, NULL); VK.iblEnvImage = VK_NULL_HANDLE; }
    if (VK.iblEnvMem) { vkFreeMemory(VK.device, VK.iblEnvMem, NULL); VK.iblEnvMem = VK_NULL_HANDLE; }
    if (VK.iblCubeSampler) { vkDestroySampler(VK.device, VK.iblCubeSampler, NULL); VK.iblCubeSampler = VK_NULL_HANDLE; }
    if (VK.iblLutSampler) { vkDestroySampler(VK.device, VK.iblLutSampler, NULL); VK.iblLutSampler = VK_NULL_HANDLE; }
}

static VkCommandBuffer ibl_begin_cmd(void) {
    VkCommandBufferAllocateInfo ai = {0};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = VK.cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(VK.device, &ai, &cb);
    VkCommandBufferBeginInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);
    return cb;
}

static void ibl_end_cmd(VkCommandBuffer cb) {
    vkEndCommandBuffer(cb);
    VkSubmitInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    vkQueueSubmit(VK.gfxQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(VK.gfxQueue);
    vkFreeCommandBuffers(VK.device, VK.cmdPool, 1, &cb);
}

static void ibl_transition(VkImage img, uint32_t mips, uint32_t layers,
                           VkImageLayout oldL, VkImageLayout newL) {
    VkCommandBuffer cb = ibl_begin_cmd();
    VkImageMemoryBarrier b = {0};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = oldL;
    b.newLayout = newL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel = 0;
    b.subresourceRange.levelCount = mips;
    b.subresourceRange.baseArrayLayer = 0;
    b.subresourceRange.layerCount = layers;
    VkPipelineStageFlags src = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags dst = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    if (oldL == VK_IMAGE_LAYOUT_UNDEFINED && newL == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldL == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newL == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    vkCmdPipelineBarrier(cb, src, dst, 0, 0, NULL, 0, NULL, 1, &b);
    ibl_end_cmd(cb);
}

static bool ibl_create_cube(uint32_t size, uint32_t mips,
                            VkImage *outImg, VkDeviceMemory *outMem, VkImageView *outView) {
    if (mips < 1) mips = 1;
    VkImageCreateInfo ci = {0};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.extent = (VkExtent3D){ size, size, 1 };
    ci.mipLevels = mips;
    ci.arrayLayers = 6;
    ci.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(VK.device, &ci, NULL, outImg) != VK_SUCCESS) return false;

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(VK.device, *outImg, &mr);
    VkMemoryAllocateInfo ai = {0};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = vlcr_findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(VK.device, &ai, NULL, outMem) != VK_SUCCESS) return false;
    vkBindImageMemory(VK.device, *outImg, *outMem, 0);

    VkImageViewCreateInfo vi = {0};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = *outImg;
    vi.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    vi.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = mips;
    vi.subresourceRange.layerCount = 6;
    if (vkCreateImageView(VK.device, &vi, NULL, outView) != VK_SUCCESS) return false;
    return true;
}

/* Upload VIBL_Cube (float RGB faces) into an existing cube image.
 *
 * All mips/faces go through one staging buffer and one command buffer:
 * the old implementation submitted and waited 6*mips times, which was the
 * main reason IBL loading stalled the GPU for seconds. */
static bool ibl_upload_cube(const VIBL_Cube *cube, VkImage img) {
    if (!cube || !cube->faces || cube->size < 1) return false;
    int mips = cube->mips > 0 ? cube->mips : 1;

    size_t total = 0;
    int regionsCount = 0;
    for (int m = 0; m < mips; ++m) {
        int s = cube->size >> m;
        if (s < 1) s = 1;
        total += (size_t)s * (size_t)s * 6 * 4 * sizeof(float);
        regionsCount += 6;
    }
    if (total == 0 || regionsCount == 0) return false;

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    if (!createBuffer(total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &staging, &stagingMem))
        return false;

    void *mapped = NULL;
    if (vkMapMemory(VK.device, stagingMem, 0, total, 0, &mapped) != VK_SUCCESS) {
        vkDestroyBuffer(VK.device, staging, NULL);
        vkFreeMemory(VK.device, stagingMem, NULL);
        return false;
    }

    VkBufferImageCopy *regions = (VkBufferImageCopy *)calloc((size_t)regionsCount, sizeof(VkBufferImageCopy));
    if (!regions) {
        vkUnmapMemory(VK.device, stagingMem);
        vkDestroyBuffer(VK.device, staging, NULL);
        vkFreeMemory(VK.device, stagingMem, NULL);
        return false;
    }

    VkDeviceSize offset = 0;
    int ri = 0;
    for (int m = 0; m < mips; ++m) {
        int s = cube->size >> m;
        if (s < 1) s = 1;
        for (int face = 0; face < 6; ++face) {
            const float *rgb = cube->faces[m * 6 + face];
            size_t npix = (size_t)s * (size_t)s;
            size_t bytes = npix * 4 * sizeof(float);
            float *dst = (float *)((char *)mapped + offset);

            if (rgb) {
                for (size_t i = 0; i < npix; ++i) {
                    dst[i * 4 + 0] = rgb[i * 3 + 0];
                    dst[i * 4 + 1] = rgb[i * 3 + 1];
                    dst[i * 4 + 2] = rgb[i * 3 + 2];
                    dst[i * 4 + 3] = 1.0f;
                }
            } else {
                memset(dst, 0, bytes);
                for (size_t i = 0; i < npix; ++i) dst[i * 4 + 3] = 1.0f;
            }

            regions[ri].bufferOffset = offset;
            regions[ri].bufferRowLength = 0;
            regions[ri].bufferImageHeight = 0;
            regions[ri].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            regions[ri].imageSubresource.mipLevel = (uint32_t)m;
            regions[ri].imageSubresource.baseArrayLayer = (uint32_t)face;
            regions[ri].imageSubresource.layerCount = 1;
            regions[ri].imageExtent = (VkExtent3D){ (uint32_t)s, (uint32_t)s, 1 };

            offset += bytes;
            ri++;
        }
    }
    vkUnmapMemory(VK.device, stagingMem);

    VkCommandBuffer cb = ibl_begin_cmd();

    VkImageMemoryBarrier b = {0};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange = (VkImageSubresourceRange){
        VK_IMAGE_ASPECT_COLOR_BIT, 0, (uint32_t)mips, 0, 6 };
    b.srcAccessMask = 0;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 1, &b);

    vkCmdCopyBufferToImage(cb, staging, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           (uint32_t)regionsCount, regions);

    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 1, &b);

    ibl_end_cmd(cb);

    free(regions);
    vkDestroyBuffer(VK.device, staging, NULL);
    vkFreeMemory(VK.device, stagingMem, NULL);
    return true;
}

static bool ibl_upload_brdf_lut(const VIBL_LUT *lut) {
    if (!lut || !lut->data || lut->size < 1) return false;
    int s = lut->size;
    /* expand RG float ??? RGBA float */
    size_t npix = (size_t)s * (size_t)s;
    size_t bytes = npix * 4 * sizeof(float);
    float *rgba = (float *)malloc(bytes);
    if (!rgba) return false;
    for (size_t i = 0; i < npix; ++i) {
        rgba[i * 4 + 0] = lut->data[i * 2 + 0];
        rgba[i * 4 + 1] = lut->data[i * 2 + 1];
        rgba[i * 4 + 2] = 0.f;
        rgba[i * 4 + 3] = 1.f;
    }

    if (!createImage((uint32_t)s, (uint32_t)s, VK_FORMAT_R32G32B32A32_SFLOAT,
                     VK_IMAGE_TILING_OPTIMAL,
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     &VK.iblBrdfImage, &VK.iblBrdfMem)) {
        free(rgba);
        return false;
    }

    VkBuffer staging; VkDeviceMemory stagingMem;
    if (!createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &staging, &stagingMem)) {
        free(rgba);
        return false;
    }
    void *mapped = NULL;
    if (vkMapMemory(VK.device, stagingMem, 0, bytes, 0, &mapped) != VK_SUCCESS) {
        vkDestroyBuffer(VK.device, staging, NULL);
        vkFreeMemory(VK.device, stagingMem, NULL);
        free(rgba);
        return false;
    }
    memcpy(mapped, rgba, bytes);
    vkUnmapMemory(VK.device, stagingMem);
    free(rgba);

    ibl_transition(VK.iblBrdfImage, 1, 1,
                   VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkCommandBuffer cb = ibl_begin_cmd();
    VkBufferImageCopy region = {0};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = (VkExtent3D){ (uint32_t)s, (uint32_t)s, 1 };
    vkCmdCopyBufferToImage(cb, staging, VK.iblBrdfImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    ibl_end_cmd(cb);
    vkDestroyBuffer(VK.device, staging, NULL);
    vkFreeMemory(VK.device, stagingMem, NULL);
    ibl_transition(VK.iblBrdfImage, 1, 1,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    VkImageViewCreateInfo vi = {0};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = VK.iblBrdfImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(VK.device, &vi, NULL, &VK.iblBrdfView) != VK_SUCCESS) return false;
    return true;
}

static bool ibl_create_samplers(void) {
    VkSamplerCreateInfo sci = {0};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.minLod = 0.f;
    sci.maxLod = 16.f;
    sci.maxAnisotropy = 1.f;
    if (vkCreateSampler(VK.device, &sci, NULL, &VK.iblCubeSampler) != VK_SUCCESS) return false;

    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.maxLod = 0.f;
    if (vkCreateSampler(VK.device, &sci, NULL, &VK.iblLutSampler) != VK_SUCCESS) return false;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Environment skybox                                                */
/* ------------------------------------------------------------------ */

static void skybox_write_descriptor(void) {
    if (!VK.skyboxDescSet || !VK.iblEnvView || !VK.iblCubeSampler) return;
    VkDescriptorImageInfo ii = {0};
    ii.sampler = VK.iblCubeSampler;
    ii.imageView = VK.iblEnvView;
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet w = {0};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = VK.skyboxDescSet;
    w.dstBinding = 0;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.descriptorCount = 1;
    w.pImageInfo = &ii;
    vkUpdateDescriptorSets(VK.device, 1, &w, 0, NULL);
}

static bool createSkyboxPipeline(void) {
    if (VK.skyboxReady) return true;

    VkShaderModule vert = createShaderModule("shaders/skybox.vert.spv");
    VkShaderModule frag = createShaderModule("shaders/skybox.frag.spv");
    if (!vert || !frag) {
        if (vert) vkDestroyShaderModule(VK.device, vert, NULL);
        if (frag) vkDestroyShaderModule(VK.device, frag, NULL);
        fprintf(stderr, "[vlcrlib] skybox shaders unavailable\n");
        return false;
    }

    VkDescriptorSetLayoutBinding bind = {0};
    bind.binding = 0;
    bind.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bind.descriptorCount = 1;
    bind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dli = {0};
    dli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dli.bindingCount = 1;
    dli.pBindings = &bind;
    if (vkCreateDescriptorSetLayout(VK.device, &dli, NULL, &VK.skyboxDSL) != VK_SUCCESS) {
        vkDestroyShaderModule(VK.device, vert, NULL);
        vkDestroyShaderModule(VK.device, frag, NULL);
        return false;
    }

    VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
    VkDescriptorPoolCreateInfo dpi = {0};
    dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(VK.device, &dpi, NULL, &VK.skyboxPool) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(VK.device, VK.skyboxDSL, NULL);
        VK.skyboxDSL = VK_NULL_HANDLE;
        vkDestroyShaderModule(VK.device, vert, NULL);
        vkDestroyShaderModule(VK.device, frag, NULL);
        return false;
    }

    VkDescriptorSetAllocateInfo ai = {0};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = VK.skyboxPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &VK.skyboxDSL;
    if (vkAllocateDescriptorSets(VK.device, &ai, &VK.skyboxDescSet) != VK_SUCCESS) {
        vkDestroyDescriptorPool(VK.device, VK.skyboxPool, NULL);
        VK.skyboxPool = VK_NULL_HANDLE;
        vkDestroyDescriptorSetLayout(VK.device, VK.skyboxDSL, NULL);
        VK.skyboxDSL = VK_NULL_HANDLE;
        vkDestroyShaderModule(VK.device, vert, NULL);
        vkDestroyShaderModule(VK.device, frag, NULL);
        return false;
    }
    skybox_write_descriptor();

    /* 48-byte push constants: right(+tanX), up(+tanY), forward(+exposure). */
    VkPushConstantRange pcr = {0};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0;
    pcr.size = 112;

    VkPipelineLayoutCreateInfo pli = {0};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &VK.skyboxDSL;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(VK.device, &pli, NULL, &VK.skyboxLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(VK.device, vert, NULL);
        vkDestroyShaderModule(VK.device, frag, NULL);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {0};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi = {0};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo ia = {0};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp = {0};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs = {0};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {0};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo dss = {0};
    dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dss.depthTestEnable = VK_TRUE;
    dss.depthWriteEnable = VK_FALSE;
    dss.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState cba = {0};
    cba.blendEnable = VK_FALSE;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb = {0};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds = {0};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo pi = {0};
    pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = 2;
    pi.pStages = stages;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &dss;
    pi.pColorBlendState = &cb;
    pi.pDynamicState = &ds;
    pi.layout = VK.skyboxLayout;
    pi.renderPass = VK.renderPass;
    pi.subpass = 0;

    VkResult r = vkCreateGraphicsPipelines(VK.device, VK_NULL_HANDLE, 1, &pi, NULL, &VK.skyboxPipeline);
    vkDestroyShaderModule(VK.device, vert, NULL);
    vkDestroyShaderModule(VK.device, frag, NULL);
    if (r != VK_SUCCESS) {
        fprintf(stderr, "[vlcrlib] skybox pipeline failed (%d)\n", (int)r);
        return false;
    }

    VK.skyboxReady = true;
    fprintf(stderr, "[vlcrlib] environment skybox pipeline ready\n");
    return true;
}

void vgraphics_DrawSkybox(void) {
    if (!VK.inFrame || !VK.skyboxEnabled || !VK.skyboxReady) return;
    if (!VK.atmosphereEnabled && !VK.iblLoaded) return;
    if (!VK.skyboxDescSet || VK.currentCmd == VK_NULL_HANDLE) return;

    /* Orthonormal camera basis from the view matrix (columns 0/1/2). */
    float rx = VK.view3D.m0, ry = VK.view3D.m4, rz = VK.view3D.m8;
    float ux = VK.view3D.m1, uy = VK.view3D.m5, uz = VK.view3D.m9;
    float fx = -VK.view3D.m2, fy = -VK.view3D.m6, fz = -VK.view3D.m10;

    float rl = sqrtf(rx*rx + ry*ry + rz*rz);
    float ul = sqrtf(ux*ux + uy*uy + uz*uz);
    float fl = sqrtf(fx*fx + fy*fy + fz*fz);
    if (rl < 1e-6f || ul < 1e-6f || fl < 1e-6f) return;
    rx /= rl; ry /= rl; rz /= rl;
    ux /= ul; uy /= ul; uz /= ul;
    fx /= fl; fy /= fl; fz /= fl;

    /* Half-FOV tangents straight from the projection matrix. */
    if (fabsf(VK.proj3D.m0) < 1e-6f || fabsf(VK.proj3D.m5) < 1e-6f) return;
    float tanX = 1.0f / VK.proj3D.m0;
    float tanY = 1.0f / VK.proj3D.m5;

    float pc[28] = {
        rx, ry, rz, tanX,
        ux, uy, uz, tanY,
        fx, fy, fz, VK.exposure > 0.0f ? VK.exposure : 1.0f,
        VK.sunDirection.x, VK.sunDirection.y, VK.sunDirection.z, VK.sunIntensity,
        VK.moonDirection.x, VK.moonDirection.y, VK.moonDirection.z, VK.moonIntensity,
        VK.atmosphereTime, VK.cloudCoverage, VK.haze, VK.starIntensity,
        VK.atmosphereEnabled ? 1.0f : 0.0f, 0.0f,
        VK.sunAngularRadius, VK.moonAngularRadius
    };

    VkCommandBuffer cb = VK.currentCmd;
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, VK.skyboxPipeline);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, VK.skyboxLayout,
                            0, 1, &VK.skyboxDescSet, 0, NULL);
    vkCmdPushConstants(cb, VK.skyboxLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), pc);
    vkCmdDraw(cb, 3, 1, 0, 0);
}

void vgraphics_SetSkybox(int enabled) { VK.skyboxEnabled = enabled ? 1 : 0; }
int  vgraphics_GetSkybox(void)        { return VK.skyboxEnabled; }

static Vector3 atmosphereNormalize(Vector3 v, Vector3 fallback) {
    float len = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    if (len < 1e-6f) return fallback;
    return (Vector3){ v.x/len, v.y/len, v.z/len };
}

void vgraphics_SetAtmosphere(const AtmosphereParams *params) {
    if (!params) return;
    VK.atmosphereEnabled = params->enabled ? 1 : 0;
    VK.atmosphereTime = params->timeOfDay;
    if (VK.atmosphereTime < 0.0f) VK.atmosphereTime = 0.0f;
    if (VK.atmosphereTime > 24.0f) VK.atmosphereTime = 24.0f;
    VK.cloudCoverage = params->cloudCoverage < 0.0f ? 0.0f
                     : (params->cloudCoverage > 1.0f ? 1.0f : params->cloudCoverage);
    VK.haze = params->haze < 0.0f ? 0.0f
            : (params->haze > 1.0f ? 1.0f : params->haze);
    VK.starIntensity = params->starIntensity < 0.0f ? 0.0f
                     : (params->starIntensity > 1.0f ? 1.0f : params->starIntensity);
    VK.sunDirection = atmosphereNormalize(params->sunDirection, (Vector3){0,1,0});
    VK.moonDirection = atmosphereNormalize(params->moonDirection, (Vector3){0,-1,0});
    VK.sunIntensity = params->sunIntensity < 0.0f ? 0.0f : params->sunIntensity;
    VK.moonIntensity = params->moonIntensity < 0.0f ? 0.0f : params->moonIntensity;
    VK.sunAngularRadius = params->sunAngularRadius > 0.0f ? params->sunAngularRadius : 0.009f;
    VK.moonAngularRadius = params->moonAngularRadius > 0.0f ? params->moonAngularRadius : 0.010f;
}

void vgraphics_GetAtmosphere(AtmosphereParams *out) {
    if (!out) return;
    out->enabled = VK.atmosphereEnabled;
    out->timeOfDay = VK.atmosphereTime;
    out->cloudCoverage = VK.cloudCoverage;
    out->haze = VK.haze;
    out->starIntensity = VK.starIntensity;
    out->sunDirection = VK.sunDirection;
    out->sunIntensity = VK.sunIntensity;
    out->moonDirection = VK.moonDirection;
    out->moonIntensity = VK.moonIntensity;
    out->sunAngularRadius = VK.sunAngularRadius;
    out->moonAngularRadius = VK.moonAngularRadius;
}

static bool ibl_ensure_default_cube(void) {
    if (VK.iblIrrView && VK.iblPreView && VK.iblBrdfView && VK.iblEnvView) return true;
    if (!VK.iblCubeSampler && !ibl_create_samplers()) return false;
    /* 1x1 gray cube for safe descriptor binding when IBL unloaded */
    float gray[4] = { 0.15f, 0.17f, 0.22f, 1.f };
    VIBL_Cube tiny = {0};
    tiny.size = 1; tiny.mips = 1;
    tiny.faces = (float **)calloc(6, sizeof(float *));
    for (int f = 0; f < 6; ++f) {
        tiny.faces[f] = (float *)malloc(3 * sizeof(float));
        tiny.faces[f][0] = gray[0]; tiny.faces[f][1] = gray[1]; tiny.faces[f][2] = gray[2];
    }
    if (!VK.iblIrrImage) {
        if (!ibl_create_cube(1, 1, &VK.iblIrrImage, &VK.iblIrrMem, &VK.iblIrrView)) return false;
        ibl_upload_cube(&tiny, VK.iblIrrImage);
    }
    if (!VK.iblPreImage) {
        if (!ibl_create_cube(1, 1, &VK.iblPreImage, &VK.iblPreMem, &VK.iblPreView)) return false;
        ibl_upload_cube(&tiny, VK.iblPreImage);
    }
    if (!VK.iblEnvImage) {
        if (!ibl_create_cube(1, 1, &VK.iblEnvImage, &VK.iblEnvMem, &VK.iblEnvView)) return false;
        ibl_upload_cube(&tiny, VK.iblEnvImage);
    }
    if (!VK.iblBrdfView) {
        VIBL_LUT lut = {0};
        lut.size = 1;
        lut.data = (float *)malloc(2 * sizeof(float));
        lut.data[0] = 1.f; lut.data[1] = 0.f;
        ibl_upload_brdf_lut(&lut);
        free(lut.data);
    }
    for (int f = 0; f < 6; ++f) free(tiny.faces[f]);
    free(tiny.faces);
    skybox_write_descriptor();
    return true;
}

static bool ibl_upload_bundle_to_gpu(const VIBL_Bundle *bundle) {
    if (!bundle || !bundle->valid) return false;

    /* Existing images/descriptors may still be referenced by in-flight
       frames; destroy only after the GPU is idle. */
    if (VK.device) vkDeviceWaitIdle(VK.device);
    ibl_destroy_gpu();

    if (!ibl_create_samplers()) return false;

    /* sharp environment cubemap for the sky background */
    VIBL_Cube env = bundle->environment;
    if (!env.faces || env.size < 1) { env = bundle->prefiltered; env.mips = 1; }
    if (!env.faces || env.size < 1) { env = bundle->irradiance; env.mips = 1; }
    uint32_t envSize = (uint32_t)(env.size > 0 ? env.size : 64);
    if (!ibl_create_cube(envSize, 1, &VK.iblEnvImage, &VK.iblEnvMem, &VK.iblEnvView))
        return false;
    if (!ibl_upload_cube(&env, VK.iblEnvImage)) return false;

    /* irradiance cube */
    uint32_t irrSize = (uint32_t)(bundle->irradiance.size > 0 ? bundle->irradiance.size : 16);
    if (!ibl_create_cube(irrSize, 1, &VK.iblIrrImage, &VK.iblIrrMem, &VK.iblIrrView))
        return false;
    if (!ibl_upload_cube(&bundle->irradiance, VK.iblIrrImage)) return false;

    /* prefiltered cube with mips */
    uint32_t preSize = (uint32_t)(bundle->prefiltered.size > 0 ? bundle->prefiltered.size : 64);
    uint32_t preMips = (uint32_t)(bundle->prefiltered.mips > 0 ? bundle->prefiltered.mips : 1);
    if (!ibl_create_cube(preSize, preMips, &VK.iblPreImage, &VK.iblPreMem, &VK.iblPreView))
        return false;
    if (!ibl_upload_cube(&bundle->prefiltered, VK.iblPreImage)) return false;

    /* BRDF LUT 2D */
    if (!ibl_upload_brdf_lut(&bundle->brdf)) return false;

    /* Point the skybox at the new environment cubemap. */
    skybox_write_descriptor();

    fprintf(stderr, "[vlcrlib] IBL uploaded to GPU: env=%u irr=%u pre=%u mips=%u lut=%d\n",
            envSize, irrSize, preSize, preMips, bundle->brdf.size);
    return true;
}

/* updateMaterial -> vgraphics_material.c */
/* ================================================================== */
/*  Model                                                             */
/* ================================================================== */
static float clampf01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/* Copy a CPU texture sidecar into the model's owned texture table. Returns the
   table index, or -1 when the texture has no CPU pixels. Deduplicates by
   Texture2D id + generation. */
static int cpuStoreTexture(const Texture2D *tex, VCpuTexture *owned,
                           uint64_t *keys, int maxTextures, int *count) {
    if (!tex || !tex->id || !count || *count >= maxTextures) return -1;
    uint64_t key = ((uint64_t)tex->id << 32) | (uint64_t)tex->generation;
    for (int i = 0; i < *count; ++i)
        if (keys[i] == key) return i;
    const uint32_t *pixels = NULL;
    int w = 0, h = 0;
    if (!vgraphics_GetCpuTexturePixels(*tex, &pixels, &w, &h) ||
        !pixels || w <= 0 || h <= 0)
        return -1;
    uint32_t *copy = (uint32_t *)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    if (!copy) return -1;
    memcpy(copy, pixels, (size_t)w * (size_t)h * sizeof(uint32_t));
    owned[*count].width = w;
    owned[*count].height = h;
    owned[*count].pixels = copy;
    keys[*count] = key;
    return (*count)++;
}

bool vgraphics_BuildCpuModel(Model model, VCpuModel *out) {
    if (out) memset(out, 0, sizeof(*out));
    if (!out || model.id == 0 || model.id >= VLCRLIB_MAX_MODELS) return false;
    ModelEntry *me = &g_modelTable[model.id];
    if (!me->meshes || me->meshCount <= 0) return false;
    int totalVerts = 0, totalIndices = 0;
    for (int mi = 0; mi < me->meshCount; ++mi) {
        Mesh mesh = me->meshes[mi];
        if (!meshHandleValid(mesh)) continue;
        MeshEntry *mv = &g_meshTable[mesh.id];
        if (!mv->cpuVertices || !mv->cpuIndices) continue;
        totalVerts += mv->cpuVertexCount;
        totalIndices += mv->indexCount;
    }
    if (totalVerts <= 0 || totalIndices <= 0) return false;

    VCpuVertex *verts = (VCpuVertex *)malloc(sizeof(VCpuVertex) * (size_t)totalVerts);
    uint32_t *indices = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)totalIndices);
    int maxTextures = me->meshCount * 2;
    if (maxTextures < 2) maxTextures = 2;
    VCpuModelPart *parts = (VCpuModelPart *)calloc((size_t)me->meshCount,
                                                   sizeof(VCpuModelPart));
    VCpuTexture *owned = (VCpuTexture *)calloc((size_t)maxTextures,
                                               sizeof(VCpuTexture));
    uint64_t *texKeys = (uint64_t *)calloc((size_t)maxTextures,
                                           sizeof(uint64_t));
    if (!verts || !indices || !parts || !owned || !texKeys) {
        free(verts); free(indices); free(parts); free(owned); free(texKeys);
        return false;
    }

    int vo = 0, io = 0, partCount = 0, ownedCount = 0;
    for (int mi = 0; mi < me->meshCount; ++mi) {
        Mesh mesh = me->meshes[mi];
        if (!meshHandleValid(mesh)) continue;
        MeshEntry *mv = &g_meshTable[mesh.id];
        if (!mv->cpuVertices || !mv->cpuIndices) continue;
        Matrix node = me->meshTransforms ? me->meshTransforms[mi] : MatrixIdentity();
        Matrix normalMat = MatrixNormalFromModel(node);

        VCpuModelPart part;
        memset(&part, 0, sizeof(part));
        part.firstIndex = io;
        part.indexCount = mv->indexCount;
        part.baseColor = (Color){255, 255, 255, 255};
        part.baseColorFactor[0] = 1.0f;
        part.baseColorFactor[1] = 1.0f;
        part.baseColorFactor[2] = 1.0f;
        part.baseColorFactor[3] = 1.0f;
        part.metallic = 0.0f;
        part.roughness = 0.5f;
        part.alphaMode = 0;
        part.alphaCutoff = 0.5f;
        part.doubleSided = 1;

        int matIndex = me->meshMaterials ? me->meshMaterials[mi] : 0;
        const Material *mat = (me->materials && matIndex >= 0 &&
                               matIndex < me->materialCount)
                            ? &me->materials[matIndex] : NULL;
        if (mat) {
            part.baseColor = mat->baseColor;
            float sum = 0.0f;
            for (int c = 0; c < 4; ++c) {
                part.baseColorFactor[c] = mat->baseColorFactor[c];
                sum += mat->baseColorFactor[c] < 0.0f ? 0.0f
                                                      : mat->baseColorFactor[c];
            }
            if (sum <= 1e-6f) {
                part.baseColorFactor[0] = mat->baseColor.r / 255.0f;
                part.baseColorFactor[1] = mat->baseColor.g / 255.0f;
                part.baseColorFactor[2] = mat->baseColor.b / 255.0f;
                part.baseColorFactor[3] = mat->baseColor.a / 255.0f;
            }
            part.metallic = mat->metallic;
            part.roughness = mat->roughness;
            part.emissive[0] = mat->emissionColor.r / 255.0f *
                               mat->emissionStrength;
            part.emissive[1] = mat->emissionColor.g / 255.0f *
                               mat->emissionStrength;
            part.emissive[2] = mat->emissionColor.b / 255.0f *
                               mat->emissionStrength;
            part.emissiveStrength = 1.0f;
            part.alphaMode = mat->alphaMode;
            part.alphaCutoff = mat->alphaCutoff > 0.0f ? mat->alphaCutoff : 0.5f;
            part.doubleSided = mat->doubleSided;
            part.unlit = mat->unlit;

            int albedoIndex = cpuStoreTexture(&mat->albedoMap, owned, texKeys,
                                              maxTextures, &ownedCount);
            if (albedoIndex >= 0) part.texture = &owned[albedoIndex];
            int emissiveIndex = cpuStoreTexture(&mat->emissionMap, owned,
                                                texKeys, maxTextures,
                                                &ownedCount);
            if (emissiveIndex >= 0)
                part.emissiveTexture = &owned[emissiveIndex];
        }

        for (int v = 0; v < mv->cpuVertexCount; ++v) {
            const Vertex3D *src = &mv->cpuVertices[v];
            verts[vo].position.x = node.m0 * src->x + node.m4 * src->y +
                                   node.m8 * src->z + node.m12;
            verts[vo].position.y = node.m1 * src->x + node.m5 * src->y +
                                   node.m9 * src->z + node.m13;
            verts[vo].position.z = node.m2 * src->x + node.m6 * src->y +
                                   node.m10 * src->z + node.m14;
            verts[vo].uv = (Vector2){ src->u, src->v };
            verts[vo].normal.x = normalMat.m0 * src->nx +
                                 normalMat.m4 * src->ny +
                                 normalMat.m8 * src->nz;
            verts[vo].normal.y = normalMat.m1 * src->nx +
                                 normalMat.m5 * src->ny +
                                 normalMat.m9 * src->nz;
            verts[vo].normal.z = normalMat.m2 * src->nx +
                                 normalMat.m6 * src->ny +
                                 normalMat.m10 * src->nz;
            float cr = src->cr, cg = src->cg, cb = src->cb, ca = src->ca;
            if (cr <= 0.0f && cg <= 0.0f && cb <= 0.0f && ca <= 0.0f) {
                cr = cg = cb = ca = 1.0f;
            }
            verts[vo].color = (Color){
                (unsigned char)(clampf01(cr) * 255.0f + 0.5f),
                (unsigned char)(clampf01(cg) * 255.0f + 0.5f),
                (unsigned char)(clampf01(cb) * 255.0f + 0.5f),
                (unsigned char)(clampf01(ca) * 255.0f + 0.5f)
            };
            vo++;
        }
        for (int i = 0; i < mv->indexCount; ++i)
            indices[io++] = mv->cpuIndices[i] + (uint32_t)(vo - mv->cpuVertexCount);
        parts[partCount++] = part;
    }

    VCpuModel created = VCpuModelCreate(verts, vo, indices, io,
                                        (Color){255, 255, 255, 255}, NULL);
    if (!created.vertices) {
        free(verts); free(indices);
        for (int t = 0; t < ownedCount; ++t) free(owned[t].pixels);
        free(parts); free(owned); free(texKeys);
        return false;
    }
    free(created.parts);
    created.parts = parts;
    created.partCount = partCount;
    created.ownedTextures = owned;
    created.ownedTextureCount = ownedCount;
    created.texture = NULL;
    created.baseColor = (Color){255, 255, 255, 255};
    free(texKeys);
    free(verts);
    free(indices);
    *out = created;
    return true;
}

Model LoadModelFromMesh(Mesh mesh) {
    Model model = {0};
    if (!meshHandleValid(mesh) || g_modelCount + 1 >= VLCRLIB_MAX_MODELS) return model;

    int idx = ++g_modelCount;
    ModelEntry *e = &g_modelTable[idx];
    e->meshCount = 1;
    e->materialCount = 1;
    e->meshes = malloc(sizeof(Mesh));
    e->meshMaterials = malloc(sizeof(int));
    e->materials = malloc(sizeof(Material));
    e->meshes[0] = mesh;
    e->meshMaterials[0] = 0;
    e->materials[0] = LoadMaterialDefault();
    model.id = (unsigned int)idx;
    model.meshCount = 1;
    model.materialCount = 1;
    return model;
}

Model LoadModel(const char *path) {
    if (!path || !path[0]) return (Model){0};
    const char *extension = strrchr(path, '.');
    if (extension && (SDL_strcasecmp(extension, ".glb") == 0 ||
                      SDL_strcasecmp(extension, ".gltf") == 0))
        return LoadModelGLTFTiny(path);
    if (!VK.device) return (Model){0};
    Mesh mesh = loadMeshOBJ(path);
    if (!meshHandleValid(mesh)) return (Model){0};
    return LoadModelFromMesh(mesh);
}

bool vgraphics_TrackModelTexture(Model model, Texture2D texture) {
    if (model.id == 0 || model.id >= VLCRLIB_MAX_MODELS) return false;
    ModelEntry *e = &g_modelTable[model.id];
    Texture2D *grown = (Texture2D *)realloc(
        e->ownedTextures, sizeof(Texture2D) * (size_t)(e->ownedTextureCount + 1));
    if (!grown) return false;
    e->ownedTextures = grown;
    e->ownedTextures[e->ownedTextureCount++] = texture;
    return true;
}

void UnloadModel(Model model) {
    if (model.id == 0 || model.id >= VLCRLIB_MAX_MODELS) return;
    vcore_InvalidateCpuModel(model.id);
    ModelEntry *e = &g_modelTable[model.id];
    if (!e->meshes) return;
    for (int i = 0; i < e->materialCount; ++i) UnloadMaterial(e->materials[i]);
    for (int i = 0; i < e->meshCount; ++i)     UnloadMesh(e->meshes[i]);
    for (int i = 0; i < e->ownedTextureCount; ++i) UnloadTexture(e->ownedTextures[i]);
    free(e->ownedTextures);
    free(e->meshes); free(e->meshMaterials); free(e->materials); free(e->meshTransforms);
    *e = (ModelEntry){0};
}

void vlcrlib_replace_model_material(Model model, int materialIndex, Material material) {
    if (model.id == 0 || model.id >= VLCRLIB_MAX_MODELS) return;
    ModelEntry *e = &g_modelTable[model.id];
    if (!e->materials || materialIndex < 0 || materialIndex >= e->materialCount) return;
    if (e->materials[materialIndex].id != material.id ||
        e->materials[materialIndex].generation != material.generation)
        UnloadMaterial(e->materials[materialIndex]);
    e->materials[materialIndex] = material;
}


void vgraphics_FlushTransparent(void);
static void flushTransparent(void) {
    if (VK.transCount <= 0 || VK.transFlushing) return;
    VK.transFlushing = 1;
    /* Simple insertion sort by distance descending (far first) */
    for (int i = 1; i < VK.transCount; ++i) {
        int j = i;
        while (j > 0 && VK.transQueue[j].dist > VK.transQueue[j-1].dist) {
            {
                unsigned int _mid = VK.transQueue[j].modelId;
                Vector3 _p = VK.transQueue[j].pos, _r = VK.transQueue[j].rot, _s = VK.transQueue[j].scale;
                Color _t = VK.transQueue[j].tint;
                float _d = VK.transQueue[j].dist;
                VK.transQueue[j] = VK.transQueue[j-1];
                VK.transQueue[j-1].modelId = _mid;
                VK.transQueue[j-1].pos = _p;
                VK.transQueue[j-1].rot = _r;
                VK.transQueue[j-1].scale = _s;
                VK.transQueue[j-1].tint = _t;
                VK.transQueue[j-1].dist = _d;
            }
            --j;
        }
    }
    for (int q = 0; q < VK.transCount; ++q) {
        DrawModel(
            (Model){ VK.transQueue[q].modelId },
            VK.transQueue[q].pos,
            VK.transQueue[q].rot,
            VK.transQueue[q].scale,
            VK.transQueue[q].tint
        );
    }
    VK.transCount = 0;
    VK.transFlushing = 0;
}
void vgraphics_FlushTransparent(void) { flushTransparent(); }

typedef struct ViewFrustum {
    float p[6][4];
} ViewFrustum;

/* Clip component `row` is dot(m_row, m_{4+row}, m_{8+row}, m_{12+row}, p).
   The Matrix struct declares its fields column-wise (m0, m4, m8, m12 are the
   first four), so the element m_{4*col+row} sits at raw offset 4*row + col --
   NOT col*4+row. */
static float matrixRow(const Matrix *m, int row, int col) {
    return ((const float *)m)[row * 4 + col];
}

static void frustumPlane(float out[4], float a, float b, float c, float d) {
    float len = sqrtf(a*a + b*b + c*c);
    if (len < 1e-8f) { out[0]=out[1]=out[2]=out[3]=0.0f; return; }
    out[0] = a / len; out[1] = b / len;
    out[2] = c / len; out[3] = d / len;
}

static ViewFrustum frustumFromViewProj(const Matrix *vp) {
    ViewFrustum f;
    /* Rows of a column-major matrix. */
    float r0[4], r1[4], r2[4], r3[4];
    for (int c = 0; c < 4; ++c) {
        r0[c] = matrixRow(vp, 0, c);
        r1[c] = matrixRow(vp, 1, c);
        r2[c] = matrixRow(vp, 2, c);
        r3[c] = matrixRow(vp, 3, c);
    }
    frustumPlane(f.p[0], r3[0]+r0[0], r3[1]+r0[1], r3[2]+r0[2], r3[3]+r0[3]); /* left */
    frustumPlane(f.p[1], r3[0]-r0[0], r3[1]-r0[1], r3[2]-r0[2], r3[3]-r0[3]); /* right */
    frustumPlane(f.p[2], r3[0]+r1[0], r3[1]+r1[1], r3[2]+r1[2], r3[3]+r1[3]); /* bottom */
    frustumPlane(f.p[3], r3[0]-r1[0], r3[1]-r1[1], r3[2]-r1[2], r3[3]-r1[3]); /* top */
    frustumPlane(f.p[4], r2[0], r2[1], r2[2], r2[3]);                         /* near (z >= 0) */
    frustumPlane(f.p[5], r3[0]-r2[0], r3[1]-r2[1], r3[2]-r2[2], r3[3]-r2[3]); /* far */
    return f;
}

static bool aabbVisible(const ViewFrustum *f, Vector3 center, Vector3 extent) {
    float radius = sqrtf(extent.x*extent.x + extent.y*extent.y + extent.z*extent.z);
    for (int i = 0; i < 6; ++i) {
        const float *p = f->p[i];
        float d = p[0]*center.x + p[1]*center.y + p[2]*center.z + p[3];
        if (d < -radius) return false;
    }
    return true;
}

void DrawModel(Model model, Vector3 position, Vector3 rotation, Vector3 scale, Color tint) {
    if (vcore_IsCpuBackend()) {
        vcore_CpuDrawModel(model, position, rotation, scale, tint);
        return;
    }
    if (model.id == 0 || model.id >= VLCRLIB_MAX_MODELS) return;
    ModelEntry *e = &g_modelTable[model.id];
    if (!e->meshes) return;

    Matrix T = MatrixTranslate(position.x, position.y, position.z);
    Matrix R = MatrixRotateXYZ(rotation);
    Matrix S = MatrixScale(scale.x, scale.y, scale.z);
    Matrix top = MatrixMultiply(T, MatrixMultiply(R, S));

    /* Keep the current scene list for the next frame's shadow and SSAO
     * prepasses. SSAO still needs depth even when shadow maps are disabled. */
    if ((VK.shadowsEnabled || VK.ssaoEnabled || VK.taaEnabled) &&
        VK.shadowCasterCount < 64) {
        int si = VK.shadowCasterCount++;
        VK.shadowCasters[si].modelId = model.id;
        VK.shadowCasters[si].meshId = 0;
        VK.shadowCasters[si].skinnedId = 0;
        VK.shadowCasters[si].position = position;
        VK.shadowCasters[si].rotation = rotation;
        VK.shadowCasters[si].scale = scale;
        VK.shadowCasters[si].transform = top;
        VK.shadowCasters[si].prevTransform = e->prevTransformValid
            ? e->prevTransform : top;
    }
    e->prevTransform = top;
    e->prevTransformValid = 1;
    Matrix VP = MatrixMultiply(VK.proj3D, VK.view3D);
    ViewFrustum frustum = frustumFromViewProj(&VP);

    /* Pass 0 = opaque/mask, pass 1 = BLEND/transmission (back-to-front approx: draw order) */
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < e->meshCount; ++i) {
            Mesh mesh = e->meshes[i];
            if (!meshHandleValid(mesh)) continue;
            MeshEntry *me = &g_meshTable[mesh.id];
            if (!me->vbo) continue;

            Matrix nodeWorld = (e->meshTransforms) ? e->meshTransforms[i] : MatrixIdentity();
            Matrix M = MatrixMultiply(top, nodeWorld);
            Matrix mvp = MatrixMultiply(VP, M);

            if (me->hasBounds) {
                Vector3 lc = {
                    (me->boundsMin.x + me->boundsMax.x) * 0.5f,
                    (me->boundsMin.y + me->boundsMax.y) * 0.5f,
                    (me->boundsMin.z + me->boundsMax.z) * 0.5f
                };
                Vector3 le = {
                    (me->boundsMax.x - me->boundsMin.x) * 0.5f,
                    (me->boundsMax.y - me->boundsMin.y) * 0.5f,
                    (me->boundsMax.z - me->boundsMin.z) * 0.5f
                };
                Vector3 wc = {
                    M.m0*lc.x + M.m4*lc.y + M.m8*lc.z + M.m12,
                    M.m1*lc.x + M.m5*lc.y + M.m9*lc.z + M.m13,
                    M.m2*lc.x + M.m6*lc.y + M.m10*lc.z + M.m14
                };
                Vector3 we = {
                    fabsf(M.m0)*le.x + fabsf(M.m4)*le.y + fabsf(M.m8)*le.z,
                    fabsf(M.m1)*le.x + fabsf(M.m5)*le.y + fabsf(M.m9)*le.z,
                    fabsf(M.m2)*le.x + fabsf(M.m6)*le.y + fabsf(M.m10)*le.z
                };
                if (!aabbVisible(&frustum, wc, we)) {
                    if (pass == 0) g_frameStats.culledDraws++;
                    continue;
                }
            }

            int matIdx = e->meshMaterials[i];
            if (matIdx < 0 || matIdx >= e->materialCount) matIdx = 0;
            Material mat = e->materials[matIdx];
            if (!material_handle_valid(&mat)) continue;
            int isBlend = (mat.alphaMode == 2) || (mat.transmissionFactor > 0.05f);
            if (pass == 0 && isBlend) continue;
            if (pass == 1 && !isBlend) continue;
            /* Queue transparent for later sorted flush */
            if (isBlend && !VK.transFlushing && VK.transCount < 64) {
                int qi = VK.transCount++;
                VK.transQueue[qi].modelId = model.id;
                VK.transQueue[qi].pos = position;
                VK.transQueue[qi].rot = rotation;
                VK.transQueue[qi].scale = scale;
                VK.transQueue[qi].tint = tint;
                Vector3 cam = VK.cameraPos;
                float dx = position.x - cam.x, dy = position.y - cam.y, dz = position.z - cam.z;
                VK.transQueue[qi].dist = dx*dx + dy*dy + dz*dz;
                continue;
            }

            Material m2 = mat;
            m2.baseColorFactor[0] = mat.baseColorFactor[0] * (tint.r / 255.0f);
            m2.baseColorFactor[1] = mat.baseColorFactor[1] * (tint.g / 255.0f);
            m2.baseColorFactor[2] = mat.baseColorFactor[2] * (tint.b / 255.0f);
            m2.baseColorFactor[3] = mat.baseColorFactor[3] * (tint.a / 255.0f);
            m2.baseColor.r = (unsigned char)(m2.baseColorFactor[0] * 255.0f + 0.5f);
            m2.baseColor.g = (unsigned char)(m2.baseColorFactor[1] * 255.0f + 0.5f);
            m2.baseColor.b = (unsigned char)(m2.baseColorFactor[2] * 255.0f + 0.5f);
            m2.baseColor.a = (unsigned char)(m2.baseColorFactor[3] * 255.0f + 0.5f);

            uint32_t slot = VK.uboPBRSlot;
            writeUBOPBR(mvp, M, &m2);

            PBRMaterialEntry *mpe = &g_matTable[mat.id];
            VkCommandBuffer cb = VK.currentCmd;
            {
                VkPipeline pl;
                if (isBlend)
                    pl = m2.doubleSided ? VK.pipelinePBRBlendNoCull : VK.pipelinePBRBlend;
                else
                    pl = m2.doubleSided ? VK.pipelinePBRNoCull : VK.pipelinePBR;
                vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl);
            }
            uint32_t dyn = slot * VK.uboPBRStride;
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, VK.layoutPBR,
                                    0, 1, &mpe->descSet, 1, &dyn);
            VkDeviceSize off = 0;
            vkCmdBindVertexBuffers(cb, 0, 1, &me->vbo, &off);
            vkCmdBindIndexBuffer(cb, me->ibo, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cb, me->indexCount, 1, 0, 0, 0);
            statsCountDraw(me->indexCount, isBlend);
        }
    }
}

/* ================================================================== */
/*  Simple 3D (DrawMesh / DrawCube / DrawSphere / DrawGrid / Line3D)  */
/* ================================================================== */
void DrawMesh(Mesh mesh, Texture2D texture, Color baseColor, Matrix model) {
    (void)texture;
    if (!meshHandleValid(mesh)) return;
    MeshEntry *e = &g_meshTable[mesh.id];
    if (!e->vbo) return;

    /* Direct meshes (including DrawCube/DrawSphere helpers) participate in
     * shadow and SSAO prepasses just like meshes submitted through DrawModel. */
    if ((VK.shadowsEnabled || VK.ssaoEnabled || VK.taaEnabled) &&
        VK.shadowCasterCount < 64) {
        int si = VK.shadowCasterCount++;
        VK.shadowCasters[si].modelId = 0;
        VK.shadowCasters[si].meshId = mesh.id;
        VK.shadowCasters[si].skinnedId = 0;
        VK.shadowCasters[si].meshGeneration = mesh.generation;
        VK.shadowCasters[si].transform = model;
        VK.shadowCasters[si].prevTransform = model;
        VK.shadowCasters[si].position = (Vector3){0};
        VK.shadowCasters[si].rotation = (Vector3){0};
        VK.shadowCasters[si].scale = (Vector3){1,1,1};
    }

    Matrix mvp = MatrixMultiply(MatrixMultiply(VK.proj3D, VK.view3D), model);
    uint32_t slot = VK.ubo3DSlot;
    writeUBO3D(mvp, model, baseColor);

    VkCommandBuffer cb = VK.currentCmd;
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, VK.pipeline3D);
    uint32_t dyn = slot * VK.ubo3DStride;
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, VK.layout3D,
                            0, 1, &VK.descSet3D, 1, &dyn);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &e->vbo, &off);
    vkCmdBindIndexBuffer(cb, e->ibo, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, e->indexCount, 1, 0, 0, 0);
    statsCountDraw(e->indexCount, 0);
}

void DrawCube(Vector3 position, Vector3 size, Color color) {
    static Mesh cube = {0};
    if (cube.id == 0) cube = GenMeshCube(1, 1, 1);
    Matrix m = MatrixMultiply(MatrixTranslate(position.x, position.y, position.z),
                              MatrixScale(size.x, size.y, size.z));
    DrawMesh(cube, (Texture2D){0}, color, m);
}

void DrawSphere(Vector3 center, float radius, Color color) {
    static Mesh sphere = {0};
    if (sphere.id == 0) sphere = GenMeshSphere(1.0f, 16, 24);
    Matrix m = MatrixMultiply(MatrixTranslate(center.x, center.y, center.z),
                              MatrixScale(radius, radius, radius));
    DrawMesh(sphere, (Texture2D){0}, color, m);
}

static void drawLines3D(const Vertex3D *verts, int count, Color color) {
    VkDeviceSize bytes = count * sizeof(Vertex3D);
    if (g_lineVboCap < bytes) {
        if (g_lineVbo)    vkDestroyBuffer(VK.device, g_lineVbo, NULL);
        if (g_lineVboMem) vkFreeMemory(VK.device, g_lineVboMem, NULL);
        g_lineVboCap = bytes * 2;
        if (!createBuffer(g_lineVboCap,
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &g_lineVbo, &g_lineVboMem)) return;
    }
    void *m = NULL;
    if (vkMapMemory(VK.device, g_lineVboMem, 0, bytes, 0, &m) != VK_SUCCESS) return;
    memcpy(m, verts, bytes);
    vkUnmapMemory(VK.device, g_lineVboMem);

    Matrix I = MatrixIdentity();
    Matrix mvp = MatrixMultiply(VK.proj3D, VK.view3D);
    uint32_t slot = VK.ubo3DSlot;
    writeUBO3D(mvp, I, color);

    VkCommandBuffer cb = VK.currentCmd;
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, VK.pipeline3DLine);
    uint32_t dyn = slot * VK.ubo3DStride;
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, VK.layout3D,
                            0, 1, &VK.descSet3D, 1, &dyn);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &g_lineVbo, &off);
    vkCmdDraw(cb, (uint32_t)count, 1, 0, 0);
}

void DrawLine3D(Vector3 start, Vector3 end, Color color) {
    Vertex3D v[2] = {
        { start.x, start.y, start.z, 0, 0, 1, 0, 0 },
        { end.x,   end.y,   end.z,   0, 0, 1, 1, 1 },
    };
    drawLines3D(v, 2, color);
}

void DrawGrid(int slices, float spacing) {
    if (slices < 1) return;
    int n = (slices * 2 + 1) * 2;
    Vertex3D *verts = malloc(n * 2 * sizeof(Vertex3D));
    int k = 0;
    float half = (float)slices * spacing;
    for (int i = -slices; i <= slices; ++i) {
        float o = i * spacing;
        verts[k++] = (Vertex3D){ -half, 0, o, 0, 1, 0, 0, 0 };
        verts[k++] = (Vertex3D){  half, 0, o, 0, 1, 0, 1, 1 };
        verts[k++] = (Vertex3D){ o, 0, -half, 0, 1, 0, 0, 0 };
        verts[k++] = (Vertex3D){ o, 0,  half, 0, 1, 0, 1, 1 };
    }
    drawLines3D(verts, k, (Color){ 130, 130, 130, 255 });
    free(verts);
}

/* ================================================================== */
/*  2D draw vertex buffer                                             */
/* ================================================================== */
void vgraphics_Set2DViewportSize(int w, int h) {
    VK.viewport2DW = w;
    VK.viewport2DH = h;
    if (VK.currentCmd != VK_NULL_HANDLE) {
        VkViewport viewport = {0, 0, (float)w, (float)h, 0, 1};
        vkCmdSetViewport(VK.currentCmd, 0, 1, &viewport);
    }
}

static VkDescriptorSet updateDescriptorSet2D(Texture2D texture) {
    if (VK.descSet2DIndex >= VLCRLIB_MAX_2D_DESCRIPTORS) return VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK.descSets2D[VK.descSet2DIndex++];
    VkDescriptorImageInfo info = {0};
    if (texture.id == 0 || texture.id >= VLCRLIB_MAX_TEXTURES ||
        !texture.generation ||
        texture.generation != g_textureTable[texture.id].generation ||
        !g_textureTable[texture.id].view) {
        TextureEntry *e = &g_textureTable[VK.whiteTexture.id];
        info.imageView = e->view;
        info.sampler   = e->samp;
    } else {
        TextureEntry *e = &g_textureTable[texture.id];
        info.imageView = e->view;
        info.sampler   = e->samp;
    }
    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet w = {0};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = descriptorSet;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &info;
    vkUpdateDescriptorSets(VK.device, 1, &w, 0, NULL);
    return descriptorSet;
}

void vgraphics_DrawVertexBuffer(const Vertex2D *vertices, int count, Texture2D texture) {
    if (count <= 0 || !vertices) return;
    if (vcore_IsCpuBackend()) {
        vcore_CpuDrawVertices(vertices, count, texture);
        return;
    }
    VkDeviceSize bytes = (VkDeviceSize)count * sizeof(Vertex2D);

    if (VK.vbo2DCapacity < VK.vbo2DOffset + bytes) {
        if (VK.vbo2DOffset != 0) return;
        if (VK.vbo2D)    vkDestroyBuffer(VK.device, VK.vbo2D, NULL);
        if (VK.vbo2DMem) vkFreeMemory(VK.device, VK.vbo2DMem, NULL);
        VK.vbo2DCapacity = 4 * 1024 * 1024;
        if (VK.vbo2DCapacity < bytes) VK.vbo2DCapacity = bytes * 2;
        if (!createBuffer(VK.vbo2DCapacity,
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &VK.vbo2D, &VK.vbo2DMem)) return;
    }
    void *mapped = NULL;
    if (vkMapMemory(VK.device, VK.vbo2DMem, VK.vbo2DOffset, bytes, 0, &mapped) != VK_SUCCESS)
        return;
    memcpy(mapped, vertices, (size_t)bytes);
    vkUnmapMemory(VK.device, VK.vbo2DMem);

    VkDescriptorSet descriptorSet = updateDescriptorSet2D(texture);
    if (descriptorSet == VK_NULL_HANDLE) return;

    int w = VK.viewport2DW > 0 ? VK.viewport2DW : (int)VK.swapExtent.width;
    int h = VK.viewport2DH > 0 ? VK.viewport2DH : (int)VK.swapExtent.height;
    Matrix ortho = MatrixOrtho(0.0f, (float)w, (float)h, 0.0f, -1.0f, 1.0f);
    float mvp[16] = {
        ortho.m0,  ortho.m1,  ortho.m2,  ortho.m3,
        ortho.m4,  ortho.m5,  ortho.m6,  ortho.m7,
        ortho.m8,  ortho.m9,  ortho.m10, ortho.m11,
        ortho.m12, ortho.m13, ortho.m14, ortho.m15
    };

    VkCommandBuffer cb = VK.currentCmd;
    VkViewport viewport2D = {0, (float)h, (float)w, -(float)h, 0, 1};
    vkCmdSetViewport(cb, 0, 1, &viewport2D);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, VK.pipeline2D);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, VK.layout2D,
                            0, 1, &descriptorSet, 0, NULL);
    vkCmdPushConstants(cb, VK.layout2D, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(mvp), mvp);
    VkDeviceSize offset = VK.vbo2DOffset;
    vkCmdBindVertexBuffers(cb, 0, 1, &VK.vbo2D, &offset);
    vkCmdDraw(cb, (uint32_t)count, 1, 0, 0);
    statsCountDraw(count, 0);
    VK.vbo2DOffset += bytes;
}

void DrawRayTracedDemo(float timeSeconds) {
    if (!VK.currentCmd || !VK.rayTraceDemoPipeline || VK.inRT) return;
    extern void vshapes_FlushBatch(void);
    vshapes_FlushBatch();
    int width = VK.viewport2DW > 0 ? VK.viewport2DW : (int)VK.swapExtent.width;
    int height = VK.viewport2DH > 0 ? VK.viewport2DH : (int)VK.swapExtent.height;
    float params[3] = { timeSeconds, (float)width, (float)height };
    VkViewport viewport = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
    VkRect2D scissor = { { 0, 0 }, { (uint32_t)width, (uint32_t)height } };
    vkCmdSetViewport(VK.currentCmd, 0, 1, &viewport);
    vkCmdSetScissor(VK.currentCmd, 0, 1, &scissor);
    vkCmdBindPipeline(VK.currentCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, VK.rayTraceDemoPipeline);
    if (VK.rayQueryDemoReady && VK.rayDemoDescriptorSet)
        vkCmdBindDescriptorSets(VK.currentCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            VK.rayTraceDemoLayout, 0, 1, &VK.rayDemoDescriptorSet, 0, NULL);
    vkCmdPushConstants(VK.currentCmd, VK.rayTraceDemoLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(params), params);
    vkCmdDraw(VK.currentCmd, 3, 1, 0, 0);
}

bool vgraphics_IsHardwareRayTracingAvailable(void) {
    return VK.rayQueryDemoReady;
}

/* ================================================================== */
/*  Camera / matrices                                                 */
/* ================================================================== */
/* Camera / matrices
 *
 * Y orientation is compensated exactly ONCE, in MatrixPerspective(): it
 * negates Y (m5 = -f) because Vulkan framebuffer coordinates grow downward
 * while the world is Y-up. The viewport below must therefore have a POSITIVE
 * height (Vulkan's native orientation).
 *
 * This used to set {0, h, w, -h}. That flipped Y a second time, cancelling
 * the projection's compensation, so every 3D object rendered vertically
 * mirrored (floor grid with its near cells at the top, objects below the
 * horizon) while the 2D HUD stayed correct because it has its own separate
 * flip. Two wrongs did not make a right.
 */
void vgraphics_Set3DMatrices(Matrix view, Matrix proj) {
    if (VK.taaEnabled) {
        /* Sub-pixel camera jitter, compensated in the temporal resolve. */
        proj.m8 -= VK.taaJitterX;
        proj.m9 -= VK.taaJitterY;
    }
    VK.view3D = view;
    VK.proj3D = proj;
    if (VK.currentCmd != VK_NULL_HANDLE) {
        /* Use the active render target, not always the swapchain: inside
           BeginTextureMode() the target may be smaller than the window. */
        float w, h;
        if (VK.inRT && VK.viewport2DW > 0 && VK.viewport2DH > 0) {
            w = (float)VK.viewport2DW;
            h = (float)VK.viewport2DH;
        } else {
            int rw = 0, rh = 0;
            vgraphics_GetRenderTargetSize(&rw, &rh);
            w = rw > 0 ? (float)rw : (float)VK.swapExtent.width;
            h = rh > 0 ? (float)rh : (float)VK.swapExtent.height;
        }
        VkViewport viewport = {0, 0, w, h, 0, 1};
        vkCmdSetViewport(VK.currentCmd, 0, 1, &viewport);
    }
}

void vgraphics_SetCameraPos(Vector3 position) {
    VK.cameraPos = position;
}

void vgraphics_SetDirectionalLight(bool enabled, Vector3 direction, Color color, float intensity) {
    VK.lightDirectionalEnabled = enabled;
    VK.lightDirectionalDirection = Vector3Normalize(direction);
    VK.lightDirectionalColor = color;
    VK.lightDirectionalIntensity = intensity;
    VCpuSetDirectionalLight(enabled, direction, color, intensity);
}

void vgraphics_SetPointLight(bool enabled, Vector3 position, Color color, float intensity, float range) {
    VK.lightPointEnabled = enabled;
    VK.lightPointPosition = position;
    VK.lightPointColor = color;
    VK.lightPointIntensity = intensity;
    VK.lightPointRange = range;
}

void vgraphics_SetSpotLight(bool enabled, Vector3 position, Vector3 direction, Color color,
                            float intensity, float range, float innerCone, float outerCone) {
    VK.lightSpotEnabled = enabled;
    VK.lightSpotPosition = position;
    VK.lightSpotDirection = Vector3Normalize(direction);
    VK.lightSpotColor = color;
    VK.lightSpotIntensity = intensity;
    VK.lightSpotRange = range;
    VK.lightSpotInnerCone = innerCone;
    VK.lightSpotOuterCone = outerCone;
}

void vgraphics_SetAmbientLight(bool enabled, Color color, float intensity) {
    VK.lightAmbientEnabled = enabled;
    VK.lightAmbientColor = color;
    VK.lightAmbientIntensity = intensity;
    VCpuSetAmbientLight(enabled, color, intensity);
}

void vgraphics_SetAreaLight(bool enabled, Vector3 position, Vector3 normal,
                            float width, float height, Color color, float intensity) {
    VK.lightAreaEnabled = enabled;
    VK.lightAreaPosition = position;
    float len = sqrtf(normal.x*normal.x + normal.y*normal.y + normal.z*normal.z);
    if (len > 1e-6f) { normal.x/=len; normal.y/=len; normal.z/=len; }
    VK.lightAreaNormal = normal;
    VK.lightAreaWidth = width > 0.01f ? width : 0.01f;
    VK.lightAreaHeight = height > 0.01f ? height : 0.01f;
    VK.lightAreaColor = color;
    VK.lightAreaIntensity = intensity;
}

/* ------------------------------------------------------------------ */
/*  Explicit GPU light array (SSBO binding 16)                        */
/* ------------------------------------------------------------------ */

static void lightPackColor(float out[4], Color color, float intensity) {
    out[0] = (float)color.r / 255.0f * intensity;
    out[1] = (float)color.g / 255.0f * intensity;
    out[2] = (float)color.b / 255.0f * intensity;
    out[3] = 1.0f;
}

VLightData VLightDataDirectional(Vector3 direction, Color color, float intensity) {
    VLightData l;
    memset(&l, 0, sizeof(l));
    Vector3 d = Vector3Normalize(direction);
    l.direction[0] = d.x; l.direction[1] = d.y; l.direction[2] = d.z;
    lightPackColor(l.color, color, intensity);
    l.params[0] = (float)VLCR_LIGHT_TYPE_DIRECTIONAL;
    l.params[1] = 1.0f;
    l.params[2] = -1.0f;
    return l;
}

VLightData VLightDataPoint(Vector3 position, Color color, float intensity, float range) {
    VLightData l;
    memset(&l, 0, sizeof(l));
    l.position[0] = position.x; l.position[1] = position.y; l.position[2] = position.z;
    l.position[3] = range > 0.0f ? range : 10.0f;
    lightPackColor(l.color, color, intensity);
    l.params[0] = (float)VLCR_LIGHT_TYPE_POINT;
    l.params[1] = 1.0f;
    l.params[2] = -1.0f;
    return l;
}

VLightData VLightDataSpot(Vector3 position, Vector3 direction, Color color,
                          float intensity, float range, float innerCone, float outerCone) {
    VLightData l;
    memset(&l, 0, sizeof(l));
    Vector3 d = Vector3Normalize(direction);
    l.position[0] = position.x; l.position[1] = position.y; l.position[2] = position.z;
    l.position[3] = range > 0.0f ? range : 12.0f;
    l.direction[0] = d.x; l.direction[1] = d.y; l.direction[2] = d.z;
    l.direction[3] = cosf(innerCone * 0.5f);
    lightPackColor(l.color, color, intensity);
    l.color[3] = cosf(outerCone * 0.5f);
    l.params[0] = (float)VLCR_LIGHT_TYPE_SPOT;
    l.params[1] = 1.0f;
    l.params[2] = -1.0f;
    return l;
}

VLightData VLightDataArea(Vector3 position, Vector3 normal, float width, float height,
                          Color color, float intensity) {
    VLightData l;
    memset(&l, 0, sizeof(l));
    Vector3 n = Vector3Normalize(normal);
    l.position[0] = position.x; l.position[1] = position.y; l.position[2] = position.z;
    l.direction[0] = n.x; l.direction[1] = n.y; l.direction[2] = n.z;
    lightPackColor(l.color, color, intensity);
    l.params[0] = (float)VLCR_LIGHT_TYPE_AREA;
    l.params[1] = 1.0f;
    l.params[2] = -1.0f;
    l.params[3] = width > 0.01f ? width : 0.01f;
    l.extra[0] = height > 0.01f ? height : 0.01f;
    return l;
}

void vgraphics_SetLightArray(const VLightData *lights, int count) {
    if (!VK.lightBufferMapped) { VK.lightArrayCount = 0; return; }
    if (!lights || count <= 0) { vgraphics_ClearLights(); return; }
    if (count > VLCRLIB_MAX_GPU_LIGHTS) count = VLCRLIB_MAX_GPU_LIGHTS;
    memcpy(VK.lightBufferMapped, lights, (size_t)count * sizeof(VLightData));
    VK.lightArrayCount = count;
}

int vgraphics_AddLight(const VLightData *light) {
    if (!light || !VK.lightBufferMapped) return -1;
    if (VK.lightArrayCount >= VLCRLIB_MAX_GPU_LIGHTS) return -1;
    VLightData *dst = (VLightData *)VK.lightBufferMapped;
    dst[VK.lightArrayCount] = *light;
    return VK.lightArrayCount++;
}

void vgraphics_ClearLights(void) {
    if (VK.lightBufferMapped && VK.lightBufferSize > 0)
        memset(VK.lightBufferMapped, 0, (size_t)VK.lightBufferSize);
    VK.lightArrayCount = 0;
}

int vgraphics_GetLightCount(void) {
    return VK.lightArrayCount;
}

void vgraphics_SetFog(bool enabled, Color color, float start, float end, float density) {
    VK.fogEnabled = enabled;
    VK.fogColor = color;
    VK.fogStart = start;
    VK.fogEnd = end > start ? end : start + 0.001f;
    VK.fogDensity = density > 0.0f ? density : 0.0f;
}


/* ================================================================== */
/*  Directional shadow map                                            */
/* ================================================================== */
static void destroySpotShadowResources(void) {
    if (!VK.device) return;
    if (VK.spotShadowFB) { vkDestroyFramebuffer(VK.device, VK.spotShadowFB, NULL); VK.spotShadowFB = VK_NULL_HANDLE; }
    if (VK.spotShadowSampler) { vkDestroySampler(VK.device, VK.spotShadowSampler, NULL); VK.spotShadowSampler = VK_NULL_HANDLE; }
    if (VK.spotShadowView) { vkDestroyImageView(VK.device, VK.spotShadowView, NULL); VK.spotShadowView = VK_NULL_HANDLE; }
    if (VK.spotShadowImage) { vkDestroyImage(VK.device, VK.spotShadowImage, NULL); VK.spotShadowImage = VK_NULL_HANDLE; }
    if (VK.spotShadowMem) { vkFreeMemory(VK.device, VK.spotShadowMem, NULL); VK.spotShadowMem = VK_NULL_HANDLE; }
    VK.spotShadowReady = false;
}

static void destroyPointShadowResources(void) {
    if (!VK.device) return;
    if (VK.pointShadowPipeline) { vkDestroyPipeline(VK.device, VK.pointShadowPipeline, NULL); VK.pointShadowPipeline = VK_NULL_HANDLE; }
    if (VK.pointShadowLayout) { vkDestroyPipelineLayout(VK.device, VK.pointShadowLayout, NULL); VK.pointShadowLayout = VK_NULL_HANDLE; }
    if (VK.pointShadowPass) { vkDestroyRenderPass(VK.device, VK.pointShadowPass, NULL); VK.pointShadowPass = VK_NULL_HANDLE; }
    if (VK.pointShadowSampler) { vkDestroySampler(VK.device, VK.pointShadowSampler, NULL); VK.pointShadowSampler = VK_NULL_HANDLE; }
    if (VK.pointShadowView) { vkDestroyImageView(VK.device, VK.pointShadowView, NULL); VK.pointShadowView = VK_NULL_HANDLE; }
    for (int i = 0; i < 6; ++i) {
        if (VK.pointShadowFB[i]) { vkDestroyFramebuffer(VK.device, VK.pointShadowFB[i], NULL); VK.pointShadowFB[i] = VK_NULL_HANDLE; }
        if (VK.pointShadowFaceViews[i]) { vkDestroyImageView(VK.device, VK.pointShadowFaceViews[i], NULL); VK.pointShadowFaceViews[i] = VK_NULL_HANDLE; }
        if (VK.pointShadowDepthFaceViews[i]) { vkDestroyImageView(VK.device, VK.pointShadowDepthFaceViews[i], NULL); VK.pointShadowDepthFaceViews[i] = VK_NULL_HANDLE; }
    }
    if (VK.pointShadowImage) { vkDestroyImage(VK.device, VK.pointShadowImage, NULL); VK.pointShadowImage = VK_NULL_HANDLE; }
    if (VK.pointShadowMem) { vkFreeMemory(VK.device, VK.pointShadowMem, NULL); VK.pointShadowMem = VK_NULL_HANDLE; }
    if (VK.pointShadowDepthImage) { vkDestroyImage(VK.device, VK.pointShadowDepthImage, NULL); VK.pointShadowDepthImage = VK_NULL_HANDLE; }
    if (VK.pointShadowDepthMem) { vkFreeMemory(VK.device, VK.pointShadowDepthMem, NULL); VK.pointShadowDepthMem = VK_NULL_HANDLE; }
    VK.pointShadowReady = false;
}

static void destroyShadowResources(void) {
    if (!VK.device) return;
    if (VK.shadowPipeline) { vkDestroyPipeline(VK.device, VK.shadowPipeline, NULL); VK.shadowPipeline = VK_NULL_HANDLE; }
    if (VK.shadowLayout)   { vkDestroyPipelineLayout(VK.device, VK.shadowLayout, NULL); VK.shadowLayout = VK_NULL_HANDLE; }
    if (VK.shadowFB)       { vkDestroyFramebuffer(VK.device, VK.shadowFB, NULL); VK.shadowFB = VK_NULL_HANDLE; }
    if (VK.shadowPass)     { vkDestroyRenderPass(VK.device, VK.shadowPass, NULL); VK.shadowPass = VK_NULL_HANDLE; }
    if (VK.shadowSampler)  { vkDestroySampler(VK.device, VK.shadowSampler, NULL); VK.shadowSampler = VK_NULL_HANDLE; }
    if (VK.shadowView)     { vkDestroyImageView(VK.device, VK.shadowView, NULL); VK.shadowView = VK_NULL_HANDLE; }
    if (VK.shadowImage)    { vkDestroyImage(VK.device, VK.shadowImage, NULL); VK.shadowImage = VK_NULL_HANDLE; }
    if (VK.shadowMem)      { vkFreeMemory(VK.device, VK.shadowMem, NULL); VK.shadowMem = VK_NULL_HANDLE; }
    VK.shadowResourcesReady = false;
    destroySpotShadowResources();
    destroyPointShadowResources();
}

static bool createShadowResources(int mapSize) {
    if (mapSize < 256) mapSize = 1024;
    if (mapSize > 4096) mapSize = 4096;
    if (VK.device) vkDeviceWaitIdle(VK.device);
    destroyShadowResources();
    VK.shadowMapSize = mapSize;
    uint32_t sz = (uint32_t)mapSize;

    if (!createImage(sz, sz, VK_FORMAT_D32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                     VK_IMAGE_USAGE_SAMPLED_BIT |
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     &VK.shadowImage, &VK.shadowMem)) {
        fprintf(stderr, "[vlcrlib] shadow depth image failed\n");
        return false;
    }

    VkImageViewCreateInfo vi = {0};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = VK.shadowImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_D32_SFLOAT;
    vi.subresourceRange = (VkImageSubresourceRange){
        VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(VK.device, &vi, NULL, &VK.shadowView) != VK_SUCCESS)
        return false;

    /* Initialise the shadow map to "fully lit" (depth 1) and put it in the
       sampled layout. Otherwise the first frame samples an UNDEFINED image
       before recordShadowPass has had a chance to run. */
    {
        VkCommandBuffer cb = ibl_begin_cmd();
        VkImageMemoryBarrier b = {0};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = VK.shadowImage;
        b.subresourceRange = (VkImageSubresourceRange){
            VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, NULL, 0, NULL, 1, &b);

        VkClearDepthStencilValue clear = { 1.0f, 0 };
        VkImageSubresourceRange range = {
            VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
        vkCmdClearDepthStencilImage(cb, VK.shadowImage,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    &clear, 1, &range);

        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, NULL, 0, NULL, 1, &b);
        ibl_end_cmd(cb);
    }

    VkSamplerCreateInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_NEAREST;
    si.minFilter = VK_FILTER_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    si.compareEnable = VK_FALSE;
    if (vkCreateSampler(VK.device, &si, NULL, &VK.shadowSampler) != VK_SUCCESS)
        return false;

    /* Depth-only render pass */
    VkAttachmentDescription att = {0};
    att.format = VK_FORMAT_D32_SFLOAT;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthRef = { 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub = {0};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency deps[2] = {0};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rpci = {0};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments = &att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    rpci.dependencyCount = 2;
    rpci.pDependencies = deps;
    if (vkCreateRenderPass(VK.device, &rpci, NULL, &VK.shadowPass) != VK_SUCCESS)
        return false;

    VkFramebufferCreateInfo fbci = {0};
    fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbci.renderPass = VK.shadowPass;
    fbci.attachmentCount = 1;
    fbci.pAttachments = &VK.shadowView;
    fbci.width = sz;
    fbci.height = sz;
    fbci.layers = 1;
    if (vkCreateFramebuffer(VK.device, &fbci, NULL, &VK.shadowFB) != VK_SUCCESS)
        return false;

    /* Pipeline layout: push constant mat4 */
    VkPushConstantRange pcr = {0};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcr.offset = 0;
    pcr.size = 64; /* mat4 */

    VkPipelineLayoutCreateInfo plci = {0};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(VK.device, &plci, NULL, &VK.shadowLayout) != VK_SUCCESS)
        return false;

    VkShaderModule vert = createShaderModule("shaders/shadow.vert.spv");
    if (!vert) {
        /* try alternate path */
        vert = createShaderModule("shadow.vert.spv");
    }
    if (!vert) {
        fprintf(stderr, "[vlcrlib] shadow.vert.spv missing ??? compile shaders/shadow.vert\n");
        destroyShadowResources();
        return false;
    }

    VkPipelineShaderStageCreateInfo stage = {0};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    stage.module = vert;
    stage.pName = "main";

    VkVertexInputBindingDescription bind = {0};
    bind.binding = 0;
    bind.stride = sizeof(Vertex3D);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attr = {0};
    attr.location = 0;
    attr.binding = 0;
    attr.format = VK_FORMAT_R32G32B32_SFLOAT;
    attr.offset = offsetof(Vertex3D, x);

    VkPipelineVertexInputStateCreateInfo vi_state = {0};
    vi_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi_state.vertexBindingDescriptionCount = 1;
    vi_state.pVertexBindingDescriptions = &bind;
    vi_state.vertexAttributeDescriptionCount = 1;
    vi_state.pVertexAttributeDescriptions = &attr;

    VkPipelineInputAssemblyStateCreateInfo ia = {0};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp = {0};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs = {0};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.depthBiasEnable = VK_TRUE;
    rs.depthBiasConstantFactor = 1.25f;
    rs.depthBiasSlopeFactor = 1.75f;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {0};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds = {0};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendStateCreateInfo cb = {0};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 0; /* depth-only */

    VkDynamicState dynStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dyn = {0};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;

    VkGraphicsPipelineCreateInfo pci = {0};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 1;
    pci.pStages = &stage;
    pci.pVertexInputState = &vi_state;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &ds;
    pci.pColorBlendState = &cb;
    pci.pDynamicState = &dyn;
    pci.layout = VK.shadowLayout;
    pci.renderPass = VK.shadowPass;
    pci.subpass = 0;

    VkResult pr = vkCreateGraphicsPipelines(VK.device, VK_NULL_HANDLE, 1, &pci, NULL, &VK.shadowPipeline);
    vkDestroyShaderModule(VK.device, vert, NULL);
    if (pr != VK_SUCCESS) {
        fprintf(stderr, "[vlcrlib] shadow pipeline failed: %d\n", (int)pr);
        destroyShadowResources();
        return false;
    }

    VK.shadowResourcesReady = true;
    fprintf(stderr, "[vlcrlib] shadow map ready %dx%d\n", mapSize, mapSize);
    return true;
}

static void shadow_draw_model_depth(unsigned int modelId, Vector3 position,
                                    Vector3 rotation, Vector3 scale) {
    if (!VK.shadowPipeline || modelId == 0 || modelId >= (unsigned)VLCRLIB_MAX_MODELS) return;
    ModelEntry *e = &g_modelTable[modelId];
    if (!e->meshes) return;

    Matrix T = MatrixTranslate(position.x, position.y, position.z);
    Matrix R = MatrixRotateXYZ(rotation);
    Matrix S = MatrixScale(scale.x, scale.y, scale.z);
    Matrix top = MatrixMultiply(T, MatrixMultiply(R, S));
    Matrix lightVP = VK.shadowLightVP;

    VkCommandBuffer cb = VK.currentCmd;
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, VK.shadowPipeline);

    for (int i = 0; i < e->meshCount; ++i) {
        Mesh mesh = e->meshes[i];
        if (!meshHandleValid(mesh)) continue;
        MeshEntry *me = &g_meshTable[mesh.id];
        if (!me->vbo) continue;

        Matrix nodeWorld = (e->meshTransforms) ? e->meshTransforms[i] : MatrixIdentity();
        Matrix M = MatrixMultiply(top, nodeWorld);
        Matrix lightMVP = MatrixMultiply(lightVP, M);

        float pc[16];
        vlcr_matrixToGpu(pc, lightMVP);
        vkCmdPushConstants(cb, VK.shadowLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, pc);

        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &me->vbo, &off);
        vkCmdBindIndexBuffer(cb, me->ibo, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, me->indexCount, 1, 0, 0, 0);
        statsCountShadowDraw(me->indexCount);
    }
}


static bool createSpotShadowResources(int mapSize) {
    if (mapSize < 256) mapSize = 1024;
    if (mapSize > 4096) mapSize = 4096;
    if (VK.device) vkDeviceWaitIdle(VK.device);
    destroySpotShadowResources();
    if (!VK.shadowPass || !VK.shadowPipeline) {
        fprintf(stderr, "[vlcrlib] spot shadows need directional shadow pipeline first (SetShadows)\n");
        /* create dir shadow resources at same size if missing */
        if (!createShadowResources(mapSize)) return false;
    }
    VK.spotShadowMapSize = mapSize;
    uint32_t sz = (uint32_t)mapSize;
    if (!createImage(sz, sz, VK_FORMAT_D32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     &VK.spotShadowImage, &VK.spotShadowMem)) return false;
    VkImageViewCreateInfo vi = {0};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = VK.spotShadowImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_D32_SFLOAT;
    vi.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(VK.device, &vi, NULL, &VK.spotShadowView) != VK_SUCCESS) return false;
    VkSamplerCreateInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    /* One nearest depth tap per PCF direction. Filtering raw depths before
     * comparison causes light leaks; PCF averages comparison results instead. */
    si.magFilter = si.minFilter = VK_FILTER_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    if (vkCreateSampler(VK.device, &si, NULL, &VK.spotShadowSampler) != VK_SUCCESS) return false;
    VkFramebufferCreateInfo fbci = {0};
    fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbci.renderPass = VK.shadowPass;
    fbci.attachmentCount = 1;
    fbci.pAttachments = &VK.spotShadowView;
    fbci.width = fbci.height = sz;
    fbci.layers = 1;
    if (vkCreateFramebuffer(VK.device, &fbci, NULL, &VK.spotShadowFB) != VK_SUCCESS) return false;
    VK.spotShadowReady = true;
    fprintf(stderr, "[vlcrlib] spot shadow map ready %dx%d\n", mapSize, mapSize);
    return true;
}

static bool createPointShadowResources(int mapSize) {
    if (mapSize < 128) mapSize = 512;
    if (mapSize > 2048) mapSize = 2048;
    if (VK.device) vkDeviceWaitIdle(VK.device);
    destroyPointShadowResources();
    VK.pointShadowMapSize = mapSize;
    uint32_t sz = (uint32_t)mapSize;

    /* Cube image R32_SFLOAT for linear depth */
    VkImageCreateInfo ci = {0};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.extent = (VkExtent3D){ sz, sz, 1 };
    ci.mipLevels = 1;
    ci.arrayLayers = 6;
    ci.format = VK_FORMAT_R32_SFLOAT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    if (vkCreateImage(VK.device, &ci, NULL, &VK.pointShadowImage) != VK_SUCCESS) return false;
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(VK.device, VK.pointShadowImage, &mr);
    VkMemoryAllocateInfo ai = {0};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = vlcr_findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(VK.device, &ai, NULL, &VK.pointShadowMem) != VK_SUCCESS) return false;
    vkBindImageMemory(VK.device, VK.pointShadowImage, VK.pointShadowMem, 0);

    for (int face = 0; face < 6; ++face) {
        VkImageViewCreateInfo vi = {0};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = VK.pointShadowImage;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R32_SFLOAT;
        vi.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, (uint32_t)face, 1 };
        if (vkCreateImageView(VK.device, &vi, NULL, &VK.pointShadowFaceViews[face]) != VK_SUCCESS)
            return false;
    }
    /* A point-shadow cube needs a depth test while rasterizing each face;
     * otherwise later draw calls overwrite nearer radial-depth fragments. */
    VkImageCreateInfo depthCi = {0};
    depthCi.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthCi.imageType = VK_IMAGE_TYPE_2D;
    depthCi.extent = (VkExtent3D){ sz, sz, 1 };
    depthCi.mipLevels = 1;
    depthCi.arrayLayers = 6;
    depthCi.format = VK_FORMAT_D32_SFLOAT;
    depthCi.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthCi.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthCi.samples = VK_SAMPLE_COUNT_1_BIT;
    depthCi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    depthCi.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(VK.device, &depthCi, NULL, &VK.pointShadowDepthImage) != VK_SUCCESS) return false;
    VkMemoryRequirements depthMr;
    vkGetImageMemoryRequirements(VK.device, VK.pointShadowDepthImage, &depthMr);
    uint32_t depthMt = vlcr_findMemoryType(depthMr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (depthMt == UINT32_MAX) return false;
    VkMemoryAllocateInfo depthAi = {0};
    depthAi.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    depthAi.allocationSize = depthMr.size;
    depthAi.memoryTypeIndex = depthMt;
    if (vkAllocateMemory(VK.device, &depthAi, NULL, &VK.pointShadowDepthMem) != VK_SUCCESS) return false;
    if (vkBindImageMemory(VK.device, VK.pointShadowDepthImage, VK.pointShadowDepthMem, 0) != VK_SUCCESS) return false;
    for (int face = 0; face < 6; ++face) {
        VkImageViewCreateInfo dvi = {0};
        dvi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        dvi.image = VK.pointShadowDepthImage;
        dvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        dvi.format = VK_FORMAT_D32_SFLOAT;
        dvi.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, (uint32_t)face, 1 };
        if (vkCreateImageView(VK.device, &dvi, NULL, &VK.pointShadowDepthFaceViews[face]) != VK_SUCCESS) return false;
    }
    {
        VkImageViewCreateInfo vi = {0};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = VK.pointShadowImage;
        vi.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        vi.format = VK_FORMAT_R32_SFLOAT;
        vi.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 };
        if (vkCreateImageView(VK.device, &vi, NULL, &VK.pointShadowView) != VK_SUCCESS) return false;
    }
    {
        VkSamplerCreateInfo si = {0};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = si.minFilter = VK_FILTER_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(VK.device, &si, NULL, &VK.pointShadowSampler) != VK_SUCCESS) return false;
    }

    /* Radial depth color plus a hardware depth attachment for nearest-hit selection. */
    VkAttachmentDescription att[2] = {0};
    att[0].format = VK_FORMAT_R32_SFLOAT;
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    att[1].format = VK_FORMAT_D32_SFLOAT;
    att[1].samples = VK_SAMPLE_COUNT_1_BIT;
    att[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub = {0};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;
    sub.pDepthStencilAttachment = &depthRef;
    VkSubpassDependency deps[2] = {0};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo rpci = {0};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 2; rpci.pAttachments = att;
    rpci.subpassCount = 1; rpci.pSubpasses = &sub;
    rpci.dependencyCount = 2; rpci.pDependencies = deps;
    if (vkCreateRenderPass(VK.device, &rpci, NULL, &VK.pointShadowPass) != VK_SUCCESS) return false;

    for (int face = 0; face < 6; ++face) {
        VkFramebufferCreateInfo fbci = {0};
        fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass = VK.pointShadowPass;
        VkImageView attachments[2] = { VK.pointShadowFaceViews[face], VK.pointShadowDepthFaceViews[face] };
        fbci.attachmentCount = 2;
        fbci.pAttachments = attachments;
        fbci.width = fbci.height = sz;
        fbci.layers = 1;
        if (vkCreateFramebuffer(VK.device, &fbci, NULL, &VK.pointShadowFB[face]) != VK_SUCCESS)
            return false;
    }

    /* Push: mat4 lightMVP + mat4 model + vec4 lightPosFar = 144 bytes */
    VkPushConstantRange pcr = {0};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.size = 144;
    VkPipelineLayoutCreateInfo plci = {0};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(VK.device, &plci, NULL, &VK.pointShadowLayout) != VK_SUCCESS)
        return false;

    VkShaderModule vert = createShaderModule("shaders/shadow_point.vert.spv");
    VkShaderModule frag = createShaderModule("shaders/shadow_point.frag.spv");
    if (!vert) vert = createShaderModule("shadow_point.vert.spv");
    if (!frag) frag = createShaderModule("shadow_point.frag.spv");
    if (!vert || !frag) {
        fprintf(stderr, "[vlcrlib] shadow_point shaders missing ??? compile shadow_point.vert/frag\n");
        if (vert) vkDestroyShaderModule(VK.device, vert, NULL);
        if (frag) vkDestroyShaderModule(VK.device, frag, NULL);
        destroyPointShadowResources();
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {0};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vert; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = frag; stages[1].pName = "main";

    VkVertexInputBindingDescription bind = {0};
    bind.binding = 0; bind.stride = sizeof(Vertex3D); bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attr = {0};
    attr.location = 0; attr.binding = 0; attr.format = VK_FORMAT_R32G32B32_SFLOAT;
    attr.offset = offsetof(Vertex3D, x);
    VkPipelineVertexInputStateCreateInfo vi_state = {0};
    vi_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi_state.vertexBindingDescriptionCount = 1; vi_state.pVertexBindingDescriptions = &bind;
    vi_state.vertexAttributeDescriptionCount = 1; vi_state.pVertexAttributeDescriptions = &attr;
    VkPipelineInputAssemblyStateCreateInfo ia = {0};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = {0};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = {0};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = {0};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds = {0};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState cba = {0};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    VkPipelineColorBlendStateCreateInfo cb = {0};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &cba;
    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn = {0};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;

    VkGraphicsPipelineCreateInfo pci = {0};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2; pci.pStages = stages;
    pci.pVertexInputState = &vi_state;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &ds;
    pci.pColorBlendState = &cb;
    pci.pDynamicState = &dyn;
    pci.layout = VK.pointShadowLayout;
    pci.renderPass = VK.pointShadowPass;

    VkResult pr = vkCreateGraphicsPipelines(VK.device, VK_NULL_HANDLE, 1, &pci, NULL, &VK.pointShadowPipeline);
    vkDestroyShaderModule(VK.device, vert, NULL);
    vkDestroyShaderModule(VK.device, frag, NULL);
    if (pr != VK_SUCCESS) {
        fprintf(stderr, "[vlcrlib] point shadow pipeline failed\n");
        destroyPointShadowResources();
        return false;
    }
    VK.pointShadowReady = true;
    fprintf(stderr, "[vlcrlib] point shadow cube ready %dx%d x6\n", mapSize, mapSize);
    return true;
}

static void shadow_draw_model_depth_to(VkCommandBuffer cb, Matrix lightVP,
                                       unsigned int modelId, Vector3 position,
                                       Vector3 rotation, Vector3 scale) {
    if (!VK.shadowPipeline || modelId == 0 || modelId >= (unsigned)VLCRLIB_MAX_MODELS) return;
    ModelEntry *e = &g_modelTable[modelId];
    if (!e->meshes) return;
    Matrix T = MatrixTranslate(position.x, position.y, position.z);
    Matrix R = MatrixRotateXYZ(rotation);
    Matrix S = MatrixScale(scale.x, scale.y, scale.z);
    Matrix top = MatrixMultiply(T, MatrixMultiply(R, S));
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, VK.shadowPipeline);
    for (int i = 0; i < e->meshCount; ++i) {
        Mesh mesh = e->meshes[i];
        if (!meshHandleValid(mesh)) continue;
        MeshEntry *me = &g_meshTable[mesh.id];
        if (!me->vbo) continue;
        Matrix nodeWorld = e->meshTransforms ? e->meshTransforms[i] : MatrixIdentity();
        Matrix M = MatrixMultiply(top, nodeWorld);
        Matrix lightMVP = MatrixMultiply(lightVP, M);
        float pc[16];
        vlcr_matrixToGpu(pc, lightMVP);
        vkCmdPushConstants(cb, VK.shadowLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, pc);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &me->vbo, &off);
        vkCmdBindIndexBuffer(cb, me->ibo, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, me->indexCount, 1, 0, 0, 0);
        statsCountShadowDraw(me->indexCount);
    }
}

static void shadow_draw_model_point_face(VkCommandBuffer cb, Matrix lightMVP,
                                         Vector3 lightPos, float farPlane,
                                         unsigned int modelId, Vector3 position,
                                         Vector3 rotation, Vector3 scale) {
    if (!VK.pointShadowPipeline || modelId == 0 || modelId >= (unsigned)VLCRLIB_MAX_MODELS) return;
    ModelEntry *e = &g_modelTable[modelId];
    if (!e->meshes) return;
    Matrix T = MatrixTranslate(position.x, position.y, position.z);
    Matrix R = MatrixRotateXYZ(rotation);
    Matrix S = MatrixScale(scale.x, scale.y, scale.z);
    Matrix top = MatrixMultiply(T, MatrixMultiply(R, S));
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, VK.pointShadowPipeline);
    for (int i = 0; i < e->meshCount; ++i) {
        Mesh mesh = e->meshes[i];
        if (!meshHandleValid(mesh)) continue;
        MeshEntry *me = &g_meshTable[mesh.id];
        if (!me->vbo) continue;
        Matrix nodeWorld = e->meshTransforms ? e->meshTransforms[i] : MatrixIdentity();
        Matrix M = MatrixMultiply(top, nodeWorld);
        Matrix faceMVP = MatrixMultiply(lightMVP, M);
        float pc[36]; /* 2*mat4 + vec4 = 36 floats */
        vlcr_matrixToGpu(pc, faceMVP);
        vlcr_matrixToGpu(pc + 16, M);
        pc[32] = lightPos.x; pc[33] = lightPos.y; pc[34] = lightPos.z; pc[35] = farPlane;
        vkCmdPushConstants(cb, VK.pointShadowLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 144, pc);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &me->vbo, &off);
        vkCmdBindIndexBuffer(cb, me->ibo, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, me->indexCount, 1, 0, 0, 0);
        statsCountShadowDraw(me->indexCount);
    }
}

static void shadow_draw_direct_mesh_depth(VkCommandBuffer cb, Matrix lightVP,
                                          unsigned int meshId, unsigned int generation,
                                          Matrix model) {
    if (!VK.shadowPipeline || meshId == 0 || meshId >= VLCRLIB_MAX_MESHES) return;
    MeshEntry *me = &g_meshTable[meshId];
    if (!me->vbo || !generation || generation != me->generation) return;
    Matrix lightMVP = MatrixMultiply(lightVP, model);
    float pc[16];
    vlcr_matrixToGpu(pc, lightMVP);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, VK.shadowPipeline);
    vkCmdPushConstants(cb, VK.shadowLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, pc);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &me->vbo, &off);
    vkCmdBindIndexBuffer(cb, me->ibo, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, me->indexCount, 1, 0, 0, 0);
    statsCountShadowDraw(me->indexCount);
}

static void shadow_draw_caster_depth(VkCommandBuffer cb, Matrix lightVP,
                                     const struct VlcrShadowCaster *caster) {
    if (caster->meshId) {
        shadow_draw_direct_mesh_depth(cb, lightVP, caster->meshId,
                                      caster->meshGeneration, caster->transform);
    } else {
        shadow_draw_model_depth_to(cb, lightVP, caster->modelId, caster->position,
                                   caster->rotation, caster->scale);
    }
}

static void shadow_draw_direct_mesh_point_face(VkCommandBuffer cb, Matrix lightVP,
                                               Vector3 lightPos, float farPlane,
                                               unsigned int meshId, unsigned int generation,
                                               Matrix model) {
    if (!VK.pointShadowPipeline || meshId == 0 || meshId >= VLCRLIB_MAX_MESHES) return;
    MeshEntry *me = &g_meshTable[meshId];
    if (!me->vbo || !generation || generation != me->generation) return;
    Matrix faceMVP = MatrixMultiply(lightVP, model);
    float pc[36];
    vlcr_matrixToGpu(pc, faceMVP);
    vlcr_matrixToGpu(pc + 16, model);
    pc[32] = lightPos.x; pc[33] = lightPos.y; pc[34] = lightPos.z; pc[35] = farPlane;
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, VK.pointShadowPipeline);
    vkCmdPushConstants(cb, VK.pointShadowLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 144, pc);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &me->vbo, &off);
    vkCmdBindIndexBuffer(cb, me->ibo, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, me->indexCount, 1, 0, 0, 0);
    statsCountShadowDraw(me->indexCount);
}

static void shadow_draw_caster_point_face(VkCommandBuffer cb, Matrix lightVP,
                                          Vector3 lightPos, float farPlane,
                                          const struct VlcrShadowCaster *caster) {
    if (caster->meshId) {
        shadow_draw_direct_mesh_point_face(cb, lightVP, lightPos, farPlane,
                                           caster->meshId, caster->meshGeneration,
                                           caster->transform);
    } else {
        shadow_draw_model_point_face(cb, lightVP, lightPos, farPlane,
                                     caster->modelId, caster->position,
                                     caster->rotation, caster->scale);
    }
}



static void recordShadowPass(VkCommandBuffer cb) {
    if (VK.shadowCasterCountPrev <= 0) return;
    VK.framePhase = VLCR_PHASE_SHADOW;

    /* ---- Directional ---- */
    if (VK.shadowsEnabled && VK.shadowResourcesReady) {
        /* The matrix follows the camera; 12 units was too tight for a scene
           viewed from 18 units away (everything fell outside the map). */
        vgraphics_ComputeDirectionalShadowMatrix(30.0f, 0.5f, 100.0f);
        VkClearValue clear;
        clear.depthStencil.depth = 1.0f;
        clear.depthStencil.stencil = 0;
        VkRenderPassBeginInfo rp = {0};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = VK.shadowPass;
        rp.framebuffer = VK.shadowFB;
        rp.renderArea.extent.width = (uint32_t)VK.shadowMapSize;
        rp.renderArea.extent.height = (uint32_t)VK.shadowMapSize;
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;
        vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);
        if (getenv("VLCR_CSM_OFF")) {
            VK.shadowCascadeCount = 0;
        } else {
            vgraphics_ComputeDirectionalShadowCascades();
        }
        uint32_t half = (uint32_t)VK.shadowMapSize / 2;
        if (VK.shadowCascadeCount <= 0) {
            VkViewport vp = { 0, 0, (float)VK.shadowMapSize, (float)VK.shadowMapSize, 0.f, 1.f };
            vkCmdSetViewport(cb, 0, 1, &vp);
            VkRect2D sc = { {0, 0}, { (uint32_t)VK.shadowMapSize, (uint32_t)VK.shadowMapSize } };
            vkCmdSetScissor(cb, 0, 1, &sc);
            for (int i = 0; i < VK.shadowCasterCountPrev; ++i)
                shadow_draw_caster_depth(cb, VK.shadowLightVP, &VK.shadowCastersPrev[i]);
        } else {
            for (int c = 0; c < VK.shadowCascadeCount; ++c) {
                int qx = (c & 1) * (int)half;
                int qy = (c >> 1) * (int)half;
                VkViewport vp = { (float)qx, (float)qy, (float)half, (float)half, 0.f, 1.f };
                vkCmdSetViewport(cb, 0, 1, &vp);
                VkRect2D sc = { { qx, qy }, { half, half } };
                vkCmdSetScissor(cb, 0, 1, &sc);
                for (int i = 0; i < VK.shadowCasterCountPrev; ++i) {
                    shadow_draw_caster_depth(cb, VK.shadowCascadeVP[c],
                                             &VK.shadowCastersPrev[i]);
                }
            }
        }
        vkCmdEndRenderPass(cb);
    }

    /* ---- Spot (perspective depth map) ---- */
    if (VK.spotShadowsEnabled && VK.spotShadowReady && VK.lightSpotEnabled) {
        vgraphics_ComputeSpotShadowMatrix(0.1f, VK.lightSpotRange > 0.f ? VK.lightSpotRange : 40.f);
        VkClearValue clear;
        clear.depthStencil.depth = 1.0f;
        clear.depthStencil.stencil = 0;
        VkRenderPassBeginInfo rp = {0};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = VK.shadowPass;
        rp.framebuffer = VK.spotShadowFB;
        rp.renderArea.extent.width = (uint32_t)VK.spotShadowMapSize;
        rp.renderArea.extent.height = (uint32_t)VK.spotShadowMapSize;
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;
        vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp = { 0, 0, (float)VK.spotShadowMapSize, (float)VK.spotShadowMapSize, 0.f, 1.f };
        vkCmdSetViewport(cb, 0, 1, &vp);
        VkRect2D sc = { {0, 0}, { (uint32_t)VK.spotShadowMapSize, (uint32_t)VK.spotShadowMapSize } };
        vkCmdSetScissor(cb, 0, 1, &sc);
        for (int i = 0; i < VK.shadowCasterCountPrev; ++i) {
            shadow_draw_caster_depth(cb, VK.spotShadowLightVP, &VK.shadowCastersPrev[i]);
        }
        vkCmdEndRenderPass(cb);
    }

    /* ---- Point (6 cube faces, linear depth) ---- */
    if (VK.pointShadowsEnabled && VK.pointShadowReady && VK.lightPointEnabled) {
        Vector3 pos = VK.lightPointPosition;
        float farP = VK.pointShadowFar > 0.f ? VK.pointShadowFar
                   : (VK.lightPointRange > 0.f ? VK.lightPointRange : 25.f);
        float nearP = 0.1f;
        /* 90 FOV perspective for cube faces */
        Matrix proj = MatrixPerspective(90.0f * 3.14159265f / 180.0f, 1.0f, nearP, farP);
        Vector3 targets[6] = {
            { pos.x+1, pos.y, pos.z }, { pos.x-1, pos.y, pos.z },
            { pos.x, pos.y+1, pos.z }, { pos.x, pos.y-1, pos.z },
            { pos.x, pos.y, pos.z+1 }, { pos.x, pos.y, pos.z-1 }
        };
        Vector3 ups[6] = {
            {0,-1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}, {0,-1,0}, {0,-1,0}
        };
        uint32_t sz = (uint32_t)VK.pointShadowMapSize;
        for (int face = 0; face < 6; ++face) {
            Matrix view = MatrixLookAt(pos, targets[face], ups[face]);
            Matrix lightVP = MatrixMultiply(proj, view);
            VkClearValue clear[2] = {0};
            clear[0].color.float32[0] = 1.0f;
            clear[0].color.float32[1] = clear[0].color.float32[2] = clear[0].color.float32[3] = 1.0f;
            clear[1].depthStencil.depth = 1.0f;
            VkRenderPassBeginInfo rp = {0};
            rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rp.renderPass = VK.pointShadowPass;
            rp.framebuffer = VK.pointShadowFB[face];
            rp.renderArea.extent.width = sz;
            rp.renderArea.extent.height = sz;
            rp.clearValueCount = 2;
            rp.pClearValues = clear;
            vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);
            VkViewport vp = { 0, 0, (float)sz, (float)sz, 0.f, 1.f };
            vkCmdSetViewport(cb, 0, 1, &vp);
            VkRect2D sc = { {0, 0}, { sz, sz } };
            vkCmdSetScissor(cb, 0, 1, &sc);
            for (int i = 0; i < VK.shadowCasterCountPrev; ++i) {
                shadow_draw_caster_point_face(cb, lightVP, pos, farP,
                                              &VK.shadowCastersPrev[i]);
            }
            vkCmdEndRenderPass(cb);
        }
    }
}


void vgraphics_SetShadows(bool enabled, int mapSize, float bias, float strength) {
    if (vcore_IsCpuBackend()) {
        VCpuSetShadows(enabled, mapSize >= 128 ? mapSize : 512, bias, strength);
        return;
    }
    VK.shadowsEnabled = enabled;
    int requested = mapSize >= 256 ? mapSize : VK.shadowMapSize;
    bool sizeChanged = requested != VK.shadowMapSize;
    VK.shadowMapSize = requested;
    VK.shadowBias = bias > 0.f ? bias : 0.005f;
    VK.shadowStrength = strength;
    if (enabled) {
        if (!VK.shadowResourcesReady || sizeChanged) {
            if (!createShadowResources(VK.shadowMapSize)) {
                fprintf(stderr, "[vlcrlib] SetShadows: resource create failed\n");
                VK.shadowsEnabled = false;
            } else {
                ibl_refresh_all_material_descriptors();
            }
        }
        vgraphics_ComputeDirectionalShadowMatrix(12.0f, 0.5f, 80.0f);
    }
}

void vgraphics_SetShadowSoftness(float radius) {
    if (vcore_IsCpuBackend()) {
        VCpuSetShadowSoftness(radius);
        return;
    }
    VK.shadowSoftness = radius < 1.f ? 1.f : (radius > 2.f ? 2.f : radius);
}

void vgraphics_SetSpotShadows(bool enabled, int mapSize, float bias, float strength) {
    VK.spotShadowsEnabled = enabled;
    int spotRequested = mapSize >= 256 ? mapSize : VK.spotShadowMapSize;
    bool spotSizeChanged = spotRequested != VK.spotShadowMapSize;
    VK.spotShadowMapSize = spotRequested;
    VK.spotShadowBias = bias > 0.f ? bias : 0.004f;
    VK.spotShadowStrength = strength;
    VK.spotShadowSoftness = VK.shadowSoftness > 0.f ? VK.shadowSoftness : 1.5f;
    if (enabled) {
        if (!VK.spotShadowReady || spotSizeChanged) {
            if (!createSpotShadowResources(VK.spotShadowMapSize)) {
                fprintf(stderr, "[vlcrlib] SetSpotShadows failed\n");
                VK.spotShadowsEnabled = false;
            } else {
                ibl_refresh_all_material_descriptors();
            }
        }
        vgraphics_ComputeSpotShadowMatrix(0.1f, VK.lightSpotRange > 0.f ? VK.lightSpotRange : 40.f);
    }
}

void vgraphics_SetPointShadows(bool enabled, int mapSize, float bias, float strength) {
    VK.pointShadowsEnabled = enabled;
    int pointRequested = mapSize >= 128 ? mapSize : VK.pointShadowMapSize;
    bool pointSizeChanged = pointRequested != VK.pointShadowMapSize;
    VK.pointShadowMapSize = pointRequested;
    VK.pointShadowBias = bias > 0.f ? bias : 0.01f;
    VK.pointShadowStrength = strength;
    VK.pointShadowFar = VK.lightPointRange > 0.f ? VK.lightPointRange : 25.f;
    if (enabled) {
        if (!VK.pointShadowReady || pointSizeChanged) {
            if (!createPointShadowResources(VK.pointShadowMapSize)) {
                fprintf(stderr, "[vlcrlib] SetPointShadows failed\n");
                VK.pointShadowsEnabled = false;
            } else {
                ibl_refresh_all_material_descriptors();
            }
        }
    }
}

/* ComputeSpotShadowMatrix in vgraphics_shadow.c */




/* Build orthographic light matrix from directional light around origin */


void vgraphics_EnableStudioLighting(void) {
    vgraphics_SetDirectionalLight(true,
        (Vector3){-0.35f, -0.85f, -0.40f},
        (Color){255, 248, 240, 255}, 2.8f);
    vgraphics_SetAmbientLight(true, (Color){90, 100, 125, 255}, 0.45f);
    vgraphics_SetPointLight(false, (Vector3){0,0,0}, WHITE, 0.f, 1.f);
    vgraphics_SetSpotLight(false, (Vector3){0,0,0}, (Vector3){0,-1,0}, WHITE, 0.f, 1.f, 0.5f, 0.4f);
    /* Never auto-bake IBL here ??? causes 2-5s freeze/TDR on some GPUs */
    vgraphics_SetIBLIntensity(0.0f);
}

bool vgraphics_LoadIBL(const char *hdrPath) {
    /* Resources are rebuilt below; do not let the skybox sample the old
       (about to be destroyed) environment while we are uploading. */
    VK.iblLoaded = false;

    VIBL_Bundle bundle = hdrPath && hdrPath[0]
        ? VIBL_BuildFromHDRFile(hdrPath, 64, 16)
        : VIBL_BuildProcedural(64, 16);
    if (!bundle.valid) {
        fprintf(stderr, "[vlcrlib] IBL bake failed for %s\n",
                hdrPath && hdrPath[0] ? hdrPath : "(procedural)");
        return false;
    }

    {
        const char *srcp = bundle.sourcePath[0] ? bundle.sourcePath
                         : (hdrPath && hdrPath[0] ? hdrPath : "procedural");
        size_t i = 0;
        for (; srcp[i] && i + 1 < sizeof(VK.iblSourcePath); ++i)
            VK.iblSourcePath[i] = srcp[i];
        VK.iblSourcePath[i] = 0;
    }
    VK.iblCubeSize = bundle.environment.size ? bundle.environment.size
                     : (bundle.prefiltered.size ? bundle.prefiltered.size : 0);

    fprintf(stderr, "[vlcrlib] IBL bake OK: %s (env=%d pre_mips=%d irr=%d lut=%d)\n",
            VK.iblSourcePath, bundle.environment.size,
            bundle.prefiltered.mips, bundle.irradiance.size, bundle.brdf.size);

    if (vcore_IsCpuBackend()) {
        if (!VCpuSetIBLBundle(&bundle)) {
            VIBL_FreeBundle(&bundle);
            return false;
        }
        VK.iblLoaded = true;
        VK.iblIntensity = 1.0f;
        fprintf(stderr, "[vlcrlib] CPU IBL ready (%s)\n", VK.iblSourcePath);
        return true;
    }

    if (!ibl_upload_bundle_to_gpu(&bundle)) {
        fprintf(stderr, "[vlcrlib] IBL GPU upload FAILED - restoring default resources\n");
        VIBL_FreeBundle(&bundle);
        VK.iblIntensity = 0.0f;
        VK.iblLoaded = false;
        if (VK.device) {
            vkDeviceWaitIdle(VK.device);
            ibl_destroy_gpu();
            ibl_ensure_default_cube();
            ibl_refresh_all_material_descriptors();
        }
        return false;
    }
    VIBL_FreeBundle(&bundle);

    VK.iblLoaded = true;
    VK.iblIntensity = 1.0f; /* enable split-sum maps in shader */
    ibl_refresh_all_material_descriptors();
    fprintf(stderr, "[vlcrlib] IBL GPU ready, intensity=1.0 (bindings 7/8/9)\n");
    return true;
}

bool vgraphics_LoadHDRCubemap(const char *hdrPath, int cubeSize) {
    if (!hdrPath || !hdrPath[0]) {
        fprintf(stderr, "[vlcrlib] LoadHDRCubemap: path required\n");
        return false;
    }
    if (cubeSize < 16) cubeSize = 128;
    VIBL_Cube cube = VIBL_LoadHDRCubemap(hdrPath, cubeSize);
    if (!cube.faces) {
        fprintf(stderr, "[vlcrlib] LoadHDRCubemap failed: %s\n", hdrPath);
        return false;
    }
    VIBL_PrintCubeInfo(&cube, "HDR cubemap");
    VK.iblLoaded = true;
    VK.iblCubeSize = cube.size;
    {
        size_t i = 0;
        for (; hdrPath[i] && i + 1 < sizeof(VK.iblSourcePath); ++i)
            VK.iblSourcePath[i] = hdrPath[i];
        VK.iblSourcePath[i] = 0;
    }
    /* CPU verify only until VkImage upload */
    VIBL_FreeCube(&cube);
    fprintf(stderr, "[vlcrlib] HDR cubemap CPU OK (%s). Upload to VkImage = step 3.\n", hdrPath);
    return true;
}

bool vgraphics_IsIBLLoaded(void) {
    if (vcore_IsCpuBackend()) return VCpuIsIBLLoaded();
    return VK.iblLoaded;
}

void vgraphics_UnloadIBL(void) {
    if (vcore_IsCpuBackend()) {
        VCpuUnloadIBL();
        VK.iblIntensity = 0.0f;
        VK.iblLoaded = false;
        VK.iblSourcePath[0] = 0;
        VK.iblCubeSize = 0;
        return;
    }
    /* Disable IBL without tearing down the GPU images immediately.
     *
     * Destroying a mip-mapped IBL cube right after a frame has been presented
     * and then refreshing the material descriptor sets crashes the NVIDIA
     * driver (nvoglv64.dll) on some systems, even after vkDeviceWaitIdle.
     * The images are small; keep them resident until the next LoadIBL (which
     * rebuilds them between frames) or until the device is destroyed. */
    VK.iblIntensity = 0.0f;
    VK.iblLoaded = false;
    VK.iblSourcePath[0] = 0;
    VK.iblCubeSize = 0;
}

void vgraphics_SetIBLIntensity(float intensity) {
    if (intensity < 0.f) intensity = 0.f;
    if (intensity > 4.f) intensity = 4.f;
    VK.iblIntensity = intensity;
    VCpuSetIBLIntensity(intensity);
}




void vgraphics_UploadBoneMatrices(const Matrix *matrices, int count) {
    if (!VK.boneBufferMapped || !matrices) return;
    if (count > VLCRLIB_MAX_BONES) count = VLCRLIB_MAX_BONES;
    /* Sprint B: column-major GPU layout via matrixToGpu (not raw Matrix memcpy) */
    for (int i = 0; i < count; ++i) {
        float gpu[16];
        vlcr_matrixToGpu(gpu, matrices[i]);
        memcpy((char *)VK.boneBufferMapped + (size_t)i * 64, gpu, 64);
    }
}

/* ================================================================== */
/*  RenderTexture                                                     */
/* ================================================================== */
RenderTexture2D LoadRenderTexture(int width, int height) {
    RenderTexture2D rt = {0};
    if (!VK.device || width <= 0 || height <= 0) return rt;
    int idx = allocateRTSlot();
    if (!idx) return rt;
    int texIdx = allocateTextureSlot();
    if (!texIdx) return rt;

    RenderTargetEntry *e = &g_rtTable[idx];

    if (!createImage((uint32_t)width, (uint32_t)height, VK.swapFormat, VK_IMAGE_TILING_OPTIMAL,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &e->colorImg, &e->colorMem)) {
        destroyRenderTargetEntry(e); return rt;
    }
    VkImageViewCreateInfo cvi = {0};
    cvi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    cvi.image = e->colorImg;
    cvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    cvi.format = VK.swapFormat;
    cvi.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    if (vkCreateImageView(VK.device, &cvi, NULL, &e->colorView) != VK_SUCCESS) { destroyRenderTargetEntry(e); return rt; }

    if (!createImage((uint32_t)width, (uint32_t)height, VK.depthFormat, VK_IMAGE_TILING_OPTIMAL,
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &e->depthImg, &e->depthMem)) {
        destroyRenderTargetEntry(e); return rt;
    }
    VkImageViewCreateInfo dvi = {0};
    dvi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    dvi.image = e->depthImg;
    dvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    dvi.format = VK.depthFormat;
    dvi.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1};
    if (vkCreateImageView(VK.device, &dvi, NULL, &e->depthView) != VK_SUCCESS) { destroyRenderTargetEntry(e); return rt; }

    VkImageView atts[2] = { e->colorView, e->depthView };
    VkFramebufferCreateInfo fbi = {0};
    fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbi.renderPass = VK.renderPassRT;
    fbi.attachmentCount = 2;
    fbi.pAttachments = atts;
    fbi.width = (uint32_t)width;
    fbi.height = (uint32_t)height;
    fbi.layers = 1;
    if (vkCreateFramebuffer(VK.device, &fbi, NULL, &e->fb) != VK_SUCCESS) { destroyRenderTargetEntry(e); return rt; }

    e->w = width;
    e->h = height;
    e->generation = rtGeneration((unsigned int)idx);

    g_textureTable[texIdx] = (TextureEntry){
        e->colorImg, e->colorMem, e->colorView, VK.sampler2D, width, height };
    g_textureTable[texIdx].generation = textureGeneration((unsigned int)texIdx);
    e->textureId = (unsigned int)texIdx;

    rt.id = (unsigned int)idx;
    rt.generation = e->generation;
    rt.texture.id     = (unsigned int)texIdx;
    rt.texture.generation = g_textureTable[texIdx].generation;
    rt.texture.width  = width;
    rt.texture.height = height;
    rt.texture.format = 0;
    return rt;
}

void UnloadRenderTexture(RenderTexture2D rt) {
    if (rt.id == 0 || rt.id >= VLCRLIB_MAX_RT) return;
    RenderTargetEntry *e = &g_rtTable[rt.id];
    if (!e->fb || !rt.generation || rt.generation != e->generation || VK.inRT)
        return;
    if (VK.inFrame && g_deferredRTReleaseCount >= VLCRLIB_MAX_RT)
        return;
    vkDeviceWaitIdle(VK.device);
    material_detach_texture(rt.texture);
    if (rt.texture.id < VLCRLIB_MAX_TEXTURES)
        g_textureTable[rt.texture.id] = (TextureEntry){0};
    invalidateTextureGeneration(rt.texture.id);
    if (VK.inFrame && g_deferredRTReleaseCount < VLCRLIB_MAX_RT) {
        g_deferredRTReleases[g_deferredRTReleaseCount++] =
            (DeferredRenderTargetRelease){ *e, VK.currentFrame };
    } else {
        if (VK.inFrame) releaseCompletedRenderTargets(0, true);
        destroyRenderTargetEntry(e);
    }
    *e = (RenderTargetEntry){0};
    invalidateRTGeneration(rt.id);
}

void BeginTextureMode(RenderTexture2D rt) {
    if (rt.id == 0 || rt.id >= VLCRLIB_MAX_RT ||
        rt.generation == 0 || rt.generation != g_rtTable[rt.id].generation)
        return;
    if (VK.inRT) return;
    RenderTargetEntry *e = &g_rtTable[rt.id];
    if (!e->fb) return;

    extern void vshapes_FlushBatch(void);
    vshapes_FlushBatch();

    VkCommandBufferAllocateInfo ai = {0};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = VK.cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cb;
    if (vkAllocateCommandBuffers(VK.device, &ai, &cb) != VK_SUCCESS) return;

    VkCommandBufferBeginInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    VkClearValue clears[2];
    clears[0] = VK.clearColor;
    clears[1].depthStencil.depth = 1.0f;
    clears[1].depthStencil.stencil = 0;

    VkRenderPassBeginInfo rp = {0};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = VK.renderPassRT;
    rp.framebuffer = e->fb;
    rp.renderArea.extent = (VkExtent2D){ (uint32_t)e->w, (uint32_t)e->h };
    rp.clearValueCount = 2;
    rp.pClearValues = clears;
    vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp = {0, 0, (float)e->w, (float)e->h, 0, 1};
    vkCmdSetViewport(cb, 0, 1, &vp);
    VkRect2D sc = { {0,0}, {(uint32_t)e->w, (uint32_t)e->h} };
    vkCmdSetScissor(cb, 0, 1, &sc);

    VK.currentCmd = cb;
    VK.viewport2DW = e->w;
    VK.viewport2DH = e->h;
    VK.ubo3DSlot = 0;
    VK.uboPBRSlot = 0;
    VK.inRT = true;
}

void EndTextureMode(void) {
    if (!VK.inRT) return;
    extern void vshapes_FlushBatch(void);
    vshapes_FlushBatch();

    VkCommandBuffer cb = VK.currentCmd;
    vkCmdEndRenderPass(cb);
    if (VK.postProcessEnabled && VK.sceneReady) {
        updateTaaParams();
        if (!recordBloomGraph(cb)) recordBloomPass(cb);
        if (VK.compositeReady && VK.compositePipeline) {
            if (!recordCompositeGraph(cb)) recordComposite(cb);
        } else {
            recordComposite(cb);
        }
    }
    vkEndCommandBuffer(cb);

    vkResetFences(VK.device, 1, &VK.offscreenFence);
    VkSubmitInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    if (vkQueueSubmit(VK.gfxQueue, 1, &si, VK.offscreenFence) != VK_SUCCESS)
        fprintf(stderr, "[vlcrlib] offscreen submit failed\n");
    vkWaitForFences(VK.device, 1, &VK.offscreenFence, VK_TRUE, UINT64_MAX);
    vkFreeCommandBuffers(VK.device, VK.cmdPool, 1, &cb);

    VK.currentCmd = VK.swapchainCmd;
    VK.viewport2DW = (int)VK.swapExtent.width;
    VK.viewport2DH = (int)VK.swapExtent.height;
    VK.ubo3DSlot = 0;
    VK.uboPBRSlot = 0;
    VK.inRT = false;
}

/* ================================================================== */
/*  Skinning                                                          */
/* ================================================================== */
static Vector3 lerp3(Vector3 a, Vector3 b, float t) {
    return (Vector3){ a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, a.z+(b.z-a.z)*t };
}
static Vector3 hermite3(Vector3 p0, Vector3 m0, Vector3 p1, Vector3 m1, float u, float span) {
    float u2 = u*u, u3 = u2*u;
    float h00 = 2*u3 - 3*u2 + 1, h10 = u3 - 2*u2 + u;
    float h01 = -2*u3 + 3*u2, h11 = u3 - u2;
    return (Vector3){h00*p0.x+h10*span*m0.x+h01*p1.x+h11*span*m1.x,
                     h00*p0.y+h10*span*m0.y+h01*p1.y+h11*span*m1.y,
                     h00*p0.z+h10*span*m0.z+h01*p1.z+h11*span*m1.z};
}
static void sampleVec3(const KeyVec3 *keys, int n, float time, Vector3 def,
                       int interpolation, Vector3 *out) {
    if (n <= 0) { *out = def; return; }
    if (time <= keys[0].t) { *out = keys[0].v; return; }
    if (time >= keys[n-1].t) { *out = keys[n-1].v; return; }
    for (int i = 0; i < n-1; ++i)
        if (time >= keys[i].t && time <= keys[i+1].t) {
            float span = keys[i+1].t - keys[i].t;
            float u = (span > 1e-6f) ? (time - keys[i].t) / span : 0.0f;
            if (interpolation == 1) *out = keys[i].v;
            else if (interpolation == 2)
                *out = hermite3(keys[i].v, keys[i].outTangent, keys[i+1].v,
                                keys[i+1].inTangent, u, span);
            else *out = lerp3(keys[i].v, keys[i+1].v, u);
            return;
        }
    *out = keys[n-1].v;
}
static void sampleQuat(const KeyQuat *keys, int n, float time, Quaternion def,
                       int interpolation, Quaternion *out) {
    if (n <= 0) { *out = def; return; }
    if (time <= keys[0].t) { *out = keys[0].q; return; }
    if (time >= keys[n-1].t) { *out = keys[n-1].q; return; }
    for (int i = 0; i < n-1; ++i)
        if (time >= keys[i].t && time <= keys[i+1].t) {
            float span = keys[i+1].t - keys[i].t;
            float u = (span > 1e-6f) ? (time - keys[i].t) / span : 0.0f;
            if (interpolation == 1) *out = keys[i].q;
            else if (interpolation == 2) {
                float u2=u*u, u3=u2*u;
                float h00=2*u3-3*u2+1, h10=u3-2*u2+u;
                float h01=-2*u3+3*u2, h11=u3-u2;
                Quaternion a=keys[i].q, b=keys[i].outTangent;
                Quaternion c=keys[i+1].q, d=keys[i+1].inTangent;
                float spanScale=span;
                *out=QuaternionNormalize((Quaternion){
                    h00*a.x+h10*spanScale*b.x+h01*c.x+h11*spanScale*d.x,
                    h00*a.y+h10*spanScale*b.y+h01*c.y+h11*spanScale*d.y,
                    h00*a.z+h10*spanScale*b.z+h01*c.z+h11*spanScale*d.z,
                    h00*a.w+h10*spanScale*b.w+h01*c.w+h11*spanScale*d.w});
            } else *out = QuaternionSlerp(keys[i].q, keys[i+1].q, u);
            return;
        }
    *out = keys[n-1].q;
}

static bool uploadSkinnedMesh(const SkinnedVertex3D *verts, int vcount,
                              const uint32_t *indices, int icount,
                              VkBuffer *vbo, VkDeviceMemory *vboMem,
                              VkBuffer *ibo, VkDeviceMemory *iboMem) {
    return uploadMeshBuffers(vbo, vboMem, ibo, iboMem,
                             verts, vcount, sizeof(SkinnedVertex3D),
                             indices, icount);
}

static int cmpFloat(const void *a, const void *b) {
    float fa = *(const float *)a, fb = *(const float *)b;
    return (fa > fb) - (fa < fb);
}

static bool createSkinnedVelocityResources(SkinnedModelEntry *me) {
    if (!VK.velocitySkinDSL || !VK.velocitySkinPool) return false;
    VkDeviceSize size = (VkDeviceSize)VLCRLIB_MAX_BONES * 64u * 2u;
    if (!createBuffer(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &me->velocityBoneBuffer, &me->velocityBoneMem))
        return false;
    if (vkMapMemory(VK.device, me->velocityBoneMem, 0, size, 0,
                    &me->velocityBoneMapped) != VK_SUCCESS) {
        vkDestroyBuffer(VK.device, me->velocityBoneBuffer, NULL);
        vkFreeMemory(VK.device, me->velocityBoneMem, NULL);
        me->velocityBoneBuffer = VK_NULL_HANDLE;
        me->velocityBoneMem = VK_NULL_HANDLE;
        return false;
    }
    VkDescriptorSetAllocateInfo ai = {0};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = VK.velocitySkinPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &VK.velocitySkinDSL;
    if (vkAllocateDescriptorSets(VK.device, &ai, &me->velocitySkinDesc) != VK_SUCCESS) {
        vkDestroyBuffer(VK.device, me->velocityBoneBuffer, NULL);
        vkFreeMemory(VK.device, me->velocityBoneMem, NULL);
        me->velocityBoneBuffer = VK_NULL_HANDLE;
        me->velocityBoneMem = VK_NULL_HANDLE;
        return false;
    }
    VkDescriptorBufferInfo vpInfo = { VK.velocityUBO, 0, 128 };
    VkDescriptorBufferInfo boneInfo = { me->velocityBoneBuffer, 0, size };
    VkWriteDescriptorSet wr[2] = {0};
    wr[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr[0].dstSet = me->velocitySkinDesc;
    wr[0].dstBinding = 0;
    wr[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    wr[0].descriptorCount = 1;
    wr[0].pBufferInfo = &vpInfo;
    wr[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr[1].dstSet = me->velocitySkinDesc;
    wr[1].dstBinding = 1;
    wr[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    wr[1].descriptorCount = 1;
    wr[1].pBufferInfo = &boneInfo;
    vkUpdateDescriptorSets(VK.device, 2, wr, 0, NULL);
    return true;
}

SkinnedModel vgraphics_CreateSkinnedModel(
    const SkinnedVertex3D *vertices, int vertexCount,
    const uint32_t *indices, int indexCount,
    const Material *material,
    const Matrix *inverseBind,
    const int *parents,
    const Vector3 *restT, const Quaternion *restR, const Vector3 *restS,
    int boneCount, const char *const *boneNames)
{
    SkinnedModel model = {0};
    if (!VK.device || !vertices || vertexCount <= 0 ||
        !indices || indexCount <= 0) return model;
    if (boneCount <= 0 || boneCount > VLCRLIB_MAX_BONES) return model;
    if (g_skModelCount + 1 >= VLCRLIB_MAX_SKMODELS) return model;
    if (g_skelCount + 1 >= VLCRLIB_MAX_SKELETONS) return model;
    if (g_animCount + 1 >= VLCRLIB_MAX_ANIMATIONS) return model;

    int modelIdx = ++g_skModelCount;
    SkinnedModelEntry *me = &g_skModelTable[modelIdx];
    memset(me, 0, sizeof(*me));
    me->meshCount = 1;
    me->materialCount = 1;
    me->materials = (Material *)calloc(1, sizeof(Material));
    me->meshMaterials = (int *)calloc(1, sizeof(int));
    me->vbos = (VkBuffer *)calloc(1, sizeof(VkBuffer));
    me->vboMems = (VkDeviceMemory *)calloc(1, sizeof(VkDeviceMemory));
    me->ibos = (VkBuffer *)calloc(1, sizeof(VkBuffer));
    me->iboMems = (VkDeviceMemory *)calloc(1, sizeof(VkDeviceMemory));
    me->indexCounts = (int *)calloc(1, sizeof(int));
    me->animationIds = (int *)calloc(1, sizeof(int));
    if (!me->materials || !me->meshMaterials || !me->vbos || !me->vboMems ||
        !me->ibos || !me->iboMems || !me->indexCounts || !me->animationIds) {
        UnloadSkinnedModel((SkinnedModel){ (unsigned int)modelIdx });
        return (SkinnedModel){0};
    }

    if (!uploadSkinnedMesh(vertices, vertexCount, indices, indexCount,
                           &me->vbos[0], &me->vboMems[0],
                           &me->ibos[0], &me->iboMems[0])) {
        UnloadSkinnedModel((SkinnedModel){ (unsigned int)modelIdx });
        return (SkinnedModel){0};
    }
    me->indexCounts[0] = indexCount;
    me->meshMaterials[0] = 0;
    me->materials[0] = material ? *material : LoadMaterialDefault();
    /* Robust 1..99 percentile bounds: FBX characters often contain a stray
       vertex/bone marker that is 100x the body size, which would make a naive
       AABB useless for camera fitting. */
    {
        float *vals = (float *)malloc((size_t)vertexCount * sizeof(float));
        if (vals) {
            const int lo = vertexCount / 100;
            const int hi = vertexCount - 1 - lo;
            for (int axis = 0; axis < 3; ++axis) {
                for (int i = 0; i < vertexCount; ++i) {
                    const SkinnedVertex3D *v = &vertices[i];
                    vals[i] = axis == 0 ? v->x : (axis == 1 ? v->y : v->z);
                }
                qsort(vals, (size_t)vertexCount, sizeof(float), cmpFloat);
                float mn = vals[lo], mx = vals[hi];
                if (axis == 0) { me->boundsMin.x = mn; me->boundsMax.x = mx; }
                else if (axis == 1) { me->boundsMin.y = mn; me->boundsMax.y = mx; }
                else { me->boundsMin.z = mn; me->boundsMax.z = mx; }
            }
            free(vals);
            me->hasBounds = 1;
        }
    }

    int skelIdx = ++g_skelCount;
    SkeletonEntry *skel = &g_skelTable[skelIdx];
    memset(skel, 0, sizeof(*skel));
    skel->boneCount = boneCount;
    for (int i = 0; i < boneCount; ++i) {
        Bone *b = &skel->bones[i];
        if (boneNames && boneNames[i])
            snprintf(b->name, sizeof(b->name), "%s", boneNames[i]);
        b->parent = parents ? parents[i] : -1;
        b->inverseBind = inverseBind ? inverseBind[i] : MatrixIdentity();
        b->local = MatrixIdentity();
        b->t = restT ? restT[i] : (Vector3){0, 0, 0};
        b->r = restR ? restR[i] : (Quaternion){0, 0, 0, 1};
        b->s = restS ? restS[i] : (Vector3){1, 1, 1};
        skel->restT[i] = b->t;
        skel->restR[i] = b->r;
        skel->restS[i] = b->s;
        skel->preTransform[i] = MatrixIdentity();
    }

    int animIdx = ++g_animCount;
    AnimationEntry *ae = &g_animTable[animIdx];
    memset(ae, 0, sizeof(*ae));
    snprintf(ae->name, sizeof(ae->name), "bind");
    ae->duration = 1.0f;
    ae->boneCount = boneCount;
    ae->skeleton = skel;
    ae->tracks = (Track *)calloc((size_t)boneCount, sizeof(Track));
    if (!ae->tracks) {
        UnloadSkinnedModel((SkinnedModel){ (unsigned int)modelIdx });
        return (SkinnedModel){0};
    }

    me->skeletonId = skelIdx;
    me->animationIds[0] = animIdx;
    me->animationCount = 1;

    model.id = (unsigned int)modelIdx;
    model.meshCount = 1;
    model.materialCount = 1;
    model.skeleton = skelIdx;
    model.animationCount = 1;
    createSkinnedVelocityResources(me);
    return model;
}

int vgraphics_AddSkinnedAnimation(SkinnedModel model, const char *name,
                                   float duration,
                                   const AnimTrackData *tracks, int trackCount)
{
    if (model.id == 0 || model.id > (unsigned int)g_skModelCount) return -1;
    if (!tracks || trackCount <= 0 || g_animCount + 1 >= VLCRLIB_MAX_ANIMATIONS)
        return -1;
    SkinnedModelEntry *me = &g_skModelTable[model.id];
    SkeletonEntry *skel = &g_skelTable[me->skeletonId];
    if (trackCount > skel->boneCount) trackCount = skel->boneCount;

    int idx = ++g_animCount;
    AnimationEntry *ae = &g_animTable[idx];
    memset(ae, 0, sizeof(*ae));
    snprintf(ae->name, sizeof(ae->name), "%s", name && name[0] ? name : "clip");
    ae->duration = duration > 0.0f ? duration : 1.0f;
    ae->boneCount = skel->boneCount;
    ae->skeleton = skel;
    ae->tracks = (Track *)calloc((size_t)skel->boneCount, sizeof(Track));
    if (!ae->tracks) return -1;

    for (int b = 0; b < trackCount; ++b) {
        Track *tr = &ae->tracks[b];
        if (tracks[b].posCount > 0 && tracks[b].pos) {
            tr->pos = (KeyVec3 *)calloc((size_t)tracks[b].posCount, sizeof(KeyVec3));
            if (tr->pos) {
                for (int i = 0; i < tracks[b].posCount; ++i) {
                    tr->pos[i].t = tracks[b].pos[i].t;
                    tr->pos[i].v = tracks[b].pos[i].v;
                }
                tr->posCount = tracks[b].posCount;
            }
        }
        if (tracks[b].rotCount > 0 && tracks[b].rot) {
            tr->rot = (KeyQuat *)calloc((size_t)tracks[b].rotCount, sizeof(KeyQuat));
            if (tr->rot) {
                for (int i = 0; i < tracks[b].rotCount; ++i) {
                    tr->rot[i].t = tracks[b].rot[i].t;
                    tr->rot[i].q = tracks[b].rot[i].q;
                }
                tr->rotCount = tracks[b].rotCount;
            }
        }
        if (tracks[b].scaleCount > 0 && tracks[b].scale) {
            tr->scale = (KeyVec3 *)calloc((size_t)tracks[b].scaleCount, sizeof(KeyVec3));
            if (tr->scale) {
                for (int i = 0; i < tracks[b].scaleCount; ++i) {
                    tr->scale[i].t = tracks[b].scale[i].t;
                    tr->scale[i].v = tracks[b].scale[i].v;
                }
                tr->scaleCount = tracks[b].scaleCount;
            }
        }
        tr->posInterpolation = 0;
        tr->rotInterpolation = 0;
        tr->scaleInterpolation = 0;
    }

    int *grown = (int *)realloc(me->animationIds,
                                sizeof(int) * (size_t)(me->animationCount + 1));
    if (!grown) return -1;
    me->animationIds = grown;
    me->animationIds[me->animationCount++] = idx;
    return idx;
}

const char *GetSkinnedModelAnimationName(SkinnedModel model, int animIndex) {
    if (model.id == 0 || model.id > (unsigned int)g_skModelCount) return "";
    SkinnedModelEntry *e = &g_skModelTable[model.id];
    if (animIndex < 0 || animIndex >= e->animationCount) return "";
    int idx = e->animationIds[animIndex];
    if (idx <= 0 || idx > g_animCount) return "";
    return g_animTable[idx].name;
}

SkinnedModel LoadSkinnedModel(const char *path) {
    if (!VK.device || !path || !path[0]) return (SkinnedModel){0};
    return vgltf_LoadSkinnedModel(path);
}

int GetSkinnedModelAnimationCount(SkinnedModel model) {
    if (model.id == 0 || model.id > (unsigned int)g_skModelCount) return 0;
    return g_skModelTable[model.id].animationCount;
}

int GetSkinnedModelBoneCount(SkinnedModel model) {
    if (model.id == 0 || model.id > (unsigned int)g_skModelCount) return 0;
    int skel = g_skModelTable[model.id].skeletonId;
    if (skel <= 0 || skel > g_skelCount) return 0;
    return g_skelTable[skel].boneCount;
}

const char *GetSkinnedModelBoneName(SkinnedModel model, int boneIndex) {
    if (model.id == 0 || model.id > (unsigned int)g_skModelCount) return "";
    int skel = g_skModelTable[model.id].skeletonId;
    if (skel <= 0 || skel > g_skelCount) return "";
    if (boneIndex < 0 || boneIndex >= g_skelTable[skel].boneCount) return "";
    return g_skelTable[skel].bones[boneIndex].name;
}

bool GetSkinnedModelBounds(SkinnedModel model, Vector3 *outMin, Vector3 *outMax) {
    if (model.id == 0 || model.id > (unsigned int)g_skModelCount) return false;
    SkinnedModelEntry *e = &g_skModelTable[model.id];
    if (!e->hasBounds) return false;
    if (outMin) *outMin = e->boundsMin;
    if (outMax) *outMax = e->boundsMax;
    return true;
}

float GetSkinnedModelAnimationDuration(SkinnedModel model, int animIndex) {
    if (model.id == 0 || model.id > (unsigned int)g_skModelCount) return 0;
    SkinnedModelEntry *e = &g_skModelTable[model.id];
    if (animIndex < 0 || animIndex >= e->animationCount) return 0;
    return g_animTable[e->animationIds[animIndex]].duration;
}

Animation GetSkinnedModelAnimation(SkinnedModel model, int animIndex) {
    Animation anim = {0};
    if (model.id == 0 || model.id > (unsigned int)g_skModelCount) return anim;
    SkinnedModelEntry *e = &g_skModelTable[model.id];
    if (animIndex < 0 || animIndex >= e->animationCount) return anim;
    int idx = e->animationIds[animIndex];
    if (idx <= 0 || idx > g_animCount) return anim;
    anim.id = (unsigned int)idx;
    anim.duration = g_animTable[idx].duration;
    anim.boneCount = g_animTable[idx].boneCount;
    return anim;
}

bool GetAnimationRootMotion(Animation animation, float fromTime, float toTime,
                            Vector3 *outDelta) {
    if (outDelta) *outDelta = (Vector3){0};
    if (animation.id == 0 || animation.id > (unsigned int)g_animCount) return false;
    AnimationEntry *ae = &g_animTable[animation.id];
    SkeletonEntry *skel = ae->skeleton;
    if (!skel) return false;
    for (int b = 0; b < skel->boneCount; ++b) {
        if (skel->bones[b].parent >= 0) continue;
        Track *tr = &ae->tracks[b];
        if (tr->posCount <= 0 || !tr->pos) continue;
        Vector3 a, c;
        sampleVec3(tr->pos, tr->posCount, fromTime, skel->restT[b],
                   tr->posInterpolation, &a);
        sampleVec3(tr->pos, tr->posCount, toTime, skel->restT[b],
                   tr->posInterpolation, &c);
        if (outDelta) *outDelta = Vector3Subtract(c, a);
        return true;
    }
    return false;
}

void SetSkinnedModelMaterial(SkinnedModel model, int idx, Material mat) {
    if (model.id == 0 || model.id > (unsigned int)g_skModelCount) return;
    SkinnedModelEntry *e = &g_skModelTable[model.id];
    if (idx < 0 || idx >= e->materialCount) return;
    e->materials[idx] = mat;
}

static void evaluateBoneGlobal(SkeletonEntry *skel, int b, Matrix *global, unsigned char *done) {
    if (done[b]) return;
    int parent = skel->bones[b].parent;
    if (parent >= 0 && parent < skel->boneCount && parent != b) {
        evaluateBoneGlobal(skel, parent, global, done);
        global[b] = MatrixMultiply(global[parent], skel->bones[b].local);
    } else global[b] = skel->bones[b].local;
    done[b] = 1;
}

static void evaluateAnimation(AnimationEntry *ae, float time, Matrix *outMatrices) {
    SkeletonEntry *skel = ae->skeleton;
    for (int b = 0; b < skel->boneCount; ++b) {
        Bone *bone = &skel->bones[b];
        Track *tr = &ae->tracks[b];
        Vector3 t; Quaternion r; Vector3 s;
        sampleVec3(tr->pos, tr->posCount, time, skel->restT[b], tr->posInterpolation, &t);
        sampleQuat(tr->rot, tr->rotCount, time, skel->restR[b], tr->rotInterpolation, &r);
        sampleVec3(tr->scale, tr->scaleCount, time, skel->restS[b], tr->scaleInterpolation, &s);
        bone->t = t; bone->r = r; bone->s = s;

        Matrix T = MatrixTranslate(t.x, t.y, t.z);
        Matrix R = QuaternionToMatrix(r);
        Matrix S = MatrixScale(s.x, s.y, s.z);
        Matrix jointLocal = MatrixMultiply(T, MatrixMultiply(R, S));
        bone->local = MatrixMultiply(skel->preTransform[b], jointLocal);
    }
    Matrix global[VLCRLIB_MAX_BONES];
    unsigned char done[VLCRLIB_MAX_BONES] = {0};
    for (int b = 0; b < skel->boneCount; ++b) {
        evaluateBoneGlobal(skel, b, global, done);
        outMatrices[b] = MatrixMultiply(global[b], skel->bones[b].inverseBind);
    }
}

static void drawSkinnedMatrices(SkinnedModel model, const Matrix *boneMatrices,
                               int boneCount,
                               Vector3 position, Vector3 rotation, Vector3 scale,
                               Color tint) {
    if (model.id == 0 || model.id > (unsigned int)g_skModelCount) return;
    SkinnedModelEntry *e = &g_skModelTable[model.id];
    SkeletonEntry *skel = &g_skelTable[e->skeletonId];
    if (boneCount > skel->boneCount) boneCount = skel->boneCount;
    vgraphics_UploadBoneMatrices(boneMatrices, boneCount);

    Matrix T = MatrixTranslate(position.x, position.y, position.z);
    Matrix R = MatrixRotateXYZ(rotation);
    Matrix S = MatrixScale(scale.x, scale.y, scale.z);
    Matrix M = MatrixMultiply(T, MatrixMultiply(R, S));
    Matrix mvp = MatrixMultiply(MatrixMultiply(VK.proj3D, VK.view3D), M);

    if ((VK.shadowsEnabled || VK.ssaoEnabled || VK.taaEnabled) &&
        VK.shadowCasterCount < 64) {
        int si = VK.shadowCasterCount++;
        VK.shadowCasters[si].modelId = 0;
        VK.shadowCasters[si].meshId = 0;
        VK.shadowCasters[si].skinnedId = model.id;
        VK.shadowCasters[si].position = position;
        VK.shadowCasters[si].rotation = rotation;
        VK.shadowCasters[si].scale = scale;
        VK.shadowCasters[si].transform = M;
        VK.shadowCasters[si].prevTransform = e->prevBonesValid
            ? e->prevTransform : M;
    }
    if (e->velocityBoneMapped) {
        float *dst = (float *)e->velocityBoneMapped;
        for (int b = 0; b < boneCount; ++b) {
            vlcr_matrixToGpu(dst + b * 16, boneMatrices[b]);
            Matrix prev = e->prevBonesValid ? e->prevBones[b] : boneMatrices[b];
            vlcr_matrixToGpu(dst + (VLCRLIB_MAX_BONES + b) * 16, prev);
            e->prevBones[b] = boneMatrices[b];
        }
    }
    e->prevTransform = M;
    e->prevBonesValid = 1;

    for (int mi = 0; mi < e->meshCount; ++mi) {
        if (!e->vbos[mi]) continue;
        Material mat = e->materials[e->meshMaterials[mi]];
        if (!material_handle_valid(&mat)) continue;
        Material m2 = mat;
        m2.baseColorFactor[0] = mat.baseColorFactor[0] * (tint.r / 255.0f);
        m2.baseColorFactor[1] = mat.baseColorFactor[1] * (tint.g / 255.0f);
        m2.baseColorFactor[2] = mat.baseColorFactor[2] * (tint.b / 255.0f);
        m2.baseColorFactor[3] = mat.baseColorFactor[3] * (tint.a / 255.0f);
        m2.baseColor.r = (unsigned char)(m2.baseColorFactor[0] * 255.0f + 0.5f);
        m2.baseColor.g = (unsigned char)(m2.baseColorFactor[1] * 255.0f + 0.5f);
        m2.baseColor.b = (unsigned char)(m2.baseColorFactor[2] * 255.0f + 0.5f);
        m2.baseColor.a = (unsigned char)(m2.baseColorFactor[3] * 255.0f + 0.5f);

        uint32_t slot = VK.uboPBRSlot;
        writeUBOPBR(mvp, M, &m2);

        PBRMaterialEntry *mpe = &g_matTable[mat.id];
        VkCommandBuffer cb = VK.currentCmd;
        int blend = (m2.alphaMode == 2) || (m2.transmissionFactor > 0.05f);
        VkPipeline pl;
        if (blend)
            pl = m2.doubleSided ? VK.pipelineSkinBlendNoCull : VK.pipelineSkinBlend;
        else
            pl = m2.doubleSided ? VK.pipelineSkinNoCull : VK.pipelineSkin;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl);

        uint32_t dyn = slot * VK.uboPBRStride;
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, VK.layoutSkin,
                                0, 1, &mpe->skinDescSet, 1, &dyn);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &e->vbos[mi], &off);
        vkCmdBindIndexBuffer(cb, e->ibos[mi], 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, e->indexCounts[mi], 1, 0, 0, 0);
        statsCountDraw(e->indexCounts[mi], blend);
    }
}

void DrawSkinnedModel(SkinnedModel model, Animation anim, float time,
                      Vector3 position, Vector3 rotation, Vector3 scale, Color tint) {
    if (model.id == 0 || model.id > (unsigned int)g_skModelCount) return;
    if (anim.id == 0 || anim.id > (unsigned int)g_animCount) return;

    SkinnedModelEntry *e = &g_skModelTable[model.id];
    AnimationEntry    *ae = &g_animTable[anim.id];
    SkeletonEntry     *skel = &g_skelTable[e->skeletonId];
    if (ae->skeleton != skel) return;

    Matrix boneMatrices[VLCRLIB_MAX_BONES];
    if (strcmp(ae->name, "bind") == 0) {
        /* The synthetic bind clip must be the rest pose. Evaluating rest TRS
           with FBX inverse-bind matrices is not guaranteed to be identity for
           every exporter (armature parents, unit scaling); drawing identity
           keeps the bind pose usable and debuggable. */
        for (int i = 0; i < skel->boneCount; ++i)
            boneMatrices[i] = MatrixIdentity();
    } else {
        evaluateAnimation(ae, time, boneMatrices);
    }
    drawSkinnedMatrices(model, boneMatrices, skel->boneCount,
                        position, rotation, scale, tint);
}

bool GetSkinnedModelRestPose(SkinnedModel model, BonePose *out, int maxBones) {
    if (!out || model.id == 0 || model.id > (unsigned int)g_skModelCount) return false;
    SkinnedModelEntry *e = &g_skModelTable[model.id];
    if (e->skeletonId <= 0 || e->skeletonId > g_skelCount) return false;
    SkeletonEntry *skel = &g_skelTable[e->skeletonId];
    int n = skel->boneCount < maxBones ? skel->boneCount : maxBones;
    for (int b = 0; b < n; ++b) {
        out[b].position = skel->restT[b];
        out[b].rotation = skel->restR[b];
        out[b].scale = skel->restS[b];
    }
    return true;
}

bool GetAnimationPose(Animation animation, float time,
                      BonePose *out, int maxBones) {
    if (!out || animation.id == 0 || animation.id > (unsigned int)g_animCount)
        return false;
    AnimationEntry *ae = &g_animTable[animation.id];
    SkeletonEntry *skel = ae->skeleton;
    if (!skel) return false;
    int n = skel->boneCount < maxBones ? skel->boneCount : maxBones;
    for (int b = 0; b < n; ++b) {
        Track *tr = &ae->tracks[b];
        sampleVec3(tr->pos, tr->posCount, time, skel->restT[b],
                   tr->posInterpolation, &out[b].position);
        sampleQuat(tr->rot, tr->rotCount, time, skel->restR[b],
                   tr->rotInterpolation, &out[b].rotation);
        sampleVec3(tr->scale, tr->scaleCount, time, skel->restS[b],
                   tr->scaleInterpolation, &out[b].scale);
    }
    return true;
}

static void poseBoneGlobal(SkeletonEntry *skel, int b,
                           const Matrix *local, Matrix *global,
                           unsigned char *done) {
    if (done[b]) return;
    int parent = skel->bones[b].parent;
    if (parent >= 0 && parent < skel->boneCount && parent != b) {
        poseBoneGlobal(skel, parent, local, global, done);
        global[b] = MatrixMultiply(global[parent], local[b]);
    } else {
        global[b] = local[b];
    }
    done[b] = 1;
}

void DrawSkinnedModelPose(SkinnedModel model, const BonePose *pose, int boneCount,
                          Vector3 position, Vector3 rotation, Vector3 scale,
                          Color tint) {
    if (!pose || model.id == 0 || model.id > (unsigned int)g_skModelCount) return;
    SkinnedModelEntry *e = &g_skModelTable[model.id];
    if (e->skeletonId <= 0 || e->skeletonId > g_skelCount) return;
    SkeletonEntry *skel = &g_skelTable[e->skeletonId];
    if (boneCount > skel->boneCount) boneCount = skel->boneCount;
    Matrix local[VLCRLIB_MAX_BONES];
    Matrix global[VLCRLIB_MAX_BONES];
    Matrix skin[VLCRLIB_MAX_BONES];
    unsigned char done[VLCRLIB_MAX_BONES] = {0};
    for (int b = 0; b < boneCount; ++b) {
        Matrix T = MatrixTranslate(pose[b].position.x, pose[b].position.y,
                                   pose[b].position.z);
        Matrix R = QuaternionToMatrix(pose[b].rotation);
        Matrix S = MatrixScale(pose[b].scale.x, pose[b].scale.y,
                               pose[b].scale.z);
        local[b] = MatrixMultiply(skel->preTransform[b],
                                  MatrixMultiply(T, MatrixMultiply(R, S)));
    }
    for (int b = 0; b < boneCount; ++b) {
        poseBoneGlobal(skel, b, local, global, done);
        skin[b] = MatrixMultiply(global[b], skel->bones[b].inverseBind);
    }
    drawSkinnedMatrices(model, skin, boneCount, position, rotation, scale, tint);
}

static Quaternion ikQInv(Quaternion q) {
    return (Quaternion){-q.x, -q.y, -q.z, q.w};
}

static Quaternion ikQFromTo(Vector3 from, Vector3 to) {
    from = Vector3Normalize(from);
    to = Vector3Normalize(to);
    float d = Vector3Dot(from, to);
    if (d > 0.99999f) return QuaternionIdentity();
    if (d < -0.99999f) {
        Vector3 axis = fabsf(from.y) > 0.9f ? (Vector3){1,0,0} : (Vector3){0,1,0};
        axis = Vector3Normalize(Vector3Cross(from, axis));
        return QuaternionFromAxisAngle(axis, 3.14159265f);
    }
    Vector3 c = Vector3Cross(from, to);
    return QuaternionNormalize((Quaternion){c.x, c.y, c.z, 1.0f + d});
}

static Quaternion ikWorldRotation(SkeletonEntry *skel, const BonePose *pose,
                                  int b, Quaternion *cache,
                                  unsigned char *done) {
    if (done[b]) return cache[b];
    int parent = skel->bones[b].parent;
    Quaternion parentQ = QuaternionIdentity();
    if (parent >= 0 && parent < skel->boneCount && parent != b)
        parentQ = ikWorldRotation(skel, pose, parent, cache, done);
    cache[b] = QuaternionNormalize(QuaternionMultiply(parentQ, pose[b].rotation));
    done[b] = 1;
    return cache[b];
}

bool ApplyTwoBoneIKToPose(SkinnedModel model, BonePose *pose, int boneCount,
                          int upperBone, int midBone, int endBone,
                          Vector3 target, Vector3 pole) {
    if (!pose || model.id == 0 || model.id > (unsigned int)g_skModelCount)
        return false;
    SkinnedModelEntry *e = &g_skModelTable[model.id];
    if (e->skeletonId <= 0 || e->skeletonId > g_skelCount) return false;
    SkeletonEntry *skel = &g_skelTable[e->skeletonId];
    if (boneCount > skel->boneCount) boneCount = skel->boneCount;
    if (upperBone < 0 || midBone < 0 || endBone < 0 ||
        upperBone >= boneCount || midBone >= boneCount || endBone >= boneCount)
        return false;

    Matrix local[VLCRLIB_MAX_BONES];
    Matrix global[VLCRLIB_MAX_BONES];
    unsigned char doneM[VLCRLIB_MAX_BONES] = {0};
    for (int b = 0; b < boneCount; ++b) {
        Matrix T = MatrixTranslate(pose[b].position.x, pose[b].position.y,
                                   pose[b].position.z);
        Matrix R = QuaternionToMatrix(pose[b].rotation);
        Matrix S = MatrixScale(pose[b].scale.x, pose[b].scale.y, pose[b].scale.z);
        local[b] = MatrixMultiply(skel->preTransform[b],
                                  MatrixMultiply(T, MatrixMultiply(R, S)));
    }
    for (int b = 0; b < boneCount; ++b)
        poseBoneGlobal(skel, b, local, global, doneM);
    Vector3 pUpper = { global[upperBone].m12, global[upperBone].m13, global[upperBone].m14 };
    Vector3 pMid   = { global[midBone].m12,   global[midBone].m13,   global[midBone].m14 };
    Vector3 pEnd   = { global[endBone].m12,   global[endBone].m13,   global[endBone].m14 };

    Vector3 newMid, newEnd;
    if (!Vector3SolveTwoBoneIK(pUpper, pMid, pEnd, target, pole, &newMid, &newEnd))
        return false;

    Quaternion worldRot[VLCRLIB_MAX_BONES];
    unsigned char doneQ[VLCRLIB_MAX_BONES] = {0};
    for (int b = 0; b < boneCount; ++b)
        ikWorldRotation(skel, pose, b, worldRot, doneQ);

    int parentUpper = skel->bones[upperBone].parent;
    Quaternion parentUpperQ = (parentUpper >= 0 && parentUpper < boneCount)
        ? worldRot[parentUpper] : QuaternionIdentity();
    Quaternion deltaUpper = ikQFromTo(Vector3Subtract(pMid, pUpper),
                                     Vector3Subtract(newMid, pUpper));
    Quaternion newUpperWorld = QuaternionNormalize(
        QuaternionMultiply(deltaUpper, worldRot[upperBone]));
    pose[upperBone].rotation = QuaternionNormalize(
        QuaternionMultiply(ikQInv(parentUpperQ), newUpperWorld));

    Quaternion deltaLower = ikQFromTo(Vector3Subtract(pEnd, pMid),
                                     Vector3Subtract(newEnd, newMid));
    Quaternion newMidWorld = QuaternionNormalize(
        QuaternionMultiply(deltaLower, worldRot[midBone]));
    pose[midBone].rotation = QuaternionNormalize(
        QuaternionMultiply(ikQInv(newUpperWorld), newMidWorld));
    return true;
}

void DrawSkinnedModelBlend(SkinnedModel model,
                           Animation from, float fromTime,
                           Animation to, float toTime, float blend,
                           Vector3 position, Vector3 rotation,
                           Vector3 scale, Color tint) {
    if (model.id == 0 || model.id > (unsigned int)g_skModelCount) return;
    if (from.id == 0 || from.id > (unsigned int)g_animCount) return;
    if (to.id == 0 || to.id > (unsigned int)g_animCount) return;

    SkinnedModelEntry *e = &g_skModelTable[model.id];
    AnimationEntry *a = &g_animTable[from.id];
    AnimationEntry *b = &g_animTable[to.id];
    SkeletonEntry *skel = &g_skelTable[e->skeletonId];
    if (a->skeleton != skel || b->skeleton != skel) return;

    float w = blend < 0.0f ? 0.0f : (blend > 1.0f ? 1.0f : blend);
    Matrix ma[VLCRLIB_MAX_BONES];
    Matrix mb[VLCRLIB_MAX_BONES];
    evaluateAnimation(a, fromTime, ma);
    evaluateAnimation(b, toTime, mb);

    Matrix out[VLCRLIB_MAX_BONES];
    for (int i = 0; i < skel->boneCount; ++i) {
        const float *pa = (const float *)&ma[i];
        const float *pb = (const float *)&mb[i];
        float *po = (float *)&out[i];
        for (int k = 0; k < 16; ++k)
            po[k] = pa[k] * (1.0f - w) + pb[k] * w;
    }
    drawSkinnedMatrices(model, out, skel->boneCount,
                        position, rotation, scale, tint);
}

void UnloadSkinnedModel(SkinnedModel model) {
    if (model.id == 0 || model.id > (unsigned int)g_skModelCount) return;
    SkinnedModelEntry *e = &g_skModelTable[model.id];
    vkDeviceWaitIdle(VK.device);

    for (int m = 0; m < e->meshCount; ++m) {
        if (e->vbos)    vkDestroyBuffer(VK.device, e->vbos[m], NULL);
        if (e->vboMems) vkFreeMemory(VK.device, e->vboMems[m], NULL);
        if (e->ibos)    vkDestroyBuffer(VK.device, e->ibos[m], NULL);
        if (e->iboMems) vkFreeMemory(VK.device, e->iboMems[m], NULL);
    }
    for (int i = 0; i < e->materialCount; ++i) UnloadMaterial(e->materials[i]);
    if (e->velocitySkinDesc && VK.velocitySkinPool)
        vkFreeDescriptorSets(VK.device, VK.velocitySkinPool, 1, &e->velocitySkinDesc);
    if (e->velocityBoneBuffer) vkDestroyBuffer(VK.device, e->velocityBoneBuffer, NULL);
    if (e->velocityBoneMem) vkFreeMemory(VK.device, e->velocityBoneMem, NULL);

    free(e->vbos); free(e->vboMems); free(e->ibos); free(e->iboMems);
    free(e->indexCounts); free(e->meshMaterials); free(e->materials);
    free(e->animationIds);
    *e = (SkinnedModelEntry){0};
}

/* ================================================================== */
/*  Shader stubs (public API, Phase 6+ ???? ??????????????)                    */
/* ================================================================== */
Shader LoadShader(const char *vs, const char *fs) { (void)vs; (void)fs; return (Shader){0}; }
void   UnloadShader(Shader s) { (void)s; }

/* ================================================================== */
/*  Frame / lifecycle                                                 */
/* ================================================================== */

/* Half-res SSAO resources (Sprint F). Pass recording is optional for now;
 * texture defaults to white so existing in-shader SSAO is unchanged. */

static bool createSSAOPipeline(void) {
    if (VK.ssaoPipeline) return true;
    VkShaderModule vert = createShaderModule("shaders/ssao.vert.spv");
    VkShaderModule frag = createShaderModule("shaders/ssao.frag.spv");
    if (!vert || !frag) {
        if (vert) vkDestroyShaderModule(VK.device, vert, NULL);
        if (frag) vkDestroyShaderModule(VK.device, frag, NULL);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {0};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi = {0};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo ia = {0};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp = {0};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs = {0};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {0};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba = {0};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    VkPipelineColorBlendStateCreateInfo cb = {0};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds = {0};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;

    /* Simple render pass for SSAO (color only) */
    VkAttachmentDescription att = {0};
    att.format = VK_FORMAT_R8_UNORM;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub = {0};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;

    VkRenderPassCreateInfo rpi = {0};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 1;
    rpi.pAttachments = &att;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sub;
    if (vkCreateRenderPass(VK.device, &rpi, NULL, &VK.ssaoPass) != VK_SUCCESS) {
        vkDestroyShaderModule(VK.device, vert, NULL);
        vkDestroyShaderModule(VK.device, frag, NULL);
        return false;
    }

    VkFramebufferCreateInfo fbci = {0};
    fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbci.renderPass = VK.ssaoPass;
    fbci.attachmentCount = 1;
    fbci.pAttachments = &VK.ssaoView;
    fbci.width = (uint32_t)VK.ssaoWidth;
    fbci.height = (uint32_t)VK.ssaoHeight;
    fbci.layers = 1;
    if (vkCreateFramebuffer(VK.device, &fbci, NULL, &VK.ssaoFB) != VK_SUCCESS) {
        vkDestroyShaderModule(VK.device, vert, NULL);
        vkDestroyShaderModule(VK.device, frag, NULL);
        return false;
    }

    /* Descriptor for depth */
    VkDescriptorSetLayoutBinding binds[2] = {0};
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[1].binding = 1;
    binds[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dli = {0};
    dli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dli.bindingCount = 2;
    dli.pBindings = binds;
    VkDescriptorSetLayout dsl;
    if (vkCreateDescriptorSetLayout(VK.device, &dli, NULL, &dsl) != VK_SUCCESS) {
        vkDestroyShaderModule(VK.device, vert, NULL);
        vkDestroyShaderModule(VK.device, frag, NULL);
        return false;
    }
    VK.ssaoDSL = dsl;

    VkPushConstantRange pcr = {0};
    pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0;
    pcr.size = 32; /* 4 floats + 2 vec2 */

    VkPipelineLayoutCreateInfo pli = {0};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &dsl;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    /* Allocate and write descriptor set for depth */
    VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 };
    VkDescriptorPoolCreateInfo dpi = {0};
    dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    VkDescriptorPool pool;
    if (vkCreateDescriptorPool(VK.device, &dpi, NULL, &pool) == VK_SUCCESS) {
        VK.ssaoPool = pool;
        VkDescriptorSetAllocateInfo ai = {0};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = pool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &dsl;
        vkAllocateDescriptorSets(VK.device, &ai, &VK.ssaoDescSet);
    }
    if (vkCreatePipelineLayout(VK.device, &pli, NULL, &VK.ssaoLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(VK.device, vert, NULL);
        vkDestroyShaderModule(VK.device, frag, NULL);
        return false;
    }

    VkGraphicsPipelineCreateInfo pi = {0};
    pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = 2;
    pi.pStages = stages;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pColorBlendState = &cb;
    pi.pDynamicState = &ds;
    pi.layout = VK.ssaoLayout;
    pi.renderPass = VK.ssaoPass;
    pi.subpass = 0;
    if (vkCreateGraphicsPipelines(VK.device, VK_NULL_HANDLE, 1, &pi, NULL, &VK.ssaoPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(VK.device, vert, NULL);
        vkDestroyShaderModule(VK.device, frag, NULL);
        return false;
    }

    vkDestroyShaderModule(VK.device, vert, NULL);
    vkDestroyShaderModule(VK.device, frag, NULL);
    fprintf(stderr, "[vlcrlib] SSAO pipeline ready\n");
    return true;
}


static bool createSSAODepthPrepass(void) {
    if (VK.ssaoDepthImage) return true;
    int w = VK.ssaoWidth > 0 ? VK.ssaoWidth : VK.swapExtent.width / 2;
    int h = VK.ssaoHeight > 0 ? VK.ssaoHeight : VK.swapExtent.height / 2;
    if (w < 1) w = 1; if (h < 1) h = 1;

    if (!vlcr_createImage((uint32_t)w, (uint32_t)h, VK_FORMAT_D32_SFLOAT,
                          VK_IMAGE_TILING_OPTIMAL,
                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          &VK.ssaoDepthImage, &VK.ssaoDepthMem)) return false;
    vlcrNameImage(VK.ssaoDepthImage, "ssao_depth");

    VkImageViewCreateInfo vi = {0};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = VK.ssaoDepthImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_D32_SFLOAT;
    vi.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(VK.device, &vi, NULL, &VK.ssaoDepthView) != VK_SUCCESS) return false;

    /* Depth-only render pass */
    VkAttachmentDescription att = {0};
    att.format = VK_FORMAT_D32_SFLOAT;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference dref = { 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub = {0};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.pDepthStencilAttachment = &dref;

    VkRenderPassCreateInfo rpi = {0};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 1;
    rpi.pAttachments = &att;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sub;
    VkSubpassDependency deps[2] = {0};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    rpi.dependencyCount = 2;
    rpi.pDependencies = deps;
    if (vkCreateRenderPass(VK.device, &rpi, NULL, &VK.ssaoDepthPass) != VK_SUCCESS) return false;

    VkFramebufferCreateInfo fbci = {0};
    fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbci.renderPass = VK.ssaoDepthPass;
    fbci.attachmentCount = 1;
    fbci.pAttachments = &VK.ssaoDepthView;
    fbci.width = (uint32_t)w;
    fbci.height = (uint32_t)h;
    fbci.layers = 1;
    if (vkCreateFramebuffer(VK.device, &fbci, NULL, &VK.ssaoDepthFB) != VK_SUCCESS) return false;

    /* First frame reads this depth before the prepass writes it. Put it in a
       defined read-only layout at creation; the prepass will discard contents. */
    {
        VkCommandBuffer cb = ibl_begin_cmd();
        VkImageMemoryBarrier b = {0};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = VK.ssaoDepthImage;
        b.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &b);
        ibl_end_cmd(cb);
    }

    fprintf(stderr, "[vlcrlib] SSAO depth prepass %dx%d ready\n", w, h);
    /* Write depth into SSAO descriptor set if available */
    if (VK.ssaoDescSet && VK.ssaoDepthView) {
        VkDescriptorImageInfo ii = {0};
        ii.imageView = VK.ssaoDepthView;
        ii.sampler = VK.ssaoSampler ? VK.ssaoSampler : VK.sampler2D;
        ii.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet wr = {0};
        wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr.dstSet = VK.ssaoDescSet;
        wr.dstBinding = 0;
        wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wr.descriptorCount = 1;
        wr.pImageInfo = &ii;
        vkUpdateDescriptorSets(VK.device, 1, &wr, 0, NULL);
    }
    return true;
}

static void recordSSAODepthPrepass(VkCommandBuffer cb) {
    if (!VK.ssaoDepthFB || !VK.ssaoDepthPass) return;
    VkClearValue clear;
    clear.depthStencil.depth = 1.0f;
    clear.depthStencil.stencil = 0;
    VkRenderPassBeginInfo rp = {0};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = VK.ssaoDepthPass;
    rp.framebuffer = VK.ssaoDepthFB;
    rp.renderArea.extent.width = (uint32_t)VK.ssaoWidth;
    rp.renderArea.extent.height = (uint32_t)VK.ssaoHeight;
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp = { 0, 0, (float)VK.ssaoWidth, (float)VK.ssaoHeight, 0.f, 1.f };
    vkCmdSetViewport(cb, 0, 1, &vp);
    VkRect2D sc = { {0,0}, { (uint32_t)VK.ssaoWidth, (uint32_t)VK.ssaoHeight } };
    vkCmdSetScissor(cb, 0, 1, &sc);
    /* Camera-space depth for accurate SSAO */
    if (VK.shadowPipeline && VK.shadowCasterCountPrev > 0) {
        Matrix camVP = MatrixMultiply(VK.proj3D, VK.view3D);
        for (int i = 0; i < VK.shadowCasterCountPrev; ++i) {
            shadow_draw_caster_depth(cb, camVP, &VK.shadowCastersPrev[i]);
        }
    }
    vkCmdEndRenderPass(cb);
}

static void recordSSAOPass(VkCommandBuffer cb) {
    if (!VK.ssaoReady || !VK.ssaoPipeline || !VK.ssaoEnabled) return;
    /* The preceding depth prepass writes the scene depth consumed by ssao.frag. */
    VkClearValue clear = {0};
    clear.color.float32[0] = 1.0f;
    VkRenderPassBeginInfo rp = {0};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = VK.ssaoPass;
    rp.framebuffer = VK.ssaoFB;
    rp.renderArea.extent.width = (uint32_t)VK.ssaoWidth;
    rp.renderArea.extent.height = (uint32_t)VK.ssaoHeight;
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);
    if (VK.ssaoDescSet && VK.ssaoPipeline) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, VK.ssaoPipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, VK.ssaoLayout, 0, 1, &VK.ssaoDescSet, 0, NULL);
        float pc[8] = {
            VK.ssaoRadius, VK.ssaoIntensity, 0.1f, 100.0f,
            1.0f / (float)VK.ssaoWidth, 1.0f / (float)VK.ssaoHeight, 1.0f, 1.0f
        };
        vkCmdPushConstants(cb, VK.ssaoLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 32, pc);
        VkViewport vp = { 0, 0, (float)VK.ssaoWidth, (float)VK.ssaoHeight, 0.f, 1.f };
        vkCmdSetViewport(cb, 0, 1, &vp);
        VkRect2D sc = { {0,0}, { (uint32_t)VK.ssaoWidth, (uint32_t)VK.ssaoHeight } };
        vkCmdSetScissor(cb, 0, 1, &sc);
        vkCmdDraw(cb, 3, 1, 0, 0); /* fullscreen triangle */
    }
    vkCmdEndRenderPass(cb);
}

static void ssaoGraphCallback(VkCommandBuffer cb, int pass, void *user) {
    (void)user;
    if (pass == 0) recordSSAODepthPrepass(cb);
    else recordSSAOPass(cb);
}

static bool recordSSAOGraph(VkCommandBuffer cb) {
    if (!VK.ssaoEnabled || !VK.ssaoDepthView || !VK.ssaoView) return false;
    /* The graph path keeps a stale depth-layout expectation after the
       prepass render pass; use the explicit prepass/SSAO pair until the
       graph barrier plan tracks the attachment final layout. */
    return false;
    VrgGraph *g = vrgCreate();
    if (!g) return false;
    VrgImageDesc depth = { "ssao_depth", (uint32_t)VK.ssaoWidth, (uint32_t)VK.ssaoHeight,
        VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
    VrgImageDesc color = { "ssao", (uint32_t)VK.ssaoWidth, (uint32_t)VK.ssaoHeight,
        VK_FORMAT_R8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    int depthId = vrgAddExternalImage(g, &depth, VK.ssaoDepthImage, VK.ssaoDepthView);
    int colorId = vrgAddExternalImage(g, &color, VK.ssaoImage, VK.ssaoView);
    int prepass = vrgAddPass(g, "ssao_depth");
    int ssao = vrgAddPass(g, "ssao");
    if (depthId < 0 || colorId < 0 || prepass < 0 || ssao < 0) {
        vrgDestroy(g);
        return false;
    }
    vrgPassWriteImage(g, prepass, depthId,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    vrgPassReadImage(g, ssao, depthId,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
    vrgPassWriteImage(g, ssao, colorId,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    bool ok = vrgCompile(g) && vrgExecute(g, cb, ssaoGraphCallback, NULL);
    if (!ok) fprintf(stderr, "[vlcrlib] SSAO graph failed: %s\n",
                     vrgGetError(g) ? vrgGetError(g) : "execute");
    vrgDestroy(g);
    return ok;
}

static bool createSSAOResources(void) {
    if (VK.ssaoReady) return true;
    int w = VK.swapExtent.width / 2;
    int h = VK.swapExtent.height / 2;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    VK.ssaoWidth = w;
    VK.ssaoHeight = h;

    if (!vlcr_createImage((uint32_t)w, (uint32_t)h, VK_FORMAT_R8_UNORM,
                          VK_IMAGE_TILING_OPTIMAL,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          &VK.ssaoImage, &VK.ssaoMem)) return false;
    vlcrNameImage(VK.ssaoImage, "ssao_color");

    VkImageViewCreateInfo vi = {0};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = VK.ssaoImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8_UNORM;
    vi.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(VK.device, &vi, NULL, &VK.ssaoView) != VK_SUCCESS) return false;

    VkSamplerCreateInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(VK.device, &si, NULL, &VK.ssaoSampler) != VK_SUCCESS) return false;

    /* Initialise the half-res AO target to white and put it in the sampled
       layout. The first frame can sample it before the SSAO pass has run. */
    {
        VkCommandBuffer cb = ibl_begin_cmd();
        VkImageMemoryBarrier b = {0};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = VK.ssaoImage;
        b.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b);
        VkClearColorValue white = { .float32 = { 1.0f, 1.0f, 1.0f, 1.0f } };
        VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdClearColorImage(cb, VK.ssaoImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &white, 1, &range);
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &b);
        ibl_end_cmd(cb);
    }

    VK.ssaoReady = true;
    fprintf(stderr, "[vlcrlib] half-res SSAO texture %dx%d ready\n", w, h);
    return true;
}



static bool createBloomPipeline(void) {
    if (VK.bloomPipeline) return true;
    VkShaderModule vert = createShaderModule("shaders/bloom.vert.spv");
    VkShaderModule frag = createShaderModule("shaders/bloom.frag.spv");
    if (!vert || !frag) return false;

    VkPipelineShaderStageCreateInfo stages[2] = {0};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi = {0};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia = {0};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = {0};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = {0};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = {0};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba = {0};
    cba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb = {0};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds = {0};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;

    VkAttachmentDescription att = {0};
    att.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub = {0};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkRenderPassCreateInfo rpi = {0};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 1;
    rpi.pAttachments = &att;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sub;
    if (vkCreateRenderPass(VK.device, &rpi, NULL, &VK.bloomPass) != VK_SUCCESS) return false;

    VkFramebufferCreateInfo fbci = {0};
    fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbci.renderPass = VK.bloomPass;
    fbci.attachmentCount = 1;
    fbci.pAttachments = &VK.bloomView;
    fbci.width = (uint32_t)VK.bloomWidth;
    fbci.height = (uint32_t)VK.bloomHeight;
    fbci.layers = 1;
    if (vkCreateFramebuffer(VK.device, &fbci, NULL, &VK.bloomFB) != VK_SUCCESS) return false;

    fbci.pAttachments = &VK.bloomView2;
    if (vkCreateFramebuffer(VK.device, &fbci, NULL, &VK.bloomFB2) != VK_SUCCESS) return false;

    VkDescriptorSetLayoutBinding bind = {0};
    bind.binding = 0;
    bind.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bind.descriptorCount = 1;
    bind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dli = {0};
    dli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dli.bindingCount = 1;
    dli.pBindings = &bind;
    VkDescriptorSetLayout dsl;
    if (vkCreateDescriptorSetLayout(VK.device, &dli, NULL, &dsl) != VK_SUCCESS) return false;
    VK.bloomDSL = dsl;

    VkPushConstantRange pcr = {0};
    pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.size = 20;
    VkPipelineLayoutCreateInfo pli = {0};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &dsl;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(VK.device, &pli, NULL, &VK.bloomLayout) != VK_SUCCESS) return false;

    /* One descriptor set per bloom stage. Updating a bound descriptor set
       while recording invalidates the command buffer, so the sets are
       written once here and only re-bound during the pass. */
    VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 };
    VkDescriptorPoolCreateInfo dpi = {0};
    dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.maxSets = 3;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    VkDescriptorPool pool;
    if (vkCreateDescriptorPool(VK.device, &dpi, NULL, &pool) == VK_SUCCESS) {
        VK.bloomPool = pool;
        VkDescriptorSet *sets[3] = { &VK.bloomDescSet, &VK.bloomDescSetH, &VK.bloomDescSetV };
        VkImageView views[3] = { VK.sceneColorView, VK.bloomView, VK.bloomView2 };
        for (int s = 0; s < 3; ++s) {
            VkDescriptorSetAllocateInfo ai = {0};
            ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ai.descriptorPool = pool;
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &dsl;
            if (vkAllocateDescriptorSets(VK.device, &ai, sets[s]) != VK_SUCCESS) continue;
            if (!views[s]) continue;
            VkDescriptorImageInfo ii = {0};
            ii.imageView = views[s];
            ii.sampler = VK.bloomSampler ? VK.bloomSampler : VK.sampler2D;
            ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet wr = {0};
            wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wr.dstSet = *sets[s];
            wr.dstBinding = 0;
            wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            wr.descriptorCount = 1;
            wr.pImageInfo = &ii;
            vkUpdateDescriptorSets(VK.device, 1, &wr, 0, NULL);
        }
    }

    VkGraphicsPipelineCreateInfo pi = {0};
    pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = 2;
    pi.pStages = stages;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pColorBlendState = &cb;
    pi.pDynamicState = &ds;
    pi.layout = VK.bloomLayout;
    pi.renderPass = VK.bloomPass;
    if (vkCreateGraphicsPipelines(VK.device, VK_NULL_HANDLE, 1, &pi, NULL, &VK.bloomPipeline) != VK_SUCCESS) return false;

    vkDestroyShaderModule(VK.device, vert, NULL);
    vkDestroyShaderModule(VK.device, frag, NULL);
    fprintf(stderr, "[vlcrlib] bloom pipeline ready\n");
    return true;
}

static void bloomDrawPass(VkCommandBuffer cb, VkFramebuffer fb,
                          VkDescriptorSet desc, const float pc[6]) {
    VkClearValue clear = {0};
    VkRenderPassBeginInfo rp = {0};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = VK.bloomPass;
    rp.framebuffer = fb;
    rp.renderArea.extent.width = (uint32_t)VK.bloomWidth;
    rp.renderArea.extent.height = (uint32_t)VK.bloomHeight;
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp = { 0, 0, (float)VK.bloomWidth, (float)VK.bloomHeight, 0.f, 1.f };
    vkCmdSetViewport(cb, 0, 1, &vp);
    VkRect2D sc = { {0,0}, { (uint32_t)VK.bloomWidth, (uint32_t)VK.bloomHeight } };
    vkCmdSetScissor(cb, 0, 1, &sc);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, VK.bloomPipeline);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, VK.bloomLayout,
                            0, 1, &desc, 0, NULL);
    vkCmdPushConstants(cb, VK.bloomLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 20, pc);
    vkCmdDraw(cb, 3, 1, 0, 0);
    vkCmdEndRenderPass(cb);
}

static void recordBloomBright(VkCommandBuffer cb) {
    if (!VK.bloomReady || !VK.bloomPipeline || !VK.bloomDescSet) return;
    float thr = VK.bloomThreshold > 0.f ? VK.bloomThreshold : 1.0f;
    float inten = VK.bloomIntensity > 0.f ? VK.bloomIntensity : 0.5f;
    float pc[6] = { thr, inten, 1.f/(float)VK.bloomWidth,
                    1.f/(float)VK.bloomHeight, 0.f, 0.f };
    bloomDrawPass(cb, VK.bloomFB, VK.bloomDescSet, pc);
}

static void recordBloomH(VkCommandBuffer cb) {
    if (!VK.bloomFB2 || !VK.bloomDescSetH) return;
    float pc[6] = { 0.f, 0.f, 1.f/(float)VK.bloomWidth,
                    1.f/(float)VK.bloomHeight, 1.f, 0.f };
    bloomDrawPass(cb, VK.bloomFB2, VK.bloomDescSetH, pc);
}

static void recordBloomV(VkCommandBuffer cb) {
    if (!VK.bloomDescSetV) return;
    float pc[6] = { 0.f, 0.f, 1.f/(float)VK.bloomWidth,
                    1.f/(float)VK.bloomHeight, 2.f, 0.f };
    bloomDrawPass(cb, VK.bloomFB, VK.bloomDescSetV, pc);
}

static void recordBloomPass(VkCommandBuffer cb) {
    if (!VK.bloomReady || !VK.bloomPipeline) return;
    recordBloomBright(cb);
    recordBloomH(cb);
    recordBloomV(cb);
}

static void bloomGraphCallback(VkCommandBuffer cb, int pass, void *user) {
    (void)user;
    if (pass == 0) recordBloomBright(cb);
    else if (pass == 1) recordBloomH(cb);
    else recordBloomV(cb);
}

static bool recordBloomGraph(VkCommandBuffer cb) {
    if (!VK.bloomReady || !VK.bloomPipeline || !VK.bloomFB) return false;
    VrgGraph *g = vrgCreate();
    if (!g) return false;
    VrgImageDesc scene = { "scene", (uint32_t)VK.bloomWidth, (uint32_t)VK.bloomHeight,
        VK.swapFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VrgImageDesc bloom = { "bloom", (uint32_t)VK.bloomWidth, (uint32_t)VK.bloomHeight,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VrgImageDesc bloom2 = bloom;
    bloom2.name = "bloom2";
    int sceneId = vrgAddExternalImage(g, &scene, VK.sceneColorImage, VK.sceneColorView);
    int bloomId = vrgAddExternalImage(g, &bloom, VK.bloomImage, VK.bloomView);
    int bloom2Id = vrgAddExternalImage(g, &bloom2, VK.bloomImage2, VK.bloomView2);
    int bright = vrgAddPass(g, "bloom_bright");
    int blurH = vrgAddPass(g, "bloom_blur_h");
    int blurV = vrgAddPass(g, "bloom_blur_v");
    if (sceneId < 0 || bloomId < 0 || bloom2Id < 0 ||
        bright < 0 || blurH < 0 || blurV < 0) {
        vrgDestroy(g);
        return false;
    }
    vrgPassReadImage(g, bright, sceneId, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    vrgPassWriteImage(g, bright, bloomId, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    vrgPassReadImage(g, blurH, bloomId, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    vrgPassWriteImage(g, blurH, bloom2Id, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    vrgPassReadImage(g, blurV, bloom2Id, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    vrgPassWriteImage(g, blurV, bloomId, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    bool ok = vrgCompile(g) && vrgExecute(g, cb, bloomGraphCallback, NULL);
    if (!ok) fprintf(stderr, "[vlcrlib] bloom graph failed: %s\n",
                     vrgGetError(g) ? vrgGetError(g) : "execute");
    vrgDestroy(g);
    return ok;
}


static bool createSceneOffscreen(void) {
    if (VK.sceneReady) return true;
    uint32_t w = VK.swapExtent.width;
    uint32_t h = VK.swapExtent.height;
    if (w < 1) w = 1; if (h < 1) h = 1;
    VK.sceneWidth = w;
    VK.sceneHeight = h;

    /* The scene target uses the swapchain format so every pipeline created
       for VK.renderPass (3D, PBR, skin, 2D, skybox) is render-pass compatible
       with VK.scenePass as well. The shaders emit display-referred linear
       colour and the sRGB attachment performs the final encode. */
    if (!vlcr_createImage(w, h, VK.swapFormat, VK_IMAGE_TILING_OPTIMAL,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          &VK.sceneColorImage, &VK.sceneColorMem)) return false;
    vlcrNameImage(VK.sceneColorImage, "scene_color");
    VkImageViewCreateInfo vi = {0};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = VK.sceneColorImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK.swapFormat;
    vi.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(VK.device, &vi, NULL, &VK.sceneColorView) != VK_SUCCESS) return false;

    if (!vlcr_createImage(w, h, VK_FORMAT_D32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          &VK.sceneDepthImage, &VK.sceneDepthMem)) return false;
    vi.image = VK.sceneDepthImage;
    vi.format = VK_FORMAT_D32_SFLOAT;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (vkCreateImageView(VK.device, &vi, NULL, &VK.sceneDepthView) != VK_SUCCESS) return false;

    /* Temporal history is renderer-owned; only barrier-planned by the graph. */
    if (!vlcr_createImage(w, h, VK.swapFormat, VK_IMAGE_TILING_OPTIMAL,
                          VK_IMAGE_USAGE_SAMPLED_BIT |
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          &VK.taaHistoryImage, &VK.taaHistoryMem)) return false;
    vlcrNameImage(VK.taaHistoryImage, "taa_history");
    vi.image = VK.taaHistoryImage;
    vi.format = VK.swapFormat;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if (vkCreateImageView(VK.device, &vi, NULL, &VK.taaHistoryView) != VK_SUCCESS)
        return false;
    transitionImageLayoutMips(VK.taaHistoryImage, VK.swapFormat,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 1);
    VK.taaHistoryWidth = w;
    VK.taaHistoryHeight = h;
    VK.taaReset = 1;

    /* Color + depth render pass for scene */
    VkAttachmentDescription atts[2] = {0};
    atts[0].format = VK.swapFormat;
    atts[0].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    atts[1].format = VK_FORMAT_D32_SFLOAT;
    atts[1].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atts[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference cref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference dref = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub = {0};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &cref;
    sub.pDepthStencilAttachment = &dref;

    /* Must match VK.renderPass's subpass dependency exactly: Vulkan render
       pass compatibility compares dependencyCount and dependency contents,
       so pipelines created for the swapchain pass can be reused here. */
    VkSubpassDependency dep = {0};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpi = {0};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 2;
    rpi.pAttachments = atts;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sub;
    rpi.dependencyCount = 1;
    rpi.pDependencies = &dep;
    if (vkCreateRenderPass(VK.device, &rpi, NULL, &VK.scenePass) != VK_SUCCESS) return false;

    VkImageView views[2] = { VK.sceneColorView, VK.sceneDepthView };
    VkFramebufferCreateInfo fbci = {0};
    fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbci.renderPass = VK.scenePass;
    fbci.attachmentCount = 2;
    fbci.pAttachments = views;
    fbci.width = w;
    fbci.height = h;
    fbci.layers = 1;
    if (vkCreateFramebuffer(VK.device, &fbci, NULL, &VK.sceneFB) != VK_SUCCESS) return false;

    VK.sceneReady = true;
    fprintf(stderr, "[vlcrlib] offscreen scene target %ux%u ready\n", w, h);
    return true;
}

static bool createBloomResources(void) {
    if (VK.bloomReady) return true;
    int w = VK.swapExtent.width / 2;
    int h = VK.swapExtent.height / 2;
    if (w < 1) w = 1; if (h < 1) h = 1;
    VK.bloomWidth = w; VK.bloomHeight = h;

    if (!vlcr_createImage((uint32_t)w, (uint32_t)h, VK_FORMAT_R16G16B16A16_SFLOAT,
                          VK_IMAGE_TILING_OPTIMAL,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          &VK.bloomImage, &VK.bloomMem)) return false;
    vlcrNameImage(VK.bloomImage, "bloom_color");
    VkImageViewCreateInfo vi = {0};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = VK.bloomImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    vi.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(VK.device, &vi, NULL, &VK.bloomView) != VK_SUCCESS) return false;

    VkSamplerCreateInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(VK.device, &si, NULL, &VK.bloomSampler) != VK_SUCCESS) return false;

    /* Ping-pong for blur */
    if (!vlcr_createImage((uint32_t)w, (uint32_t)h, VK_FORMAT_R16G16B16A16_SFLOAT,
                          VK_IMAGE_TILING_OPTIMAL,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          &VK.bloomImage2, &VK.bloomMem2)) return false;
    vi.image = VK.bloomImage2;
    if (vkCreateImageView(VK.device, &vi, NULL, &VK.bloomView2) != VK_SUCCESS) return false;
    VK.bloomReady = true;
    fprintf(stderr, "[vlcrlib] half-res bloom texture %dx%d ready\n", w, h);
    return true;
}

VkInitResult InitGraphicsDevice(void) {
    /* Public lifecycle is idempotent for a live device; re-initializing would
       memset live Vulkan handles and leak the previous device. */
    if (VK.device) return VKINIT_OK;
    memset(&VK, 0, sizeof(VK));
    /* Default studio lighting (overridable via Set*Light) */
    VK.lightDirectionalEnabled = true;
    VK.lightDirectionalDirection = (Vector3){-0.35f, -0.85f, -0.40f};
    VK.lightDirectionalColor = (Color){255, 248, 240, 255};
    VK.lightDirectionalIntensity = 2.2f;
    VK.lightAmbientEnabled = true;
    VK.lightAmbientColor = (Color){70, 80, 110, 255};
    VK.lightAmbientIntensity = 0.28f;
    VK.iblIntensity = 0.0f; /* procedural until LoadIBL */
    VK.iblLoaded = false;
    VK.skyboxEnabled = 1;
    VK.atmosphereEnabled = 0;
    VK.atmosphereTime = 12.0f;
    VK.cloudCoverage = 0.20f;
    VK.haze = 0.08f;
    VK.starIntensity = 0.7f;
    VK.sunDirection = (Vector3){ 0.0f, 1.0f, 0.0f };
    VK.sunIntensity = 4.0f;
    VK.moonDirection = (Vector3){ 0.0f, -1.0f, 0.0f };
    VK.moonIntensity = 0.5f;
    VK.sunAngularRadius = 0.009f;
    VK.moonAngularRadius = 0.011f;
    VK.shadowsEnabled = false;
    VK.shadowMapSize = 2048;
    VK.exposure = 1.0f;
    VK.fxaaEnabled = 1;
    VK.ssaoEnabled = 1;
    VK.ssaoRadius = 0.35f;
    VK.ssaoIntensity = 0.85f;
    VK.bloomThreshold = 1.0f;
    VK.bloomIntensity = 0.35f;
    VK.shadowBias = 0.005f;
    VK.shadowStrength = 1.0f;
    VK.shadowSoftness = 1.5f;
    VK.shadowLightVP = MatrixIdentity();
    VK.shadowResourcesReady = false;
    VK.shadowCasterCount = 0;
    VK.transCount = 0;
    VK.shadowCasterCountPrev = 0;
    VK.clearColor = (VkClearValue){ .color = {{0.05f, 0.05f, 0.08f, 1.0f}} };
    VK.view = MatrixIdentity();
    VK.proj = MatrixIdentity();

    if (!createInstance()) return VKINIT_ERR_INSTANCE;

    SDL_Window *win = (SDL_Window *)vcore_GetWindow();
    if (!SDL_Vulkan_CreateSurface(win, VK.instance, NULL, &VK.surface)) return VKINIT_ERR_SURFACE;

    if (!pickPhysicalDevice()) return VKINIT_ERR_PHYSICAL_DEVICE;
    if (!createDevice())       return VKINIT_ERR_DEVICE;

    int w, h; vcore_GetScreenSize(&w, &h);
    if (!createSwapchain((uint32_t)w, (uint32_t)h)) return VKINIT_ERR_SWAPCHAIN;
    if (!createDepthResources())                    return VKINIT_ERR_RENDERPASS;
    if (!createRenderPass())                        return VKINIT_ERR_RENDERPASS;
    if (!createFramebuffers())                      return VKINIT_ERR_RENDERPASS;
    if (!createCommands())                          return VKINIT_ERR_COMMAND;
    if (!createSync())                              return VKINIT_ERR_SYNC;

    if (!createRenderPassRT()) return VKINIT_ERR_RENDERPASS;
    createSSAOResources(); createSSAOPipeline(); createSSAODepthPrepass();
    /* Share the depth-only pipeline with SSAO even when shadow mapping is off. */
    if (!createShadowResources(VK.shadowMapSize))
        fprintf(stderr, "[vlcrlib] SSAO depth pipeline unavailable; AO will remain neutral\n");
    /* scene target must exist before bloom/composite descriptor sets are
       written: both sample VK.sceneColorView. */
    createBloomResources();
    createSceneOffscreen();
    createVelocityResources();
    createBloomPipeline();
    createCompositePipeline(); /* non-fatal */

    VkFenceCreateInfo fci = {0};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(VK.device, &fci, NULL, &VK.offscreenFence) != VK_SUCCESS)
        return VKINIT_ERR_SYNC;

    if (!createSampler2D())        return VKINIT_ERR_TEXTURE;
    if (!createDescriptorSet2D())  return VKINIT_ERR_PIPELINE;
    if (!createPipeline2D())       return VKINIT_ERR_PIPELINE;
    if (VK.rayQuerySupported && !createRayQueryDemoResources())
        fprintf(stderr, "[vlcrlib] ray-query demo setup failed; using software fallback shader\n");
    if (!createRayTraceDemoPipeline()) return VKINIT_ERR_PIPELINE;
    if (!createWhiteTexture())     return VKINIT_ERR_TEXTURE;
    if (!ibl_ensure_default_cube()) return VKINIT_ERR_TEXTURE;
    if (!createSkyboxPipeline())
        fprintf(stderr, "[vlcrlib] environment skybox unavailable; background clear only\n");

    /* 3D unlit */
    VkPipelineLayoutCreateInfo pli = {0};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &VK.dsl3D; /* created below... order fix: create layout after dsl3D */
    /* dsl3D ?????????????????? ?? createDescriptorSet3D, ???? ?????? ?????????? ???? pipeline.
       ?????????????? ?????????????? dsl3D ?????????? ????????????????: */
    {
        VkDescriptorSetLayoutBinding binds[2] = {0};
        binds[0].binding = 0;
        binds[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        binds[0].descriptorCount = 1;
        binds[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        binds[1].binding = 1;
        binds[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binds[1].descriptorCount = 1;
        binds[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo li = {0};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 2;
        li.pBindings = binds;
        vkCreateDescriptorSetLayout(VK.device, &li, NULL, &VK.dsl3D);
    }
    if (vkCreatePipelineLayout(VK.device, &pli, NULL, &VK.layout3D) != VK_SUCCESS)
        return VKINIT_ERR_PIPELINE;

    if (!createUBO3D())            return VKINIT_ERR_PIPELINE;

    /* createDescriptorSet3D ???????????????????? ?????? ?????????????????? dsl3D */
    {
        VkDescriptorPoolSize ps[2] = {0};
        ps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; ps[0].descriptorCount = 1;
        ps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps[1].descriptorCount = 1;
        VkDescriptorPoolCreateInfo pi = {0};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.poolSizeCount = 2; pi.pPoolSizes = ps; pi.maxSets = 1;
        vkCreateDescriptorPool(VK.device, &pi, NULL, &VK.descPool3D);
        VkDescriptorSetAllocateInfo ai = {0};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = VK.descPool3D;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &VK.dsl3D;
        vkAllocateDescriptorSets(VK.device, &ai, &VK.descSet3D);
        VkDescriptorBufferInfo bi = { VK.ubo3D, 0, sizeof(UBO3D) };
        VkWriteDescriptorSet w0 = {0};
        w0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w0.dstSet = VK.descSet3D; w0.dstBinding = 0;
        w0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        w0.descriptorCount = 1; w0.pBufferInfo = &bi;
        TextureEntry *te = &g_textureTable[VK.whiteTexture.id];
        VkDescriptorImageInfo ii = { te->samp, te->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet w1 = {0};
        w1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w1.dstSet = VK.descSet3D; w1.dstBinding = 1;
        w1.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w1.descriptorCount = 1; w1.pImageInfo = &ii;
        VkWriteDescriptorSet ws[2] = { w0, w1 };
        vkUpdateDescriptorSets(VK.device, 2, ws, 0, NULL);
    }

    if (!createPipeline3D(false))  return VKINIT_ERR_PIPELINE;
    if (!createPipeline3D(true))   return VKINIT_ERR_PIPELINE;

    /* PBR */
    if (!createUBOPBR())                 return VKINIT_ERR_PIPELINE;
    if (!createLightBuffer())            return VKINIT_ERR_PIPELINE;
    if (!createDescriptorSetLayoutPBR()) return VKINIT_ERR_PIPELINE;
    if (!createPipelinePBR())            return VKINIT_ERR_PIPELINE;

    /* Skin */
    if (!createBoneBuffer())              return VKINIT_ERR_PIPELINE;
    if (!createDescriptorSetLayoutSkin()) return VKINIT_ERR_PIPELINE;
    if (!createPipelineSkin())            return VKINIT_ERR_PIPELINE;

    VK.view3D = MatrixIdentity();
    VK.proj3D = MatrixIdentity();
    VK.viewport2DW = 0;
    VK.viewport2DH = 0;

    /* Do NOT auto-LoadIBL here: bake/upload can hang or TDR the GPU (~2-5s then
       window dies). Call vgraphics_LoadIBL(NULL) or path explicitly when ready. */
    VK.iblIntensity = 0.0f;
    VK.iblLoaded = false;
    fprintf(stderr, "[vlcrlib] IBL off by default (shader studio env). Call LoadIBL to enable.\n");

    return VKINIT_OK;
}

void CloseGraphicsDevice(void) {
    if (!VK.device) return;
    int validationErrors = VK.validationErrors;
    vkDeviceWaitIdle(VK.device);
    releaseCompletedTextures(0, true);
    releaseCompletedMeshes(0, true);
    releaseCompletedRenderTargets(0, true);
    material_release_completed(0, true);
    destroyShadowResources();

    if (VK.pipeline2D)     vkDestroyPipeline(VK.device, VK.pipeline2D, NULL);
    if (VK.layout2D)       vkDestroyPipelineLayout(VK.device, VK.layout2D, NULL);
    if (VK.vert2D)         vkDestroyShaderModule(VK.device, VK.vert2D, NULL);
    if (VK.frag2D)         vkDestroyShaderModule(VK.device, VK.frag2D, NULL);
    if (VK.rayTraceDemoPipeline) vkDestroyPipeline(VK.device, VK.rayTraceDemoPipeline, NULL);
    if (VK.rayTraceDemoLayout) vkDestroyPipelineLayout(VK.device, VK.rayTraceDemoLayout, NULL);
    if (VK.rayTraceDemoVert) vkDestroyShaderModule(VK.device, VK.rayTraceDemoVert, NULL);
    if (VK.rayTraceDemoFrag) vkDestroyShaderModule(VK.device, VK.rayTraceDemoFrag, NULL);
    destroyRayQueryDemoResources();
    if (VK.dsl2D)          vkDestroyDescriptorSetLayout(VK.device, VK.dsl2D, NULL);
    if (VK.descPool2D)     vkDestroyDescriptorPool(VK.device, VK.descPool2D, NULL);
    if (VK.sampler2D)      vkDestroySampler(VK.device, VK.sampler2D, NULL);
    if (VK.vbo2D)          vkDestroyBuffer(VK.device, VK.vbo2D, NULL);
    if (VK.vbo2DMem)       vkFreeMemory(VK.device, VK.vbo2DMem, NULL);

    if (VK.pipeline3D)     vkDestroyPipeline(VK.device, VK.pipeline3D, NULL);
    if (VK.pipeline3DLine) vkDestroyPipeline(VK.device, VK.pipeline3DLine, NULL);
    if (VK.layout3D)       vkDestroyPipelineLayout(VK.device, VK.layout3D, NULL);
    if (VK.dsl3D)          vkDestroyDescriptorSetLayout(VK.device, VK.dsl3D, NULL);
    if (VK.descPool3D)     vkDestroyDescriptorPool(VK.device, VK.descPool3D, NULL);
    if (VK.ubo3D)          vkDestroyBuffer(VK.device, VK.ubo3D, NULL);
    if (VK.ubo3DMem)       vkFreeMemory(VK.device, VK.ubo3DMem, NULL);

    if (VK.pipelinePBR)            vkDestroyPipeline(VK.device, VK.pipelinePBR, NULL);
    if (VK.pipelinePBRNoCull)      vkDestroyPipeline(VK.device, VK.pipelinePBRNoCull, NULL);
    if (VK.pipelinePBRBlend)       vkDestroyPipeline(VK.device, VK.pipelinePBRBlend, NULL);
    if (VK.pipelinePBRBlendNoCull) vkDestroyPipeline(VK.device, VK.pipelinePBRBlendNoCull, NULL);
    if (VK.skyboxPipeline) vkDestroyPipeline(VK.device, VK.skyboxPipeline, NULL);
    if (VK.skyboxLayout)   vkDestroyPipelineLayout(VK.device, VK.skyboxLayout, NULL);
    if (VK.skyboxPool)     vkDestroyDescriptorPool(VK.device, VK.skyboxPool, NULL);
    if (VK.skyboxDSL)      vkDestroyDescriptorSetLayout(VK.device, VK.skyboxDSL, NULL);
    if (VK.layoutPBR)      vkDestroyPipelineLayout(VK.device, VK.layoutPBR, NULL);
        ibl_destroy_gpu();
    if (VK.dslPBR)         vkDestroyDescriptorSetLayout(VK.device, VK.dslPBR, NULL);
    if (VK.descPoolPBR)    vkDestroyDescriptorPool(VK.device, VK.descPoolPBR, NULL);
    if (VK.uboPBR)         vkDestroyBuffer(VK.device, VK.uboPBR, NULL);
    if (VK.uboPBRMem)      vkFreeMemory(VK.device, VK.uboPBRMem, NULL);
    if (VK.lightBuffer)    vkDestroyBuffer(VK.device, VK.lightBuffer, NULL);
    if (VK.lightBufferMem) vkFreeMemory(VK.device, VK.lightBufferMem, NULL);

    if (VK.pipelineSkin)            vkDestroyPipeline(VK.device, VK.pipelineSkin, NULL);
    if (VK.pipelineSkinNoCull)      vkDestroyPipeline(VK.device, VK.pipelineSkinNoCull, NULL);
    if (VK.pipelineSkinBlend)       vkDestroyPipeline(VK.device, VK.pipelineSkinBlend, NULL);
    if (VK.pipelineSkinBlendNoCull) vkDestroyPipeline(VK.device, VK.pipelineSkinBlendNoCull, NULL);
    if (VK.layoutSkin)     vkDestroyPipelineLayout(VK.device, VK.layoutSkin, NULL);
    if (VK.dslSkin)        vkDestroyDescriptorSetLayout(VK.device, VK.dslSkin, NULL);
    if (VK.descPoolSkin)   vkDestroyDescriptorPool(VK.device, VK.descPoolSkin, NULL);
    if (VK.boneBuffer)     vkDestroyBuffer(VK.device, VK.boneBuffer, NULL);
    if (VK.boneBufferMem)  vkFreeMemory(VK.device, VK.boneBufferMem, NULL);

    if (g_lineVbo)         vkDestroyBuffer(VK.device, g_lineVbo, NULL);
    if (g_lineVboMem)      vkFreeMemory(VK.device, g_lineVboMem, NULL);

    /* Post-process: SSAO. */
    if (VK.ssaoPipeline)   vkDestroyPipeline(VK.device, VK.ssaoPipeline, NULL);
    if (VK.ssaoLayout)     vkDestroyPipelineLayout(VK.device, VK.ssaoLayout, NULL);
    if (VK.ssaoDepthFB)    vkDestroyFramebuffer(VK.device, VK.ssaoDepthFB, NULL);
    if (VK.ssaoDepthPass)  vkDestroyRenderPass(VK.device, VK.ssaoDepthPass, NULL);
    if (VK.ssaoDepthView)  vkDestroyImageView(VK.device, VK.ssaoDepthView, NULL);
    if (VK.ssaoDepthImage) vkDestroyImage(VK.device, VK.ssaoDepthImage, NULL);
    if (VK.ssaoDepthMem)   vkFreeMemory(VK.device, VK.ssaoDepthMem, NULL);
    if (VK.ssaoFB)         vkDestroyFramebuffer(VK.device, VK.ssaoFB, NULL);
    if (VK.ssaoPass)       vkDestroyRenderPass(VK.device, VK.ssaoPass, NULL);
    if (VK.ssaoView)       vkDestroyImageView(VK.device, VK.ssaoView, NULL);
    if (VK.ssaoImage)      vkDestroyImage(VK.device, VK.ssaoImage, NULL);
    if (VK.ssaoMem)        vkFreeMemory(VK.device, VK.ssaoMem, NULL);
    if (VK.ssaoSampler)    vkDestroySampler(VK.device, VK.ssaoSampler, NULL);
    if (VK.ssaoPool)       vkDestroyDescriptorPool(VK.device, VK.ssaoPool, NULL);
    if (VK.ssaoDSL)        vkDestroyDescriptorSetLayout(VK.device, VK.ssaoDSL, NULL);

    /* Post-process: bloom. */
    if (VK.bloomPipeline)  vkDestroyPipeline(VK.device, VK.bloomPipeline, NULL);
    if (VK.bloomLayout)    vkDestroyPipelineLayout(VK.device, VK.bloomLayout, NULL);
    if (VK.bloomFB)        vkDestroyFramebuffer(VK.device, VK.bloomFB, NULL);
    if (VK.bloomFB2)       vkDestroyFramebuffer(VK.device, VK.bloomFB2, NULL);
    if (VK.bloomPass)      vkDestroyRenderPass(VK.device, VK.bloomPass, NULL);
    if (VK.bloomView)      vkDestroyImageView(VK.device, VK.bloomView, NULL);
    if (VK.bloomView2)     vkDestroyImageView(VK.device, VK.bloomView2, NULL);
    if (VK.bloomImage)     vkDestroyImage(VK.device, VK.bloomImage, NULL);
    if (VK.bloomImage2)    vkDestroyImage(VK.device, VK.bloomImage2, NULL);
    if (VK.bloomMem)       vkFreeMemory(VK.device, VK.bloomMem, NULL);
    if (VK.bloomMem2)      vkFreeMemory(VK.device, VK.bloomMem2, NULL);
    if (VK.bloomSampler)   vkDestroySampler(VK.device, VK.bloomSampler, NULL);
    if (VK.bloomPool)      vkDestroyDescriptorPool(VK.device, VK.bloomPool, NULL);
    if (VK.bloomDSL)       vkDestroyDescriptorSetLayout(VK.device, VK.bloomDSL, NULL);

    /* Post-process: offscreen scene and composite. */
    if (VK.compositePipeline) vkDestroyPipeline(VK.device, VK.compositePipeline, NULL);
    if (VK.compositeLayout)   vkDestroyPipelineLayout(VK.device, VK.compositeLayout, NULL);
    if (VK.compositePool)     vkDestroyDescriptorPool(VK.device, VK.compositePool, NULL);
    if (VK.compositeDSL)      vkDestroyDescriptorSetLayout(VK.device, VK.compositeDSL, NULL);
    if (VK.compositePass)     vkDestroyRenderPass(VK.device, VK.compositePass, NULL);
    if (VK.sceneFB)           vkDestroyFramebuffer(VK.device, VK.sceneFB, NULL);
    if (VK.scenePass)         vkDestroyRenderPass(VK.device, VK.scenePass, NULL);
    if (VK.sceneColorView)    vkDestroyImageView(VK.device, VK.sceneColorView, NULL);
    if (VK.sceneColorImage)   vkDestroyImage(VK.device, VK.sceneColorImage, NULL);
    if (VK.sceneColorMem)     vkFreeMemory(VK.device, VK.sceneColorMem, NULL);
    if (VK.sceneDepthView)    vkDestroyImageView(VK.device, VK.sceneDepthView, NULL);
    if (VK.sceneDepthImage)   vkDestroyImage(VK.device, VK.sceneDepthImage, NULL);
    if (VK.sceneDepthMem)     vkFreeMemory(VK.device, VK.sceneDepthMem, NULL);
    if (VK.taaHistoryView)    vkDestroyImageView(VK.device, VK.taaHistoryView, NULL);
    if (VK.taaHistoryImage)   vkDestroyImage(VK.device, VK.taaHistoryImage, NULL);
    if (VK.taaHistoryMem)     vkFreeMemory(VK.device, VK.taaHistoryMem, NULL);
    if (VK.taaUBO)            vkDestroyBuffer(VK.device, VK.taaUBO, NULL);
    if (VK.taaUBOMem)         vkFreeMemory(VK.device, VK.taaUBOMem, NULL);
    if (VK.velocitySkinPipeline) vkDestroyPipeline(VK.device, VK.velocitySkinPipeline, NULL);
    if (VK.velocitySkinLayout)   vkDestroyPipelineLayout(VK.device, VK.velocitySkinLayout, NULL);
    if (VK.velocitySkinDSL)      vkDestroyDescriptorSetLayout(VK.device, VK.velocitySkinDSL, NULL);
    if (VK.velocitySkinPool)     vkDestroyDescriptorPool(VK.device, VK.velocitySkinPool, NULL);
    if (VK.velocityPipeline)  vkDestroyPipeline(VK.device, VK.velocityPipeline, NULL);
    if (VK.velocityLayout)    vkDestroyPipelineLayout(VK.device, VK.velocityLayout, NULL);
    if (VK.velocityDSL)       vkDestroyDescriptorSetLayout(VK.device, VK.velocityDSL, NULL);
    if (VK.velocityPool)      vkDestroyDescriptorPool(VK.device, VK.velocityPool, NULL);
    if (VK.velocityFB)        vkDestroyFramebuffer(VK.device, VK.velocityFB, NULL);
    if (VK.velocityPass)      vkDestroyRenderPass(VK.device, VK.velocityPass, NULL);
    if (VK.velocityView)      vkDestroyImageView(VK.device, VK.velocityView, NULL);
    if (VK.velocityImage)     vkDestroyImage(VK.device, VK.velocityImage, NULL);
    if (VK.velocityMem)       vkFreeMemory(VK.device, VK.velocityMem, NULL);
    if (VK.velocityUBO)       vkDestroyBuffer(VK.device, VK.velocityUBO, NULL);
    if (VK.velocityUBOMem)    vkFreeMemory(VK.device, VK.velocityUBOMem, NULL);

    /* Meshes */
    for (int i = 1; i <= g_meshCount; ++i) {
        MeshEntry *e = &g_meshTable[i];
        destroyMeshEntry(e);
        invalidateMeshGeneration((unsigned int)i);
    }
    g_meshCount = 0;

    /* Render targets own their color image, also exposed through the texture
       table. Clear that alias before cleaning up ordinary textures below. */
    for (int i = 1; i <= g_rtCount; ++i) {
        RenderTargetEntry *rt = &g_rtTable[i];
        unsigned int textureId = rt->textureId;
        destroyRenderTargetEntry(rt);
        if (textureId > 0 && textureId < VLCRLIB_MAX_TEXTURES) {
            g_textureTable[textureId] = (TextureEntry){0};
            invalidateTextureGeneration(textureId);
        }
        invalidateRTGeneration((unsigned int)i);
        invalidateRTGeneration((unsigned int)i);
    }
    g_rtCount = 0;

    /* Textures */
    for (int i = 1; i <= g_textureCount; ++i) {
        TextureEntry *e = &g_textureTable[i];
        destroyTextureEntry(e);
        invalidateTextureGeneration((unsigned int)i);
    }
    g_textureCount = 0;

    /* Models ??? ids are 1-based (slot 0 unused) */
    for (int i = 1; i <= g_modelCount; ++i) {
        free(g_modelTable[i].meshes);
        free(g_modelTable[i].meshMaterials);
        free(g_modelTable[i].meshTransforms);
        free(g_modelTable[i].materials);
        free(g_modelTable[i].ownedTextures);
        memset(&g_modelTable[i], 0, sizeof(g_modelTable[i]));
    }
    g_modelCount = 0;
    material_invalidate_all();
    g_matCount = 0;

    /* Skinned models ??? ids are 1-based */
    for (int i = 1; i <= g_skModelCount; ++i) {
        SkinnedModelEntry *e = &g_skModelTable[i];
        free(e->vbos); free(e->vboMems); free(e->ibos); free(e->iboMems);
        free(e->indexCounts); free(e->meshMaterials); free(e->materials);
        free(e->animationIds);
        *e = (SkinnedModelEntry){0};
    }
    g_skModelCount = 0;
    g_skelCount = 0;
    g_animCount = 0;

    /* Sync */
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (VK.semImgAvailable) vkDestroySemaphore(VK.device, VK.semImgAvailable[i], NULL);
        if (VK.inFlight)        vkDestroyFence(VK.device, VK.inFlight[i], NULL);
    }
    for (uint32_t i = 0; i < VK.imageCount; ++i)
        if (VK.semRenderDone) vkDestroySemaphore(VK.device, VK.semRenderDone[i], NULL);
    free(VK.semImgAvailable);
    free(VK.semRenderDone);
    free(VK.inFlight);
    free(VK.imagesInFlight);

    for (uint32_t i = 0; i < VK.imageCount; ++i) {
        if (VK.framebuffers) vkDestroyFramebuffer(VK.device, VK.framebuffers[i], NULL);
        if (VK.views)        vkDestroyImageView(VK.device, VK.views[i], NULL);
    }
    free(VK.framebuffers);
    free(VK.views);
    free(VK.images);
    free(VK.cmdBuffers);

    if (VK.offscreenFence) vkDestroyFence(VK.device, VK.offscreenFence, NULL);
    if (VK.renderPassRT)   vkDestroyRenderPass(VK.device, VK.renderPassRT, NULL);
    if (VK.depthView)      vkDestroyImageView(VK.device, VK.depthView, NULL);
    if (VK.depthImage)     vkDestroyImage(VK.device, VK.depthImage, NULL);
    if (VK.depthMem)       vkFreeMemory(VK.device, VK.depthMem, NULL);

    if (VK.cmdPool)        vkDestroyCommandPool(VK.device, VK.cmdPool, NULL);
    if (VK.renderPass)     vkDestroyRenderPass(VK.device, VK.renderPass, NULL);
    if (VK.gpuTimerPool)   vkDestroyQueryPool(VK.device, VK.gpuTimerPool, NULL);
    if (VK.swapchain)      vkDestroySwapchainKHR(VK.device, VK.swapchain, NULL);
    vkDestroyDevice(VK.device, NULL);
    if (VK.debugMessenger) {
        PFN_vkDestroyDebugUtilsMessengerEXT destroyMessenger =
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                VK.instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroyMessenger) destroyMessenger(VK.instance, VK.debugMessenger, NULL);
        VK.debugMessenger = VK_NULL_HANDLE;
    }
    vkDestroySurfaceKHR(VK.instance, VK.surface, NULL);
    vkDestroyInstance(VK.instance, NULL);
    memset(&VK, 0, sizeof(VK));
    VK.validationErrors = validationErrors;
}

int vgraphics_GetValidationErrorCount(void) {
    return VK.validationErrors;
}

void vgraphics_GetRenderTargetSize(int *width, int *height) {
    int w = (int)VK.swapExtent.width, h = (int)VK.swapExtent.height;
    if (VK.postProcessEnabled && VK.sceneReady && VK.sceneWidth > 0 &&
        VK.sceneHeight > 0) {
        w = (int)VK.sceneWidth;
        h = (int)VK.sceneHeight;
    }
    if (width) *width = w;
    if (height) *height = h;
}

void vgraphics_ResetFrameStats(void) {
    uint64_t frame = VK.frameIndex;
    memset(&g_frameStats, 0, sizeof(g_frameStats));
    g_frameStats.frameIndex = frame;
    g_frameStats.width  = (int)VK.swapExtent.width;
    g_frameStats.height = (int)VK.swapExtent.height;
}

void vgraphics_GetFrameStats(FrameStats *out) {
    if (!out) return;
    *out = g_frameStats;

    uint64_t bytes = 0;
    int textures = 0, meshes = 0;
    for (int i = 1; i <= g_textureCount && i < VLCRLIB_MAX_TEXTURES; ++i) {
        TextureEntry *t = &g_textureTable[i];
        if (!t->view) continue;
        textures++;
        uint64_t base = (uint64_t)t->width * (uint64_t)t->height * 4u;
        bytes += base + base / 3u; /* approximate full mip chain */
    }
    for (int i = 1; i <= g_meshCount && i < VLCRLIB_MAX_MESHES; ++i) {
        MeshEntry *m = &g_meshTable[i];
        if (!m->vbo) continue;
        meshes++;
        bytes += (uint64_t)m->vertexCount * sizeof(Vertex3D);
        bytes += (uint64_t)m->indexCount * sizeof(uint32_t);
    }
    out->textureCount = textures;
    out->meshCount = meshes;
    out->estimatedGpuBytes = bytes;
}

bool vgraphics_CaptureScreenshot(const char *path) {
    if (!path || !path[0] || !VK.device || VK.inFrame) return false;
    if (!VK.images || VK.imageCount == 0 || VK.currentImage >= VK.imageCount)
        return false;

    vkDeviceWaitIdle(VK.device);
    const uint32_t w = VK.swapExtent.width;
    const uint32_t h = VK.swapExtent.height;
    const VkDeviceSize bytes = (VkDeviceSize)w * h * 4;

    /* The previously presented image is no longer owned by us. Acquire a
       fresh image, copy it, then present it again; otherwise validation
       correctly flags a layout transition on an unacquired swapchain image. */
    VkFence acquireFence = VK_NULL_HANDLE;
    VkFenceCreateInfo fci = {0};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(VK.device, &fci, NULL, &acquireFence) != VK_SUCCESS)
        return false;
    uint32_t captureIndex = 0;
    VkResult ar = vkAcquireNextImageKHR(VK.device, VK.swapchain, UINT64_MAX,
                                        VK_NULL_HANDLE, acquireFence, &captureIndex);
    if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) {
        vkDestroyFence(VK.device, acquireFence, NULL);
        return false;
    }
    vkWaitForFences(VK.device, 1, &acquireFence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(VK.device, acquireFence, NULL);

    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (!createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &buffer, &memory))
        return false;

    VkCommandBufferAllocateInfo cai = {0};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = VK.cmdPool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(VK.device, &cai, &cb) != VK_SUCCESS) {
        vkDestroyBuffer(VK.device, buffer, NULL);
        vkFreeMemory(VK.device, memory, NULL);
        return false;
    }

    VkCommandBufferBeginInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    VkImageMemoryBarrier b = {0};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = VK.images[captureIndex];
    b.subresourceRange = (VkImageSubresourceRange){
        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    b.srcAccessMask = 0;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, NULL, 0, NULL, 1, &b);

    VkBufferImageCopy region = {0};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = w;
    region.imageExtent.height = h;
    region.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(cb, VK.images[captureIndex],
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           buffer, 1, &region);

    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.dstAccessMask = 0;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                         0, NULL, 0, NULL, 1, &b);
    vkEndCommandBuffer(cb);

    VkSubmitInfo submit = {0};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cb;
    vkQueueSubmit(VK.gfxQueue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(VK.gfxQueue);
    vkFreeCommandBuffers(VK.device, VK.cmdPool, 1, &cb);

    VkPresentInfoKHR present = {0};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.swapchainCount = 1;
    present.pSwapchains = &VK.swapchain;
    present.pImageIndices = &captureIndex;
    vkQueuePresentKHR(VK.presentQueue, &present);
    vkQueueWaitIdle(VK.presentQueue);

    void *mapped = NULL;
    if (vkMapMemory(VK.device, memory, 0, bytes, 0, &mapped) != VK_SUCCESS) {
        vkDestroyBuffer(VK.device, buffer, NULL);
        vkFreeMemory(VK.device, memory, NULL);
        return false;
    }

    unsigned char *rgba = (unsigned char *)malloc((size_t)bytes);
    bool ok = rgba != NULL;
    if (rgba) {
        const unsigned char *src = (const unsigned char *)mapped;
        bool bgra = VK.swapFormat == VK_FORMAT_B8G8R8A8_SRGB ||
                    VK.swapFormat == VK_FORMAT_B8G8R8A8_UNORM;
        for (VkDeviceSize i = 0; i < (VkDeviceSize)w * h; ++i) {
            rgba[i*4+0] = bgra ? src[i*4+2] : src[i*4+0];
            rgba[i*4+1] = src[i*4+1];
            rgba[i*4+2] = bgra ? src[i*4+0] : src[i*4+2];
            rgba[i*4+3] = src[i*4+3];
        }
        ok = stbi_write_png(path, (int)w, (int)h, 4, rgba, (int)w * 4) != 0;
        free(rgba);
    }
    vkUnmapMemory(VK.device, memory);
    vkDestroyBuffer(VK.device, buffer, NULL);
    vkFreeMemory(VK.device, memory, NULL);
    return ok;
}

typedef struct GpuFrameTimes {
    float total, shadow, scene, post;
} GpuFrameTimes;

static GpuFrameTimes readGpuFrameTimes(uint32_t frameSlot) {
    GpuFrameTimes out = {0};
    if (!VK.gpuTimerPool || !(g_timerSlotValid & (1u << frameSlot)))
        return out;
    uint64_t ts[4] = {0, 0, 0, 0};
    VkResult r = vkGetQueryPoolResults(VK.device, VK.gpuTimerPool,
                                       frameSlot * 4, 4, sizeof(ts), ts,
                                       sizeof(uint64_t),
                                       VK_QUERY_RESULT_64_BIT);
    if (r != VK_SUCCESS || ts[3] <= ts[0]) return out;
    double period = (double)VK.gpuTimerPeriod / 1.0e6;
    out.total  = (float)((double)(ts[3] - ts[0]) * period);
    out.shadow = ts[1] > ts[0] ? (float)((double)(ts[1] - ts[0]) * period) : 0.0f;
    out.scene  = ts[2] > ts[1] ? (float)((double)(ts[2] - ts[1]) * period) : 0.0f;
    out.post   = ts[3] > ts[2] ? (float)((double)(ts[3] - ts[2]) * period) : 0.0f;
    return out;
}

static float halton(int index, int base) {
    float f = 1.0f, r = 0.0f;
    while (index > 0) {
        f /= (float)base;
        r += f * (float)(index % base);
        index /= base;
    }
    return r;
}

void BeginVulkanFrame(void) {
    if (!VK.device || VK.frameFailed) return;
    if (VK.taaEnabled) {
        int idx = (VK.taaFrame++ % 8) + 1;
        float w = VK.swapExtent.width > 0 ? (float)VK.swapExtent.width : 1.0f;
        float h = VK.swapExtent.height > 0 ? (float)VK.swapExtent.height : 1.0f;
        VK.taaJitterX = (halton(idx, 2) - 0.5f) * (2.0f / w);
        VK.taaJitterY = (halton(idx, 3) - 0.5f) * (2.0f / h);
    } else {
        VK.taaJitterX = VK.taaJitterY = 0.0f;
    }
    VK.framePhase = VLCR_PHASE_ACQUIRE;
    VK.currentCmd = VK_NULL_HANDLE;
    VkResult frameResult = vkWaitForFences(VK.device, 1, &VK.inFlight[VK.currentFrame], VK_TRUE, UINT64_MAX);
    if (frameResult != VK_SUCCESS) {
        VK.frameFailed = true;
        VK.lastFrameResult = (int)frameResult;
        fprintf(stderr, "[vlcrlib] waiting for frame fence failed: %d\n", (int)frameResult);
        return;
    }

    GpuFrameTimes gpu = readGpuFrameTimes(VK.currentFrame);
    g_timerSlotValid &= ~(1u << VK.currentFrame);
    vgraphics_ResetFrameStats();
    g_frameStats.gpuFrameMs = gpu.total;
    g_frameStats.shadowMs  = gpu.shadow;
    g_frameStats.sceneMs   = gpu.scene;
    g_frameStats.postMs    = gpu.post;
    g_frameStatsStart = SDL_GetPerformanceCounter();

    /* The frame-slot fence has signaled, so GPU work from its previous use
       can no longer reference resources deferred during that frame. */
    releaseCompletedTextures(VK.currentFrame, false);
    releaseCompletedMeshes(VK.currentFrame, false);
    releaseCompletedRenderTargets(VK.currentFrame, false);
    material_release_completed(VK.currentFrame, false);

    bool hasDirtyMaterialDescriptors = false;
    for (int i = 1; i <= g_matCount && i < VLCRLIB_MAX_MATERIALS; ++i)
        if (VK.matDescDirty[i] && g_matTable[i].descSet) {
            hasDirtyMaterialDescriptors = true;
            break;
        }
    /* Descriptor sets are shared across frames in flight. A single slot fence
       does not prove that another slot has stopped using the same set. */
    if (hasDirtyMaterialDescriptors) vkDeviceWaitIdle(VK.device);
    for (int i = 1; i <= g_matCount && i < VLCRLIB_MAX_MATERIALS; ++i) {
        if (VK.matDescDirty[i] && g_matTable[i].descSet) {
            updateMaterialDescriptorSet(&g_matTable[i]);
            VK.matDescDirty[i] = 0;
        }
    }

    VK.descSet2DIndex = VK.currentFrame * (VLCRLIB_MAX_2D_DESCRIPTORS / MAX_FRAMES_IN_FLIGHT);
    VK.vbo2DOffset = 0;
    VK.uboPBRSlot = 0;
    VK.ubo3DSlot = 0;
    VK.inFrame = 1;
    VK.frameIndex++;
    VK.framePhase = VLCR_PHASE_SCENE;

    if (VK.debugLog && (VK.frameIndex == 1 || (VK.frameIndex % 120) == 0)) {
        fprintf(stderr,
            "[vlcrlib] frame=%d debugView=%d dir=%d point=%d spot=%d ibl=%.2f materials=%d UBO_PBR=%zu stride=%u\n",
            VK.frameIndex, VK.debugView,
            VK.lightDirectionalEnabled ? 1 : 0,
            VK.lightPointEnabled ? 1 : 0,
            VK.lightSpotEnabled ? 1 : 0,
            VK.iblIntensity, g_matCount,
            sizeof(UBO_PBR), VK.uboPBRStride);
        if (sizeof(UBO_PBR) < (size_t)VLCR_UBO_PBR_EXPECTED_MIN)
            fprintf(stderr, "[vlcrlib] WARNING: UBO_PBR unexpectedly small\n");
    }

    VkResult r = vkAcquireNextImageKHR(VK.device, VK.swapchain, UINT64_MAX,
                                       VK.semImgAvailable[VK.currentFrame],
                                       VK_NULL_HANDLE, &VK.currentImage);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        if (!vgraphics_internal_recreate()) {
            VK.frameFailed = true;
            VK.lastFrameResult = (int)VK_ERROR_INITIALIZATION_FAILED;
        }
        VK.framePhase = VLCR_PHASE_IDLE;
        VK.inFrame = 0;
        return;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
        VK.frameFailed = true;
        VK.lastFrameResult = (int)r;
        fprintf(stderr, "[vlcrlib] acquiring swapchain image failed: %d\n", (int)r);
        VK.framePhase = VLCR_PHASE_IDLE;
        VK.inFrame = 0;
        return;
    }

    if (VK.imagesInFlight[VK.currentImage] != VK_NULL_HANDLE) {
        frameResult = vkWaitForFences(VK.device, 1, &VK.imagesInFlight[VK.currentImage],
                                      VK_TRUE, UINT64_MAX);
        if (frameResult != VK_SUCCESS) {
            VK.frameFailed = true;
            VK.lastFrameResult = (int)frameResult;
            fprintf(stderr, "[vlcrlib] waiting for swapchain image fence failed: %d\n", (int)frameResult);
            VK.framePhase = VLCR_PHASE_IDLE;
            VK.inFrame = 0;
            return;
        }
    }

    VK.swapchainCmd = VK.cmdBuffers[VK.currentImage];
    VK.currentCmd   = VK.swapchainCmd;
    VK.inRT         = false;

    VkCommandBuffer cb = VK.currentCmd;
    frameResult = vkResetCommandBuffer(cb, 0);
    if (frameResult != VK_SUCCESS) {
        VK.frameFailed = true;
        VK.lastFrameResult = (int)frameResult;
        fprintf(stderr, "[vlcrlib] resetting frame command buffer failed: %d\n", (int)frameResult);
        VK.currentCmd = VK_NULL_HANDLE;
        VK.framePhase = VLCR_PHASE_IDLE;
        VK.inFrame = 0;
        return;
    }

    VkCommandBufferBeginInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    frameResult = vkBeginCommandBuffer(cb, &bi);
    if (frameResult != VK_SUCCESS) {
        VK.frameFailed = true;
        VK.lastFrameResult = (int)frameResult;
        fprintf(stderr, "[vlcrlib] beginning frame command buffer failed: %d\n", (int)frameResult);
        VK.currentCmd = VK_NULL_HANDLE;
        VK.framePhase = VLCR_PHASE_IDLE;
        VK.inFrame = 0;
        return;
    }

    if (VK.gpuTimerPool) {
        vkCmdResetQueryPool(cb, VK.gpuTimerPool, VK.currentFrame * 4, 4);
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            VK.gpuTimerPool, VK.currentFrame * 4);
    }

    /* Shadow and SSAO depth passes use the last completed scene submission. */
    recordShadowPass(cb);

    /* Half-res SSAO, graph-planned depth prepass + resolve. */
    if (!recordSSAOGraph(cb)) {
        recordSSAODepthPrepass(cb);
        recordSSAOPass(cb);
    }
    if (VK.gpuTimerPool) {
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            VK.gpuTimerPool, VK.currentFrame * 4 + 1);
    }

    VK.transCount = 0;

    VkClearValue clears[2];
    clears[0] = VK.clearColor;
    clears[1].depthStencil.depth = 1.0f;
    clears[1].depthStencil.stencil = 0;

    VkRenderPassBeginInfo rp = {0};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    if (VK.postProcessEnabled && VK.sceneReady) {
        rp.renderPass = VK.scenePass;
        rp.framebuffer = VK.sceneFB;
    } else {
        rp.renderPass = VK.renderPass;
        rp.framebuffer = VK.framebuffers[VK.currentImage];
    }
    VkExtent2D targetExtent = VK.swapExtent;
    if (VK.postProcessEnabled && VK.sceneReady &&
        VK.sceneWidth > 0 && VK.sceneHeight > 0) {
        targetExtent.width = VK.sceneWidth;
        targetExtent.height = VK.sceneHeight;
    }
    rp.renderArea.extent = targetExtent;
    rp.clearValueCount = 2;
    rp.pClearValues = clears;
    vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp = {0, 0, (float)targetExtent.width,
                     (float)targetExtent.height, 0, 1};
    vkCmdSetViewport(cb, 0, 1, &vp);
    VkRect2D sc = { {0,0}, targetExtent };
    vkCmdSetScissor(cb, 0, 1, &sc);

    VK.ubo3DSlot  = 0;
    VK.uboPBRSlot = 0;
}



static bool createTaaUBO(void) {
    if (VK.taaUBO) return true;
    VK.taaUBOStride = 256;
    VkDeviceSize size = (VkDeviceSize)VK.taaUBOStride * MAX_FRAMES_IN_FLIGHT;
    if (!createBuffer(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &VK.taaUBO, &VK.taaUBOMem)) return false;
    if (vkMapMemory(VK.device, VK.taaUBOMem, 0, size, 0, &VK.taaUBOMapped)
        != VK_SUCCESS) return false;
    return true;
}

static bool createVelocityResources(void) {
    if (VK.velocityPipeline || !VK.sceneDepthView) return true;
    uint32_t w = VK.swapExtent.width, h = VK.swapExtent.height;
    if (w < 1 || h < 1) return false;

    if (!vlcr_createImage(w, h, VK_FORMAT_R16G16_SFLOAT,
                          VK_IMAGE_TILING_OPTIMAL,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          &VK.velocityImage, &VK.velocityMem)) return false;
    vlcrNameImage(VK.velocityImage, "velocity_color");
    VkImageViewCreateInfo vi = {0};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = VK.velocityImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R16G16_SFLOAT;
    vi.subresourceRange = (VkImageSubresourceRange){
        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(VK.device, &vi, NULL, &VK.velocityView) != VK_SUCCESS)
        return false;
    vlcrInitSampledImage(VK.velocityImage, VK_IMAGE_ASPECT_COLOR_BIT);

    VkAttachmentDescription atts[2] = {0};
    atts[0].format = VK_FORMAT_R16G16_SFLOAT;
    atts[0].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    atts[1].format = VK_FORMAT_D32_SFLOAT;
    atts[1].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    atts[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atts[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    atts[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkAttachmentReference cref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference dref = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
    VkSubpassDescription sub = {0};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &cref;
    sub.pDepthStencilAttachment = &dref;
    VkRenderPassCreateInfo rpi = {0};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 2;
    rpi.pAttachments = atts;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sub;
    if (vkCreateRenderPass(VK.device, &rpi, NULL, &VK.velocityPass) != VK_SUCCESS)
        return false;

    VkImageView fbViews[2] = { VK.velocityView, VK.sceneDepthView };
    VkFramebufferCreateInfo fbci = {0};
    fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbci.renderPass = VK.velocityPass;
    fbci.attachmentCount = 2;
    fbci.pAttachments = fbViews;
    fbci.width = w;
    fbci.height = h;
    fbci.layers = 1;
    if (vkCreateFramebuffer(VK.device, &fbci, NULL, &VK.velocityFB) != VK_SUCCESS)
        return false;

    VkDescriptorSetLayoutBinding bind = {0};
    bind.binding = 0;
    bind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    bind.descriptorCount = 1;
    bind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo dli = {0};
    dli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dli.bindingCount = 1;
    dli.pBindings = &bind;
    if (vkCreateDescriptorSetLayout(VK.device, &dli, NULL, &VK.velocityDSL) != VK_SUCCESS)
        return false;

    VkPushConstantRange pcr = {0};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcr.size = 128;
    VkPipelineLayoutCreateInfo pli = {0};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &VK.velocityDSL;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(VK.device, &pli, NULL, &VK.velocityLayout) != VK_SUCCESS)
        return false;

    VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1 };
    VkDescriptorPoolCreateInfo dpi = {0};
    dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(VK.device, &dpi, NULL, &VK.velocityPool) != VK_SUCCESS)
        return false;
    VkDescriptorSetAllocateInfo ai = {0};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = VK.velocityPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &VK.velocityDSL;
    if (vkAllocateDescriptorSets(VK.device, &ai, &VK.velocityDescSet) != VK_SUCCESS)
        return false;

    VK.velocityUBOStride = 256;
    VkDeviceSize ubSize = (VkDeviceSize)VK.velocityUBOStride * MAX_FRAMES_IN_FLIGHT;
    if (!createBuffer(ubSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &VK.velocityUBO, &VK.velocityUBOMem)) return false;
    if (vkMapMemory(VK.device, VK.velocityUBOMem, 0, ubSize, 0,
                    &VK.velocityUBOMapped) != VK_SUCCESS) return false;
    VkDescriptorBufferInfo bi = { VK.velocityUBO, 0, 128 };
    VkWriteDescriptorSet wr = {0};
    wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr.dstSet = VK.velocityDescSet;
    wr.dstBinding = 0;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    wr.descriptorCount = 1;
    wr.pBufferInfo = &bi;
    vkUpdateDescriptorSets(VK.device, 1, &wr, 0, NULL);

    VkShaderModule vert = createShaderModule("shaders/velocity.vert.spv");
    VkShaderModule frag = createShaderModule("shaders/velocity.frag.spv");
    if (!vert || !frag) return false;
    VkPipelineShaderStageCreateInfo stages[2] = {0};
    for (int i = 0; i < 2; ++i) {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage = i == 0 ? VK_SHADER_STAGE_VERTEX_BIT
                                 : VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[i].module = i == 0 ? vert : frag;
        stages[i].pName = "main";
    }
    VkVertexInputBindingDescription vb = {
        0, sizeof(Vertex3D), VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription va = {
        0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };
    VkPipelineVertexInputStateCreateInfo vinput = {0};
    vinput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vinput.vertexBindingDescriptionCount = 1;
    vinput.pVertexBindingDescriptions = &vb;
    vinput.vertexAttributeDescriptionCount = 1;
    vinput.pVertexAttributeDescriptions = &va;
    VkPipelineInputAssemblyStateCreateInfo ia = {0};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = {0};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = {0};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = {0};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo dss = {0};
    dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dss.depthTestEnable = VK_TRUE;
    dss.depthWriteEnable = VK_FALSE;
    dss.depthCompareOp = VK_COMPARE_OP_EQUAL;
    VkPipelineColorBlendAttachmentState cba = {0};
    cba.colorWriteMask = 0x3;
    VkPipelineColorBlendStateCreateInfo cb = {0};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds = {0};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo pi = {0};
    pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = 2;
    pi.pStages = stages;
    pi.pVertexInputState = &vinput;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &dss;
    pi.pColorBlendState = &cb;
    pi.pDynamicState = &ds;
    pi.layout = VK.velocityLayout;
    pi.renderPass = VK.velocityPass;
    if (vkCreateGraphicsPipelines(VK.device, VK_NULL_HANDLE, 1, &pi, NULL,
                                  &VK.velocityPipeline) != VK_SUCCESS) return false;

    /* Skinned velocity: per-model current/previous bone buffers. */
    VkDescriptorSetLayoutBinding sbind[2] = {0};
    sbind[0].binding = 0;
    sbind[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    sbind[0].descriptorCount = 1;
    sbind[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    sbind[1].binding = 1;
    sbind[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sbind[1].descriptorCount = 1;
    sbind[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo sdli = {0};
    sdli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    sdli.bindingCount = 2;
    sdli.pBindings = sbind;
    if (vkCreateDescriptorSetLayout(VK.device, &sdli, NULL,
                                    &VK.velocitySkinDSL) != VK_SUCCESS) return false;
    VkPipelineLayoutCreateInfo spli = pli;
    spli.pSetLayouts = &VK.velocitySkinDSL;
    if (vkCreatePipelineLayout(VK.device, &spli, NULL,
                               &VK.velocitySkinLayout) != VK_SUCCESS) return false;
    VkDescriptorPoolSize sps[2] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VLCRLIB_MAX_SKMODELS },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VLCRLIB_MAX_SKMODELS }
    };
    VkDescriptorPoolCreateInfo sdpi = {0};
    sdpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    sdpi.maxSets = VLCRLIB_MAX_SKMODELS;
    sdpi.poolSizeCount = 2;
    sdpi.pPoolSizes = sps;
    if (vkCreateDescriptorPool(VK.device, &sdpi, NULL,
                               &VK.velocitySkinPool) != VK_SUCCESS) return false;

    VkShaderModule svert = createShaderModule("shaders/velocity_skin.vert.spv");
    VkShaderModule sfrag = createShaderModule("shaders/velocity.frag.spv");
    if (!svert || !sfrag) return false;
    VkPipelineShaderStageCreateInfo sstages[2] = {0};
    for (int i = 0; i < 2; ++i) {
        sstages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        sstages[i].stage = i == 0 ? VK_SHADER_STAGE_VERTEX_BIT
                                  : VK_SHADER_STAGE_FRAGMENT_BIT;
        sstages[i].module = i == 0 ? svert : sfrag;
        sstages[i].pName = "main";
    }
    VkVertexInputBindingDescription svb = {
        0, sizeof(SkinnedVertex3D), VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription sva[3] = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
        { 1, 0, VK_FORMAT_R8G8B8A8_UINT, (uint32_t)offsetof(SkinnedVertex3D, joints) },
        { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, (uint32_t)offsetof(SkinnedVertex3D, weights) }
    };
    VkPipelineVertexInputStateCreateInfo svinput = {0};
    svinput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    svinput.vertexBindingDescriptionCount = 1;
    svinput.pVertexBindingDescriptions = &svb;
    svinput.vertexAttributeDescriptionCount = 3;
    svinput.pVertexAttributeDescriptions = sva;
    VkGraphicsPipelineCreateInfo spi = pi;
    spi.pStages = sstages;
    spi.pVertexInputState = &svinput;
    spi.layout = VK.velocitySkinLayout;
    if (vkCreateGraphicsPipelines(VK.device, VK_NULL_HANDLE, 1, &spi, NULL,
                                  &VK.velocitySkinPipeline) != VK_SUCCESS) return false;
    vkDestroyShaderModule(VK.device, svert, NULL);
    vkDestroyShaderModule(VK.device, sfrag, NULL);

    vkDestroyShaderModule(VK.device, vert, NULL);
    vkDestroyShaderModule(VK.device, frag, NULL);
    return true;
}

static bool createCompositePipeline(void) {
    if (VK.compositeReady) return true;
    VkShaderModule vert = createShaderModule("shaders/composite.vert.spv");
    VkShaderModule frag = createShaderModule("shaders/composite.frag.spv");
    if (!vert || !frag) return false;
    if (!createTaaUBO()) return false;

    VkPipelineShaderStageCreateInfo stages[2] = {0};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi = {0};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia = {0};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = {0};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = {0};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = {0};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba = {0};
    cba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb = {0};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds = {0};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;

    VkPipelineDepthStencilStateCreateInfo dss = {0};
    dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dss.depthTestEnable = VK_FALSE;
    dss.depthWriteEnable = VK_FALSE;
    dss.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    dss.stencilTestEnable = VK_FALSE;

    /* Swapchain-compatible render pass for composite. It includes the depth
       attachment (unused, DONT_CARE) and the same subpass dependency as
       VK.renderPass so the existing swapchain framebuffers can be used. */
    VkAttachmentDescription atts[2] = {0};
    atts[0].format = VK.swapFormat;
    atts[0].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atts[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    atts[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    atts[1].format = VK.depthFormat;
    atts[1].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub = {0};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    sub.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep = {0};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpi = {0};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 2;
    rpi.pAttachments = atts;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sub;
    rpi.dependencyCount = 1;
    rpi.pDependencies = &dep;
    if (vkCreateRenderPass(VK.device, &rpi, NULL, &VK.compositePass) != VK_SUCCESS) return false;

    VkDescriptorSetLayoutBinding binds[6] = {0};
    for (int i = 0; i < 5; ++i) {
        binds[i].binding = (uint32_t)i;
        binds[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binds[i].descriptorCount = 1;
        binds[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    binds[4].binding = 4;
    binds[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    binds[4].descriptorCount = 1;
    binds[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[5].binding = 5;
    binds[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[5].descriptorCount = 1;
    binds[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dli = {0};
    dli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dli.bindingCount = 6;
    dli.pBindings = binds;
    VkDescriptorSetLayout dsl;
    if (vkCreateDescriptorSetLayout(VK.device, &dli, NULL, &dsl) != VK_SUCCESS) return false;
    VK.compositeDSL = dsl;

    VkPushConstantRange pcr = {0};
    pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.size = 12;
    VkPipelineLayoutCreateInfo pli = {0};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &dsl;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(VK.device, &pli, NULL, &VK.compositeLayout) != VK_SUCCESS) return false;

    /* Allocate scene + bloom + history + depth + TAA UBO descriptors. */
    VkDescriptorPoolSize ps[2] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1 }
    };
    VkDescriptorPoolCreateInfo dpi = {0};
    dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.maxSets = 1;
    dpi.poolSizeCount = 2;
    dpi.pPoolSizes = ps;
    VkDescriptorPool pool;
    if (vkCreateDescriptorPool(VK.device, &dpi, NULL, &pool) == VK_SUCCESS) {
        VK.compositePool = pool;
        VkDescriptorSetAllocateInfo ai = {0};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = pool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &dsl;
        if (vkAllocateDescriptorSets(VK.device, &ai, &VK.compositeDescSet) == VK_SUCCESS) {
            VkDescriptorImageInfo imgs[5] = {0};
            imgs[0].imageView = VK.sceneColorView;
            imgs[0].sampler = VK.bloomSampler ? VK.bloomSampler : VK.sampler2D;
            imgs[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imgs[1].imageView = VK.bloomReady ? VK.bloomView : VK.sceneColorView;
            imgs[1].sampler = VK.bloomSampler ? VK.bloomSampler : VK.sampler2D;
            imgs[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imgs[2].imageView = VK.taaHistoryView ? VK.taaHistoryView : VK.sceneColorView;
            imgs[2].sampler = VK.bloomSampler ? VK.bloomSampler : VK.sampler2D;
            imgs[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imgs[3].imageView = VK.sceneDepthView;
            imgs[3].sampler = VK.bloomSampler ? VK.bloomSampler : VK.sampler2D;
            imgs[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imgs[4].imageView = VK.velocityView ? VK.velocityView : VK.sceneColorView;
            imgs[4].sampler = VK.bloomSampler ? VK.bloomSampler : VK.sampler2D;
            imgs[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkDescriptorBufferInfo taaInfo = {
                VK.taaUBO, 0, sizeof(TaaParams)
            };
            VkWriteDescriptorSet wr[6] = {0};
            for (int i = 0; i < 5; ++i) {
                wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                wr[i].dstSet = VK.compositeDescSet;
                wr[i].dstBinding = (uint32_t)i;
                wr[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                wr[i].descriptorCount = 1;
                wr[i].pImageInfo = &imgs[i];
            }
            wr[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wr[4].dstSet = VK.compositeDescSet;
            wr[4].dstBinding = 4;
            wr[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            wr[4].descriptorCount = 1;
            wr[4].pBufferInfo = &taaInfo;
            wr[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wr[5].dstSet = VK.compositeDescSet;
            wr[5].dstBinding = 5;
            wr[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            wr[5].descriptorCount = 1;
            wr[5].pImageInfo = &imgs[4];
            vkUpdateDescriptorSets(VK.device, 6, wr, 0, NULL);
        }
    }

    VkGraphicsPipelineCreateInfo pi = {0};
    pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = 2;
    pi.pStages = stages;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &dss;
    pi.pColorBlendState = &cb;
    pi.pDynamicState = &ds;
    pi.layout = VK.compositeLayout;
    pi.renderPass = VK.compositePass;
    if (vkCreateGraphicsPipelines(VK.device, VK_NULL_HANDLE, 1, &pi, NULL, &VK.compositePipeline) != VK_SUCCESS) return false;

    vkDestroyShaderModule(VK.device, vert, NULL);
    vkDestroyShaderModule(VK.device, frag, NULL);
    VK.compositeReady = true;
    fprintf(stderr, "[vlcrlib] composite pipeline ready\n");
    return true;
}

static void updateTaaParams(void) {
    if (!VK.taaUBOMapped) return;
    TaaParams *p = (TaaParams *)((char *)VK.taaUBOMapped +
                                (size_t)VK.currentFrame * VK.taaUBOStride);
    Matrix cur = MatrixMultiply(VK.proj3D, VK.view3D);
    vlcr_matrixToGpu(p->curVP, cur);
    vlcr_matrixToGpu(p->prevVP, VK.taaPrevVP);
    p->params[0] = (VK.taaEnabled && VK.taaHistoryView) ? 1.0f : 0.0f;
    p->params[1] = VK.taaFeedback > 0.0f ? VK.taaFeedback : 0.1f;
    p->params[2] = VK.taaReset ? 1.0f : 0.0f;
    p->params[3] = 0.0f;
    VK.taaPrevVP = cur;
    VK.taaReset = 0;
}

static void compositeGraphPass(VkCommandBuffer cb, int pass, void *user) {
    (void)pass; (void)user;
    recordComposite(cb);
}

static void recordVelocityPass(VkCommandBuffer cb) {
    if (!VK.velocityPipeline || !VK.velocityFB || !VK.velocityUBOMapped)
        return;

    Matrix curVP = MatrixMultiply(VK.proj3D, VK.view3D);
    float *ub = (float *)((char *)VK.velocityUBOMapped +
                          (size_t)VK.currentFrame * VK.velocityUBOStride);
    vlcr_matrixToGpu(ub, curVP);
    vlcr_matrixToGpu(ub + 16, VK.taaPrevVP);

    VkImageMemoryBarrier db = {0};
    db.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    db.srcQueueFamilyIndex = db.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    db.image = VK.sceneDepthImage;
    db.subresourceRange = (VkImageSubresourceRange){
        VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
    db.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    db.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    db.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    db.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0,
                         0, NULL, 0, NULL, 1, &db);

    VkClearValue clear = {0};
    VkRenderPassBeginInfo rp = {0};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = VK.velocityPass;
    rp.framebuffer = VK.velocityFB;
    rp.renderArea.extent = VK.swapExtent;
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, VK.velocityPipeline);
    VkViewport vp = { 0, 0, (float)VK.swapExtent.width,
                      (float)VK.swapExtent.height, 0.f, 1.f };
    vkCmdSetViewport(cb, 0, 1, &vp);
    VkRect2D sc = { {0,0}, VK.swapExtent };
    vkCmdSetScissor(cb, 0, 1, &sc);
    uint32_t dyn = VK.currentFrame * VK.velocityUBOStride;
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            VK.velocityLayout, 0, 1, &VK.velocityDescSet,
                            1, &dyn);

    for (int i = 0; i < VK.shadowCasterCount; ++i) {
        const struct VlcrShadowCaster *c = &VK.shadowCasters[i];
        Matrix curBase = c->transform;
        Matrix prevBase = c->prevTransform;
        if (c->skinnedId) {
            if (c->skinnedId > (unsigned int)g_skModelCount) continue;
            SkinnedModelEntry *se = &g_skModelTable[c->skinnedId];
            if (!se->velocitySkinDesc) continue;
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              VK.velocitySkinPipeline);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    VK.velocitySkinLayout, 0, 1,
                                    &se->velocitySkinDesc, 1, &dyn);
            float pc[32];
            vlcr_matrixToGpu(pc, c->transform);
            vlcr_matrixToGpu(pc + 16, c->prevTransform);
            vkCmdPushConstants(cb, VK.velocitySkinLayout,
                               VK_SHADER_STAGE_VERTEX_BIT, 0, 128, pc);
            for (int mi = 0; mi < se->meshCount; ++mi) {
                VkDeviceSize off = 0;
                vkCmdBindVertexBuffers(cb, 0, 1, &se->vbos[mi], &off);
                vkCmdBindIndexBuffer(cb, se->ibos[mi], 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cb, (uint32_t)se->indexCounts[mi], 1, 0, 0, 0);
            }
            continue;
        }
        if (c->modelId) {
            if (c->modelId >= VLCRLIB_MAX_MODELS) continue;
            ModelEntry *me = &g_modelTable[c->modelId];
            if (!me->meshes) continue;
            for (int mi = 0; mi < me->meshCount; ++mi) {
                Mesh mesh = me->meshes[mi];
                if (!meshHandleValid(mesh)) continue;
                MeshEntry *mv = &g_meshTable[mesh.id];
                if (!mv->vbo) continue;
                Matrix node = me->meshTransforms ? me->meshTransforms[mi]
                                                 : MatrixIdentity();
                Matrix cur = MatrixMultiply(curBase, node);
                Matrix prev = MatrixMultiply(prevBase, node);
                float pc[32];
                vlcr_matrixToGpu(pc, cur);
                vlcr_matrixToGpu(pc + 16, prev);
                vkCmdPushConstants(cb, VK.velocityLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT, 0, 128, pc);
                VkDeviceSize off = 0;
                vkCmdBindVertexBuffers(cb, 0, 1, &mv->vbo, &off);
                vkCmdBindIndexBuffer(cb, mv->ibo, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cb, (uint32_t)mv->indexCount, 1, 0, 0, 0);
            }
        } else if (c->meshId) {
            if (c->meshId >= VLCRLIB_MAX_MESHES) continue;
            MeshEntry *mv = &g_meshTable[c->meshId];
            if (!mv->vbo) continue;
            float pc[32];
            vlcr_matrixToGpu(pc, curBase);
            vlcr_matrixToGpu(pc + 16, prevBase);
            vkCmdPushConstants(cb, VK.velocityLayout,
                               VK_SHADER_STAGE_VERTEX_BIT, 0, 128, pc);
            VkDeviceSize off = 0;
            vkCmdBindVertexBuffers(cb, 0, 1, &mv->vbo, &off);
            vkCmdBindIndexBuffer(cb, mv->ibo, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cb, (uint32_t)mv->indexCount, 1, 0, 0, 0);
        }
    }
    vkCmdEndRenderPass(cb);

    db.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    db.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    db.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    db.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, NULL, 0, NULL, 1, &db);
}

static void recordTaaHistoryCopy(VkCommandBuffer cb) {
    if (!VK.taaEnabled || !VK.taaHistoryImage) return;
    if (VK.taaHistoryWidth != VK.swapExtent.width ||
        VK.taaHistoryHeight != VK.swapExtent.height) {
        VkImageMemoryBarrier toPresent = {0};
        toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toPresent.srcQueueFamilyIndex = toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.image = VK.images[VK.currentImage];
        toPresent.subresourceRange = (VkImageSubresourceRange){
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL,
                             0, NULL, 1, &toPresent);
        VK.taaReset = 1;
        return;
    }
    VkImageMemoryBarrier b[2] = {0};
    for (int i = 0; i < 2; ++i) {
        b[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b[i].subresourceRange = (VkImageSubresourceRange){
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    }
    b[0].image = VK.taaHistoryImage;
    b[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    b[0].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL,
                         1, &b[0]);
    VkImageCopy region = {0};
    region.srcSubresource = (VkImageSubresourceLayers){
        VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstSubresource = region.srcSubresource;
    region.extent = (VkExtent3D){ VK.swapExtent.width, VK.swapExtent.height, 1 };
    vkCmdCopyImage(cb, VK.images[VK.currentImage],
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK.taaHistoryImage,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    b[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0,
                         NULL, 1, &b[0]);

    b[1].image = VK.images[VK.currentImage];
    b[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b[1].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b[1].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b[1].dstAccessMask = 0;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0,
                         NULL, 1, &b[1]);
}

/* Final composite is planned by the render graph. The graph owns no images
   here: scene colour, bloom and the acquired swapchain image are imported and
   the graph emits the PRESENT -> COLOR -> PRESENT barriers. */
static bool recordCompositeGraph(VkCommandBuffer cb) {
    VrgGraph *g = vrgCreate();
    if (!g) return false;
    uint32_t w = VK.swapExtent.width, h = VK.swapExtent.height;
    VrgImageDesc scene = { "scene", w, h, VK.swapFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VrgImageDesc bloom = { "bloom", w, h, VK.swapFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VrgImageDesc target = { "swapchain", w, h, VK.swapFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK.taaEnabled ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                      : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
    int sceneId = vrgAddExternalImage(g, &scene, VK.sceneColorImage, VK.sceneColorView);
    int bloomId = vrgAddExternalImage(g, &bloom,
        VK.bloomReady ? VK.bloomImage : VK.sceneColorImage,
        VK.bloomReady ? VK.bloomView : VK.sceneColorView);
    int targetId = vrgAddExternalImage(g, &target, VK.images[VK.currentImage],
                                       VK.views[VK.currentImage]);
    int pass = vrgAddPass(g, "composite");
    if (sceneId < 0 || bloomId < 0 || targetId < 0 || pass < 0) {
        vrgDestroy(g);
        return false;
    }
    vrgPassReadImage(g, pass, sceneId, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    vrgPassReadImage(g, pass, bloomId, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    vrgPassWriteImage(g, pass, targetId, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    bool ok = vrgCompile(g) && vrgExecute(g, cb, compositeGraphPass, NULL);
    if (ok && VK.taaEnabled) recordTaaHistoryCopy(cb);
    if (!ok) fprintf(stderr, "[vlcrlib] composite graph failed: %s\n",
                     vrgGetError(g) ? vrgGetError(g) : "execute");
    vrgDestroy(g);
    return ok;
}

static void recordComposite(VkCommandBuffer cb) {
    if (!VK.postProcessEnabled || !VK.sceneReady) return;
    if (VK.compositeReady && VK.compositePipeline) {
        /* Shader composite: scene + bloom ??? swapchain */
        VkClearValue clear[2] = {0};
        clear[0].color.float32[0] = 0.0f;
        clear[1].depthStencil.depth = 1.0f;
        clear[1].depthStencil.stencil = 0;
        VkRenderPassBeginInfo rp = {0};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = VK.compositePass;
        rp.framebuffer = VK.framebuffers[VK.currentImage];
        rp.renderArea.extent = VK.swapExtent;
        rp.clearValueCount = 2;
        rp.pClearValues = clear;
        vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, VK.compositePipeline);
        if (VK.compositeDescSet) {
            uint32_t dyn = VK.currentFrame * VK.taaUBOStride;
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    VK.compositeLayout, 0, 1,
                                    &VK.compositeDescSet, 1, &dyn);
        }
        float bloom = (VK.bloomReady && VK.debugView == 0)
                    ? (VK.bloomIntensity > 0.f ? VK.bloomIntensity : 0.3f) : 0.0f;
        float pc[3] = {
            bloom,
            VK.exposure,
            (VK.fxaaEnabled && !VK.taaEnabled && VK.debugView == 0) ? 1.0f : 0.0f
        };
        vkCmdPushConstants(cb, VK.compositeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 12, pc);
        VkViewport vp = { 0, 0, (float)VK.swapExtent.width, (float)VK.swapExtent.height, 0.f, 1.f };
        vkCmdSetViewport(cb, 0, 1, &vp);
        VkRect2D sc = { {0,0}, VK.swapExtent };
        vkCmdSetScissor(cb, 0, 1, &sc);
        vkCmdDraw(cb, 3, 1, 0, 0);
        vkCmdEndRenderPass(cb);
        return;
    }
    /* Fallback blit */
    VkImageBlit blit = {0};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[1] = (VkOffset3D){ (int)VK.swapExtent.width, (int)VK.swapExtent.height, 1 };
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[1] = (VkOffset3D){ (int)VK.swapExtent.width, (int)VK.swapExtent.height, 1 };

    /* Transition scene to transfer src, swapchain image to transfer dst */
    VkImageMemoryBarrier b[2] = {0};
    b[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b[0].image = VK.sceneColorImage;
    b[0].subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    b[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b[1].srcAccessMask = 0;
    b[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b[1].image = VK.images[VK.currentImage];
    b[1].subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 2, b);

    vkCmdBlitImage(cb, VK.sceneColorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK.images[VK.currentImage], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blit, VK_FILTER_LINEAR);

    /* Transition swapchain to present */
    b[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b[1].dstAccessMask = 0;
    b[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b[1].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0, 0, NULL, 0, NULL, 1, &b[1]);
}

void EndVulkanFrame(void) {
    if (VK.frameFailed) return;
    Uint64 now = SDL_GetPerformanceCounter();
    double freq = (double)SDL_GetPerformanceFrequency();
    g_frameStats.cpuFrameMs = freq > 0.0
        ? (float)((double)(now - g_frameStatsStart) * 1000.0 / freq)
        : 0.0f;
    g_frameStats.frameIndex = VK.frameIndex;
    VK.framePhase = VLCR_PHASE_PRESENT;
    VK.inFrame = 0;
    if (VK.currentCmd == VK_NULL_HANDLE) return;

    VkCommandBuffer cb = VK.currentCmd;
    vkCmdEndRenderPass(cb);
    if (VK.gpuTimerPool) {
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            VK.gpuTimerPool, VK.currentFrame * 4 + 2);
    }
    if (VK.postProcessEnabled && VK.sceneReady) {
        /* Bloom must run after the scene pass and before composite: the
           composite shader samples the blurred bright-pass texture. */
        if (VK.taaEnabled) recordVelocityPass(cb);
        updateTaaParams();
        if (!recordBloomGraph(cb)) recordBloomPass(cb);
        if (VK.compositeReady && VK.compositePipeline) {
            if (!recordCompositeGraph(cb)) recordComposite(cb);
        } else {
            recordComposite(cb);
        }
    }

    /* Preserve this frame's caster transforms for the next frame's depth
     * prepasses. Copying at frame start skipped the just-recorded scene and
     * introduced an unintended extra frame of lag. */
    VK.shadowCasterCountPrev = VK.shadowCasterCount;
    for (int i = 0; i < VK.shadowCasterCount; ++i) {
        VK.shadowCastersPrev[i] = VK.shadowCasters[i];
    }
    VK.shadowCasterCount = 0;

    if (VK.gpuTimerPool) {
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            VK.gpuTimerPool, VK.currentFrame * 4 + 3);
        g_timerSlotValid |= (1u << VK.currentFrame);
    }

    VkResult frameResult = vkEndCommandBuffer(cb);
    if (frameResult != VK_SUCCESS) {
        VK.frameFailed = true;
        VK.lastFrameResult = (int)frameResult;
        fprintf(stderr, "[vlcrlib] ending frame command buffer failed: %d\n", (int)frameResult);
        VK.currentCmd = VK_NULL_HANDLE;
        VK.framePhase = VLCR_PHASE_IDLE;
        return;
    }

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &VK.semImgAvailable[VK.currentFrame];
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &VK.semRenderDone[VK.currentImage];

    /* Reset only after command recording succeeds. Otherwise a recording error
       would leave the frame fence unsignaled and the next frame could hang. */
    frameResult = vkResetFences(VK.device, 1, &VK.inFlight[VK.currentFrame]);
    if (frameResult != VK_SUCCESS) {
        VK.frameFailed = true;
        VK.lastFrameResult = (int)frameResult;
        fprintf(stderr, "[vlcrlib] resetting frame fence failed: %d\n", (int)frameResult);
        VK.currentCmd = VK_NULL_HANDLE;
        VK.framePhase = VLCR_PHASE_IDLE;
        return;
    }

    VkResult submitResult = vkQueueSubmit(VK.gfxQueue, 1, &si, VK.inFlight[VK.currentFrame]);
    if (submitResult != VK_SUCCESS) {
        fprintf(stderr, "[vlcrlib] vkQueueSubmit failed: %d\n", (int)submitResult);
        /* No successful submit will signal the reset fence; fail closed so the
           next frame cannot block forever waiting on it. */
        VK.frameFailed = true;
        VK.lastFrameResult = (int)submitResult;
        VK.currentCmd = VK_NULL_HANDLE;
        VK.framePhase = VLCR_PHASE_IDLE;
        return;
    }
    VK.imagesInFlight[VK.currentImage] = VK.inFlight[VK.currentFrame];

    VkPresentInfoKHR pi = {0};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &VK.semRenderDone[VK.currentImage];
    pi.swapchainCount = 1;
    pi.pSwapchains = &VK.swapchain;
    pi.pImageIndices = &VK.currentImage;

    VkResult pr = vkQueuePresentKHR(VK.presentQueue, &pi);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR || VK.framebufferResized) {
        VK.framebufferResized = false;
        if (!vgraphics_internal_recreate()) {
            VK.frameFailed = true;
            VK.lastFrameResult = (int)VK_ERROR_INITIALIZATION_FAILED;
        }
    } else if (pr != VK_SUCCESS) {
        VK.frameFailed = true;
        VK.lastFrameResult = (int)pr;
        fprintf(stderr, "[vlcrlib] presenting swapchain image failed: %d\n", (int)pr);
    }

    VK.currentFrame = (VK.currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    VK.currentCmd = VK_NULL_HANDLE;
    VK.framePhase = VLCR_PHASE_IDLE;
}

void SetVulkanClearColor(float r, float g, float b, float a) {
    VK.clearColor.color.float32[0] = r;
    VK.clearColor.color.float32[1] = g;
    VK.clearColor.color.float32[2] = b;
    VK.clearColor.color.float32[3] = a;
}

void SetGraphicsDebugView(int mode) {
    if (mode < 0) mode = 0;
    if (mode > 7) mode = 7;
    VK.debugView = mode;
}
int GetGraphicsDebugView(void) { return VK.debugView; }
void SetGraphicsDebugLog(int enabled) { VK.debugLog = enabled ? 1 : 0; }
void SetGraphicsExposure(float exposure) {
    VK.exposure = exposure < 0.01f ? 0.01f : (exposure > 16.0f ? 16.0f : exposure);
    VCpuSetExposure(VK.exposure > 0.0f ? VK.exposure : 1.0f);
}
float GetGraphicsExposure(void) { return VK.exposure > 0.0f ? VK.exposure : 1.0f; }
int GetGraphicsFramePhase(void) { return VK.framePhase; }
int GetGraphicsLastFrameResult(void) { return VK.lastFrameResult; }
void SetBloom(float threshold, float intensity) {
    VK.bloomThreshold = threshold < 0.1f ? 0.1f : threshold;
    VK.bloomIntensity = intensity < 0.0f ? 0.0f : (intensity > 4.0f ? 4.0f : intensity);
}
void SetPostProcess(int enabled) { VK.postProcessEnabled = enabled ? 1 : 0; }
int  GetPostProcess(void) { return VK.postProcessEnabled; }
void SetFXAA(int enabled) {
    VK.fxaaEnabled = enabled ? 1 : 0;
    if (VK.fxaaEnabled) VK.postProcessEnabled = 1;
}
int GetFXAA(void) { return VK.fxaaEnabled; }
void SetTAA(int enabled) {
    VK.taaEnabled = enabled ? 1 : 0;
    if (VK.taaEnabled) {
        VK.postProcessEnabled = 1;
        VK.taaReset = 1;
    }
}
int GetTAA(void) { return VK.taaEnabled; }

void SetSSAO(int enabled, float radius, float intensity) {
    VK.ssaoEnabled = enabled ? 1 : 0;
    VK.ssaoRadius = radius < 0.05f ? 0.05f : (radius > 2.0f ? 2.0f : radius);
    VK.ssaoIntensity = intensity < 0.0f ? 0.0f : (intensity > 2.0f ? 2.0f : intensity);
}
void GetSSAO(int *enabled, float *radius, float *intensity) {
    if (enabled) *enabled = VK.ssaoEnabled;
    if (radius) *radius = VK.ssaoRadius;
    if (intensity) *intensity = VK.ssaoIntensity;
}
void SetShadowQuality(float bias, float strength, float softRadius) {
    VK.shadowBias = bias > 0.0f ? bias : 0.005f;
    VK.shadowStrength = strength < 0.0f ? 0.0f : (strength > 1.0f ? 1.0f : strength);
    VK.shadowSoftness = softRadius < 1.0f ? 1.0f : (softRadius > 3.0f ? 3.0f : softRadius);
    VK.spotShadowBias = VK.shadowBias;
    VK.spotShadowStrength = VK.shadowStrength;
    VK.spotShadowSoftness = VK.shadowSoftness;
}

void SetVulkanViewport(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
void SetVulkanViewMatrix(Matrix m)       { VK.view = m; }
void SetVulkanProjectionMatrix(Matrix m) { VK.proj = m; }

void vgraphics_Resize(void) {
    VK.framebufferResized = true;
}
Mesh vgraphics_RegisterMesh(const Vertex3D *verts, int vertexCount,
                             const uint32_t *indices, int indexCount)
{
    return registerMesh(verts, vertexCount, indices, indexCount);
}

Model vgraphics_CreateModelFromMeshes(const Mesh *meshes,
                                       const int *meshMaterials, int meshCount,
                                       const Material *materials,
                                       int materialCount)
{
    return vgraphics_CreateModelFromMeshesEx(meshes, meshMaterials, NULL,
                                             meshCount, materials, materialCount);
}

/* transforms[i] = glTF node world matrix for draw slot i (may be NULL ??? identity) */
Model vgraphics_CreateModelFromMeshesEx(const Mesh *meshes,
                                        const int *meshMaterials,
                                        const Matrix *transforms,
                                        int meshCount,
                                        const Material *materials,
                                        int materialCount)
{
    Model model = {0};
    if (!meshes || meshCount <= 0) return model;
    if (g_modelCount + 1 >= VLCRLIB_MAX_MODELS) return model;
    if (materialCount > 0 && !materials) return model;

    int idx = ++g_modelCount;
    ModelEntry *e = &g_modelTable[idx];
    memset(e, 0, sizeof(*e));

    e->meshes          = (Mesh *)malloc(sizeof(Mesh) * (size_t)meshCount);
    e->meshMaterials   = (int *)malloc(sizeof(int) * (size_t)meshCount);
    e->meshTransforms  = (Matrix *)malloc(sizeof(Matrix) * (size_t)meshCount);
    e->materials       = (Material *)malloc(sizeof(Material) * (size_t)(materialCount > 0 ? materialCount : 1));

    if (!e->meshes || !e->meshMaterials || !e->meshTransforms || !e->materials) {
        free(e->meshes); free(e->meshMaterials); free(e->meshTransforms); free(e->materials);
        g_modelCount--;
        return model;
    }

    for (int i = 0; i < materialCount; ++i)
        e->materials[i] = materials[i];
    if (materialCount <= 0) {
        e->materials[0] = LoadMaterialDefault();
        materialCount = 1;
    }
    for (int i = 0; i < meshCount; ++i) {
        e->meshes[i]          = meshes[i];
        int mi = meshMaterials ? meshMaterials[i] : 0;
        e->meshMaterials[i]   = (mi >= 0 && mi < materialCount) ? mi : 0;
        e->meshTransforms[i]  = transforms ? transforms[i] : MatrixIdentity();
    }

    e->meshCount     = meshCount;
    e->materialCount = materialCount;

    model.id            = (unsigned int)idx;
    model.meshCount     = meshCount;
    model.materialCount = materialCount;
    return model;
}

static void updateCompositeDescriptor(void) {
    if (!VK.compositeDescSet) return;
    VkDescriptorImageInfo imgs[5] = {0};
    imgs[0].imageView = VK.sceneColorView;
    imgs[0].sampler = VK.bloomSampler ? VK.bloomSampler : VK.sampler2D;
    imgs[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imgs[1].imageView = VK.bloomReady ? VK.bloomView : VK.sceneColorView;
    imgs[1].sampler = imgs[0].sampler;
    imgs[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imgs[2].imageView = VK.taaHistoryView ? VK.taaHistoryView : VK.sceneColorView;
    imgs[2].sampler = imgs[0].sampler;
    imgs[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imgs[3].imageView = VK.sceneDepthView;
    imgs[3].sampler = imgs[0].sampler;
    imgs[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imgs[4].imageView = VK.velocityView ? VK.velocityView : VK.sceneColorView;
    imgs[4].sampler = imgs[0].sampler;
    imgs[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorBufferInfo taaInfo = { VK.taaUBO, 0, sizeof(TaaParams) };
    VkWriteDescriptorSet wr[6] = {0};
    for (int i = 0; i < 5; ++i) {
        wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr[i].dstSet = VK.compositeDescSet;
        wr[i].dstBinding = (uint32_t)i;
        wr[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wr[i].descriptorCount = 1;
        wr[i].pImageInfo = &imgs[i];
    }
    wr[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    wr[4].pImageInfo = NULL;
    wr[4].pBufferInfo = &taaInfo;
    wr[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr[5].dstSet = VK.compositeDescSet;
    wr[5].dstBinding = 5;
    wr[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr[5].descriptorCount = 1;
    wr[5].pImageInfo = &imgs[4];
    vkUpdateDescriptorSets(VK.device, 6, wr, 0, NULL);
}

static void updateBloomDescriptors(void) {
    VkImageView views[3] = { VK.sceneColorView, VK.bloomView, VK.bloomView2 };
    VkDescriptorSet sets[3] = { VK.bloomDescSet, VK.bloomDescSetH, VK.bloomDescSetV };
    for (int i = 0; i < 3; ++i) {
        if (!sets[i] || !views[i]) continue;
        VkDescriptorImageInfo ii = {0};
        ii.imageView = views[i];
        ii.sampler = VK.bloomSampler ? VK.bloomSampler : VK.sampler2D;
        ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet wr = {0};
        wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr.dstSet = sets[i];
        wr.dstBinding = 0;
        wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wr.descriptorCount = 1;
        wr.pImageInfo = &ii;
        vkUpdateDescriptorSets(VK.device, 1, &wr, 0, NULL);
    }
}

static void updateSsaoDescriptor(void) {
    if (!VK.ssaoDescSet || !VK.ssaoDepthView) return;
    VkDescriptorImageInfo ii = {0};
    ii.imageView = VK.ssaoDepthView;
    ii.sampler = VK.ssaoSampler ? VK.ssaoSampler : VK.sampler2D;
    ii.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet wr = {0};
    wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr.dstSet = VK.ssaoDescSet;
    wr.dstBinding = 0;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr.descriptorCount = 1;
    wr.pImageInfo = &ii;
    vkUpdateDescriptorSets(VK.device, 1, &wr, 0, NULL);
}

static bool createOrUpdateOffscreenTargets(void) {
    if (!VK.device) return false;
    vkDeviceWaitIdle(VK.device);

    /* Scene color/depth/TAA history. */
    if (VK.sceneFB) vkDestroyFramebuffer(VK.device, VK.sceneFB, NULL);
    if (VK.scenePass) vkDestroyRenderPass(VK.device, VK.scenePass, NULL);
    if (VK.sceneColorView) vkDestroyImageView(VK.device, VK.sceneColorView, NULL);
    if (VK.sceneColorImage) vkDestroyImage(VK.device, VK.sceneColorImage, NULL);
    if (VK.sceneColorMem) vkFreeMemory(VK.device, VK.sceneColorMem, NULL);
    if (VK.sceneDepthView) vkDestroyImageView(VK.device, VK.sceneDepthView, NULL);
    if (VK.sceneDepthImage) vkDestroyImage(VK.device, VK.sceneDepthImage, NULL);
    if (VK.sceneDepthMem) vkFreeMemory(VK.device, VK.sceneDepthMem, NULL);
    if (VK.taaHistoryView) vkDestroyImageView(VK.device, VK.taaHistoryView, NULL);
    if (VK.taaHistoryImage) vkDestroyImage(VK.device, VK.taaHistoryImage, NULL);
    if (VK.taaHistoryMem) vkFreeMemory(VK.device, VK.taaHistoryMem, NULL);
    VK.sceneFB = VK_NULL_HANDLE;
    VK.scenePass = VK_NULL_HANDLE;
    VK.sceneColorView = VK_NULL_HANDLE;
    VK.sceneColorImage = VK_NULL_HANDLE;
    VK.sceneColorMem = VK_NULL_HANDLE;
    VK.sceneDepthView = VK_NULL_HANDLE;
    VK.sceneDepthImage = VK_NULL_HANDLE;
    VK.sceneDepthMem = VK_NULL_HANDLE;
    VK.taaHistoryView = VK_NULL_HANDLE;
    VK.taaHistoryImage = VK_NULL_HANDLE;
    VK.taaHistoryMem = VK_NULL_HANDLE;
    VK.sceneReady = false;
    VK.taaHistoryWidth = VK.taaHistoryHeight = 0;
    VK.taaReset = 1;
    if (!createSceneOffscreen()) return false;

    /* Velocity target is tied to scene depth. */
    if (VK.velocityFB) vkDestroyFramebuffer(VK.device, VK.velocityFB, NULL);
    if (VK.velocityView) vkDestroyImageView(VK.device, VK.velocityView, NULL);
    if (VK.velocityImage) vkDestroyImage(VK.device, VK.velocityImage, NULL);
    if (VK.velocityMem) vkFreeMemory(VK.device, VK.velocityMem, NULL);
    VK.velocityFB = VK_NULL_HANDLE;
    VK.velocityView = VK_NULL_HANDLE;
    VK.velocityImage = VK_NULL_HANDLE;
    VK.velocityMem = VK_NULL_HANDLE;
    if (VK.velocityPass && VK.sceneDepthView) {
        uint32_t w = VK.sceneWidth, h = VK.sceneHeight;
        if (!vlcr_createImage(w, h, VK_FORMAT_R16G16_SFLOAT,
                              VK_IMAGE_TILING_OPTIMAL,
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              &VK.velocityImage, &VK.velocityMem)) return false;
        vlcrNameImage(VK.velocityImage, "velocity_color");
        VkImageViewCreateInfo vi = {0};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = VK.velocityImage;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R16G16_SFLOAT;
        vi.subresourceRange = (VkImageSubresourceRange){
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(VK.device, &vi, NULL, &VK.velocityView) != VK_SUCCESS)
            return false;
        vlcrInitSampledImage(VK.velocityImage, VK_IMAGE_ASPECT_COLOR_BIT);
        VkImageView views[2] = { VK.velocityView, VK.sceneDepthView };
        VkFramebufferCreateInfo fb = {0};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = VK.velocityPass;
        fb.attachmentCount = 2;
        fb.pAttachments = views;
        fb.width = w; fb.height = h; fb.layers = 1;
        if (vkCreateFramebuffer(VK.device, &fb, NULL, &VK.velocityFB) != VK_SUCCESS)
            return false;
    }

    /* Bloom ping-pong targets. */
    if (VK.bloomFB) vkDestroyFramebuffer(VK.device, VK.bloomFB, NULL);
    if (VK.bloomFB2) vkDestroyFramebuffer(VK.device, VK.bloomFB2, NULL);
    if (VK.bloomView) vkDestroyImageView(VK.device, VK.bloomView, NULL);
    if (VK.bloomView2) vkDestroyImageView(VK.device, VK.bloomView2, NULL);
    if (VK.bloomImage) vkDestroyImage(VK.device, VK.bloomImage, NULL);
    if (VK.bloomImage2) vkDestroyImage(VK.device, VK.bloomImage2, NULL);
    if (VK.bloomMem) vkFreeMemory(VK.device, VK.bloomMem, NULL);
    if (VK.bloomMem2) vkFreeMemory(VK.device, VK.bloomMem2, NULL);
    VK.bloomFB = VK.bloomFB2 = VK_NULL_HANDLE;
    VK.bloomView = VK.bloomView2 = VK_NULL_HANDLE;
    VK.bloomImage = VK.bloomImage2 = VK_NULL_HANDLE;
    VK.bloomMem = VK.bloomMem2 = VK_NULL_HANDLE;
    if (VK.bloomPass) {
        int w = (int)VK.sceneWidth / 2, h = (int)VK.sceneHeight / 2;
        if (w < 1) w = 1; if (h < 1) h = 1;
        VK.bloomWidth = w; VK.bloomHeight = h;
        for (int i = 0; i < 2; ++i) {
            VkImage *img = i ? &VK.bloomImage2 : &VK.bloomImage;
            VkDeviceMemory *mem = i ? &VK.bloomMem2 : &VK.bloomMem;
            VkImageView *view = i ? &VK.bloomView2 : &VK.bloomView;
            if (!vlcr_createImage((uint32_t)w, (uint32_t)h,
                    VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, img, mem)) return false;
            VkImageViewCreateInfo vi = {0};
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image = *img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            vi.subresourceRange = (VkImageSubresourceRange){
                VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            if (vkCreateImageView(VK.device, &vi, NULL, view) != VK_SUCCESS)
                return false;
        }
        VkFramebufferCreateInfo fb = {0};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = VK.bloomPass;
        fb.attachmentCount = 1;
        fb.width = (uint32_t)w; fb.height = (uint32_t)h; fb.layers = 1;
        fb.pAttachments = &VK.bloomView;
        if (vkCreateFramebuffer(VK.device, &fb, NULL, &VK.bloomFB) != VK_SUCCESS)
            return false;
        fb.pAttachments = &VK.bloomView2;
        if (vkCreateFramebuffer(VK.device, &fb, NULL, &VK.bloomFB2) != VK_SUCCESS)
            return false;
        updateBloomDescriptors();
    }

    /* SSAO color + half-res depth. */
    if (VK.ssaoFB) vkDestroyFramebuffer(VK.device, VK.ssaoFB, NULL);
    if (VK.ssaoDepthFB) vkDestroyFramebuffer(VK.device, VK.ssaoDepthFB, NULL);
    if (VK.ssaoView) vkDestroyImageView(VK.device, VK.ssaoView, NULL);
    if (VK.ssaoImage) vkDestroyImage(VK.device, VK.ssaoImage, NULL);
    if (VK.ssaoMem) vkFreeMemory(VK.device, VK.ssaoMem, NULL);
    if (VK.ssaoDepthView) vkDestroyImageView(VK.device, VK.ssaoDepthView, NULL);
    if (VK.ssaoDepthImage) vkDestroyImage(VK.device, VK.ssaoDepthImage, NULL);
    if (VK.ssaoDepthMem) vkFreeMemory(VK.device, VK.ssaoDepthMem, NULL);
    VK.ssaoFB = VK.ssaoDepthFB = VK_NULL_HANDLE;
    VK.ssaoView = VK.ssaoDepthView = VK_NULL_HANDLE;
    VK.ssaoImage = VK.ssaoDepthImage = VK_NULL_HANDLE;
    VK.ssaoMem = VK.ssaoDepthMem = VK_NULL_HANDLE;
    if (VK.ssaoPass && VK.ssaoDepthPass) {
        int w = (int)VK.sceneWidth / 2, h = (int)VK.sceneHeight / 2;
        if (w < 1) w = 1; if (h < 1) h = 1;
        VK.ssaoWidth = w; VK.ssaoHeight = h;
        if (!vlcr_createImage((uint32_t)w, (uint32_t)h, VK_FORMAT_R8_UNORM,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                &VK.ssaoImage, &VK.ssaoMem)) return false;
        VkImageViewCreateInfo vi = {0};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = VK.ssaoImage; vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R8_UNORM;
        vi.subresourceRange = (VkImageSubresourceRange){
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(VK.device, &vi, NULL, &VK.ssaoView) != VK_SUCCESS)
            return false;
        if (!vlcr_createImage((uint32_t)w, (uint32_t)h, VK_FORMAT_D32_SFLOAT,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                &VK.ssaoDepthImage, &VK.ssaoDepthMem)) return false;
        vi.image = VK.ssaoDepthImage; vi.format = VK_FORMAT_D32_SFLOAT;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (vkCreateImageView(VK.device, &vi, NULL, &VK.ssaoDepthView) != VK_SUCCESS)
            return false;
        VkFramebufferCreateInfo fb = {0};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = VK.ssaoPass;
        fb.attachmentCount = 1;
        fb.pAttachments = &VK.ssaoView;
        fb.width = (uint32_t)w; fb.height = (uint32_t)h; fb.layers = 1;
        if (vkCreateFramebuffer(VK.device, &fb, NULL, &VK.ssaoFB) != VK_SUCCESS)
            return false;
        fb.renderPass = VK.ssaoDepthPass;
        fb.pAttachments = &VK.ssaoDepthView;
        if (vkCreateFramebuffer(VK.device, &fb, NULL, &VK.ssaoDepthFB) != VK_SUCCESS)
            return false;
        updateSsaoDescriptor();
    }

    updateCompositeDescriptor();
    ibl_refresh_all_material_descriptors();
    return true;
}

bool vgraphics_internal_recreate(void) {
    int w = 0, h = 0;
    vcore_GetScreenSize(&w, &h);
    while (w == 0 || h == 0) {
        SDL_Event ev;
        SDL_WaitEvent(&ev);
        if (ev.type == SDL_EVENT_QUIT) return false;
        vcore_GetScreenSize(&w, &h);
    }

    vkDeviceWaitIdle(VK.device);

    /* Present semaphores belong to the old swapchain's image set. Retire
     * them only after all device work using that swapchain has completed. */
    if (VK.semRenderDone) {
        for (uint32_t i = 0; i < VK.imageCount; ++i)
            vkDestroySemaphore(VK.device, VK.semRenderDone[i], NULL);
        free(VK.semRenderDone);
        VK.semRenderDone = NULL;
    }

    for (uint32_t i = 0; i < VK.imageCount; ++i) {
        vkDestroyFramebuffer(VK.device, VK.framebuffers[i], NULL);
        vkDestroyImageView(VK.device, VK.views[i], NULL);
    }
    free(VK.framebuffers); VK.framebuffers = NULL;
    free(VK.views);        VK.views = NULL;
    free(VK.images);       VK.images = NULL;
    free(VK.imagesInFlight); VK.imagesInFlight = NULL;

    vkFreeCommandBuffers(VK.device, VK.cmdPool, VK.imageCount, VK.cmdBuffers);
    free(VK.cmdBuffers); VK.cmdBuffers = NULL;

    vkDestroySwapchainKHR(VK.device, VK.swapchain, NULL);
    VK.swapchain = VK_NULL_HANDLE;

    vkDestroyImageView(VK.device, VK.depthView, NULL);
    vkDestroyImage(VK.device, VK.depthImage, NULL);
    vkFreeMemory(VK.device, VK.depthMem, NULL);
    VK.depthView = VK_NULL_HANDLE;

    if (!createSwapchain((uint32_t)w, (uint32_t)h)) return false;
    VK.semRenderDone = calloc(VK.imageCount, sizeof(VkSemaphore));
    if (!VK.semRenderDone) return false;
    VkSemaphoreCreateInfo semInfo = {0};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < VK.imageCount; ++i)
        if (vkCreateSemaphore(VK.device, &semInfo, NULL, &VK.semRenderDone[i]) != VK_SUCCESS)
            return false;
    if (!createDepthResources())                    return false;
    if (!createFramebuffers())                      return false;

    VK.cmdBuffers = calloc(VK.imageCount, sizeof(VkCommandBuffer));
    VkCommandBufferAllocateInfo ai = {0};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = VK.cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = VK.imageCount;
    if (vkAllocateCommandBuffers(VK.device, &ai, VK.cmdBuffers) != VK_SUCCESS) return false;
    VK.imagesInFlight = calloc(VK.imageCount, sizeof(VkFence));
    if (!VK.imagesInFlight) return false;
    if (!createOrUpdateOffscreenTargets()) return false;
    return true;
}

