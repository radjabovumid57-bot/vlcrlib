/*
 * Minimal, real Vulkan Ray Query scene for the ray-tracing example.
 * Builds a triangle-soup BLAS and one-instance TLAS, then exposes them and
 * the per-triangle material table to the fragment ray-query shader.
 */
#include "vgraphics_internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#define RAY_DEMO_MAX_TRIANGLES 2048u
#define RAY_DEMO_VERTEX_STRIDE 16u

typedef struct RayDemoMesh {
    float vertices[RAY_DEMO_MAX_TRIANGLES * 3u * 4u];
    uint32_t materials[RAY_DEMO_MAX_TRIANGLES];
    uint32_t triangleCount;
} RayDemoMesh;

typedef struct RayDemoFns {
    PFN_vkCreateAccelerationStructureKHR createAS;
    PFN_vkDestroyAccelerationStructureKHR destroyAS;
    PFN_vkGetAccelerationStructureBuildSizesKHR getBuildSizes;
    PFN_vkGetAccelerationStructureDeviceAddressKHR getASAddress;
    PFN_vkCmdBuildAccelerationStructuresKHR cmdBuildAS;
} RayDemoFns;

static RayDemoFns g_rt;

static bool loadRTFunctions(void) {
    g_rt.createAS = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(VK.device, "vkCreateAccelerationStructureKHR");
    g_rt.destroyAS = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(VK.device, "vkDestroyAccelerationStructureKHR");
    g_rt.getBuildSizes = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(VK.device, "vkGetAccelerationStructureBuildSizesKHR");
    g_rt.getASAddress = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(VK.device, "vkGetAccelerationStructureDeviceAddressKHR");
    g_rt.cmdBuildAS = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(VK.device, "vkCmdBuildAccelerationStructuresKHR");
    return g_rt.createAS && g_rt.destroyAS && g_rt.getBuildSizes && g_rt.getASAddress && g_rt.cmdBuildAS;
}

static void addTriangle(RayDemoMesh *m, Vector3 a, Vector3 b, Vector3 c, uint32_t material) {
    if (!m || m->triangleCount >= RAY_DEMO_MAX_TRIANGLES) return;
    uint32_t tri = m->triangleCount++;
    Vector3 p[3] = {a,b,c};
    for (uint32_t i=0; i<3; ++i) {
        size_t o = ((size_t)tri*3u+i)*4u;
        m->vertices[o+0] = p[i].x;
        m->vertices[o+1] = p[i].y;
        m->vertices[o+2] = p[i].z;
        m->vertices[o+3] = 1.0f;
    }
    m->materials[tri] = material;
}

static void addQuad(RayDemoMesh *m, Vector3 a, Vector3 b, Vector3 c, Vector3 d, uint32_t material) {
    addTriangle(m,a,b,c,material);
    addTriangle(m,a,c,d,material);
}

static void addBox(RayDemoMesh *m, Vector3 lo, Vector3 hi, uint32_t material) {
    Vector3 p000={lo.x,lo.y,lo.z}, p001={lo.x,lo.y,hi.z};
    Vector3 p010={lo.x,hi.y,lo.z}, p011={lo.x,hi.y,hi.z};
    Vector3 p100={hi.x,lo.y,lo.z}, p101={hi.x,lo.y,hi.z};
    Vector3 p110={hi.x,hi.y,lo.z}, p111={hi.x,hi.y,hi.z};
    addQuad(m,p000,p100,p110,p010,material);
    addQuad(m,p001,p011,p111,p101,material);
    addQuad(m,p000,p001,p101,p100,material);
    addQuad(m,p010,p110,p111,p011,material);
    addQuad(m,p000,p010,p011,p001,material);
    addQuad(m,p100,p101,p111,p110,material);
}

