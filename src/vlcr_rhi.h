// ============================================================================
// vlcr_rhi.h — Render Hardware Interface (RHI) для vlcrlib
// ----------------------------------------------------------------------------
// Цель: AAA-уровень абстракции. НИ ОДИН Vk*/SDL_* тип не поднимается выше
// этого слоя. Всё состояние GPU — opaque handles (id в таблицах), как в
// The-Forge / Diligent / bgfx.
//
// Фичи, заложенные в дизайн:
//   * bindless textures (descriptor indexing) — 1 layout на всё
//   * dynamic rendering — никаких VkRenderPass/VkFramebuffer объектов
//   * simplified barriers — state-machine модель ресурсов (как DX12)
//   * frames-in-flight = 3, кадр строится в один проход, submit в конце
//   * staging ring — никаких vkQueueWaitIdle при загрузке
//   * поддержка бэкендов: VULKAN (сейчас), DX12/METAL (потом)
// ============================================================================
#ifndef VLCR_RHI_H
#define VLCR_RHI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- Handles: 32-bit id + generation, 0 == INVALID --------------------------
typedef uint32_t RHIBuffer;      // 0 == invalid
typedef uint32_t RHITexture;
typedef uint32_t RHISampler;
typedef uint32_t RHIShader;
typedef uint32_t RHIPipeline;
typedef uint32_t RHIDescriptorSet;
typedef uint32_t RHICommandList; // один на кадр на тред
typedef uint32_t RHIFence;

#define RHI_INVALID 0u

// ---- Enums ------------------------------------------------------------------
typedef enum RHIFormat {
    RHI_FORMAT_UNDEFINED = 0,
    RHI_FORMAT_RGBA8_UNORM,  RHI_FORMAT_BGRA8_UNORM,
    RHI_FORMAT_RGBA8_SRGB,
    RHI_FORMAT_D32_FLOAT,    RHI_FORMAT_D24S8,
    RHI_FORMAT_RGBA16_FLOAT, RHI_FORMAT_RGBA32_FLOAT,
    RHI_FORMAT_RG32_FLOAT,   RHI_FORMAT_RGB32_FLOAT,
    RHI_FORMAT_R32_FLOAT,    RHI_FORMAT_R32_UINT,
    RHI_FORMAT_R16_UNORM,    RHI_FORMAT_R8_UNORM,
} RHIFormat;

typedef enum RHIResourceState {
    // Упрощённая модель: движок говорит "как будет ИСПОЛЬЗОВАТЬСЯ",
    // бэкенд сам вычисляет barriers. Никаких srcAccess/dstStage вверху.
    RHI_STATE_UNDEFINED = 0,   // может быть потеряно
    RHI_STATE_PRESENT,         // swapchain image
    RHI_STATE_RENDER_TARGET,   // color attachment write
    RHI_STATE_DEPTH_WRITE,
    RHI_STATE_DEPTH_READ,      // depth test, no write
    RHI_STATE_SHADER_RESOURCE, // sampled в шейдере
    RHI_STATE_UNORDERED_ACCESS,// RW-текстура / SSBO / compute
    RHI_STATE_COPY_DST,
    RHI_STATE_COPY_SRC,
    RHI_STATE_VERTEX_BUFFER,
    RHI_STATE_INDEX_BUFFER,
    RHI_STATE_INDIRECT_ARGS,   // indirect draw buffer
} RHIResourceState;

typedef enum RHIBufferUsage {
    RHI_BUF_VERTEX   = 1 << 0,
    RHI_BUF_INDEX    = 1 << 1,
    RHI_BUF_UNIFORM  = 1 << 2,
    RHI_BUF_STORAGE  = 1 << 3,  // SSBO / RW
    RHI_BUF_INDIRECT = 1 << 4,
    RHI_BUF_TRANSFER = 1 << 5,  // staging / copy
} RHIBufferUsage;

typedef enum RHITextureUsage {
    RHI_TEX_SAMPLED  = 1 << 0,
    RHI_TEX_COLOR_RT = 1 << 1,
    RHI_TEX_DEPTH_RT = 1 << 2,
    RHI_TEX_STORAGE  = 1 << 3,  // UAV
    RHI_TEX_TRANSFER = 1 << 4,
} RHITextureUsage;

typedef enum RHIQueueType { RHI_QUEUE_GRAPHICS, RHI_QUEUE_COMPUTE, RHI_QUEUE_TRANSFER } RHIQueueType;

typedef enum RHIShaderStage { RHI_STAGE_VS, RHI_STAGE_FS, RHI_STAGE_CS } RHIShaderStage;

