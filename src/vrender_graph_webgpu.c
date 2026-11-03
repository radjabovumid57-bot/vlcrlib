#include "vrender_graph_webgpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct VrgwImage {
    const char       *name;
    uint32_t          width;
    uint32_t          height;
    WGPUTextureFormat format;
    WGPUTextureUsageFlags createUsage;
    WGPUTexture       texture;
    WGPUTextureView   view;
    int               imported;
    VrgwUsage         initialUsage;
} VrgwImage;

typedef struct VrgwPass {
    const char *name;
} VrgwPass;

typedef struct VrgwUse {
    int       image;
    int       pass;
    VrgwUsage usage;
    int       write;
} VrgwUse;

struct VrgwGraph {
    VrgwImage images[VRGW_MAX_IMAGES];
    int       imageCount;

    VrgwPass  passes[VRGW_MAX_PASSES];
    int       passCount;

    VrgwUse   uses[VRGW_MAX_USES];
    int       useCount;

    int       order[VRGW_MAX_PASSES];
    int       orderCount;

    VrgwTransition transitions[VRGW_MAX_USES];
    int            transitionCount;

    bool compiled;
    char error[256];
};

static void vrgwSetError(VrgwGraph *g, const char *text) {
    if (!g) return;
    snprintf(g->error, sizeof(g->error), "%s", text ? text : "error");
}

static bool vrgwValidImage(const VrgwGraph *g, int image) {
    return g && image >= 0 && image < g->imageCount;
}

static bool vrgwValidPass(const VrgwGraph *g, int pass) {
    return g && pass >= 0 && pass < g->passCount;
}

VrgwGraph *vrgwCreate(void) {
    return (VrgwGraph *)calloc(1, sizeof(VrgwGraph));
}

void vrgwDestroy(VrgwGraph *g) {
    if (!g) return;
    for (int i = 0; i < g->imageCount; ++i) {
        if (g->images[i].imported) continue;
        if (g->images[i].view) wgpuTextureViewRelease(g->images[i].view);
        if (g->images[i].texture) wgpuTextureRelease(g->images[i].texture);
    }
    free(g);
}

int vrgwAddImage(VrgwGraph *g, const char *name,
                 uint32_t width, uint32_t height,
                 WGPUTextureFormat format,
                 WGPUTextureUsageFlags createUsage) {
    if (!g || width == 0 || height == 0) return -1;
    if (g->compiled || g->imageCount >= VRGW_MAX_IMAGES) return -1;
    VrgwImage *img = &g->images[g->imageCount];
    memset(img, 0, sizeof(*img));
    img->name = name ? name : "image";
    img->width = width;
    img->height = height;
    img->format = format;
    img->createUsage = createUsage;
    img->initialUsage = VRGW_USAGE_NONE;
    return g->imageCount++;
}

int vrgwImportImage(VrgwGraph *g, const char *name,
                    WGPUTexture texture, WGPUTextureView view,
                    uint32_t width, uint32_t height,
                    WGPUTextureFormat format, VrgwUsage initialUsage) {
    if (!g || !texture || !view) return -1;
    if (g->compiled || g->imageCount >= VRGW_MAX_IMAGES) return -1;
    VrgwImage *img = &g->images[g->imageCount];
    memset(img, 0, sizeof(*img));
    img->name = name ? name : "imported";
    img->width = width;
    img->height = height;
    img->format = format;
    img->texture = texture;
    img->view = view;
    img->imported = 1;
    img->initialUsage = initialUsage;
    return g->imageCount++;
}

int vrgwAddPass(VrgwGraph *g, const char *name) {
    if (!g || g->compiled || g->passCount >= VRGW_MAX_PASSES) return -1;
    g->passes[g->passCount].name = name ? name : "pass";
    return g->passCount++;
}