static void addSphere(RayDemoMesh *m, Vector3 center, float radius, uint32_t material) {
    /* Keep the glass silhouette smooth while staying below the demo mesh cap. */
    const uint32_t rings=28, slices=32;
    for (uint32_t y=0; y<rings; ++y) {
        float v0=(float)y/(float)rings, v1=(float)(y+1)/(float)rings;
        float t0=(v0-0.5f)*3.14159265359f, t1=(v1-0.5f)*3.14159265359f;
        for (uint32_t x=0; x<slices; ++x) {
            float u0=(float)x/(float)slices*6.28318530718f;
            float u1=(float)(x+1)/(float)slices*6.28318530718f;
            Vector3 p00={center.x+radius*cosf(t0)*cosf(u0),center.y+radius*sinf(t0),center.z+radius*cosf(t0)*sinf(u0)};
            Vector3 p10={center.x+radius*cosf(t0)*cosf(u1),center.y+radius*sinf(t0),center.z+radius*cosf(t0)*sinf(u1)};
            Vector3 p01={center.x+radius*cosf(t1)*cosf(u0),center.y+radius*sinf(t1),center.z+radius*cosf(t1)*sinf(u0)};
            Vector3 p11={center.x+radius*cosf(t1)*cosf(u1),center.y+radius*sinf(t1),center.z+radius*cosf(t1)*sinf(u1)};
            addTriangle(m,p00,p01,p10,material);
            addTriangle(m,p10,p01,p11,material);
        }
    }
}

static void makeCornellMesh(RayDemoMesh *m) {
    memset(m,0,sizeof(*m));
    const float x=2.5f, y0=-1.5f, y1=2.5f, z0=-2.5f, z1=7.0f;
    addQuad(m,(Vector3){-x,y0,z0},(Vector3){x,y0,z0},(Vector3){x,y0,z1},(Vector3){-x,y0,z1},0); /* floor */
    addQuad(m,(Vector3){-x,y1,z0},(Vector3){-x,y1,z1},(Vector3){x,y1,z1},(Vector3){x,y1,z0},0); /* ceiling */
    addQuad(m,(Vector3){-x,y0,z0},(Vector3){-x,y1,z0},(Vector3){x,y1,z0},(Vector3){x,y0,z0},0); /* back */
    addQuad(m,(Vector3){-x,y0,z1},(Vector3){-x,y1,z1},(Vector3){-x,y1,z0},(Vector3){-x,y0,z0},2); /* green */
    addQuad(m,(Vector3){x,y0,z0},(Vector3){x,y1,z0},(Vector3){x,y1,z1},(Vector3){x,y0,z1},1); /* red */
    addBox(m,(Vector3){-1.45f,y0,-2.12f},(Vector3){-0.18f,0.20f,-0.86f},3);
    addSphere(m,(Vector3){0.72f,-0.72f,-1.45f},0.76f,4);
    addQuad(m,(Vector3){-0.55f,2.465f,-1.48f},(Vector3){0.55f,2.465f,-1.48f},
              (Vector3){0.55f,2.465f,-0.79f},(Vector3){-0.55f,2.465f,-0.79f},5);
}

static bool makeBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryFlags,
                       const void *data, VkBuffer *buffer, VkDeviceMemory *memory) {
    VkBufferCreateInfo bi={0}; bi.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size=size; bi.usage=usage; bi.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(VK.device,&bi,NULL,buffer)!=VK_SUCCESS) return false;
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(VK.device,*buffer,&req);
    uint32_t mt=vlcr_findMemoryType(req.memoryTypeBits,memoryFlags);
    if (mt==UINT32_MAX) return false;
    VkMemoryAllocateFlagsInfo flags={0}; flags.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flags.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo ai={0}; ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.pNext=&flags; ai.allocationSize=req.size; ai.memoryTypeIndex=mt;
    if (vkAllocateMemory(VK.device,&ai,NULL,memory)!=VK_SUCCESS) return false;
    if (vkBindBufferMemory(VK.device,*buffer,*memory,0)!=VK_SUCCESS) return false;
    if (data) {
        void *mapped=NULL;
        if (vkMapMemory(VK.device,*memory,0,size,0,&mapped)!=VK_SUCCESS) return false;
        memcpy(mapped,data,(size_t)size);
        vkUnmapMemory(VK.device,*memory);
    }
    return true;
}