typedef enum RHICullMode { RHI_CULL_NONE, RHI_CULL_BACK, RHI_CULL_FRONT } RHICullMode;
typedef enum RHIFillMode { RHI_FILL_SOLID, RHI_FILL_WIREFRAME } RHIFillMode;
typedef enum RHIBlendMode { RHI_BLEND_OPAQUE, RHI_BLEND_ALPHA, RHI_BLEND_ADDITIVE, RHI_BLEND_PREMULTIPLIED } RHIBlendMode;
typedef enum RHICompareOp { RHI_CMP_NEVER, RHI_CMP_LESS, RHI_CMP_LEQUAL, RHI_CMP_EQUAL, RHI_CMP_ALWAYS } RHICompareOp;
typedef enum RHIPrimitive { RHI_PRIM_TRIANGLES, RHI_PRIM_LINES, RHI_PRIM_TRIANGLE_STRIP } RHIPrimitive;
typedef enum RHIFilter { RHI_FILTER_NEAREST, RHI_FILTER_LINEAR } RHIFilter;
typedef enum RHIAddressMode { RHI_ADDR_WRAP, RHI_ADDR_CLAMP, RHI_ADDR_MIRROR } RHIAddressMode;

// ---- Creation structs --------------------------------------------------------
typedef struct RHIAllocDesc { // через VMA в бэкенде
    const char *name;         // для RenderDoc / PIX
} RHIAllocDesc;

typedef struct RHIBufferDesc {
    uint64_t size;
    RHIBufferUsage usage;
    bool     hostVisible;     // частый CPU write (UBO) -> persistent mapped
    bool     hostDynamic;     // ring-buffer семантика (per-frame offset)
    RHIAllocDesc debug;
} RHIBufferDesc;

typedef struct RHITextureDesc {
    uint32_t width, height, depth;   // depth=1 (или layers для array)
    RHIFormat format;
    uint32_t  mipLevels, arrayLayers;
    uint32_t  samples;               // 1, 2, 4, 8 — MSAA
    uint32_t  usage;                 // bitmask RHITextureUsage
    bool      cube;
    RHIAllocDesc debug;
} RHITextureDesc;

typedef struct RHISamplerDesc {
    RHIFilter      minFilter, magFilter;
    RHIFilter      mipFilter;
    RHIAddressMode addressU, addressV, addressW;
    bool           compareEnable;    // PCF shadow sampling
    RHICompareOp   compareOp;
    float          maxAnisotropy;    // 0 = off
} RHISamplerDesc;

typedef struct RHIShaderDesc {
    const uint32_t *code;            // SPIR-V (или DXIL — по бэкенду)
    size_t          codeSize;
    const char     *entryPoint;      // "main"
    RHIShaderStage  stage;
} RHIShaderDesc;

typedef struct RHIVertexAttrib {
    uint32_t location;
    RHIFormat format;                // float2/3/4, unorm8x4...
    uint32_t offset;
} RHIVertexAttrib;

// Root signature / pipeline layout — декларативно:
typedef enum RHIBindingType { RHI_BIND_UNIFORM, RHI_BIND_STORAGE, RHI_BIND_SAMPLED_TEX, RHI_BIND_STORAGE_TEX, RHI_BIND_SAMPLER } RHIBindingType;

typedef struct RHIBindPoint {
    uint32_t        set;             // 0 = per-frame, 1 = per-pass, 2 = per-draw
    uint32_t        binding;
    RHIBindingType  type;
    uint32_t        count;           // 1, или RHI_BINDLESS_COUNT
    RHIShaderStage  stages;          // bitmask (у нас enum, значит stage_flags int)
} RHIBindPoint;

#define RHI_BINDLESS_COUNT 0xFFFFFFFFu

typedef struct RHIPipelineDesc {
    RHIShader vs, fs;                // 0 для compute
    RHIShader cs;                    // compute pipeline (vs/fs = 0)
    // Vertex layout
    uint32_t             stride;
    uint32_t             attribCount;
    const RHIVertexAttrib *attribs;
    // Raster
    RHIPrimitive prim;
    RHICullMode  cull;
    RHIFillMode  fill;
    // Depth
    bool        depthTest, depthWrite;
    RHICompareOp depthCompare;
    // Blend
    RHIBlendMode blend;
    bool         blendEnable;
    // Layout
    const RHIBindPoint *bindPoints;
    uint32_t            bindPointCount;
    RHIFormat           colorFormat;  // обязательно: dynamic rendering
    RHIFormat           depthFormat;  // или UNDEFINED
    uint32_t            samples;      // MSAA
    RHIAllocDesc        debug;
} RHIPipelineDesc;

// ---- Swapchain / device ------------------------------------------------------
typedef struct RHIDeviceDesc {
    void        *window;             // SDL_Window* — сюда, и никуда выше
    uint32_t     framesInFlight;     // обычно 3
    bool         enableValidation;
    bool         enableBindless;     // descriptor indexing
    bool         vsync;
} RHIDeviceDesc;

