/**
 * vgraphics_pipeline.c — shader load, descriptor layouts, graphics pipelines (Sprint E)
 */
#include "vgraphics.h"
#include "vgraphics_internal.h"
#include "vfiles.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef VLCRLIB_EMBEDDED_SHADERS
/* Generated from the compiled .spv files by scripts/embed_shaders.cmake;
   Android APKs do not carry the shaders/ directory next to the library. */
const unsigned char *vlcrlib_EmbeddedShaderLookup(const char *path,
                                                  unsigned int *size);
#endif

VkShaderModule createShaderModule(const char *path) {
    unsigned int sz = 0;
    char *owned = (char *)LoadFileData(path, &sz);
    if (!owned) {
        const char *basePath = SDL_GetBasePath();
        if (basePath) {
            char resolvedPath[1024];
            snprintf(resolvedPath, sizeof(resolvedPath), "%s%s", basePath, path);
            owned = (char *)LoadFileData(resolvedPath, &sz);
        }
    }

    const unsigned char *code = (const unsigned char *)owned;
    unsigned int codeSize = sz;
#ifdef VLCRLIB_EMBEDDED_SHADERS
    if (!code) {
        codeSize = 0;
        code = vlcrlib_EmbeddedShaderLookup(path, &codeSize);
        if (code)
            fprintf(stderr, "[vlcrlib] shader %s: using embedded copy\n", path);
    }
#endif
    if (!code) {
        fprintf(stderr, "[vlcrlib] cannot open shader %s\n", path);
        return VK_NULL_HANDLE;
    }
    if (codeSize == 0) {
        fprintf(stderr, "[vlcrlib] shader %s is empty\n", path);
        if (owned) UnloadFileData((unsigned char *)owned);
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo ci = {0};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = (size_t)codeSize;
    ci.pCode    = (const uint32_t *)code;
    VkShaderModule mod = VK_NULL_HANDLE;
    VkResult r = vkCreateShaderModule(VK.device, &ci, NULL, &mod);
    if (r != VK_SUCCESS) {
        fprintf(stderr, "[vlcrlib] vkCreateShaderModule failed (%d): %s\n",
                (int)r, path);
        mod = VK_NULL_HANDLE;
    }
    if (owned) UnloadFileData((unsigned char *)owned);
    return mod;
}

bool createRayTraceDemoPipeline(void) {
    VK.rayTraceDemoVert = createShaderModule("shaders/raytrace_demo.vert.spv");
    VK.rayTraceDemoFrag = createShaderModule(VK.rayQueryDemoReady
        ? "shaders/raytrace_query.frag.spv" : "shaders/raytrace_demo.frag.spv");
    if (!VK.rayTraceDemoVert || !VK.rayTraceDemoFrag) return false;

    VkPipelineShaderStageCreateInfo stages[2] = {0};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = VK.rayTraceDemoVert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = VK.rayTraceDemoFrag;
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
    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn = {0};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;
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
    dss.depthTestEnable = VK_FALSE;
    dss.depthWriteEnable = VK_FALSE;
    VkPipelineColorBlendAttachmentState blendAttachment = {0};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend = {0};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    VkPushConstantRange push = {0};
    push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push.offset = 0;
    push.size = sizeof(float) * 3;
    VkPipelineLayoutCreateInfo layoutInfo = {0};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (VK.rayQueryDemoReady) {
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &VK.rayDemoDescriptorLayout;
    }
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(VK.device, &layoutInfo, NULL, &VK.rayTraceDemoLayout) != VK_SUCCESS)
        return false;

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
    pi.pColorBlendState = &blend;
    pi.pDynamicState = &dyn;
    pi.layout = VK.rayTraceDemoLayout;
    pi.renderPass = VK.renderPass;
    pi.subpass = 0;
    VkResult result = vkCreateGraphicsPipelines(VK.device, VK_NULL_HANDLE, 1, &pi, NULL,
                                                &VK.rayTraceDemoPipeline);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[vlcrlib] ray tracing demo pipeline creation failed: %d\n", (int)result);
        return false;
    }
    return true;
}

