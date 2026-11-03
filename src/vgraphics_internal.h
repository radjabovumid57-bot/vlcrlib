/**
 * vgraphics_internal.h — architecture notes + shared Vulkan state (Sprint D/E)
 *
 * ---------------------------------------------------------------------------
 * Module map
 * ---------------------------------------------------------------------------
 *  vgraphics_ubo.h       GPU UBO layouts + descriptor binding IDs
 *  vgraphics_internal.h  VkState, frame phases, shared helpers
 *  vgraphics.h           public raylib-like API
 *  vgraphics.c           device, pipelines, draw
 *  vgraphics_shadow.c    shadow matrices
 *  vgraphics_material.c  materials + UBO + descriptors
 *  vgraphics_pipeline.c  shaders + pipelines + DSL
 *  vgraphics_device.c    instance / device / swapchain / sync
 *  vgltf.cpp             glTF/GLB import
 *  vibl.c                IBL helpers
 *
 * Frame phases: IDLE → ACQUIRE → SHADOW → SCENE → PRESENT → IDLE
 *
 * Descriptor set0: see vgraphics_ubo.h VLCR_BIND_*
 */
#ifndef VLCRLIB_VGRAPHICS_INTERNAL_H
#define VLCRLIB_VGRAPHICS_INTERNAL_H

#include "vgraphics_ubo.h"
#include "vcore.h"
#include "vmath.h"
#include "vgraphics.h"  /* Texture2D, Color, Vector3 */
#include <vulkan/vulkan.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MAX_FRAMES_IN_FLIGHT
#define MAX_FRAMES_IN_FLIGHT 2
#endif

#ifndef VLCRLIB_MAX_MATERIALS
#define VLCRLIB_MAX_MATERIALS 128
#define VLCRLIB_MAX_BONES       256
#define VLCRLIB_MAX_SKMODELS    32
#endif
#ifndef VLCRLIB_MAX_2D_DESCRIPTORS
#define VLCRLIB_MAX_2D_DESCRIPTORS 512
#endif