static bool vrgwAddUse(VrgwGraph *g, int pass, int image,
                       VrgwUsage usage, bool write) {
    if (!vrgwValidPass(g, pass) || !vrgwValidImage(g, image)) return false;
    if (g->compiled || g->useCount >= VRGW_MAX_USES) return false;
    if (usage == VRGW_USAGE_NONE) return false;
    VrgwUse *u = &g->uses[g->useCount++];
    u->pass = pass;
    u->image = image;
    u->usage = usage;
    u->write = write ? 1 : 0;
    return true;
}

bool vrgwPassReadImage(VrgwGraph *g, int pass, int image, VrgwUsage usage) {
    return vrgwAddUse(g, pass, image, usage, false);
}

bool vrgwPassWriteImage(VrgwGraph *g, int pass, int image, VrgwUsage usage) {
    return vrgwAddUse(g, pass, image, usage, true);
}

bool vrgwPassReadWriteImage(VrgwGraph *g, int pass, int image, VrgwUsage usage) {
    return vrgwPassReadImage(g, pass, image, usage) &&
           vrgwPassWriteImage(g, pass, image, usage);
}

static bool vrgwCreateTransients(VrgwGraph *g, WGPUDevice device) {
    for (int i = 0; i < g->imageCount; ++i) {
        VrgwImage *img = &g->images[i];
        if (img->imported) continue;
        WGPUTextureDescriptor t = {0};
        t.size.width = img->width;
        t.size.height = img->height;
        t.size.depthOrArrayLayers = 1;
        t.mipLevelCount = 1;
        t.sampleCount = 1;
        t.dimension = WGPUTextureDimension_2D;
        t.format = img->format;
        t.usage = img->createUsage;
        img->texture = wgpuDeviceCreateTexture(device, &t);
        if (!img->texture) {
            vrgwSetError(g, "transient texture creation failed");
            return false;
        }
        img->view = wgpuTextureCreateView(img->texture, NULL);
        if (!img->view) {
            vrgwSetError(g, "transient view creation failed");
            return false;
        }
    }
    return true;
}