bool createSampler2D(void) {
    VkSamplerCreateInfo ci = {0};
    ci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ci.magFilter    = VK_FILTER_LINEAR;
    ci.minFilter    = VK_FILTER_LINEAR;
    ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    ci.minLod       = 0.0f;
    ci.maxLod       = 16.0f;  /* allow full mip chain (Sprint A point 6) */
    ci.mipLodBias   = 0.0f;
    return vkCreateSampler(VK.device, &ci, NULL, &VK.sampler2D) == VK_SUCCESS;
}

bool createDescriptorSet2D(void) {
    VkDescriptorSetLayoutBinding b = {0};
    b.binding         = 0;
    b.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo li = {0};
    li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 1;
    li.pBindings    = &b;
    if (vkCreateDescriptorSetLayout(VK.device, &li, NULL, &VK.dsl2D) != VK_SUCCESS) return false;

    VkDescriptorPoolSize ps = {0};
    ps.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps.descriptorCount = VLCRLIB_MAX_2D_DESCRIPTORS;

    VkDescriptorPoolCreateInfo pi = {0};
    pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pi.poolSizeCount = 1;
    pi.pPoolSizes    = &ps;
    pi.maxSets       = VLCRLIB_MAX_2D_DESCRIPTORS;
    if (vkCreateDescriptorPool(VK.device, &pi, NULL, &VK.descPool2D) != VK_SUCCESS) return false;

    VkDescriptorSetAllocateInfo ai = {0};
    VkDescriptorSetLayout layouts[VLCRLIB_MAX_2D_DESCRIPTORS];
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = VK.descPool2D;
    ai.descriptorSetCount = VLCRLIB_MAX_2D_DESCRIPTORS;
    for (uint32_t i = 0; i < VLCRLIB_MAX_2D_DESCRIPTORS; ++i) layouts[i] = VK.dsl2D;
    ai.pSetLayouts        = layouts;
    return vkAllocateDescriptorSets(VK.device, &ai, VK.descSets2D) == VK_SUCCESS;
}

bool createPipeline2D(void) {
    VK.vert2D = createShaderModule("shaders/2d.vert.spv");
    VK.frag2D = createShaderModule("shaders/2d.frag.spv");
    if (!VK.vert2D || !VK.frag2D) return false;

    VkPipelineShaderStageCreateInfo stages[2] = {0};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = VK.vert2D;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = VK.frag2D;
    stages[1].pName  = "main";

    VkVertexInputBindingDescription bind = {0};
    bind.binding   = 0;
    bind.stride    = sizeof(Vertex2D);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3] = {0};
    attrs[0].location = 0; attrs[0].binding = 0;
    attrs[0].format   = VK_FORMAT_R32G32_SFLOAT;
    attrs[0].offset   = offsetof(Vertex2D, x);
    attrs[1].location = 1; attrs[1].binding = 0;
    attrs[1].format   = VK_FORMAT_R32G32_SFLOAT;
    attrs[1].offset   = offsetof(Vertex2D, u);
    attrs[2].location = 2; attrs[2].binding = 0;
    attrs[2].format   = VK_FORMAT_R8G8B8A8_UNORM;
    attrs[2].offset   = offsetof(Vertex2D, r);

    VkPipelineVertexInputStateCreateInfo vi = {0};
    vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &bind;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia = {0};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp = {0};
    vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds = {0};
    ds.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2;
    ds.pDynamicStates    = dyn;

    VkPipelineRasterizationStateCreateInfo rs = {0};
    rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {0};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo dss = {0};
    dss.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dss.depthTestEnable  = VK_FALSE;
    dss.depthWriteEnable = VK_FALSE;
    dss.depthCompareOp   = VK_COMPARE_OP_ALWAYS;

    VkPipelineColorBlendAttachmentState cba = {0};
    cba.blendEnable         = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp        = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    cba.alphaBlendOp        = VK_BLEND_OP_ADD;
    cba.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb = {0};
    cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments    = &cba;

    VkPushConstantRange pcr = {0};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcr.offset     = 0;
    pcr.size       = sizeof(float) * 16;

    VkPipelineLayoutCreateInfo li = {0};
    li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    li.setLayoutCount         = 1;
    li.pSetLayouts            = &VK.dsl2D;
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges    = &pcr;
    if (vkCreatePipelineLayout(VK.device, &li, NULL, &VK.layout2D) != VK_SUCCESS) return false;

    VkGraphicsPipelineCreateInfo pi = {0};
    pi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount          = 2;
    pi.pStages             = stages;
    pi.pVertexInputState   = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState      = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState   = &ms;
    pi.pDepthStencilState  = &dss;
    pi.pColorBlendState    = &cb;
    pi.pDynamicState       = &ds;
    pi.layout              = VK.layout2D;
    pi.renderPass          = VK.renderPass;
    pi.subpass             = 0;

    VkResult r = vkCreateGraphicsPipelines(VK.device, VK_NULL_HANDLE, 1, &pi, NULL, &VK.pipeline2D);
    return r == VK_SUCCESS;
}