typedef struct VkState {
    VkInstance        instance;
    VkSurfaceKHR      surface;
    VkPhysicalDevice  phys;
    VkDevice          device;
    uint32_t          queueFamily;
    VkQueue           gfxQueue;
    VkQueue           presentQueue;

    /* Optional Vulkan hardware ray-query path. The normal renderer must keep
     * working on devices that expose only rasterization. */
    bool              rayQuerySupported;
    VkDebugUtilsMessengerEXT debugMessenger;
    int               validationErrors;

    VkSwapchainKHR    swapchain;
    VkFormat          swapFormat;
    VkQueryPool       gpuTimerPool;
    float             gpuTimerPeriod;
    VkExtent2D        swapExtent;
    uint32_t          imageCount;
    VkImage          *images;
    VkImageView      *views;
    VkFramebuffer    *framebuffers;

    VkRenderPass      renderPass;
    VkCommandPool     cmdPool;
    VkCommandBuffer  *cmdBuffers;

    VkSemaphore      *semImgAvailable;
    VkSemaphore      *semRenderDone;
    VkFence          *inFlight;
    VkFence           *imagesInFlight;

    uint32_t          currentFrame;
    uint32_t          currentImage;

    VkClearValue      clearColor;
    Matrix            view;
    Matrix            proj;

    bool              framebufferResized;

    /* depth */
    VkFormat          depthFormat;
    VkImage           depthImage;
    VkDeviceMemory    depthMem;
    VkImageView       depthView;

    /* 2D pipeline */
    VkPipeline              pipeline2D;
    VkPipelineLayout        layout2D;
    VkDescriptorSetLayout   dsl2D;
    VkDescriptorPool        descPool2D;
    VkDescriptorSet         descSets2D[VLCRLIB_MAX_2D_DESCRIPTORS];
    uint32_t                descSet2DIndex;
    VkSampler               sampler2D;
    VkBuffer                vbo2D;
    VkDeviceMemory          vbo2DMem;
    VkDeviceSize            vbo2DCapacity;
    VkDeviceSize            vbo2DOffset;
    VkShaderModule          vert2D;
    VkShaderModule          frag2D;

    /* Fullscreen analytic ray-tracing demo pipeline (Vulkan fragment stage). */
    VkPipeline              rayTraceDemoPipeline;
    VkPipelineLayout        rayTraceDemoLayout;
    VkShaderModule          rayTraceDemoVert;
    VkShaderModule          rayTraceDemoFrag;

    /* Hardware Ray Query scene used by the ray-tracing example when available. */
    VkAccelerationStructureKHR rayDemoBlas, rayDemoTlas;
    VkBuffer                rayDemoBlasBuffer, rayDemoTlasBuffer;
    VkDeviceMemory          rayDemoBlasMemory, rayDemoTlasMemory;
    VkBuffer                rayDemoVertexBuffer, rayDemoMaterialBuffer;
    VkDeviceMemory          rayDemoVertexMemory, rayDemoMaterialMemory;
    VkDescriptorSetLayout   rayDemoDescriptorLayout;
    VkDescriptorPool        rayDemoDescriptorPool;
    VkDescriptorSet         rayDemoDescriptorSet;
    bool                    rayQueryDemoReady;

    int                     viewport2DW;
    int                     viewport2DH;

    /* 3D pipeline (unlit, per-object base color) */
    VkPipeline              pipeline3D;
    VkPipeline              pipeline3DLine;
    VkPipelineLayout        layout3D;
    VkDescriptorSetLayout   dsl3D;
    VkDescriptorPool        descPool3D;
    VkDescriptorSet         descSet3D;
    VkBuffer                ubo3D;
    VkDeviceMemory          ubo3DMem;
    void                   *ubo3DMapped;
    uint32_t                ubo3DStride;
    uint32_t                ubo3DSlot;
    uint32_t                ubo3DMaxSlots;

    Matrix                  view3D;
    Matrix                  proj3D;

    /* PBR pipeline (back-face cull + doubleSided / no-cull variant) */
    VkPipeline              pipelinePBR;
    VkPipeline              pipelinePBRNoCull;
    VkPipeline              pipelinePBRBlend;
    VkPipeline              pipelinePBRBlendNoCull;
    VkPipelineLayout        layoutPBR;
    VkDescriptorSetLayout   dslPBR;
    VkDescriptorPool        descPoolPBR;
    VkBuffer                uboPBR;
    VkDeviceMemory          uboPBRMem;
    void                   *uboPBRMapped;
    uint32_t                uboPBRStride;
    uint32_t                uboPBRSlot;
    uint32_t                uboPBRMaxSlots;

    /* Optional explicit light array (SSBO binding 16). */
    VkBuffer                lightBuffer;
    VkDeviceMemory          lightBufferMem;
    void                   *lightBufferMapped;
    VkDeviceSize            lightBufferSize;
    int                     lightArrayCount;

    Vector3                 cameraPos;
    bool                    lightDirectionalEnabled;
    Vector3                 lightDirectionalDirection;
    Color                   lightDirectionalColor;
    float                   lightDirectionalIntensity;
    bool                    lightPointEnabled;
    Vector3                 lightPointPosition;
    Color                   lightPointColor;
    float                   lightPointIntensity;
    float                   lightPointRange;
    bool                    lightSpotEnabled;
    Vector3                 lightSpotPosition;
    Vector3                 lightSpotDirection;
    Color                   lightSpotColor;
    float                   lightSpotIntensity;
    float                   lightSpotRange;
    float                   lightSpotInnerCone;
    float                   lightSpotOuterCone;
    bool                    lightAmbientEnabled;
    Color                   lightAmbientColor;
    float                   lightAmbientIntensity;
    bool                    lightAreaEnabled;
    Vector3                 lightAreaPosition;
    Vector3                 lightAreaNormal;
    float                   lightAreaWidth;
    float                   lightAreaHeight;
    Color                   lightAreaColor;
    float                   lightAreaIntensity;
    bool                    fogEnabled;
    Color                   fogColor;
    float                   fogStart;
    float                   fogEnd;
    float                   fogDensity;
    bool                    shadowsEnabled;
    int                     shadowMapSize;
    float                   shadowBias;
    float                   shadowStrength;
    float                   shadowSoftness;
    Matrix                  shadowLightVP;
    Matrix                  shadowCascadeVP[4];
    float                   shadowCascadeSplits[4];
    int                     shadowCascadeCount;
    VkImage                 shadowImage;
    VkDeviceMemory          shadowMem;
    VkImageView             shadowView;
    VkSampler               shadowSampler;
    VkFramebuffer           shadowFB;
    VkRenderPass            shadowPass;
    VkPipeline              shadowPipeline;
    VkPipelineLayout        shadowLayout;
    bool                    shadowPassActive;
    bool                    shadowResourcesReady;
    struct VlcrShadowCaster {
        unsigned int modelId;
        unsigned int meshId;
        unsigned int skinnedId;
        unsigned int meshGeneration;
        Vector3 position, rotation, scale;
        Matrix transform;
        Matrix prevTransform;
    } shadowCasters[64];
    int                     shadowCasterCount;
    struct VlcrShadowCaster shadowCastersPrev[64];
    int                     shadowCasterCountPrev;
    /* Spot shadow (2D depth) */
    bool                    spotShadowsEnabled;
    int                     spotShadowMapSize;
    float                   spotShadowBias;
    float                   spotShadowStrength;
    float                   spotShadowSoftness;
    Matrix                  spotShadowLightVP;
    VkImage                 spotShadowImage;
    VkDeviceMemory          spotShadowMem;
    VkImageView             spotShadowView;
    VkSampler               spotShadowSampler;
    VkFramebuffer           spotShadowFB;
    bool                    spotShadowReady;
    /* Point shadow (cube linear depth) */
    bool                    pointShadowsEnabled;
    int                     pointShadowMapSize;
    float                   pointShadowBias;
    float                   pointShadowStrength;
    float                   pointShadowFar;
    VkImage                 pointShadowImage;
    VkDeviceMemory          pointShadowMem;
    VkImageView             pointShadowView; /* cube */
    VkImageView             pointShadowFaceViews[6];
    VkImage                 pointShadowDepthImage;
    VkDeviceMemory          pointShadowDepthMem;
    VkImageView             pointShadowDepthFaceViews[6];
    VkFramebuffer           pointShadowFB[6];
    VkSampler               pointShadowSampler;
    VkRenderPass            pointShadowPass;
    VkPipeline              pointShadowPipeline;
    VkPipelineLayout        pointShadowLayout;
    bool                    pointShadowReady;
    float                   iblIntensity;
    float                   exposure;       /* Sprint C: tonemap pre-scale */
    int                     ssaoEnabled;
    float                   ssaoRadius;
    float                   ssaoIntensity;
    /* Half-res SSAO (Sprint F) */
    VkImage                 ssaoImage; VkDeviceMemory ssaoMem; VkImageView ssaoView;
    VkSampler               ssaoSampler;
    VkFramebuffer           ssaoFB;
    VkRenderPass            ssaoPass;
    VkPipeline              ssaoPipeline;
    VkPipelineLayout        ssaoLayout;
    VkDescriptorSet         ssaoDescSet;
    VkDescriptorSetLayout   ssaoDSL;
    VkDescriptorPool        ssaoPool;
    bool                    ssaoReady;
    int                     ssaoWidth, ssaoHeight;
    /* Depth prepass for SSAO */
    VkImage                 ssaoDepthImage; VkDeviceMemory ssaoDepthMem; VkImageView ssaoDepthView;
    VkRenderPass            ssaoDepthPass;
    VkFramebuffer           ssaoDepthFB;
    /* Half-res Bloom */
    VkImage                 bloomImage; VkDeviceMemory bloomMem; VkImageView bloomView;
    VkImage                 bloomImage2; VkDeviceMemory bloomMem2; VkImageView bloomView2;
    VkFramebuffer           bloomFB2;
    VkSampler               bloomSampler;
    VkFramebuffer           bloomFB;
    VkRenderPass            bloomPass;
    VkPipeline              bloomPipeline;
    VkPipelineLayout        bloomLayout;
    VkDescriptorSet         bloomDescSet;
    VkDescriptorSet         bloomDescSetH;  /* blur horizontal: bloomView  */
    VkDescriptorSet         bloomDescSetV;  /* blur vertical:   bloomView2 */
    VkDescriptorSetLayout   bloomDSL;
    VkDescriptorPool        bloomPool;
    bool                    bloomReady;
    int                     bloomWidth, bloomHeight;
    /* Offscreen scene color for post-process */
    VkImage                 sceneColorImage; VkDeviceMemory sceneColorMem; VkImageView sceneColorView;
    VkImage                 sceneDepthImage; VkDeviceMemory sceneDepthMem; VkImageView sceneDepthView;
    VkFramebuffer           sceneFB;
    VkRenderPass            scenePass;
    bool                    sceneReady;
    uint32_t                sceneWidth, sceneHeight;
    int                     postProcessEnabled; /* 1 = render to sceneFB */
    int                     fxaaEnabled;
    int                     taaEnabled;
    int                     taaReset;
    float                   taaFeedback;
    float                   taaJitterX, taaJitterY;
    int                     taaFrame;
    Matrix                  taaPrevVP;
    VkImage                 taaHistoryImage;
    VkDeviceMemory          taaHistoryMem;
    VkImageView             taaHistoryView;
    VkSampler               taaSampler;
    uint32_t                taaHistoryWidth, taaHistoryHeight;
    VkBuffer                taaUBO;
    VkDeviceMemory          taaUBOMem;
    void                   *taaUBOMapped;
    uint32_t                taaUBOStride;
    VkImage                 velocityImage;
    VkDeviceMemory          velocityMem;
    VkImageView             velocityView;
    VkRenderPass            velocityPass;
    VkFramebuffer           velocityFB;
    VkPipeline              velocityPipeline;
    VkPipelineLayout        velocityLayout;
    VkDescriptorSetLayout   velocityDSL;
    VkDescriptorPool        velocityPool;
    VkDescriptorSet         velocityDescSet;
    VkBuffer                velocityUBO;
    VkDeviceMemory          velocityUBOMem;
    void                   *velocityUBOMapped;
    uint32_t                velocityUBOStride;
    VkDescriptorSetLayout   velocitySkinDSL;
    VkPipelineLayout        velocitySkinLayout;
    VkPipeline              velocitySkinPipeline;
    VkDescriptorPool        velocitySkinPool;
    /* Transparent sort queue */
    struct { unsigned int modelId; Vector3 pos, rot, scale; Color tint; float dist; } transQueue[64];
    int                     transCount;
    int                     transFlushing;
    VkPipeline              compositePipeline;
    VkPipelineLayout        compositeLayout;
    VkDescriptorSet         compositeDescSet;
    VkDescriptorSetLayout   compositeDSL;
    VkDescriptorPool        compositePool;
    VkRenderPass            compositePass;
    bool                    compositeReady;
    float                   bloomThreshold;
    float                   bloomIntensity;
    int                     debugView;      /* Sprint B */
    int                     debugLog;
    int                     inFrame;
    int                     frameIndex;
    int                     framePhase; /* VlcrFramePhase */
    bool                    frameFailed;
    int                     lastFrameResult; /* VkResult value; keep Vulkan out of public headers */
    int                     matDescDirty[VLCRLIB_MAX_MATERIALS];
    bool                    iblLoaded;
    char                    iblSourcePath[512];
    int                     iblCubeSize;
    VkImage                 iblIrrImage; VkDeviceMemory iblIrrMem; VkImageView iblIrrView;
    VkImage                 iblPreImage; VkDeviceMemory iblPreMem; VkImageView iblPreView;
    VkImage                 iblBrdfImage; VkDeviceMemory iblBrdfMem; VkImageView iblBrdfView;
    /* Sharp source environment cubemap (mip 0) used as the sky background. */
    VkImage                 iblEnvImage; VkDeviceMemory iblEnvMem; VkImageView iblEnvView;
    VkSampler               iblCubeSampler; VkSampler iblLutSampler;

    /* Environment background (fullscreen skybox) */
    VkPipeline              skyboxPipeline;
    VkPipelineLayout        skyboxLayout;
    VkDescriptorSetLayout   skyboxDSL;
    VkDescriptorPool        skyboxPool;
    VkDescriptorSet         skyboxDescSet;
    bool                    skyboxReady;
    int                     skyboxEnabled;
    /* Procedural atmosphere (skybox.frag) */
    int                     atmosphereEnabled;
    float                   atmosphereTime;
    float                   cloudCoverage;
    float                   haze;
    float                   starIntensity;
    Vector3                 sunDirection;
    float                   sunIntensity;
    Vector3                 moonDirection;
    float                   moonIntensity;
    float                   sunAngularRadius;
    float                   moonAngularRadius;

    /* Skinning */
    VkPipeline              pipelineSkin;
    VkPipeline              pipelineSkinNoCull;
    VkPipeline              pipelineSkinBlend;
    VkPipeline              pipelineSkinBlendNoCull;
    VkPipelineLayout        layoutSkin;
    VkDescriptorSetLayout   dslSkin;
    VkDescriptorPool        descPoolSkin;
    VkBuffer                boneBuffer;
    VkDeviceMemory          boneBufferMem;
    void                   *boneBufferMapped;
    uint32_t                boneBufferSize;

    /* Offscreen RT */
    VkRenderPass            renderPassRT;
    VkCommandBuffer         currentCmd;
    VkCommandBuffer         swapchainCmd;
    VkFence                 offscreenFence;
    bool                    inRT;

    Texture2D               whiteTexture;
} VkState;


