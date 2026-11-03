#ifndef VLCRLIB_VRENDER_GRAPH_WEBGPU_H
#define VLCRLIB_VRENDER_GRAPH_WEBGPU_H

/*
 * WebGPU render-graph planner.
 *
 * WebGPU has no explicit image barriers: ordering between render passes is
 * implicit. The graph therefore owns the CPU-side dependency plan (pass order
 * and per-pass usage transitions) plus the transient offscreen textures, and
 * invokes a callback per pass in dependency order. Imported textures
 * (swapchain-sized scene targets, history buffers) are never created or
 * destroyed by the graph.
 */

#include <stdbool.h>
#include <stdint.h>
#include <webgpu/webgpu.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VRGW_MAX_IMAGES 32
#define VRGW_MAX_PASSES 32
#define VRGW_MAX_USES  128

typedef enum VrgwUsage {
    VRGW_USAGE_NONE             = 0,
    VRGW_USAGE_SAMPLED          = 1u << 0,
    VRGW_USAGE_COLOR_ATTACHMENT = 1u << 1,
    VRGW_USAGE_DEPTH_ATTACHMENT = 1u << 2,
    VRGW_USAGE_COPY_SRC         = 1u << 3,
    VRGW_USAGE_COPY_DST         = 1u << 4,
} VrgwUsage;

typedef struct VrgwTransition {
    int        image;
    int        pass;
    VrgwUsage  oldUsage;
    VrgwUsage  newUsage;
} VrgwTransition;

typedef struct VrgwGraph VrgwGraph;

typedef void (*VrgwPassCallback)(WGPUCommandEncoder enc, int pass, void *user);

VrgwGraph *vrgwCreate(void);

/* Destroys graph-owned transient textures and views. Imported textures are
   left untouched. */
void vrgwDestroy(VrgwGraph *g);

/* Transient image: allocated by vrgwCompile() and freed by vrgwDestroy(). */
int vrgwAddImage(VrgwGraph *g, const char *name,
                 uint32_t width, uint32_t height,
                 WGPUTextureFormat format,
                 WGPUTextureUsageFlags createUsage);

/* Imported image: produced outside the graph (for example by the scene pass).
   `initialUsage` describes that external use; a read with initialUsage NONE and
   no writer is rejected for transients but tolerated for imports. */
int vrgwImportImage(VrgwGraph *g, const char *name,
                    WGPUTexture texture, WGPUTextureView view,
                    uint32_t width, uint32_t height,
                    WGPUTextureFormat format, VrgwUsage initialUsage);

int  vrgwAddPass(VrgwGraph *g, const char *name);

bool vrgwPassReadImage(VrgwGraph *g, int pass, int image, VrgwUsage usage);
bool vrgwPassWriteImage(VrgwGraph *g, int pass, int image, VrgwUsage usage);
bool vrgwPassReadWriteImage(VrgwGraph *g, int pass, int image, VrgwUsage usage);

/* Builds the pass order, the usage-transition plan and the transient textures. */
bool vrgwCompile(VrgwGraph *g, WGPUDevice device);

/* Calls `callback` for every pass in dependency order. */
bool vrgwExecute(VrgwGraph *g, WGPUCommandEncoder enc,
                 VrgwPassCallback callback, void *user);

int  vrgwGetImageCount(const VrgwGraph *g);
int  vrgwGetPassCount(const VrgwGraph *g);
int  vrgwGetPassOrder(const VrgwGraph *g, int index);
int  vrgwGetTransitionCount(const VrgwGraph *g);
const VrgwTransition *vrgwGetTransition(const VrgwGraph *g, int index);

WGPUTexture     vrgwGetTexture(const VrgwGraph *g, int image);
WGPUTextureView vrgwGetView(const VrgwGraph *g, int image);

const char *vrgwGetError(const VrgwGraph *g);

#ifdef __cplusplus
}
#endif

#endif /* VLCRLIB_VRENDER_GRAPH_WEBGPU_H */