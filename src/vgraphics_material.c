/**
 * vgraphics_material.c ??? materials, UBO_PBR write, descriptor updates (Sprint E)
 */
#include "vgraphics.h"
#include "vgraphics_internal.h"
#include "vmath.h"
#include <stdio.h>
#include <string.h>

/* tables defined in vgraphics.c */
extern TextureEntry g_textureTable[];
extern int g_textureCount;
extern PBRMaterialEntry g_matTable[];
extern int g_matCount;

typedef struct DeferredMaterialRelease {
    VkDescriptorSet sets[2];
    uint32_t frameSlot;
} DeferredMaterialRelease;
static DeferredMaterialRelease g_deferredMaterialReleases[VLCRLIB_MAX_MATERIALS];
static int g_deferredMaterialReleaseCount = 0;
static unsigned int g_materialGenerations[VLCRLIB_MAX_MATERIALS];

static unsigned int material_generation(unsigned int slot) {
    if (!slot || slot >= VLCRLIB_MAX_MATERIALS) return 0;
    if (!g_materialGenerations[slot]) g_materialGenerations[slot] = 1;
    return g_materialGenerations[slot];
}

bool material_handle_valid(const Material *material) {
    if (!material || material->id == 0 ||
        material->id >= VLCRLIB_MAX_MATERIALS ||
        material->generation == 0 ||
        material->generation != g_matTable[material->id].generation)
        return false;
    /* Without a device there is no descriptor set; the live generation is
       the only validity token the CPU backend has. */
    return VK.device ? g_matTable[material->id].descSet != VK_NULL_HANDLE
                     : true;
}

void material_detach_texture(Texture2D texture) {
    if (!texture.id || !texture.generation) return;
    for (int i = 1; i <= g_matCount && i < VLCRLIB_MAX_MATERIALS; ++i) {
        PBRMaterialEntry *m = &g_matTable[i];
        bool changed = false;
        if (m->albedo.id == texture.id && m->albedo.generation == texture.generation) { m->albedo = (Texture2D){0}; changed = true; }
        if (m->normal.id == texture.id && m->normal.generation == texture.generation) { m->normal = (Texture2D){0}; changed = true; }
        if (m->mr.id == texture.id && m->mr.generation == texture.generation) { m->mr = (Texture2D){0}; changed = true; }
        if (m->ao.id == texture.id && m->ao.generation == texture.generation) { m->ao = (Texture2D){0}; changed = true; }
        if (m->emission.id == texture.id && m->emission.generation == texture.generation) { m->emission = (Texture2D){0}; changed = true; }
        if (m->specular.id == texture.id && m->specular.generation == texture.generation) { m->specular = (Texture2D){0}; changed = true; }
        if (m->clearcoat.id == texture.id && m->clearcoat.generation == texture.generation) { m->clearcoat = (Texture2D){0}; changed = true; }
        if (!changed) continue;
        if (!VK.device) {
            /* CPU-only material: no descriptor set to update. */
        } else if (!VK.inFrame) {
            updateMaterialDescriptorSet(m);
        } else {
            VK.matDescDirty[i] = 1;
        }
        vcore_InvalidateCpuModels();
    }
}

void material_invalidate_all(void) {
    for (unsigned int i = 1; i < VLCRLIB_MAX_MATERIALS; ++i) {
        unsigned int next = material_generation(i) + 1;
        g_materialGenerations[i] = next ? next : 1;
        g_matTable[i] = (PBRMaterialEntry){0};
    }
}

void material_release_completed(uint32_t frameSlot, bool waitAll) {
    if (waitAll && VK.device) vkDeviceWaitIdle(VK.device);
    for (int i = 0; i < g_deferredMaterialReleaseCount;) {
        DeferredMaterialRelease *pending = &g_deferredMaterialReleases[i];
        if (!waitAll && pending->frameSlot != frameSlot) { ++i; continue; }
        if (VK.device && VK.descPoolPBR && pending->sets[0])
            vkFreeDescriptorSets(VK.device, VK.descPoolPBR, 1, &pending->sets[0]);
        if (VK.device && VK.descPoolSkin && pending->sets[1])
            vkFreeDescriptorSets(VK.device, VK.descPoolSkin, 1, &pending->sets[1]);
        g_deferredMaterialReleases[i] =
            g_deferredMaterialReleases[--g_deferredMaterialReleaseCount];
    }
}