typedef struct RHISwapchainInfo {
    uint32_t width, height;
    RHIFormat format;                // обычно BGRA8_SRGB
    uint32_t currentImageIndex;      // валидно после RHI_BeginFrame
    RHITexture backbuffer;           // текущий image как handle
} RHISwapchainInfo;

// ---- Device lifecycle ---------------------------------------------------------
bool     RHI_Init(const RHIDeviceDesc *desc);
void     RHI_Shutdown(void);
void     RHI_WaitIdle(void);                       // только на загрузке уровня!
bool     RHI_Resize(uint32_t w, uint32_t h);
RHISwapchainInfo RHI_GetSwapchainInfo(void);

// ---- Frames -------------------------------------------------------------------
// Модель кадра: BeginFrame -> строим cmd list(s) -> EndFrame(submit) -> Present.
// Внутри: fences/semaphores per frame, acquire, resource state tracking.
RHICommandList RHI_BeginFrame(void);               // cmd list текущего кадра
void     RHI_EndFrame(void);                       // submit всех очередей + present
float    RHI_GetFrameGpuTimeMs(void);              // timestamp queries
uint32_t RHI_GetCurrentFrameIndex(void);           // 0..framesInFlight-1
RHIFence RHI_CreateFence(void);
void     RHI_WaitFence(RHIFence f, uint64_t value);
void     RHI_SignalFence(RHICommandList cmd, RHIFence f, uint64_t value);

// ---- Resource creation/destruction (потокобезопасно, т.к. deferred) ----------
RHIBuffer  RHI_CreateBuffer(const RHIBufferDesc *desc, const void *initialData);
RHITexture RHI_CreateTexture(const RHITextureDesc *desc, const void *initialData);
RHISampler RHI_CreateSampler(const RHISamplerDesc *desc);
RHIShader  RHI_CreateShader(const RHIShaderDesc *desc);
RHIPipeline RHI_CreatePipeline(const RHIPipelineDesc *desc);
void RHI_DestroyBuffer(RHIBuffer b);    // deferred: реально удалится через N кадров
void RHI_DestroyTexture(RHITexture t);
void RHI_DestroySampler(RHISampler s);
void RHI_DestroyShader(RHIShader s);
void RHI_DestroyPipeline(RHIPipeline p);
// Destruction маркеры для RAII-обёрток поверх
void RHI_RetainResource(uint32_t handle);  // ref-counting
void RHI_ReleaseResource(uint32_t handle);

// ---- Uploads: staging ring, БЕЗ WaitIdle --------------------------------------
// Возвращает offset в буфере после завершения текущего кадра.
// Для текстур: бэкенд сам делает layout transitions + мипмап-генерацию.
bool RHI_WriteBuffer(RHIBuffer dst, uint64_t dstOffset, const void *data, uint64_t size);
bool RHI_WriteTexture(RHITexture dst, const void *data, uint32_t mip, uint32_t layer);

// ---- Bindless -----------------------------------------------------------------
// Одна глобальная таблица. Движок резервирует диапазон, текстуры добавляются
// как handle -> GPU-visible id. Bindless set биндится один раз на кадр.
uint32_t RHI_BindlessAllocTexture(RHITexture t);    // returns texId (index)
void     RHI_BindlessFreeTexture(uint32_t texId);
RHISampler RHI_GetStaticSampler(RHISamplerDesc *desc); // 4-5 статических семплеров

// ---- Command list encoding ------------------------------------------------------
void RHI_CmdBegin(RHICommandList cmd);              // обычно уже сделано в BeginFrame
void RHI_CmdEnd(RHICommandList cmd);

// Переход состояния — ЯВНЫЙ только для межпроходных зависимостей.
// Внутри прохода барьеры расставляет frame graph.
void RHI_CmdBarrier(RHICommandList cmd, RHITexture t, RHIResourceState before, RHIResourceState after, uint32_t mip, uint32_t layer);
void RHI_CmdBarrierBuffer(RHICommandList cmd, RHIBuffer b, RHIResourceState before, RHIResourceState after);

typedef struct RHIColorClear { float r,g,b,a; } RHIColorClear;
typedef struct RHIDepthClear  { float depth; uint32_t stencil; } RHIDepthClear;

// Begin/End rendering — аналог dynamic rendering, attachments явные
void RHI_CmdBeginRendering(RHICommandList cmd,
    uint32_t colorCount, const RHITexture *colors, const RHIColorClear *clearColor,
    RHITexture depth, const RHIDepthClear *clearDepth);
void RHI_CmdEndRendering(RHICommandList cmd);

