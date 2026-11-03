#ifndef VLCRLIB_VRENDER_GRAPH_H
#define VLCRLIB_VRENDER_GRAPH_H

/*
 * Minimal render-graph foundation.
 *
 * The graph owns only CPU-side declarations: images, passes, reads/writes and
 * the barrier plan. Vulkan image allocation/execution is a separate step so
 * the planner can be unit-tested without a device.
 */

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VRG_MAX_IMAGES 64
#define VRG_MAX_PASSES 128
#define VRG_MAX_USES   512
#define VRG_MAX_BARRIERS 1024

typedef struct VrgImageDesc {
    const char       *name;
    uint32_t          width;
    uint32_t          height;
    VkFormat          format;
    VkImageUsageFlags usage;
    VkImageLayout     initialLayout;
    VkImageLayout     finalLayout;
} VrgImageDesc;

typedef struct VrgImageBarrier {
    int                    image;
    int                    pass;       /* -1 = graph epilogue */
    VkImageLayout          oldLayout;
    VkImageLayout          newLayout;
    VkAccessFlags          srcAccess;
    VkAccessFlags          dstAccess;
    VkPipelineStageFlags   srcStage;
    VkPipelineStageFlags   dstStage;
} VrgImageBarrier;

typedef struct VrgPassInfo {
    const char *name;
    int         readCount;
    int         writeCount;
} VrgPassInfo;

typedef struct VrgGraph VrgGraph;

VrgGraph *vrgCreate(void);
void      vrgDestroy(VrgGraph *g);

int  vrgAddImage(VrgGraph *g, const VrgImageDesc *desc);
int  vrgAddPass(VrgGraph *g, const char *name);

bool vrgPassReadImage(VrgGraph *g, int pass, int image,
                      VkImageLayout layout,
                      VkPipelineStageFlags stage,
                      VkAccessFlags access);
bool vrgPassWriteImage(VrgGraph *g, int pass, int image,
                       VkImageLayout layout,
                       VkPipelineStageFlags stage,
                       VkAccessFlags access);

/* Builds the ordered pass list and barrier plan. Returns false on invalid
   handles, a read before any writer, or a cycle. */
bool vrgCompile(VrgGraph *g);

int  vrgGetImageCount(const VrgGraph *g);
int  vrgGetPassCount(const VrgGraph *g);
const VrgImageDesc *vrgGetImage(const VrgGraph *g, int image);
const VrgPassInfo  *vrgGetPass(const VrgGraph *g, int pass);
int  vrgGetPassOrder(const VrgGraph *g, int index);

int  vrgGetBarrierCount(const VrgGraph *g);
const VrgImageBarrier *vrgGetBarrier(const VrgGraph *g, int index);

/* Human-readable compile error, or NULL. */
const char *vrgGetError(const VrgGraph *g);

/* Optional Vulkan execution. Call vrgCompile() first. Images are transient
   graph-owned resources; vrgFreeVulkan() must be called before vrgDestroy()
   while the device is still alive. */
typedef struct VrgVulkanImage {
    VkImage        image;
    VkImageView    view;
    VkDeviceMemory memory;
} VrgVulkanImage;

typedef void (*VrgPassCallback)(VkCommandBuffer cb, int pass, void *user);

/* External images participate in barrier planning but are never allocated or
   destroyed by the graph. External-only graphs execute without allocation. */
int  vrgAddExternalImage(VrgGraph *g, const VrgImageDesc *desc,
                         VkImage image, VkImageView view);

bool vrgAllocateVulkan(VrgGraph *g, VkDevice device, VkPhysicalDevice physical);
void vrgFreeVulkan(VrgGraph *g, VkDevice device);
VkImage     vrgGetVkImage(const VrgGraph *g, int image);
VkImageView vrgGetVkImageView(const VrgGraph *g, int image);

/* Emits the compiled barriers and calls `callback` for every pass in graph
   order. A NULL callback is valid for barrier-only tests. */
bool vrgExecute(VrgGraph *g, VkCommandBuffer cb,
                VrgPassCallback callback, void *user);

#ifdef __cplusplus
}
#endif

#endif /* VLCRLIB_VRENDER_GRAPH_H */