bool vrgwCompile(VrgwGraph *g, WGPUDevice device) {
    if (!g || !device) return false;
    if (g->compiled) return true;
    g->error[0] = '\0';
    g->orderCount = 0;
    g->transitionCount = 0;

    if (g->passCount == 0) {
        g->compiled = true;
        return true;
    }

    unsigned char edges[VRGW_MAX_PASSES][VRGW_MAX_PASSES];
    memset(edges, 0, sizeof(edges));

    int lastWriter[VRGW_MAX_IMAGES];
    uint64_t readers[VRGW_MAX_IMAGES][1];
    for (int i = 0; i < VRGW_MAX_IMAGES; ++i) {
        lastWriter[i] = -1;
        readers[i][0] = 0;
    }

    for (int u = 0; u < g->useCount; ++u) {
        const VrgwUse *use = &g->uses[u];
        int img = use->image;
        int pass = use->pass;

        if (!use->write) {
            if (lastWriter[img] >= 0 && lastWriter[img] != pass)
                edges[lastWriter[img]][pass] = 1;
            else if (lastWriter[img] < 0 && !g->images[img].imported)
                vrgwSetError(g, "read before write on a transient image");
            if (lastWriter[img] < 0 && !g->images[img].imported)
                return false;
            readers[img][0] |= (uint64_t)1 << pass;
        } else {
            if (lastWriter[img] >= 0 && lastWriter[img] != pass)
                edges[lastWriter[img]][pass] = 1;
            for (int r = 0; r < g->passCount; ++r) {
                if (r == pass) continue;
                if (readers[img][0] & ((uint64_t)1 << r))
                    edges[r][pass] = 1;
            }
            readers[img][0] = 0;
            lastWriter[img] = pass;
        }
    }

    int indegree[VRGW_MAX_PASSES] = {0};
    for (int a = 0; a < g->passCount; ++a)
        for (int b = 0; b < g->passCount; ++b)
            if (edges[a][b]) indegree[b]++;

    bool emitted[VRGW_MAX_PASSES] = {false};
    while (g->orderCount < g->passCount) {
        int next = -1;
        for (int p = 0; p < g->passCount; ++p) {
            if (!emitted[p] && indegree[p] == 0) { next = p; break; }
        }
        if (next < 0) {
            vrgwSetError(g, "render graph cycle");
            return false;
        }
        emitted[next] = true;
        g->order[g->orderCount++] = next;
        for (int b = 0; b < g->passCount; ++b)
            if (edges[next][b]) indegree[b]--;
    }

    /* Usage-transition plan. WebGPU synchronises passes implicitly, so this is
       a validation/diagnostic record rather than barrier emission. */
    VrgwUsage lastUsage[VRGW_MAX_IMAGES];
    VrgwUsage lastAccess[VRGW_MAX_IMAGES];
    for (int i = 0; i < g->imageCount; ++i) {
        lastUsage[i] = g->images[i].initialUsage;
        lastAccess[i] = VRGW_USAGE_NONE;
    }
    for (int oi = 0; oi < g->orderCount; ++oi) {
        int pass = g->order[oi];
        for (int u = 0; u < g->useCount; ++u) {
            const VrgwUse *use = &g->uses[u];
            if (use->pass != pass) continue;
            int img = use->image;
            bool hazard = use->write || (lastUsage[img] != use->usage) ||
                          (lastAccess[img] & VRGW_USAGE_COLOR_ATTACHMENT) ||
                          (lastAccess[img] & VRGW_USAGE_DEPTH_ATTACHMENT) ||
                          (lastAccess[img] & VRGW_USAGE_COPY_DST) ||
                          (use->usage & VRGW_USAGE_COLOR_ATTACHMENT) ||
                          (use->usage & VRGW_USAGE_DEPTH_ATTACHMENT) ||
                          (use->usage & VRGW_USAGE_COPY_DST);
            if (hazard && g->transitionCount < VRGW_MAX_USES) {
                VrgwTransition *tr = &g->transitions[g->transitionCount++];
                tr->image = img;
                tr->pass = pass;
                tr->oldUsage = lastUsage[img];
                tr->newUsage = use->usage;
            }
            lastUsage[img] = use->usage;
            lastAccess[img] = use->usage;
        }
    }

    if (!vrgwCreateTransients(g, device)) {
        for (int i = 0; i < g->imageCount; ++i) {
            VrgwImage *img = &g->images[i];
            if (img->imported) continue;
            if (img->view) wgpuTextureViewRelease(img->view);
            if (img->texture) wgpuTextureRelease(img->texture);
            img->view = NULL;
            img->texture = NULL;
        }
        return false;
    }

    g->compiled = true;
    return true;
}

bool vrgwExecute(VrgwGraph *g, WGPUCommandEncoder enc,
                 VrgwPassCallback callback, void *user) {
    if (!g || !enc || !g->compiled) return false;
    for (int i = 0; i < g->orderCount; ++i)
        if (callback) callback(enc, g->order[i], user);
    return true;
}

int vrgwGetImageCount(const VrgwGraph *g) { return g ? g->imageCount : 0; }
int vrgwGetPassCount(const VrgwGraph *g) { return g ? g->passCount : 0; }

int vrgwGetPassOrder(const VrgwGraph *g, int index) {
    if (!g || index < 0 || index >= g->orderCount) return -1;
    return g->order[index];
}

int vrgwGetTransitionCount(const VrgwGraph *g) {
    return g ? g->transitionCount : 0;
}

const VrgwTransition *vrgwGetTransition(const VrgwGraph *g, int index) {
    if (!g || index < 0 || index >= g->transitionCount) return NULL;
    return &g->transitions[index];
}

WGPUTexture vrgwGetTexture(const VrgwGraph *g, int image) {
    if (!vrgwValidImage(g, image) || !g->compiled) return NULL;
    return g->images[image].texture;
}

WGPUTextureView vrgwGetView(const VrgwGraph *g, int image) {
    if (!vrgwValidImage(g, image) || !g->compiled) return NULL;
    return g->images[image].view;
}

const char *vrgwGetError(const VrgwGraph *g) {
    return (g && g->error[0]) ? g->error : NULL;
}