bool createUBO3D(void) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(VK.phys, &props);
    uint32_t align = (uint32_t)props.limits.minUniformBufferOffsetAlignment;
    if (align < 1) align = 1;
    VK.ubo3DStride = ((uint32_t)sizeof(UBO3D) + align - 1) / align * align;
    VK.ubo3DMaxSlots = 4096;
    VkDeviceSize size = (VkDeviceSize)VK.ubo3DStride * VK.ubo3DMaxSlots;

    if (!createBuffer(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &VK.ubo3D, &VK.ubo3DMem)) return false;
    return vkMapMemory(VK.device, VK.ubo3DMem, 0, size, 0, &VK.ubo3DMapped) == VK_SUCCESS;
}

bool createDescriptorSet3D(void) {
    VkDescriptorSetLayoutBinding binds[2] = {0};
    binds[0].binding         = 0;
    binds[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[1].binding         = 1;
    binds[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo li = {0};
    li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 2;
    li.pBindings    = binds;
    if (vkCreateDescriptorSetLayout(VK.device, &li, NULL, &VK.dsl3D) != VK_SUCCESS) return false;

    VkDescriptorPoolSize ps[2] = {0};
    ps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    ps[0].descriptorCount = 1;
    ps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo pi = {0};
    pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.poolSizeCount = 2;
    pi.pPoolSizes    = ps;
    pi.maxSets       = 1;
    if (vkCreateDescriptorPool(VK.device, &pi, NULL, &VK.descPool3D) != VK_SUCCESS) return false;

    VkDescriptorSetAllocateInfo ai = {0};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = VK.descPool3D;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &VK.dsl3D;
    if (vkAllocateDescriptorSets(VK.device, &ai, &VK.descSet3D) != VK_SUCCESS) return false;

    VkDescriptorBufferInfo bi = { VK.ubo3D, 0, sizeof(UBO3D) };
    VkWriteDescriptorSet w0 = {0};
    w0.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w0.dstSet          = VK.descSet3D;
    w0.dstBinding      = 0;
    w0.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    w0.descriptorCount = 1;
    w0.pBufferInfo     = &bi;

    TextureEntry *e = &g_textureTable[VK.whiteTexture.id];
    VkDescriptorImageInfo ii = { e->samp, e->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet w1 = {0};
    w1.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w1.dstSet          = VK.descSet3D;
    w1.dstBinding      = 1;
    w1.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w1.descriptorCount = 1;
    w1.pImageInfo      = &ii;

    VkWriteDescriptorSet ws[2] = { w0, w1 };
    vkUpdateDescriptorSets(VK.device, 2, ws, 0, NULL);
    return true;
}

bool createPipeline3D(bool isLine) {
    VkShaderModule vert = createShaderModule("shaders/3d.vert.spv");
    VkShaderModule frag = createShaderModule("shaders/3d.frag.spv");
    if (!vert || !frag) return false;

    VkPipelineShaderStageCreateInfo stages[2] = {0};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;   stages[0].pName = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;   stages[1].pName = "main";

    VkVertexInputBindingDescription bind = {0};
    bind.binding   = 0;
    bind.stride    = sizeof(Vertex3D);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3] = {0};
    attrs[0].location = 0; attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = offsetof(Vertex3D, x);
    attrs[1].location = 1; attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = offsetof(Vertex3D, nx);
    attrs[2].location = 2; attrs[2].binding = 0;
    attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[2].offset = offsetof(Vertex3D, u);

    VkPipelineVertexInputStateCreateInfo vi = {0};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia = {0};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = isLine ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
                         : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp = {0};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;

    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds = {0};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;

    VkPipelineRasterizationStateCreateInfo rs = {0};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = isLine ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {0};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo dss = {0};
    dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dss.depthTestEnable  = VK_TRUE;
    dss.depthWriteEnable = isLine ? VK_FALSE : VK_TRUE;
    dss.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState cba = {0};
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb = {0};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &cba;

    /* layout3D создаётся один раз до pipeline */
    VkGraphicsPipelineCreateInfo pi = {0};
    pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = 2; pi.pStages = stages;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &dss;
    pi.pColorBlendState = &cb;
    pi.pDynamicState = &ds;
    pi.layout = VK.layout3D;
    pi.renderPass = VK.renderPass;
    pi.subpass = 0;

    VkPipeline *out = isLine ? &VK.pipeline3DLine : &VK.pipeline3D;
    VkResult r = vkCreateGraphicsPipelines(VK.device, VK_NULL_HANDLE, 1, &pi, NULL, out);
    vkDestroyShaderModule(VK.device, vert, NULL);
    vkDestroyShaderModule(VK.device, frag, NULL);
    return r == VK_SUCCESS;
}

bool createUBOPBR(void) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(VK.phys, &props);
    uint32_t align = (uint32_t)props.limits.minUniformBufferOffsetAlignment;
    if (align < 1) align = 1;
    VK.uboPBRStride = ((uint32_t)sizeof(UBO_PBR) + align - 1) / align * align;
    VK.uboPBRMaxSlots = 4096;
    VkDeviceSize size = (VkDeviceSize)VK.uboPBRStride * VK.uboPBRMaxSlots;

    if (!createBuffer(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &VK.uboPBR, &VK.uboPBRMem)) return false;
    return vkMapMemory(VK.device, VK.uboPBRMem, 0, size, 0, &VK.uboPBRMapped) == VK_SUCCESS;
}

bool createLightBuffer(void) {
    VK.lightBufferSize = (VkDeviceSize)sizeof(VLightData) * VLCRLIB_MAX_GPU_LIGHTS;
    if (!createBuffer(VK.lightBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &VK.lightBuffer, &VK.lightBufferMem))
        return false;
    if (vkMapMemory(VK.device, VK.lightBufferMem, 0, VK.lightBufferSize, 0,
                    &VK.lightBufferMapped) != VK_SUCCESS)
        return false;
    memset(VK.lightBufferMapped, 0, (size_t)VK.lightBufferSize);
    VK.lightArrayCount = 0;
    return true;
}

bool createDescriptorSetLayoutPBR(void) {
    /* 0=UBO, 1-5=mat, 6=dirShadow, 7-9=IBL, 10=spotShadow, 11=pointShadowCube */
    /* 0=UBO 1-5=mat 6-11=shadow/IBL 12=unused pad 13=specular 14=clearcoat */
    VkDescriptorSetLayoutBinding binds[17] = {0};
    binds[0].binding = 0;
    binds[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    for (int i = 1; i <= 15; ++i) {
        binds[i].binding         = (uint32_t)i;
        binds[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binds[i].descriptorCount = 1;
        binds[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    binds[16].binding         = 16;
    binds[16].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binds[16].descriptorCount = 1;
    binds[16].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo li = {0};
    li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 17;
    li.pBindings    = binds;
    if (vkCreateDescriptorSetLayout(VK.device, &li, NULL, &VK.dslPBR) != VK_SUCCESS) return false;

    VkDescriptorPoolSize ps[3] = {0};
    ps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    ps[0].descriptorCount = VLCRLIB_MAX_MATERIALS;
    ps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps[1].descriptorCount = VLCRLIB_MAX_MATERIALS * 16;
    ps[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ps[2].descriptorCount = VLCRLIB_MAX_MATERIALS;

    VkDescriptorPoolCreateInfo pi = {0};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pi.poolSizeCount = 3;
    pi.pPoolSizes    = ps;
    pi.maxSets       = VLCRLIB_MAX_MATERIALS;
    return vkCreateDescriptorPool(VK.device, &pi, NULL, &VK.descPoolPBR) == VK_SUCCESS;
}

bool createPipelinePBR(void) {
    VkShaderModule vert = createShaderModule("shaders/pbr.vert.spv");
    VkShaderModule frag = createShaderModule("shaders/pbr.frag.spv");
    if (!vert || !frag) return false;

    VkPipelineShaderStageCreateInfo stages[2] = {0};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;   stages[0].pName = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;   stages[1].pName = "main";

    VkVertexInputBindingDescription bind = {0};
    bind.binding   = 0;
    bind.stride    = sizeof(Vertex3D);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[5] = {0};
    attrs[0].location = 0; attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = offsetof(Vertex3D, x);
    attrs[1].location = 1; attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = offsetof(Vertex3D, nx);
    attrs[2].location = 2; attrs[2].binding = 0;
    attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[2].offset = offsetof(Vertex3D, u);
    attrs[3].location = 3; attrs[3].binding = 0;
    attrs[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[3].offset = offsetof(Vertex3D, tx);
    attrs[4].location = 4; attrs[4].binding = 0;
    attrs[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[4].offset = offsetof(Vertex3D, cr);

    VkPipelineVertexInputStateCreateInfo vi = {0};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 5;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia = {0};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp = {0};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;

    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds = {0};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;

    VkPipelineRasterizationStateCreateInfo rs = {0};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_BACK_BIT;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {0};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo dss = {0};
    dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dss.depthTestEnable  = VK_TRUE;
    dss.depthWriteEnable = VK_TRUE;
    dss.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState cba = {0};
    cba.blendEnable    = VK_FALSE;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb = {0};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &cba;

    VkPipelineLayoutCreateInfo pli = {0};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &VK.dslPBR;
    if (vkCreatePipelineLayout(VK.device, &pli, NULL, &VK.layoutPBR) != VK_SUCCESS) return false;

    VkGraphicsPipelineCreateInfo pi = {0};
    pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = 2; pi.pStages = stages;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &dss;
    pi.pColorBlendState = &cb;
    pi.pDynamicState = &ds;
    pi.layout = VK.layoutPBR;
    pi.renderPass = VK.renderPass;
    pi.subpass = 0;

    VkResult r = vkCreateGraphicsPipelines(VK.device, VK_NULL_HANDLE, 1, &pi, NULL, &VK.pipelinePBR);
    if (r != VK_SUCCESS) {
        vkDestroyShaderModule(VK.device, vert, NULL);
        vkDestroyShaderModule(VK.device, frag, NULL);
        return false;
    }
    /* doubleSided materials: no back-face culling (Sprint A point 4) */
    rs.cullMode = VK_CULL_MODE_NONE;
    r = vkCreateGraphicsPipelines(VK.device, VK_NULL_HANDLE, 1, &pi, NULL, &VK.pipelinePBRNoCull);
    if (r != VK_SUCCESS) {
        vkDestroyShaderModule(VK.device, vert, NULL);
        vkDestroyShaderModule(VK.device, frag, NULL);
        return false;
    }
    /* Transparent BLEND: alpha blend, no depth write (Sprint C point 18) */
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    dss.depthWriteEnable = VK_FALSE;
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    r = vkCreateGraphicsPipelines(VK.device, VK_NULL_HANDLE, 1, &pi, NULL, &VK.pipelinePBRBlend);
    if (r != VK_SUCCESS) {
        vkDestroyShaderModule(VK.device, vert, NULL);
        vkDestroyShaderModule(VK.device, frag, NULL);
        return false;
    }
    rs.cullMode = VK_CULL_MODE_NONE;
    r = vkCreateGraphicsPipelines(VK.device, VK_NULL_HANDLE, 1, &pi, NULL, &VK.pipelinePBRBlendNoCull);
    vkDestroyShaderModule(VK.device, vert, NULL);
    vkDestroyShaderModule(VK.device, frag, NULL);
    return r == VK_SUCCESS;
}

bool createBoneBuffer(void) {
    VkDeviceSize size = (VkDeviceSize)VLCRLIB_MAX_BONES * sizeof(Matrix);
    VK.boneBufferSize = (uint32_t)size;

    if (!createBuffer(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &VK.boneBuffer, &VK.boneBufferMem)) return false;

    if (vkMapMemory(VK.device, VK.boneBufferMem, 0, size, 0, &VK.boneBufferMapped) != VK_SUCCESS)
        return false;

    Matrix I = MatrixIdentity();
    for (int i = 0; i < VLCRLIB_MAX_BONES; ++i)
        memcpy((char *)VK.boneBufferMapped + i * sizeof(Matrix), &I, sizeof(Matrix));
    return true;
}

bool createDescriptorSetLayoutSkin(void) {
    /* 0-11 match PBR; 12 = bones UBO; 13 = specular; 14 = clearcoat */
    VkDescriptorSetLayoutBinding binds[17] = {0};
    binds[0].binding = 0;
    binds[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    for (int i = 1; i <= 15; ++i) {
        binds[i].binding         = (uint32_t)i;
        binds[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binds[i].descriptorCount = 1;
        binds[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    binds[12].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binds[12].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
    binds[16].binding         = 16;
    binds[16].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binds[16].descriptorCount = 1;
    binds[16].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo li = {0};
    li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 17;
    li.pBindings    = binds;
    if (vkCreateDescriptorSetLayout(VK.device, &li, NULL, &VK.dslSkin) != VK_SUCCESS) return false;

    VkDescriptorPoolSize ps[4] = {0};
    ps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    ps[0].descriptorCount = VLCRLIB_MAX_SKMODELS * 4 + VLCRLIB_MAX_MATERIALS;
    ps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps[1].descriptorCount = (VLCRLIB_MAX_SKMODELS * 4 + VLCRLIB_MAX_MATERIALS) * 16;
    ps[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ps[2].descriptorCount = VLCRLIB_MAX_SKMODELS * 4 + VLCRLIB_MAX_MATERIALS;
    ps[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ps[3].descriptorCount = VLCRLIB_MAX_SKMODELS * 4 + VLCRLIB_MAX_MATERIALS;

    VkDescriptorPoolCreateInfo pi = {0};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pi.poolSizeCount = 4;
    pi.pPoolSizes    = ps;
    pi.maxSets       = VLCRLIB_MAX_SKMODELS * 4 + VLCRLIB_MAX_MATERIALS;
    return vkCreateDescriptorPool(VK.device, &pi, NULL, &VK.descPoolSkin) == VK_SUCCESS;
}

bool createPipelineSkin(void) {
    VkShaderModule vert = createShaderModule("shaders/skin.vert.spv");
    VkShaderModule frag = createShaderModule("shaders/pbr.frag.spv");
    if (!vert || !frag) return false;

    VkPipelineShaderStageCreateInfo stages[2] = {0};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;   stages[0].pName = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;   stages[1].pName = "main";

    VkVertexInputBindingDescription bind = {0};
    bind.binding   = 0;
    bind.stride    = sizeof(SkinnedVertex3D);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[6] = {0};
    attrs[0].location = 0; attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = offsetof(SkinnedVertex3D, x);
    attrs[1].location = 1; attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = offsetof(SkinnedVertex3D, nx);
    attrs[2].location = 2; attrs[2].binding = 0;
    attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[2].offset = offsetof(SkinnedVertex3D, u);
    attrs[3].location = 3; attrs[3].binding = 0;
    attrs[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[3].offset = offsetof(SkinnedVertex3D, tx);
    attrs[4].location = 4; attrs[4].binding = 0;
    attrs[4].format = VK_FORMAT_R8G8B8A8_UINT;
    attrs[4].offset = offsetof(SkinnedVertex3D, joints);
    attrs[5].location = 5; attrs[5].binding = 0;
    attrs[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[5].offset = offsetof(SkinnedVertex3D, weights);

    VkPipelineVertexInputStateCreateInfo vi = {0};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 6;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia = {0};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp = {0};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;

    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds = {0};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;

    VkPipelineRasterizationStateCreateInfo rs = {0};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_BACK_BIT;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {0};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo dss = {0};
    dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dss.depthTestEnable  = VK_TRUE;
    dss.depthWriteEnable = VK_TRUE;
    dss.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState cba = {0};
    cba.blendEnable    = VK_FALSE;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb = {0};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &cba;

    VkPipelineLayoutCreateInfo pli = {0};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &VK.dslSkin;
    if (vkCreatePipelineLayout(VK.device, &pli, NULL, &VK.layoutSkin) != VK_SUCCESS) return false;

    VkGraphicsPipelineCreateInfo pi = {0};
    pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = 2; pi.pStages = stages;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &dss;
    pi.pColorBlendState = &cb;
    pi.pDynamicState = &ds;
    pi.layout = VK.layoutSkin;
    pi.renderPass = VK.renderPass;
    pi.subpass = 0;

    VkResult r = vkCreateGraphicsPipelines(VK.device, VK_NULL_HANDLE, 1, &pi, NULL, &VK.pipelineSkin);
    if (r != VK_SUCCESS) {
        vkDestroyShaderModule(VK.device, vert, NULL);
        vkDestroyShaderModule(VK.device, frag, NULL);
        return false;
    }
    rs.cullMode = VK_CULL_MODE_NONE;
    r = vkCreateGraphicsPipelines(VK.device, VK_NULL_HANDLE, 1, &pi, NULL, &VK.pipelineSkinNoCull);
    if (r != VK_SUCCESS) {
        vkDestroyShaderModule(VK.device, vert, NULL);
        vkDestroyShaderModule(VK.device, frag, NULL);
        return false;
    }
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    dss.depthWriteEnable = VK_FALSE;
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    r = vkCreateGraphicsPipelines(VK.device, VK_NULL_HANDLE, 1, &pi, NULL, &VK.pipelineSkinBlend);
    if (r != VK_SUCCESS) {
        vkDestroyShaderModule(VK.device, vert, NULL);
        vkDestroyShaderModule(VK.device, frag, NULL);
        return false;
    }
    rs.cullMode = VK_CULL_MODE_NONE;
    r = vkCreateGraphicsPipelines(VK.device, VK_NULL_HANDLE, 1, &pi, NULL, &VK.pipelineSkinBlendNoCull);
    vkDestroyShaderModule(VK.device, vert, NULL);
    vkDestroyShaderModule(VK.device, frag, NULL);
    return r == VK_SUCCESS;
}