void writeUBOPBR(Matrix mvp, Matrix model, const Material *mat) {
    if (VK.uboPBRSlot >= VK.uboPBRMaxSlots) {
        static int once = 0;
        if (!once) { fprintf(stderr, "[vlcrlib] UBO_PBR slots exhausted (%u)\n", VK.uboPBRMaxSlots); once = 1; }
        return;
    }
    uint32_t offset = VK.uboPBRSlot * VK.uboPBRStride;
    UBO_PBR *u = (UBO_PBR *)((char *)VK.uboPBRMapped + offset);

    vlcr_matrixToGpu(u->mvp, mvp);
    vlcr_matrixToGpu(u->model, model);
    vlcr_matrixToGpu(u->normalMat, MatrixNormalFromModel(model));
    u->cameraPos[0] = VK.cameraPos.x; u->cameraPos[1] = VK.cameraPos.y; u->cameraPos[2] = VK.cameraPos.z; u->cameraPos[3] = 1;
    u->lightDir[0] = -0.4f; u->lightDir[1] = -1.0f; u->lightDir[2] = -0.3f; u->lightDir[3] = 0;
    u->lightColor[0] = 1.0f; u->lightColor[1] = 0.97f; u->lightColor[2] = 0.92f; u->lightColor[3] = 1;
    /* Linear baseColorFactor (glTF / PBR). Fallback to Color/255 if factors unset. */
    u->baseColor[0] = mat->baseColorFactor[0];
    u->baseColor[1] = mat->baseColorFactor[1];
    u->baseColor[2] = mat->baseColorFactor[2];
    u->baseColor[3] = mat->baseColorFactor[3];
    u->metallicRoughnessAO[0] = mat->metallic;
    u->metallicRoughnessAO[1] = mat->roughness;
    u->metallicRoughnessAO[2] = mat->aoStrength;
    u->metallicRoughnessAO[3] = 0.0f;
    u->emissive[0] = mat->emissionColor.r / 255.0f;
    u->emissive[1] = mat->emissionColor.g / 255.0f;
    u->emissive[2] = mat->emissionColor.b / 255.0f;
    u->emissive[3] = mat->emissionStrength;
    u->textureFlags[0] = mat->albedoMap.id ? 1.0f : 0.0f;
    u->textureFlags[1] = mat->normalMap.id ? 1.0f : 0.0f;
    u->textureFlags[2] = mat->metallicRoughnessMap.id ? 1.0f : 0.0f;
    u->textureFlags[3] = mat->aoMap.id ? 1.0f : 0.0f;
    u->textureFlags2[0] = mat->emissionMap.id ? 1.0f : 0.0f;
    u->textureFlags2[1] = (float)VK.debugView;
    u->textureFlags2[2] = VK.exposure > 0.0f ? VK.exposure : 1.0f;
    u->textureFlags2[3] = VK.shadowSoftness > 0.0f ? VK.shadowSoftness : 1.5f;
    u->materialParams[0] = mat->ior > 0.0f ? mat->ior : 1.5f;
    u->materialParams[1] = mat->normalScale;
    u->materialParams[2] = VK.iblIntensity;
    u->materialParams[3] = mat->alphaCutoff > 0.0f ? mat->alphaCutoff : 0.5f;
    u->specularExt[0] = mat->specularColor[0];
    u->specularExt[1] = mat->specularColor[1];
    u->specularExt[2] = mat->specularColor[2];
    u->specularExt[3] = mat->specularFactor;
    u->khrClearcoat[0] = mat->clearcoatFactor;
    u->khrClearcoat[1] = mat->clearcoatRoughness;
    u->khrClearcoat[2] = mat->sheenRoughness;
    u->khrClearcoat[3] = mat->transmissionFactor;
    u->khrSheenColor[0] = mat->sheenColor[0];
    u->khrSheenColor[1] = mat->sheenColor[1];
    u->khrSheenColor[2] = mat->sheenColor[2];
    u->khrSheenColor[3] = mat->thicknessFactor;
    u->khrVolume[0] = mat->attenuationColor[0];
    u->khrVolume[1] = mat->attenuationColor[1];
    u->khrVolume[2] = mat->attenuationColor[2];
    u->khrVolume[3] = mat->attenuationDistance;
    u->khrAnisoIri[0] = mat->anisotropyStrength;
    u->khrAnisoIri[1] = mat->anisotropyRotation;
    u->khrAnisoIri[2] = mat->iridescenceFactor;
    u->khrAnisoIri[3] = mat->iridescenceIor;
    u->khrIriDisp[0] = mat->iridescenceThicknessMin;
    u->khrIriDisp[1] = mat->iridescenceThicknessMax;
    u->khrIriDisp[2] = isfinite(mat->emissiveStrengthKhr)
        ? fmaxf(mat->emissiveStrengthKhr, 0.0f) : 1.0f;
    {
        float flags = 0.0f;
        if (mat->unlit) flags += (float)VLCR_MATFLAG_UNLIT;
        if (mat->alphaMode == 1) flags += (float)VLCR_MATFLAG_ALPHAMASK;
        if (mat->alphaMode == 2) flags += (float)VLCR_MATFLAG_ALPHABLEND;
        if (mat->doubleSided) flags += (float)VLCR_MATFLAG_DOUBLESIDED;
        if (mat->dispersion > 0.0f) flags += (float)VLCR_MATFLAG_DISPERSION;
        if (mat->specularMap.id) flags += (float)VLCR_MATFLAG_SPECULAR_MAP;
        if (mat->clearcoatMap.id) flags += (float)VLCR_MATFLAG_CLEARCOAT_MAP;
        u->khrIriDisp[3] = flags;
    }
    u->fogParams[0] = VK.fogEnabled ? 1.0f : 0.0f;
    u->fogParams[1] = VK.fogStart;
    u->fogParams[2] = VK.fogEnd;
    u->fogParams[3] = VK.fogDensity;
    u->fogColor[0] = VK.fogColor.r / 255.0f;
    u->fogColor[1] = VK.fogColor.g / 255.0f;
    u->fogColor[2] = VK.fogColor.b / 255.0f;
    u->fogColor[3] = 1.0f;
    u->lightMeta[0] = (float)VK.lightArrayCount;
    u->lightMeta[1] = 0.0f;
    u->lightMeta[2] = 0.0f;
    u->lightMeta[3] = 0.0f;
    for (int c = 0; c < 4; ++c) {
        vlcr_matrixToGpu(u->cascadeVP[c], VK.shadowCascadeVP[c]);
        u->cascadeSplits[c] = VK.shadowCascadeSplits[c];
    }
    u->cascadeAtlas[0] = 0.5f;
    u->cascadeAtlas[1] = 0.5f;
    u->cascadeAtlas[2] = (float)VK.shadowCascadeCount;
    u->directionalLight[0] = VK.lightDirectionalDirection.x;
    u->directionalLight[1] = VK.lightDirectionalDirection.y;
    u->directionalLight[2] = VK.lightDirectionalDirection.z;
    u->directionalLight[3] = VK.lightDirectionalEnabled ? VK.lightDirectionalIntensity : 0.0f;
    u->directionalColor[0] = VK.lightDirectionalColor.r / 255.0f;
    u->directionalColor[1] = VK.lightDirectionalColor.g / 255.0f;
    u->directionalColor[2] = VK.lightDirectionalColor.b / 255.0f;
    u->pointLight[0] = VK.lightPointPosition.x;
    u->pointLight[1] = VK.lightPointPosition.y;
    u->pointLight[2] = VK.lightPointPosition.z;
    u->pointLight[3] = VK.lightPointEnabled ? VK.lightPointRange : 0.0f;
    u->pointColor[0] = VK.lightPointColor.r / 255.0f;
    u->pointColor[1] = VK.lightPointColor.g / 255.0f;
    u->pointColor[2] = VK.lightPointColor.b / 255.0f;
    u->pointColor[3] = VK.lightPointIntensity;
    u->spotLight[0] = VK.lightSpotPosition.x;
    u->spotLight[1] = VK.lightSpotPosition.y;
    u->spotLight[2] = VK.lightSpotPosition.z;
    u->spotLight[3] = VK.lightSpotEnabled ? VK.lightSpotRange : 0.0f;
    u->spotDirection[0] = VK.lightSpotDirection.x;
    u->spotDirection[1] = VK.lightSpotDirection.y;
    u->spotDirection[2] = VK.lightSpotDirection.z;
    u->spotColor[0] = VK.lightSpotColor.r / 255.0f;
    u->spotColor[1] = VK.lightSpotColor.g / 255.0f;
    u->spotColor[2] = VK.lightSpotColor.b / 255.0f;
    u->spotColor[3] = VK.lightSpotIntensity;
    u->spotParams[0] = cosf(VK.lightSpotInnerCone * 0.5f);
    u->spotParams[1] = cosf(VK.lightSpotOuterCone * 0.5f);
    u->spotParams[2] = (VK.ssaoEnabled && VK.ssaoIntensity > 0.0f) ? VK.ssaoIntensity : 0.0f;
    u->spotParams[3] = VK.ssaoRadius > 0.0f ? VK.ssaoRadius : 0.35f;
    u->ambientLight[0] = VK.lightAmbientColor.r / 255.0f;
    u->ambientLight[1] = VK.lightAmbientColor.g / 255.0f;
    u->ambientLight[2] = VK.lightAmbientColor.b / 255.0f;
    u->ambientLight[3] = VK.lightAmbientEnabled ? VK.lightAmbientIntensity : 0.0f;
    /* Area light */
    u->areaLightPos[0] = VK.lightAreaPosition.x;
    u->areaLightPos[1] = VK.lightAreaPosition.y;
    u->areaLightPos[2] = VK.lightAreaPosition.z;
    u->areaLightPos[3] = VK.lightAreaEnabled ? 1.0f : 0.0f;
    u->areaLightColor[0] = VK.lightAreaColor.r / 255.0f;
    u->areaLightColor[1] = VK.lightAreaColor.g / 255.0f;
    u->areaLightColor[2] = VK.lightAreaColor.b / 255.0f;
    u->areaLightColor[3] = VK.lightAreaIntensity;
    u->areaLightSize[0] = VK.lightAreaWidth;
    u->areaLightSize[1] = VK.lightAreaHeight;
    u->areaLightSize[2] = VK.bloomThreshold;
    u->areaLightSize[3] = VK.bloomIntensity;
    u->areaLightNormal[0] = VK.lightAreaNormal.x;
    u->areaLightNormal[1] = VK.lightAreaNormal.y;
    u->areaLightNormal[2] = VK.lightAreaNormal.z;
    u->areaLightNormal[3] = 0;
    vlcr_matrixToGpu(u->lightViewProj, VK.shadowLightVP);
    u->shadowParams[0] = VK.shadowsEnabled ? 1.0f : 0.0f;
    u->shadowParams[1] = VK.shadowBias;
    u->shadowParams[2] = VK.shadowStrength;
    /* .w = map size for correct PCF texel; soft radius via shadowSoftness encoded if < 8 else mapSize */
    u->shadowParams[3] = (float)(VK.shadowMapSize > 0 ? VK.shadowMapSize : 2048);
    vlcr_matrixToGpu(u->spotLightViewProj, VK.spotShadowLightVP);
    u->shadowParamsSpot[0] = VK.spotShadowsEnabled ? 1.0f : 0.0f;
    u->shadowParamsSpot[1] = VK.spotShadowBias;
    u->shadowParamsSpot[2] = VK.spotShadowStrength;
    u->shadowParamsSpot[3] = (float)(VK.spotShadowMapSize > 0 ? VK.spotShadowMapSize : 2048);
    u->shadowParamsPoint[0] = VK.pointShadowsEnabled ? 1.0f : 0.0f;
    u->shadowParamsPoint[1] = VK.pointShadowBias;
    u->shadowParamsPoint[2] = VK.pointShadowStrength;
    u->shadowParamsPoint[3] = VK.pointShadowFar > 0.f ? VK.pointShadowFar
                            : (VK.lightPointRange > 0.f ? VK.lightPointRange : 25.f);
    VK.uboPBRSlot++;
}