void RHI_CmdSetPipeline(RHICommandList cmd, RHIPipeline p);
void RHI_CmdSetViewport(RHICommandList cmd, float x, float y, float w, float h, float minD, float maxD);
void RHI_CmdSetScissor(RHICommandList cmd, int32_t x, int32_t y, uint32_t w, uint32_t h);

// Descriptor sets (только per-frame/per-pass; per-draw = push constants + bindless)
typedef struct RHIDescriptorWrite {
    uint32_t set;                 // как в RHIBindPoint
    uint32_t binding;
    union {
        struct { RHIBuffer buffer; uint64_t offset, range; } buf;
        struct { RHITexture texture; RHISampler sampler; }    tex;
        struct { RHIBuffer buffer; } uavBuf;
    };
    uint32_t arrayIndex;          // для bindless-подобных массивов
} RHIDescriptorWrite;
void RHI_CmdPushDescriptors(RHICommandList cmd, const RHIDescriptorWrite *writes, uint32_t count);

// Push constants (64-128 байт) — MVP, material id, draw data
void RHI_CmdPushConstants(RHICommandList cmd, const void *data, uint32_t size);

// Draws
void RHI_CmdBindVertexBuffer(RHICommandList cmd, RHIBuffer vb, uint64_t offset);
void RHI_CmdBindIndexBuffer(RHICommandList cmd, RHIBuffer ib, RHIFormat fmt, uint64_t offset);
void RHI_CmdDraw(RHICommandList cmd, uint32_t vtxCount, uint32_t instanceCount, uint32_t firstVtx, uint32_t firstInst);
void RHI_CmdDrawIndexed(RHICommandList cmd, uint32_t idxCount, uint32_t instanceCount, uint32_t firstIdx, int32_t vtxOffset, uint32_t firstInst);
void RHI_CmdDrawIndirect(RHICommandList cmd, RHIBuffer args, uint64_t offset, uint32_t drawCount, uint32_t stride);
void RHI_CmdDrawIndirectCount(RHICommandList cmd, RHIBuffer args, uint64_t argsOffset, RHIBuffer count, uint64_t countOffset, uint32_t maxDraws, uint32_t stride);
void RHI_CmdDispatch(RHICommandList cmd, uint32_t gx, uint32_t gy, uint32_t gz);
void RHI_CmdDispatchIndirect(RHICommandList cmd, RHIBuffer args, uint64_t offset);

// ---- GPU-driven рендер (цель движка) -------------------------------------------
// cull/compute пишет indirect args + count, затем один DrawIndirectCount.
// Все данные — в storage buffers, никаких per-draw дескрипторов.

// ---- Sync compute<->graphics (тонко!) ------------------------------------------
void RHI_CmdExecuteBarrier(RHICommandList cmd);     // generic memory barrier между проходами

// ---- Frame graph (уровень выше RHI) -------------------------------------------
// Использование:
//   RGBuilder *rg = RG_Create();
//   RGPass *gbuffer = RG_AddPass(rg, "gbuffer");
//   RG_Read(rg, gbuffer, sceneTex, RHI_STATE_SHADER_RESOURCE);
//   RG_Write(rg, gbuffer, gbufferColor, RHI_STATE_RENDER_TARGET);
//   ...
//   RG_Execute(rg, cmd);  // топологическая сортировка, barriers, culling
//   RG_Destroy(rg);
typedef struct RGBuilder RGBuilder;
typedef struct RGPass RGPass;
typedef void (*RGExecuteFn)(RHICommandList cmd, void *userData);

RGBuilder* RG_Create(void);
void RG_Destroy(RGBuilder*);
RGPass* RG_AddPass(RGBuilder*, const char *name);
void RG_Read(RGBuilder*, RGPass*, RHITexture, RHIResourceState);
void RG_Write(RGBuilder*, RGPass*, RHITexture, RHIResourceState);
void RG_ReadBuffer(RGBuilder*, RGPass*, RHIBuffer, RHIResourceState);
void RG_WriteBuffer(RGBuilder*, RGPass*, RHIBuffer, RHIResourceState);
// Transient-текстуры: graph аллоцирует пул и реюзает память между проходами
RHITexture RG_CreateTransientTexture(RGBuilder*, const RHITextureDesc*);
void RG_SetExecute(RGBuilder*, RGPass*, RGExecuteFn, void *userData);
void RG_Execute(RGBuilder*, RHICommandList cmd);    // culls unused passes
void RG_Present(RGBuilder*, RHITexture final);      // import backbuffer

// ---- Debug --------------------------------------------------------------------
void RHI_BeginDebugLabel(RHICommandList cmd, const char *name, float r, float g, float b);
void RHI_EndDebugLabel(RHICommandList cmd);
bool RHI_CaptureNextFrame(const char *path);        // rdc/PIX capture

#ifdef __cplusplus
}
#endif
#endif // VLCR_RHI_H