static VkDeviceAddress bufferAddress(VkBuffer buffer) {
    VkBufferDeviceAddressInfo ai={0}; ai.sType=VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    ai.buffer=buffer;
    /* Android's Vulkan loader does not export the Vulkan 1.2 core symbol;
       resolve it through the device dispatch table (KHR fallback first). */
    static PFN_vkGetBufferDeviceAddress pfn = NULL;
    static PFN_vkGetBufferDeviceAddressKHR pfnKhr = NULL;
    if (!pfn && !pfnKhr && VK.device) {
        pfn = (PFN_vkGetBufferDeviceAddress)
            vkGetDeviceProcAddr(VK.device, "vkGetBufferDeviceAddress");
        if (!pfn)
            pfnKhr = (PFN_vkGetBufferDeviceAddressKHR)
                vkGetDeviceProcAddr(VK.device, "vkGetBufferDeviceAddressKHR");
    }
    if (pfn) return pfn(VK.device, &ai);
    if (pfnKhr) return pfnKhr(VK.device, &ai);
    return 0;
}

static bool createASBuffer(VkDeviceSize size, VkBuffer *buffer, VkDeviceMemory *memory) {
    return makeBuffer(size,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR|
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,NULL,buffer,memory);
}

static bool submitBuilds(VkAccelerationStructureBuildGeometryInfoKHR *infos,
                         const VkAccelerationStructureBuildRangeInfoKHR **ranges, uint32_t count) {
    VkCommandBufferAllocateInfo ai={0}; ai.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool=VK.cmdPool; ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount=1;
    VkCommandBuffer cb=VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(VK.device,&ai,&cb)!=VK_SUCCESS) return false;
    VkCommandBufferBeginInfo begin={0}; begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    bool ok=vkBeginCommandBuffer(cb,&begin)==VK_SUCCESS;
    if (ok) {
        for (uint32_t i=0;i<count;i++) {
            g_rt.cmdBuildAS(cb,1,&infos[i],&ranges[i]);
            VkMemoryBarrier barrier={0}; barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            barrier.srcAccessMask=VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
            barrier.dstAccessMask=VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
            VkPipelineStageFlags dstStage = i+1<count
                ? VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
                : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                dstStage,0,1,&barrier,0,NULL,0,NULL);
        }
        ok=vkEndCommandBuffer(cb)==VK_SUCCESS;
    }
    if (ok) {
        VkSubmitInfo si={0}; si.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount=1; si.pCommandBuffers=&cb;
        ok=vkQueueSubmit(VK.gfxQueue,1,&si,VK_NULL_HANDLE)==VK_SUCCESS && vkQueueWaitIdle(VK.gfxQueue)==VK_SUCCESS;
    }
    vkFreeCommandBuffers(VK.device,VK.cmdPool,1,&cb);
    return ok;
}

