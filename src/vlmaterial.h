#ifndef VLCRLIB_VLMATERIAL_H
#define VLCRLIB_VLMATERIAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "vgraphics.h"

typedef Material VLPBRMaterial;

#define VLMATERIAL_MAP_ALBEDO             MATERIAL_MAP_ALBEDO
#define VLMATERIAL_MAP_NORMAL             MATERIAL_MAP_NORMAL
#define VLMATERIAL_MAP_METALLIC_ROUGHNESS MATERIAL_MAP_METALLICROUGHNESS
#define VLMATERIAL_MAP_AO                 MATERIAL_MAP_AO
#define VLMATERIAL_MAP_EMISSION           MATERIAL_MAP_EMISSION

static inline VLPBRMaterial VLPBRMaterialCreate(void) {
    return LoadMaterialDefault();
}

static inline void VLPBRMaterialSetBaseColor(VLPBRMaterial *material, Color color) {
    SetMaterialColor(material, color);
}

static inline void VLPBRMaterialSetAlbedoMap(VLPBRMaterial *material, Texture2D texture) {
    SetMaterialTexture(material, VLMATERIAL_MAP_ALBEDO, texture);
}

static inline void VLPBRMaterialSetNormalMap(VLPBRMaterial *material, Texture2D texture) {
    SetMaterialTexture(material, VLMATERIAL_MAP_NORMAL, texture);
}

static inline void VLPBRMaterialSetMetallicRoughness(VLPBRMaterial *material,
                                                      float metallic,
                                                      float roughness,
                                                      Texture2D texture) {
    if (!material) return;
    SetMaterialPBR(material, metallic, roughness);
    SetMaterialTexture(material, VLMATERIAL_MAP_METALLIC_ROUGHNESS, texture);
}

static inline void VLPBRMaterialSetAOMap(VLPBRMaterial *material, Texture2D texture) {
    SetMaterialTexture(material, VLMATERIAL_MAP_AO, texture);
}

static inline void VLPBRMaterialSetIOR(VLPBRMaterial *material, float ior) {
    SetMaterialIOR(material, ior);
}

static inline void VLPBRMaterialSetSpecular(VLPBRMaterial *material,
                                            float specularFactor,
                                            float r, float g, float b) {
    SetMaterialSpecular(material, specularFactor, r, g, b);
}

static inline void VLPBRMaterialSetEmission(VLPBRMaterial *material,
                                             Color color,
                                             float strength,
                                             Texture2D texture) {
    SetMaterialEmission(material, color, strength);
    SetMaterialTexture(material, VLMATERIAL_MAP_EMISSION, texture);
}

static inline void VLPBRMaterialDestroy(VLPBRMaterial material) {
    UnloadMaterial(material);
}

#ifdef __cplusplus
}
#endif
#endif /* VLCRLIB_VLMATERIAL_H */