void ibl_refresh_all_material_descriptors(void) {
    for (int i = 1; i <= g_matCount; ++i) {
        if (g_matTable[i].descSet)
            updateMaterialDescriptorSet(&g_matTable[i]);
    }
}


void updateMaterialDescriptorSet(PBRMaterialEntry *m) {
    if (!m) return;
    TextureEntry *white = &g_textureTable[VK.whiteTexture.id];

    /* Material structs are copied by value. Resolve IDs only when the slot is
       still live; an unloaded/stale ID must bind the fallback texture. */
    TextureEntry *entries[7] = { NULL };
    Texture2D maps[7] = { m->albedo, m->normal, m->mr, m->ao,
                          m->emission, m->specular, m->clearcoat };
    for (int i = 0; i < 7; ++i) {
        if (maps[i].id > 0 && maps[i].id < VLCRLIB_MAX_TEXTURES &&
            maps[i].generation != 0 &&
            maps[i].generation == g_textureTable[maps[i].id].generation &&
            g_textureTable[maps[i].id].view)
            entries[i] = &g_textureTable[maps[i].id];
    }

    VkImageView views[5] = {
        entries[0] ? entries[0]->view : white->view,
        entries[1] ? entries[1]->view : white->view,
        entries[2] ? entries[2]->view : white->view,
        entries[3] ? entries[3]->view : white->view,
        entries[4] ? entries[4]->view : white->view,
    };
    VkSampler samps[5] = {
        entries[0] ? entries[0]->samp : white->samp,
        entries[1] ? entries[1]->samp : white->samp,
        entries[2] ? entries[2]->samp : white->samp,
        entries[3] ? entries[3]->samp : white->samp,
        entries[4] ? entries[4]->samp : white->samp,
    };

    VkDescriptorSet targets[2] = { m->descSet, m->skinDescSet };
    for (int t = 0; t < 2; ++t) {
        if (targets[t] == VK_NULL_HANDLE) continue;
        int bindingCount = 17;
        VkWriteDescriptorSet writes[17] = {0};
        VkDescriptorImageInfo imgs[12] = {0};
        /* IBL image info must stay alive until vkUpdateDescriptorSets below:
           writes[7..9].pImageInfo point into it. This array used to live in an
           inner block, leaving those three pointers dangling -> 0xC0000005
           inside the ICD (nvoglv64.dll) the moment a material was created. */
        VkDescriptorImageInfo iblImgs[3] = {0};

        VkDescriptorBufferInfo bi = { VK.uboPBR, 0, sizeof(UBO_PBR) };
        VkDescriptorBufferInfo lightBi = { VK.lightBuffer, 0, VK.lightBufferSize };
        /* Must stay alive until vkUpdateDescriptorSets: this used to be a
           block-local and Release optimization clobbered it -> ICD crash. */
        VkDescriptorBufferInfo bbi = { VK.boneBuffer, 0, VK.boneBufferSize };
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = targets[t];
        writes[0].dstBinding      = 0;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo     = &bi;

        for (int i = 0; i < 5; ++i) {
            imgs[i].imageView   = views[i];
            imgs[i].sampler     = samps[i];
            imgs[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            writes[i+1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i+1].dstSet          = targets[t];
            writes[i+1].dstBinding      = (uint32_t)(i + 1);
            writes[i+1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i+1].descriptorCount = 1;
            writes[i+1].pImageInfo      = &imgs[i];
        }

        /* binding 6: dir shadow — bind white unless the pass actually runs,
           otherwise the descriptor references an image still in UNDEFINED. */
        bool haveDirShadow = VK.shadowsEnabled && VK.shadowView != VK_NULL_HANDLE &&
                             VK.shadowSampler != VK_NULL_HANDLE;
        imgs[5].imageView = haveDirShadow ? VK.shadowView : white->view;
        imgs[5].sampler   = haveDirShadow ? VK.shadowSampler : white->samp;
        imgs[5].imageLayout = haveDirShadow
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
            : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet = targets[t];
        writes[6].dstBinding = 6;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[6].descriptorCount = 1;
        writes[6].pImageInfo = &imgs[5];

        /* 7-9 IBL */
        {
            VkImageView irrV = VK.iblIrrView ? VK.iblIrrView : white->view;
            VkImageView preV = VK.iblPreView ? VK.iblPreView : white->view;
            VkImageView lutV = VK.iblBrdfView ? VK.iblBrdfView : white->view;
            VkSampler cubeS = VK.iblCubeSampler ? VK.iblCubeSampler : white->samp;
            VkSampler lutS  = VK.iblLutSampler ? VK.iblLutSampler : white->samp;
            iblImgs[0].imageView = irrV; iblImgs[0].sampler = cubeS;
            iblImgs[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            iblImgs[1].imageView = preV; iblImgs[1].sampler = cubeS;
            iblImgs[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            iblImgs[2].imageView = lutV; iblImgs[2].sampler = lutS;
            iblImgs[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            for (int k = 0; k < 3; ++k) {
                writes[7 + k].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[7 + k].dstSet = targets[t];
                writes[7 + k].dstBinding = (uint32_t)(7 + k);
                writes[7 + k].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[7 + k].descriptorCount = 1;
                writes[7 + k].pImageInfo = &iblImgs[k];
            }
        }

        /* 10 spot shadow */
        bool haveSpotShadow = VK.spotShadowsEnabled && VK.spotShadowView != VK_NULL_HANDLE &&
                              VK.spotShadowSampler != VK_NULL_HANDLE;
        imgs[6].imageView = haveSpotShadow ? VK.spotShadowView : white->view;
        imgs[6].sampler   = haveSpotShadow ? VK.spotShadowSampler : white->samp;
        imgs[6].imageLayout = haveSpotShadow
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
            : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        writes[10].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[10].dstSet = targets[t];
        writes[10].dstBinding = 10;
        writes[10].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[10].descriptorCount = 1;
        writes[10].pImageInfo = &imgs[6];

        /* 11 point shadow cube — the binding is a samplerCube, so the
           fallback must be a cube view even when point shadows are off. */
        bool havePointShadow = VK.pointShadowsEnabled && VK.pointShadowView != VK_NULL_HANDLE &&
                               VK.pointShadowSampler != VK_NULL_HANDLE;
        imgs[7].imageView = havePointShadow ? VK.pointShadowView
                                            : (VK.iblPreView ? VK.iblPreView : white->view);
        imgs[7].sampler   = havePointShadow ? VK.pointShadowSampler
                                            : (VK.iblCubeSampler ? VK.iblCubeSampler : white->samp);
        imgs[7].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        writes[11].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[11].dstSet = targets[t];
        writes[11].dstBinding = 11;
        writes[11].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[11].descriptorCount = 1;
        writes[11].pImageInfo = &imgs[7];

        if (t == 1) {
            writes[12].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[12].dstSet          = targets[t];
            writes[12].dstBinding      = 12;
            writes[12].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[12].descriptorCount = 1;
            writes[12].pBufferInfo     = &bbi;
        } else {
            imgs[8].imageView = white->view;
            imgs[8].sampler = white->samp;
            imgs[8].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            writes[12].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[12].dstSet = targets[t];
            writes[12].dstBinding = 12;
            writes[12].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[12].descriptorCount = 1;
            writes[12].pImageInfo = &imgs[8];
        }

        /* 13 specular, 14 clearcoat */
        {
            imgs[9].imageView = entries[5] ? entries[5]->view : white->view;
            imgs[9].sampler   = entries[5] ? entries[5]->samp : white->samp;
            imgs[9].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            writes[13].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[13].dstSet = targets[t];
            writes[13].dstBinding = 13;
            writes[13].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[13].descriptorCount = 1;
            writes[13].pImageInfo = &imgs[9];
        }
        {
            imgs[10].imageView = entries[6] ? entries[6]->view : white->view;
            imgs[10].sampler   = entries[6] ? entries[6]->samp : white->samp;
            imgs[10].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            writes[14].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[14].dstSet = targets[t];
            writes[14].dstBinding = 14;
            writes[14].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[14].descriptorCount = 1;
            writes[14].pImageInfo = &imgs[10];
        }
        /* binding 15: half-res SSAO (white if not ready) */
        {
            imgs[11].imageView = VK.ssaoReady ? VK.ssaoView : white->view;
            imgs[11].sampler   = VK.ssaoReady ? VK.ssaoSampler : white->samp;
            imgs[11].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            writes[15].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[15].dstSet = targets[t];
            writes[15].dstBinding = 15;
            writes[15].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[15].descriptorCount = 1;
            writes[15].pImageInfo = &imgs[11];
        }
        /* binding 16: optional explicit light array (SSBO) */
        {
            writes[16].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[16].dstSet = targets[t];
            writes[16].dstBinding = 16;
            writes[16].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[16].descriptorCount = 1;
            writes[16].pBufferInfo = &lightBi;
        }
        vkUpdateDescriptorSets(VK.device, (uint32_t)bindingCount, writes, 0, NULL);
    }
}

Material LoadMaterialDefault(void) {
    Material mat = {0};
    int idx = 0;
    for (int i = 1; i < VLCRLIB_MAX_MATERIALS; ++i) {
        if (!g_matTable[i].descSet && !g_matTable[i].skinDescSet &&
            g_matTable[i].generation == 0) {
            idx = i;
            if (i > g_matCount) g_matCount = i;
            break;
        }
    }
    if (!idx) return mat;
    unsigned int generation = material_generation((unsigned int)idx);
    PBRMaterialEntry *e = &g_matTable[idx];

    /* CPU mode has no descriptor sets; the Material struct itself carries
       everything vgraphics_BuildCpuModel() needs. */
    if (VK.device) {
        VkDescriptorSetAllocateInfo ai = {0};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = VK.descPoolPBR;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &VK.dslPBR;
        if (vkAllocateDescriptorSets(VK.device, &ai, &e->descSet) != VK_SUCCESS) {
            *e = (PBRMaterialEntry){0}; return mat;
        }

        VkDescriptorSetAllocateInfo ai2 = {0};
        ai2.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai2.descriptorPool = VK.descPoolSkin;
        ai2.descriptorSetCount = 1;
        ai2.pSetLayouts = &VK.dslSkin;
        if (vkAllocateDescriptorSets(VK.device, &ai2, &e->skinDescSet) != VK_SUCCESS) {
            vkFreeDescriptorSets(VK.device, VK.descPoolPBR, 1, &e->descSet);
            *e = (PBRMaterialEntry){0}; return mat;
        }
    }

    e->generation = generation;
    if (VK.device) updateMaterialDescriptorSet(e);
    mat.id = (unsigned int)idx;
    mat.generation = generation;
    mat.baseColor = (Color){ 255, 255, 255, 255 };
    mat.baseColorFactor[0] = 1.0f;
    mat.baseColorFactor[1] = 1.0f;
    mat.baseColorFactor[2] = 1.0f;
    mat.baseColorFactor[3] = 1.0f;
    mat.metallic  = 0.0f;
    mat.roughness = 1.0f;
    mat.ior = 1.5f;
    mat.normalScale = 1.0f;
    mat.aoStrength = 1.0f;
    mat.emissionColor = (Color){ 0, 0, 0, 255 };
    mat.emissionStrength = 0.0f;
    mat.specularFactor = 1.0f;
    mat.specularColor[0] = 1.0f;
    mat.specularColor[1] = 1.0f;
    mat.specularColor[2] = 1.0f;
    mat.emissiveStrengthKhr = 1.0f;
    mat.unlit = 0;
    mat.clearcoatFactor = 0.0f;
    mat.clearcoatRoughness = 0.0f;
    mat.sheenColor[0] = mat.sheenColor[1] = mat.sheenColor[2] = 0.0f;
    mat.sheenRoughness = 0.0f;
    mat.transmissionFactor = 0.0f;
    mat.thicknessFactor = 0.0f;
    mat.attenuationDistance = 0.0f; /* 0 = infinite */
    mat.attenuationColor[0] = mat.attenuationColor[1] = mat.attenuationColor[2] = 1.0f;
    mat.anisotropyStrength = 0.0f;
    mat.anisotropyRotation = 0.0f;
    mat.iridescenceFactor = 0.0f;
    mat.iridescenceIor = 1.3f;
    mat.iridescenceThicknessMin = 100.0f;
    mat.iridescenceThicknessMax = 400.0f;
    mat.dispersion = 0.0f;
    mat.alphaMode = 0;
    mat.alphaCutoff = 0.5f;
    mat.doubleSided = 0;
    return mat;
}

void SetMaterialTexture(Material *material, int mapType, Texture2D tex) {
    if (!material_handle_valid(material)) return;
    /* A nonzero ID is not proof that a texture is still alive. CPU-only
       textures have cpuPixels but no image view. */
    if (tex.id >= VLCRLIB_MAX_TEXTURES ||
        (tex.id != 0 && (!tex.generation ||
         tex.generation != g_textureTable[tex.id].generation ||
         (!g_textureTable[tex.id].view &&
          !g_textureTable[tex.id].cpuPixels))))
        tex = (Texture2D){0};
    PBRMaterialEntry *e = &g_matTable[material->id];
    switch (mapType) {
        case MATERIAL_MAP_ALBEDO:            material->albedoMap = tex; e->albedo = tex; break;
        case MATERIAL_MAP_NORMAL:            material->normalMap = tex; e->normal = tex; break;
        case MATERIAL_MAP_METALLICROUGHNESS: material->metallicRoughnessMap = tex; e->mr = tex; break;
        case MATERIAL_MAP_AO:                material->aoMap = tex; e->ao = tex; break;
        case MATERIAL_MAP_EMISSION:          material->emissionMap = tex; e->emission = tex; break;
        case MATERIAL_MAP_SPECULAR:          material->specularMap = tex; e->specular = tex; break;
        case MATERIAL_MAP_CLEARCOAT:         material->clearcoatMap = tex; e->clearcoat = tex; break;
        default: return;
    }
    vcore_InvalidateCpuModels();
    if (!VK.device) return;
    /* Sprint B: defer descriptor write if mid-frame */
    if (!VK.inFrame) {
        updateMaterialDescriptorSet(e);
        if (material->id < VLCRLIB_MAX_MATERIALS)
            VK.matDescDirty[material->id] = 0;
    } else {
        VK.matDescDirty[material->id] = 1;
    }
}

void SetMaterialColor(Material *material, Color color) {
    if (!material) return;
    material->baseColor = color;
    /* Treat API Color as linear 0..1 for PBR factor (simple path). */
    material->baseColorFactor[0] = color.r / 255.0f;
    material->baseColorFactor[1] = color.g / 255.0f;
    material->baseColorFactor[2] = color.b / 255.0f;
    material->baseColorFactor[3] = color.a / 255.0f;
    vcore_InvalidateCpuModels();
}

void SetMaterialBaseColorFactor(Material *material, float r, float g, float b, float a) {
    if (!material) return;
    material->baseColorFactor[0] = r < 0.f ? 0.f : r;
    material->baseColorFactor[1] = g < 0.f ? 0.f : g;
    material->baseColorFactor[2] = b < 0.f ? 0.f : b;
    material->baseColorFactor[3] = a < 0.f ? 0.f : a;
    material->baseColor = (Color){
        (unsigned char)(material->baseColorFactor[0] * 255.f + 0.5f),
        (unsigned char)(material->baseColorFactor[1] * 255.f + 0.5f),
        (unsigned char)(material->baseColorFactor[2] * 255.f + 0.5f),
        (unsigned char)(material->baseColorFactor[3] * 255.f + 0.5f)
    };
    vcore_InvalidateCpuModels();
}

void SetMaterialPBR(Material *material, float metallic, float roughness) {
    if (!material) return;
    material->metallic = metallic;
    material->roughness = roughness;
    vcore_InvalidateCpuModels();
}

void SetMaterialIOR(Material *material, float ior) {
    if (!material) return;
    material->ior = ior < 1.0f ? 1.0f : ior;
}

void SetMaterialEmission(Material *material, Color color, float strength) {
    if (!material) return;
    material->emissionColor = color;
    material->emissionStrength = strength < 0.0f ? 0.0f : strength;
    vcore_InvalidateCpuModels();
}

void SetMaterialSpecular(Material *material, float specularFactor,
                         float specularColorR, float specularColorG, float specularColorB) {
    if (!material) return;
    material->specularFactor = specularFactor < 0.0f ? 0.0f : specularFactor;
    material->specularColor[0] = specularColorR < 0.0f ? 0.0f : specularColorR;
    material->specularColor[1] = specularColorG < 0.0f ? 0.0f : specularColorG;
    material->specularColor[2] = specularColorB < 0.0f ? 0.0f : specularColorB;
}

void UnloadMaterial(Material material) {
    if (!material_handle_valid(&material)) return;
    if (VK.inFrame && g_deferredMaterialReleaseCount >= VLCRLIB_MAX_MATERIALS)
        return;
    PBRMaterialEntry *e = &g_matTable[material.id];
    if (!e->descSet && !e->skinDescSet) {
        /* CPU-only material: no descriptors to free. */
        *e = (PBRMaterialEntry){0};
        unsigned int next = material_generation(material.id) + 1;
        g_materialGenerations[material.id] = next ? next : 1;
        vcore_InvalidateCpuModels();
        return;
    }
    if (VK.device) vkDeviceWaitIdle(VK.device);
    if (VK.inFrame && g_deferredMaterialReleaseCount < VLCRLIB_MAX_MATERIALS) {
        g_deferredMaterialReleases[g_deferredMaterialReleaseCount++] =
            (DeferredMaterialRelease){ { e->descSet, e->skinDescSet }, VK.currentFrame };
    } else {
        if (VK.inFrame) material_release_completed(0, true);
        if (VK.device && VK.descPoolPBR && e->descSet)
            vkFreeDescriptorSets(VK.device, VK.descPoolPBR, 1, &e->descSet);
        if (VK.device && VK.descPoolSkin && e->skinDescSet)
            vkFreeDescriptorSets(VK.device, VK.descPoolSkin, 1, &e->skinDescSet);
    }
    *e = (PBRMaterialEntry){0};
    unsigned int next = material_generation(material.id) + 1;
    g_materialGenerations[material.id] = next ? next : 1;
    vcore_InvalidateCpuModels();
}