bool createRayQueryDemoResources(void) {
    if (!VK.rayQuerySupported) return false;
    if (!loadRTFunctions()) return false;

    RayDemoMesh *mesh=(RayDemoMesh *)calloc(1,sizeof(*mesh));
    if (!mesh) return false;
    makeCornellMesh(mesh);
    uint32_t triangleCount=mesh->triangleCount;
    VkDeviceSize vertexBytes=(VkDeviceSize)mesh->triangleCount*3u*RAY_DEMO_VERTEX_STRIDE;
    VkDeviceSize materialBytes=(VkDeviceSize)mesh->triangleCount*sizeof(uint32_t);
    bool ok=makeBuffer(vertexBytes,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,mesh->vertices,
        &VK.rayDemoVertexBuffer,&VK.rayDemoVertexMemory) &&
        makeBuffer(materialBytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,mesh->materials,
        &VK.rayDemoMaterialBuffer,&VK.rayDemoMaterialMemory);

    VkDeviceAddress vertexAddr=bufferAddress(VK.rayDemoVertexBuffer);
    VkAccelerationStructureGeometryKHR blasGeom={0};
    blasGeom.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    blasGeom.geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    blasGeom.flags=VK_GEOMETRY_OPAQUE_BIT_KHR;
    blasGeom.geometry.triangles.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    blasGeom.geometry.triangles.vertexFormat=VK_FORMAT_R32G32B32_SFLOAT;
    blasGeom.geometry.triangles.vertexData.deviceAddress=vertexAddr;
    blasGeom.geometry.triangles.vertexStride=RAY_DEMO_VERTEX_STRIDE;
    blasGeom.geometry.triangles.maxVertex=mesh->triangleCount*3u-1u;
    blasGeom.geometry.triangles.indexType=VK_INDEX_TYPE_NONE_KHR;
    VkAccelerationStructureBuildGeometryInfoKHR bi={0};
    bi.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    bi.type=VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    bi.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    bi.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    bi.geometryCount=1; bi.pGeometries=&blasGeom;
    uint32_t primCount=mesh->triangleCount;
    VkAccelerationStructureBuildSizesInfoKHR sizes={0};
    sizes.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    if (ok) g_rt.getBuildSizes(VK.device,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&bi,&primCount,&sizes);
    ok=ok && createASBuffer(sizes.accelerationStructureSize,&VK.rayDemoBlasBuffer,&VK.rayDemoBlasMemory);
    VkAccelerationStructureCreateInfoKHR aci={0}; aci.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    aci.buffer=VK.rayDemoBlasBuffer; aci.size=sizes.accelerationStructureSize;
    aci.type=VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    ok=ok && g_rt.createAS(VK.device,&aci,NULL,&VK.rayDemoBlas)==VK_SUCCESS;

    VkBuffer scratch=VK_NULL_HANDLE; VkDeviceMemory scratchMem=VK_NULL_HANDLE;
    VkDeviceSize scratchSize=sizes.buildScratchSize;
    ok=ok && makeBuffer(scratchSize,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,NULL,&scratch,&scratchMem);
    VkAccelerationStructureBuildGeometryInfoKHR buildBlas=bi;
    buildBlas.dstAccelerationStructure=VK.rayDemoBlas;
    buildBlas.scratchData.deviceAddress=bufferAddress(scratch);
    VkAccelerationStructureBuildRangeInfoKHR blasRange={0}; blasRange.primitiveCount=primCount;
    const VkAccelerationStructureBuildRangeInfoKHR *blasRangePtr=&blasRange;

    VkAccelerationStructureInstanceKHR instance={0};
    instance.transform.matrix[0][0]=instance.transform.matrix[1][1]=instance.transform.matrix[2][2]=1.0f;
    instance.instanceCustomIndex=0; instance.mask=0xff; instance.flags=VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    VkAccelerationStructureDeviceAddressInfoKHR addrInfo={0};
    addrInfo.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addrInfo.accelerationStructure=VK.rayDemoBlas;
    instance.accelerationStructureReference=g_rt.getASAddress(VK.device,&addrInfo);
    VkBuffer instanceBuffer=VK_NULL_HANDLE; VkDeviceMemory instanceMemory=VK_NULL_HANDLE;
    ok=ok && makeBuffer(sizeof(instance),VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &instance,&instanceBuffer,&instanceMemory);
    VkAccelerationStructureGeometryKHR tlasGeom={0};
    tlasGeom.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    tlasGeom.geometryType=VK_GEOMETRY_TYPE_INSTANCES_KHR;
    tlasGeom.geometry.instances.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    tlasGeom.geometry.instances.arrayOfPointers=VK_FALSE;
    tlasGeom.geometry.instances.data.deviceAddress=bufferAddress(instanceBuffer);
    VkAccelerationStructureBuildGeometryInfoKHR tlasInfo={0};
    tlasInfo.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    tlasInfo.type=VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tlasInfo.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    tlasInfo.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    tlasInfo.geometryCount=1; tlasInfo.pGeometries=&tlasGeom;
    uint32_t instanceCount=1;
    VkAccelerationStructureBuildSizesInfoKHR tlasSizes={0};
    tlasSizes.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    if (ok) g_rt.getBuildSizes(VK.device,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&tlasInfo,&instanceCount,&tlasSizes);
    ok=ok && createASBuffer(tlasSizes.accelerationStructureSize,&VK.rayDemoTlasBuffer,&VK.rayDemoTlasMemory);
    aci.buffer=VK.rayDemoTlasBuffer; aci.size=tlasSizes.accelerationStructureSize;
    aci.type=VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    ok=ok && g_rt.createAS(VK.device,&aci,NULL,&VK.rayDemoTlas)==VK_SUCCESS;
    VkBuffer tlasScratch=VK_NULL_HANDLE; VkDeviceMemory tlasScratchMem=VK_NULL_HANDLE;
    if (tlasSizes.buildScratchSize>scratchSize) scratchSize=tlasSizes.buildScratchSize;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps={0};
    asProps.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
    VkPhysicalDeviceProperties2 props={0}; props.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2; props.pNext=&asProps;
    vkGetPhysicalDeviceProperties2(VK.phys,&props);
    VkDeviceSize align=asProps.minAccelerationStructureScratchOffsetAlignment;
    if (!align) align=1;
    /* Use one scratch allocation sized for the larger build, with device address alignment from the driver. */
    if (scratch) { vkDestroyBuffer(VK.device,scratch,NULL); vkFreeMemory(VK.device,scratchMem,NULL); scratch=VK_NULL_HANDLE; scratchMem=VK_NULL_HANDLE; }
    ok=ok && makeBuffer(scratchSize+align,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,NULL,&scratch,&scratchMem);
    VkDeviceAddress scratchAddr=bufferAddress(scratch);
    if (align>1) scratchAddr=(scratchAddr+(align-1))&~(VkDeviceAddress)(align-1);
    buildBlas.scratchData.deviceAddress=scratchAddr;
    tlasInfo.dstAccelerationStructure=VK.rayDemoTlas; tlasInfo.scratchData.deviceAddress=scratchAddr;
    VkAccelerationStructureBuildRangeInfoKHR tlasRange={0}; tlasRange.primitiveCount=1;
    const VkAccelerationStructureBuildRangeInfoKHR *tlasRangePtr=&tlasRange;
    VkAccelerationStructureBuildGeometryInfoKHR builds[2]={buildBlas,tlasInfo};
    const VkAccelerationStructureBuildRangeInfoKHR *ranges[2]={blasRangePtr,tlasRangePtr};
    ok=ok && submitBuilds(builds,ranges,2);

    if (scratch) vkDestroyBuffer(VK.device,scratch,NULL);
    if (scratchMem) vkFreeMemory(VK.device,scratchMem,NULL);
    if (tlasScratch) vkDestroyBuffer(VK.device,tlasScratch,NULL);
    if (tlasScratchMem) vkFreeMemory(VK.device,tlasScratchMem,NULL);
    if (instanceBuffer) vkDestroyBuffer(VK.device,instanceBuffer,NULL);
    if (instanceMemory) vkFreeMemory(VK.device,instanceMemory,NULL);
    free(mesh);
    if (!ok) { fprintf(stderr,"[vlcrlib] Vulkan ray-query AS build failed; keeping raster fallback\n"); destroyRayQueryDemoResources(); return false; }

    VkDescriptorSetLayoutBinding bindings[3]={0};
    bindings[0].binding=0; bindings[0].descriptorType=VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[0].descriptorCount=1; bindings[0].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
    for (uint32_t i=1;i<3;i++) { bindings[i].binding=i; bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount=1; bindings[i].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT; }
    VkDescriptorSetLayoutCreateInfo li={0}; li.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount=3; li.pBindings=bindings;
    ok=vkCreateDescriptorSetLayout(VK.device,&li,NULL,&VK.rayDemoDescriptorLayout)==VK_SUCCESS;
    VkDescriptorPoolSize poolSizes[2]={{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2}};
    VkDescriptorPoolCreateInfo pi={0}; pi.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets=1; pi.poolSizeCount=2; pi.pPoolSizes=poolSizes;
    ok=ok && vkCreateDescriptorPool(VK.device,&pi,NULL,&VK.rayDemoDescriptorPool)==VK_SUCCESS;
    VkDescriptorSetAllocateInfo dai={0}; dai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool=VK.rayDemoDescriptorPool; dai.descriptorSetCount=1; dai.pSetLayouts=&VK.rayDemoDescriptorLayout;
    ok=ok && vkAllocateDescriptorSets(VK.device,&dai,&VK.rayDemoDescriptorSet)==VK_SUCCESS;
    VkWriteDescriptorSetAccelerationStructureKHR asWrite={0};
    asWrite.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    asWrite.accelerationStructureCount=1; asWrite.pAccelerationStructures=&VK.rayDemoTlas;
    VkDescriptorBufferInfo vertexInfo={VK.rayDemoVertexBuffer,0,vertexBytes};
    VkDescriptorBufferInfo materialInfo={VK.rayDemoMaterialBuffer,0,materialBytes};
    VkWriteDescriptorSet writes[3]={0};
    writes[0].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[0].pNext=&asWrite;
    writes[0].dstSet=VK.rayDemoDescriptorSet; writes[0].dstBinding=0; writes[0].descriptorCount=1;
    writes[0].descriptorType=VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    for (uint32_t i=1;i<3;i++) { writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet=VK.rayDemoDescriptorSet; writes[i].dstBinding=i; writes[i].descriptorCount=1;
        writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; }
    writes[1].pBufferInfo=&vertexInfo; writes[2].pBufferInfo=&materialInfo;
    if (ok) vkUpdateDescriptorSets(VK.device,3,writes,0,NULL);
    VK.rayQueryDemoReady=ok;
    if (ok) fprintf(stderr,"[vlcrlib] hardware Ray Query scene ready (%u triangles, BLAS + TLAS)\n",triangleCount);
    if (!ok) destroyRayQueryDemoResources();
    return ok;
}