/* Global Vulkan context — defined in vgraphics.c */

#ifndef VLCRLIB_MAX_TEXTURES
#define VLCRLIB_MAX_TEXTURES 256
#endif

typedef struct TextureEntry {
    VkImage        img;
    VkDeviceMemory mem;
    VkImageView    view;
    VkSampler      samp;
    int            width, height;
    int            ownSamp;   /* 1 = per-texture sampler; destroy on unload */
    unsigned int   generation;
    uint32_t      *cpuPixels; /* packed 0xAARRGGBB sidecar for the CPU paths */
} TextureEntry;

typedef struct PBRMaterialEntry {
    VkDescriptorSet descSet;
    VkDescriptorSet skinDescSet;
    Texture2D       albedo, normal, mr, ao, emission;
    Texture2D       specular, clearcoat;
    unsigned int    generation;
} PBRMaterialEntry;

extern TextureEntry g_textureTable[VLCRLIB_MAX_TEXTURES];
extern int g_textureCount;
extern PBRMaterialEntry g_matTable[VLCRLIB_MAX_MATERIALS];
extern int g_matCount;

void writeUBOPBR(Matrix mvp, Matrix model, const Material *mat);
void updateMaterialDescriptorSet(PBRMaterialEntry *m);
void ibl_refresh_all_material_descriptors(void);
void material_release_completed(uint32_t frameSlot, bool waitAll);
bool material_handle_valid(const Material *material);
void material_invalidate_all(void);
void material_detach_texture(Texture2D texture);


extern VkState VK;
int deviceTypeScore(VkPhysicalDeviceType t);
const char *deviceTypeName(VkPhysicalDeviceType t);
bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer *buf, VkDeviceMemory *mem);
bool createRayTraceDemoPipeline(void);
bool createRayQueryDemoResources(void);
void destroyRayQueryDemoResources(void);

/* Shared helpers implemented in vgraphics.c (used by modules) */
uint32_t vlcr_findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props);
bool     vlcr_createImage(uint32_t w, uint32_t h, VkFormat fmt,
                          VkImageTiling tiling, VkImageUsageFlags usage,
                          VkMemoryPropertyFlags props,
                          VkImage *outImg, VkDeviceMemory *outMem);
void     vlcr_matrixToGpu(float out[16], Matrix m);
VkShaderModule createShaderModule(const char *path); /* vgraphics_pipeline.c */
void vgraphics_DrawSkybox(void);

#ifdef __cplusplus
}
#endif

#endif /* VLCRLIB_VGRAPHICS_INTERNAL_H */