void destroyRayQueryDemoResources(void) {
    if (!VK.device) return;
    if (VK.rayDemoDescriptorPool) vkDestroyDescriptorPool(VK.device,VK.rayDemoDescriptorPool,NULL);
    if (VK.rayDemoDescriptorLayout) vkDestroyDescriptorSetLayout(VK.device,VK.rayDemoDescriptorLayout,NULL);
    if (VK.rayDemoTlas && g_rt.destroyAS) g_rt.destroyAS(VK.device,VK.rayDemoTlas,NULL);
    if (VK.rayDemoBlas && g_rt.destroyAS) g_rt.destroyAS(VK.device,VK.rayDemoBlas,NULL);
    if (VK.rayDemoTlasBuffer) vkDestroyBuffer(VK.device,VK.rayDemoTlasBuffer,NULL);
    if (VK.rayDemoBlasBuffer) vkDestroyBuffer(VK.device,VK.rayDemoBlasBuffer,NULL);
    if (VK.rayDemoTlasMemory) vkFreeMemory(VK.device,VK.rayDemoTlasMemory,NULL);
    if (VK.rayDemoBlasMemory) vkFreeMemory(VK.device,VK.rayDemoBlasMemory,NULL);
    if (VK.rayDemoVertexBuffer) vkDestroyBuffer(VK.device,VK.rayDemoVertexBuffer,NULL);
    if (VK.rayDemoMaterialBuffer) vkDestroyBuffer(VK.device,VK.rayDemoMaterialBuffer,NULL);
    if (VK.rayDemoVertexMemory) vkFreeMemory(VK.device,VK.rayDemoVertexMemory,NULL);
    if (VK.rayDemoMaterialMemory) vkFreeMemory(VK.device,VK.rayDemoMaterialMemory,NULL);
    VK.rayDemoDescriptorPool=VK_NULL_HANDLE; VK.rayDemoDescriptorLayout=VK_NULL_HANDLE;
    VK.rayDemoTlas=VK_NULL_HANDLE; VK.rayDemoBlas=VK_NULL_HANDLE;
    VK.rayDemoTlasBuffer=VK_NULL_HANDLE; VK.rayDemoBlasBuffer=VK_NULL_HANDLE;
    VK.rayDemoTlasMemory=VK_NULL_HANDLE; VK.rayDemoBlasMemory=VK_NULL_HANDLE;
    VK.rayDemoVertexBuffer=VK_NULL_HANDLE; VK.rayDemoMaterialBuffer=VK_NULL_HANDLE;
    VK.rayDemoVertexMemory=VK_NULL_HANDLE; VK.rayDemoMaterialMemory=VK_NULL_HANDLE;
    VK.rayDemoDescriptorSet=VK_NULL_HANDLE; VK.rayQueryDemoReady=false;
}
