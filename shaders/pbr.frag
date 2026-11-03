#version 450

/*
 * glTF 2.0 metallic-roughness PBR fragment shader.
 *
 * Reference implementation notes (studied):
 *   - Khronos glTF 2.0 spec, Appendix B (BRDF implementation)
 *   - Khronos glTF-Sample-Viewer / glTF-WebGL-PBR (pbr-frag.glsl)
 *   - Sascha Willems, Vulkan-glTF-PBR (material_pbr.frag, env map generation)
 *   - NVIDIA vk_gltf_renderer (gltf_material_eval.h.slang, env_bake.slang)
 *
 * Kept deliberately close to the engine's UBO layout so no C-side
 * descriptor/UBO changes are required.
 */

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in vec4 vTangent;
layout(location = 4) in vec4 vColor;

layout(set = 0, binding = 0) uniform UBO {
    mat4 mvp;
    mat4 model;
    mat4 normalMat;
    vec4 cameraPos;
    vec4 lightDir;
    vec4 lightColor;
    vec4 baseColor;
    vec4 metallicRoughnessAO;
    vec4 emissive;
    vec4 textureFlags;
    vec4 textureFlags2;
    vec4 materialParams;
    vec4 directionalLight;
    vec4 directionalColor;
    vec4 pointLight;
    vec4 pointColor;
    vec4 spotLight;
    vec4 spotDirection;
    vec4 spotColor;
    vec4 spotParams;
    vec4 ambientLight;
    vec4 areaLightPos;
    vec4 areaLightColor;
    vec4 areaLightSize;
    vec4 areaLightNormal;
    mat4 lightViewProj;
    vec4 shadowParams;
    mat4 spotLightViewProj;
    vec4 shadowParamsSpot;
    vec4 shadowParamsPoint;
    vec4 specularExt;
    vec4 khrClearcoat;
    vec4 khrSheenColor;
    vec4 khrVolume;
    vec4 khrAnisoIri;
    vec4 khrIriDisp;
    vec4 fogParams;
    vec4 fogColor;
    vec4 lightMeta;       /* x = explicit light-array count */
    mat4 cascadeVP[4];    /* directional shadow cascades (atlas quadrants) */
    vec4 cascadeSplits;   /* camera distance at each cascade end */
    vec4 cascadeAtlas;    /* xy = atlas scale, z = cascade count */
} u;

layout(set = 0, binding = 1)  uniform sampler2D albedoTex;
layout(set = 0, binding = 2)  uniform sampler2D normalTex;
layout(set = 0, binding = 3)  uniform sampler2D mrTex;
layout(set = 0, binding = 4)  uniform sampler2D aoTex;
layout(set = 0, binding = 5)  uniform sampler2D emTex;
layout(set = 0, binding = 6)  uniform sampler2D shadowMap;
layout(set = 0, binding = 7)  uniform samplerCube irradianceMap;
layout(set = 0, binding = 8)  uniform samplerCube prefilteredMap;
layout(set = 0, binding = 9)  uniform sampler2D brdfLUT;
layout(set = 0, binding = 10) uniform sampler2D spotShadowMap;
layout(set = 0, binding = 11) uniform samplerCube pointShadowCube;
layout(set = 0, binding = 13) uniform sampler2D specularTex;
layout(set = 0, binding = 14) uniform sampler2D clearcoatTex;
layout(set = 0, binding = 15) uniform sampler2D ssaoTex;

/* Optional explicit light array (SSBO). Used when u.lightMeta.x > 0. */
struct GpuLight {
    vec4 position;   /* xyz, w = range */
    vec4 direction;  /* xyz, w = innerConeCos */
    vec4 color;      /* rgb = color*intensity, w = outerConeCos */
    vec4 params;     /* x=type, y=enabled, z=shadowIndex, w=areaWidth */
    vec4 extra;      /* x=areaHeight */
};
layout(std430, set = 0, binding = 16) readonly buffer GpuLightBuffer {
    GpuLight gLights[];
};

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265358979323846;
const float MIN_ROUGHNESS = 0.045;

/* Material flag bits — keep in sync with vgraphics_ubo.h. */
const int MATFLAG_UNLIT        = 1;
const int MATFLAG_ALPHAMASK    = 2;
const int MATFLAG_ALPHABLEND   = 4;
const int MATFLAG_DOUBLESIDED  = 8;
const int MATFLAG_DISPERSION   = 16;
const int MATFLAG_SPECULAR_MAP = 32;
const int MATFLAG_CLEARCOAT_MAP = 64;

/* ------------------------------------------------------------------ */
/*  BRDF terms (glTF 2.0 Appendix B)                                  */
/* ------------------------------------------------------------------ */

/* Trowbridge-Reitz / GGX distribution. alpha = perceptualRoughness^2. */
float NDF_GGX(float NdotH, float alpha) {
    float a2 = alpha * alpha;
    float d  = (NdotH * a2 - NdotH) * NdotH + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

/* Anisotropic GGX (Burley / glTF KHR_materials_anisotropy). */
float NDF_GGX_Aniso(float NdotH, float TdotH, float BdotH, float at, float ab) {
    float a2 = at * ab;
    vec3 v = vec3(ab * TdotH, at * BdotH, a2 * NdotH);
    float v2 = max(dot(v, v), 1e-7);
    float w2 = a2 / v2;
    return a2 * w2 * w2 / PI;
}

/* Height-correlated Smith-Joint GGX visibility: G / (4 NdotL NdotV). */
float V_SmithGGXCorrelated(float NdotL, float NdotV, float alpha) {
    float a2 = alpha * alpha;
    float gv = NdotL * sqrt(max(NdotV * NdotV * (1.0 - a2) + a2, 0.0));
    float gl = NdotV * sqrt(max(NdotL * NdotL * (1.0 - a2) + a2, 0.0));
    return 0.5 / max(gv + gl, 1e-6);
}

/* Schlick Fresnel with F90 = 1 (correct for metals and dielectrics in glTF). */
vec3 F_Schlick(vec3 F0, float u) {
    float f = pow(clamp(1.0 - u, 0.0, 1.0), 5.0);
    return F0 + (vec3(1.0) - F0) * f;
}

/* Karis' analytic split-sum approximation, used when no LUT is bound. */
vec2 envBRDFApprox(float NdotV, float roughness) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

/* KHR_materials_sheen: Charlie distribution + Ashikhmin visibility. */
float D_Charlie(float NdotH, float roughness) {
    float invR = 1.0 / max(roughness, 0.07);
    float cos2 = NdotH * NdotH;
    float sin2 = max(1.0 - cos2, 0.0);
    return (2.0 + invR) * pow(sin2, invR * 0.5) / (2.0 * PI);
}

float V_Ashikhmin(float NdotL, float NdotV) {
    return 1.0 / max(4.0 * (NdotL + NdotV - NdotL * NdotV), 1e-4);
}

/* Directional / punctual direct lighting. */
vec3 evaluateLight(vec3 N, vec3 V, vec3 T, vec3 B, float aniso,
                   vec3 albedo, float metallic,
                   float roughness, vec3 F0, vec3 L, vec3 radiance) {
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    if (NdotL <= 0.0 || NdotV <= 0.0) return vec3(0.0);

    vec3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    float alpha = roughness * roughness;

    float D;
    float visAlpha = alpha;
    if (aniso > 0.001) {
        /* Exaggerated tangent/bitangent roughness split: the physical
           sqrt(1 +/- aniso) range is almost invisible on small spheres. */
        float at = max(alpha * max(1.0 - aniso * 0.98, 0.02), 1e-4);
        float ab = max(alpha * (1.0 + aniso * 4.0), 1e-4);
        D = NDF_GGX_Aniso(NdotH, dot(T, H), dot(B, H), at, ab);
        visAlpha = sqrt(at * ab);
    } else {
        D = NDF_GGX(NdotH, alpha);
    }
    float Vis = V_SmithGGXCorrelated(NdotL, NdotV, visAlpha);
    vec3 F = F_Schlick(F0, VdotH);

    vec3 specular = D * Vis * F;
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * radiance * NdotL;
}

/* ------------------------------------------------------------------ */
/*  Shadows                                                           */
/* ------------------------------------------------------------------ */

/* 2D shadow map PCF. Soft radius comes from textureFlags2.w. */
float shadowPCF2D(sampler2D smap, mat4 lightVP, vec3 worldPos,
                  vec3 surfaceNormal, vec3 lightDirection,
                  float enabled, float bias, float strength) {
    if (enabled < 0.5) return 1.0;

    vec4 sc = lightVP * vec4(worldPos, 1.0);
    if (sc.w <= 0.0) return 1.0;
    sc.xyz /= sc.w;

    vec2 uv = sc.xy * 0.5 + 0.5;
    /* Vulkan ZO depth: [0,1] already, do not remap. */
    float depth = sc.z;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 ||
        depth < 0.0 || depth > 1.0)
        return 1.0;

    ivec2 size = textureSize(smap, 0);
    if (size.x <= 0 || size.y <= 0) return 1.0;

    ivec2 center = ivec2(uv * vec2(size));
    float nDotL = clamp(dot(normalize(surfaceNormal), normalize(lightDirection)), 0.0, 1.0);
    float adaptiveBias = bias * (1.0 + 2.0 * (1.0 - nDotL));

    int r = int(clamp(u.textureFlags2.w, 1.0, 3.0) + 0.5);
    float shadow = 0.0;
    int count = 0;
    for (int x = -r; x <= r; ++x) {
        for (int y = -r; y <= r; ++y) {
            ivec2 coord = center + ivec2(x, y);
            if (coord.x < 0 || coord.y < 0 || coord.x >= size.x || coord.y >= size.y) {
                shadow += 1.0;
            } else {
                float closest = texelFetch(smap, coord, 0).r;
                shadow += (depth - adaptiveBias > closest) ? 0.0 : 1.0;
            }
            count++;
        }
    }
    shadow /= float(max(count, 1));
    return mix(1.0, shadow, clamp(strength, 0.0, 1.0));
}

/* Directional cascades are rendered into four atlas quadrants. */
float shadowCascadePCF(mat4 lightVP, vec2 atlasOffset, vec3 worldPos,
                       vec3 surfaceNormal, vec3 lightDirection,
                       float bias, float strength) {
    vec4 sc = lightVP * vec4(worldPos, 1.0);
    if (sc.w <= 0.0) return 1.0;
    sc.xyz /= sc.w;
    vec2 uv = (sc.xy * 0.5 + 0.5) * u.cascadeAtlas.xy + atlasOffset;
    float depth = sc.z;
    ivec2 size = textureSize(shadowMap, 0);
    if (size.x <= 0 || size.y <= 0) return 1.0;
    ivec2 qOrigin = ivec2(atlasOffset * vec2(size));
    ivec2 qSize = ivec2(size) / 2;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 ||
        depth < 0.0 || depth > 1.0)
        return 1.0;
    ivec2 center = ivec2(uv * vec2(size));
    float nDotL = clamp(dot(normalize(surfaceNormal), normalize(lightDirection)), 0.0, 1.0);
    float adaptiveBias = bias * (1.0 + 2.0 * (1.0 - nDotL));
    int r = int(clamp(u.textureFlags2.w, 1.0, 3.0) + 0.5);
    float shadow = 0.0;
    int count = 0;
    for (int x = -r; x <= r; ++x) {
        for (int y = -r; y <= r; ++y) {
            ivec2 coord = center + ivec2(x, y);
            if (coord.x < qOrigin.x || coord.y < qOrigin.y ||
                coord.x >= qOrigin.x + qSize.x || coord.y >= qOrigin.y + qSize.y) {
                shadow += 1.0;
            } else {
                float closest = texelFetch(shadowMap, coord, 0).r;
                shadow += (depth - adaptiveBias > closest) ? 0.0 : 1.0;
            }
            count++;
        }
    }
    shadow /= float(max(count, 1));
    return mix(1.0, shadow, clamp(strength, 0.0, 1.0));
}

float shadowDirectional(vec3 worldPos, vec3 surfaceNormal, vec3 lightDirection) {
    if (u.shadowParams.x < 0.5) return 1.0;
    if (u.cascadeAtlas.z < 0.5)
        return shadowPCF2D(shadowMap, u.lightViewProj, worldPos, surfaceNormal,
                           lightDirection, u.shadowParams.x, u.shadowParams.y,
                           u.shadowParams.z);

    float dist = length(worldPos - u.cameraPos.xyz);
    int c = 0;
    if (dist > u.cascadeSplits.x) c = 1;
    if (dist > u.cascadeSplits.y) c = 2;
    if (dist > u.cascadeSplits.z) c = 3;
    vec2 offset = vec2(float(c & 1) * 0.5, float(c >> 1) * 0.5);
    mat4 vp = u.cascadeVP[0];
    if (c == 1) vp = u.cascadeVP[1];
    else if (c == 2) vp = u.cascadeVP[2];
    else if (c == 3) vp = u.cascadeVP[3];
    return shadowCascadePCF(vp, offset, worldPos, surfaceNormal, lightDirection,
                            u.shadowParams.y, u.shadowParams.z);
}

/* Omnidirectional point shadow (linear depth packed in a cube). */
float shadowPointPCF(vec3 lightPos, vec3 worldPos, vec3 surfaceNormal) {
    if (u.shadowParamsPoint.x < 0.5) return 1.0;

    vec3 L = worldPos - lightPos;
    float dist = length(L);
    if (dist <= 1e-5) return 1.0;

    float far = max(u.shadowParamsPoint.w, 0.01);
    float current = dist / far;
    float nDotL = clamp(dot(normalize(surfaceNormal), normalize(-L)), 0.0, 1.0);
    float bias = u.shadowParamsPoint.y * (1.0 + 2.0 * (1.0 - nDotL));
    float strength = u.shadowParamsPoint.z;

    vec3 n = normalize(L);
    vec3 up = abs(n.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 t = normalize(cross(up, n));
    vec3 b = cross(n, t);

    const vec2 offs[9] = vec2[](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(-1.0, 0.0),
        vec2(0.0, 1.0), vec2(0.0, -1.0),
        vec2(0.7, 0.7), vec2(-0.7, 0.7), vec2(0.7, -0.7), vec2(-0.7, -0.7)
    );

    float shadow = 0.0;
    float disk = 0.01;
    for (int i = 0; i < 9; ++i) {
        vec3 dir = normalize(L + (t * offs[i].x + b * offs[i].y) * disk * dist);
        float closest = texture(pointShadowCube, dir).r;
        shadow += (current - bias > closest) ? 0.0 : 1.0;
    }
    shadow /= 9.0;
    return mix(1.0, shadow, clamp(strength, 0.0, 1.0));
}

/* ------------------------------------------------------------------ */
/*  Environment / IBL                                                 */
/* ------------------------------------------------------------------ */

/* Small procedural studio fallback used when no HDR IBL is loaded.
 * Kept in sync with vibl.c::proceduralEnv so the look is stable. */
vec3 sampleStudioEnvironment(vec3 direction, float roughness) {
    vec3 d = normalize(direction);
    float horizon = clamp(d.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 ground  = vec3(0.04, 0.04, 0.05);
    vec3 skyLow  = vec3(0.12, 0.18, 0.28);
    vec3 skyHigh = vec3(0.60, 0.78, 1.10);

    float t = smoothstep(0.0, 1.0, horizon);
    vec3 sky = mix(skyLow, skyHigh, t);
    vec3 env = mix(ground, sky, smoothstep(0.40, 0.60, horizon));

    /* Key + rim lights, broader with roughness (cheap prefiltering). */
    float exponent = mix(150.0, 4.0, roughness * roughness);
    float key = pow(max(dot(d, normalize(vec3(-0.40, 0.85, -0.30))), 0.0), exponent * 0.5);
    env += vec3(8.0, 7.5, 6.5) * key;

    float rim = pow(max(dot(d, normalize(vec3(0.70, 0.30, 0.40))), 0.0), exponent);
    env += vec3(2.0, 2.5, 3.5) * rim;
    return env;
}

/* Split-sum IBL (irradiance + prefiltered radiance + BRDF LUT). */
vec3 evaluateIBL(vec3 N, vec3 V, vec3 albedo, float metallic,
                 float roughness, vec3 F0, float ao) {
    float NdotV = max(dot(N, V), 1e-4);
    vec3 R = reflect(-V, N);

    bool haveMaps = u.materialParams.z > 0.01;
    vec3 irradiance;
    vec3 prefiltered;
    vec2 brdf;

    if (haveMaps) {
        irradiance = texture(irradianceMap, N).rgb;
        float maxLod = max(float(textureQueryLevels(prefilteredMap) - 1), 0.0);
        prefiltered = textureLod(prefilteredMap, R, roughness * maxLod).rgb;
        brdf = texture(brdfLUT, vec2(NdotV, roughness)).rg;
    } else {
        /* No baked maps: use the procedural studio environment. */
        irradiance = sampleStudioEnvironment(N, 1.0) * 0.35;
        prefiltered = sampleStudioEnvironment(R, roughness);
        brdf = envBRDFApprox(NdotV, roughness);
    }

    /* Lagarde's specular occlusion: keeps rough metals from glowing in AO. */
    float specOcc = clamp(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao,
                          0.0, 1.0);

    vec3 F = F_Schlick(F0, NdotV);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    vec3 diffuse = kD * albedo * irradiance * ao;
    vec3 specular = prefiltered * (F0 * brdf.x + brdf.y) * specOcc;
    float scale = haveMaps ? max(u.materialParams.z, 0.0) : 1.0;
    return (diffuse + specular) * scale;
}

/* ------------------------------------------------------------------ */
/*  Colour helpers                                                    */
/* ------------------------------------------------------------------ */

vec3 tonemap_ACES(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 srgbToLinear(vec3 color) {
    vec3 low  = color / 12.92;
    vec3 high = pow((color + 0.055) / 1.055, vec3(2.4));
    return mix(high, low, lessThanEqual(color, vec3(0.04045)));
}

vec3 applyDistanceFog(vec3 color, vec3 worldPosition) {
    if (u.fogParams.x < 0.5) return color;
    float distToCamera = length(worldPosition - u.cameraPos.xyz);
    float linearFog = clamp((u.fogParams.z - distToCamera) /
                            max(u.fogParams.z - u.fogParams.y, 1e-4), 0.0, 1.0);
    float expFog = exp(-max(u.fogParams.w, 0.0) * distToCamera);
    float fogAmount = u.fogParams.w > 0.0 ? 1.0 - expFog : 1.0 - linearFog;

    /* Atmospheric in-scattering: haze brightens toward the sun/moon. This is
       an analytic approximation of volumetric scattering (no shadow shafts). */
    vec3 fogCol = srgbToLinear(u.fogColor.rgb);
    vec3 viewDir = normalize(worldPosition - u.cameraPos.xyz);
    float sunAmount = pow(max(dot(viewDir, normalize(-u.directionalLight.xyz)), 0.0), 8.0);
    fogCol += u.directionalColor.rgb * sunAmount * 0.35 *
              smoothstep(0.0, 0.5, u.directionalLight.w);

    return mix(color, fogCol, clamp(fogAmount, 0.0, 1.0));
}

/* Range-windowed inverse-square-ish falloff. The window guarantees the
 * light reaches exactly zero at `range` (glTF KHR_lights_punctual). */
float rangeAttenuation(float dist, float range) {
    /* Inverse-square falloff with a smooth window that reaches zero at
       `range` (glTF KHR_lights_punctual behaviour). */
    float invSq = 1.0 / max(dist * dist, 0.01);
    if (range <= 0.0) return invSq;
    float t = clamp(dist / range, 0.0, 1.0);
    float t4 = t * t * t * t;
    float window = (1.0 - t4) * (1.0 - t4);
    return invSq * window;
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

void main() {
    int flags = int(u.khrIriDisp.w + 0.5);
    bool isUnlit  = (flags & MATFLAG_UNLIT) != 0;
    bool isMask   = (flags & MATFLAG_ALPHAMASK) != 0;
    bool isBlend  = (flags & MATFLAG_ALPHABLEND) != 0;
    bool hasSpecMap      = (flags & MATFLAG_SPECULAR_MAP) != 0;
    bool hasClearcoatMap = (flags & MATFLAG_CLEARCOAT_MAP) != 0;

    /* ---- base colour / alpha ---- */
    vec4 base = u.baseColor;
    if (u.textureFlags.x > 0.5) base *= texture(albedoTex, vUV);

    float alpha = base.a;
    /* optional glTF COLOR_0; zero alpha == "no color" for engine meshes */
    vec3 vertexColor = (vColor.a > 0.0) ? vColor.rgb : vec3(1.0);
    vec3 albedo = base.rgb * vertexColor;

    float alphaCutoff = max(u.materialParams.w, 0.0);
    if (isMask && alpha < alphaCutoff) discard;

    /* ---- metallic / roughness / AO ---- */
    vec3 mrSample = u.textureFlags.z > 0.5 ? texture(mrTex, vUV).rgb
                                           : vec3(0.0, 1.0, 0.0);
    float metallic  = clamp(mrSample.b * u.metallicRoughnessAO.x, 0.0, 1.0);
    float roughness = clamp(mrSample.g * u.metallicRoughnessAO.y,
                            MIN_ROUGHNESS, 1.0);

    float aoSample = u.textureFlags.w > 0.5 ? texture(aoTex, vUV).r : 1.0;
    float aoStrength = u.metallicRoughnessAO.z > 0.0 ? u.metallicRoughnessAO.z : 1.0;
    float ao = clamp(mix(1.0, aoSample, aoStrength), 0.0, 1.0);

    /* ---- emissive (KHR_materials_emissive_strength) ---- */
    vec3 emissiveSample = u.textureFlags2.x > 0.5 ? texture(emTex, vUV).rgb
                                                  : vec3(1.0);
    vec3 emissive = emissiveSample * u.emissive.rgb * u.emissive.a
                  * max(u.khrIriDisp.z, 0.0);

    /* ---- KHR_materials_unlit ---- */
    if (isUnlit) {
        vec3 c = albedo + emissive;
        c *= max(u.textureFlags2.z, 0.01);
        c = applyDistanceFog(c, vWorldPos);
        outColor = vec4(tonemap_ACES(c), isBlend ? alpha : 1.0);
        return;
    }

    /* ---- normal mapping ---- */
    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing) N = -N;

    if (u.textureFlags.y > 0.5) {
        vec3 nm = texture(normalTex, vUV).xyz * 2.0 - 1.0;
        /* Two-channel (BC5/RG) normal maps decode blue as 0. Reconstruct +Z. */
        if (nm.z < 0.05)
            nm.z = sqrt(max(1.0 - dot(nm.xy, nm.xy), 0.0));
        nm.xy *= u.materialParams.y;
        nm = normalize(nm);

        if (dot(vTangent.xyz, vTangent.xyz) > 1e-5) {
            vec3 T = normalize(vTangent.xyz);
            T = normalize(T - N * dot(N, T));           /* Gram-Schmidt */
            float handedness = (vTangent.w < 0.0) ? -1.0 : 1.0;
            vec3 B = cross(N, T) * handedness;
            N = normalize(mat3(T, B, N) * nm);
        } else {
            /* Screen-space TBN fallback (no TANGENT attribute). */
            vec3 dp1  = dFdx(vWorldPos);
            vec3 dp2  = dFdy(vWorldPos);
            vec2 duv1 = dFdx(vUV);
            vec2 duv2 = dFdy(vUV);
            float det = duv1.x * duv2.y - duv1.y * duv2.x;
            if (abs(det) > 1e-8) {
                vec3 T = normalize((dp1 * duv2.y - dp2 * duv1.y) / det);
                vec3 B = normalize((dp2 * duv1.x - dp1 * duv2.x) / det);
                T = normalize(T - N * dot(N, T));
                B = normalize(B - N * dot(N, B) - T * dot(T, B));
                N = normalize(mat3(T, B, N) * nm);
            }
        }
    }

    vec3 V = normalize(u.cameraPos.xyz - vWorldPos);

    /* ---- KHR_materials_anisotropy: tangent frame for anisotropic GGX ---- */
    float aniso = clamp(u.khrAnisoIri.x, 0.0, 1.0);
    vec3 Ta;
    if (dot(vTangent.xyz, vTangent.xyz) > 1e-5) {
        Ta = normalize(vTangent.xyz);
    } else {
        vec3 dp1 = dFdx(vWorldPos);
        vec3 dp2 = dFdy(vWorldPos);
        vec2 duv1 = dFdx(vUV);
        vec2 duv2 = dFdy(vUV);
        float det = duv1.x * duv2.y - duv1.y * duv2.x;
        if (abs(det) > 1e-8)
            Ta = normalize((dp1 * duv2.y - dp2 * duv1.y) / det);
        else
            Ta = normalize(cross(N, abs(N.y) < 0.99 ? vec3(0.0, 1.0, 0.0)
                                                    : vec3(1.0, 0.0, 0.0)));
    }
    Ta = normalize(Ta - N * dot(N, Ta));
    if (aniso > 0.001) {
        vec3 Bt = normalize(cross(N, Ta));
        float ca = cos(u.khrAnisoIri.y);
        float sa = sin(u.khrAnisoIri.y);
        vec3 rotated = Ta * ca + Bt * sa;
        Ta = normalize(rotated - N * dot(N, rotated));
    }
    vec3 Ba = normalize(cross(N, Ta));

    /* ---- KHR_materials_specular / F0 ---- */
    vec3 specularColor = clamp(u.specularExt.rgb, 0.0, 1.0);
    float specularFactor = clamp(u.specularExt.a, 0.0, 1.0);
    if (hasSpecMap) {
        vec4 st = texture(specularTex, vUV);
        specularColor *= st.rgb;
        specularFactor *= st.a;
    }

    float ior = clamp(u.materialParams.x, 1.0, 3.0);
    float dielectricF0 = pow((ior - 1.0) / (ior + 1.0), 2.0);
    vec3 F0 = mix(min(vec3(dielectricF0) * specularColor, vec3(1.0)) * specularFactor,
                  albedo, metallic);

    /* ---- KHR_materials_iridescence (thin-film tint on F0) ---- */
    float iri = clamp(u.khrAnisoIri.z, 0.0, 1.0);
    if (iri > 0.001) {
        float NdotV0 = max(dot(N, V), 1e-4);
        float thickness = mix(u.khrIriDisp.x, u.khrIriDisp.y, 1.0 - NdotV0);
        float filmIor = clamp(u.khrAnisoIri.w, 1.0, 3.0);
        float sinT2 = (1.0 - NdotV0 * NdotV0) / (filmIor * filmIor);
        float cosT = sqrt(max(1.0 - sinT2, 0.0));
        float opd = 2.0 * filmIor * thickness * cosT; /* nanometres */
        vec3 phase = 6.2831853 * opd / vec3(650.0, 550.0, 450.0);
        vec3 film = 0.5 + 0.5 * cos(phase);
        F0 = mix(F0, clamp(F0 * 0.5 + film * 0.5, 0.0, 1.0), iri);
    }

    /* ---- KHR_materials_clearcoat ---- */
    float cc = clamp(u.khrClearcoat.x, 0.0, 1.0);
    float ccr = clamp(u.khrClearcoat.y, 0.04, 1.0);
    if (hasClearcoatMap) {
        vec4 cct = texture(clearcoatTex, vUV);
        cc *= cct.r;
        ccr = clamp(ccr * cct.g, 0.04, 1.0);
    }

    /* ---- KHR_materials_sheen ---- */
    vec3 sheenColor = max(u.khrSheenColor.rgb, vec3(0.0));
    float sheenRoughness = clamp(u.khrClearcoat.z, 0.07, 1.0);

    /* ---- direct lighting ---- */
    vec3 direct = vec3(0.0);
    float shDir = 1.0;

    if (u.lightMeta.x > 0.5) {
        /* Explicit light-array path: loop over the SSBO records. */
        int count = int(u.lightMeta.x);
        for (int li = 0; li < count; ++li) {
            GpuLight gl = gLights[li];
            int type = int(gl.params.x + 0.5);
            if (gl.params.y < 0.5) continue;
            bool hasShadow = gl.params.z >= 0.0;
            int shadowIdx = hasShadow ? int(gl.params.z + 0.5) : -1;

            if (type == 0) {
                vec3 L = normalize(-gl.direction.xyz);
                float sh = (hasShadow && shadowIdx == 0)
                    ? shadowDirectional(vWorldPos, N, L)
                    : 1.0;
                shDir = sh;
                vec3 radiance = gl.color.rgb * sh;
                direct += evaluateLight(N, V, Ta, Ba, aniso, albedo, metallic,
                                        roughness, F0, L, radiance);

                if (cc > 0.001) {
                    float NdotL = max(dot(N, L), 0.0);
                    float NdotV = max(dot(N, V), 1e-4);
                    vec3 H = normalize(V + L);
                    float NdotH = max(dot(N, H), 0.0);
                    float VdotH = max(dot(V, H), 0.0);
                    float alphaC = ccr * ccr;
                    float Dc = NDF_GGX(NdotH, alphaC);
                    float Vc = V_SmithGGXCorrelated(NdotL, NdotV, alphaC);
                    vec3 Fc = F_Schlick(vec3(0.04), VdotH);
                    direct += Dc * Vc * Fc * radiance * NdotL * cc;
                }
                if (dot(sheenColor, sheenColor) > 1e-6) {
                    float NdotL = max(dot(N, L), 0.0);
                    float NdotV = max(dot(N, V), 1e-4);
                    vec3 H = normalize(V + L);
                    float NdotH = max(dot(N, H), 0.0);
                    float Ds = D_Charlie(NdotH, sheenRoughness);
                    float Vs = V_Ashikhmin(NdotL, NdotV);
                    direct += sheenColor * Ds * Vs * radiance * NdotL * (1.0 - metallic);
                }
            } else if (type == 1) {
                vec3 toL = gl.position.xyz - vWorldPos;
                float dist = length(toL);
                vec3 L = toL / max(dist, 1e-4);
                float atten = rangeAttenuation(dist, max(gl.position.w, 0.001));
                float sh = (hasShadow && shadowIdx == 2)
                    ? shadowPointPCF(gl.position.xyz, vWorldPos, N) : 1.0;
                vec3 radiance = gl.color.rgb * atten * sh;
                direct += evaluateLight(N, V, Ta, Ba, aniso, albedo, metallic,
                                        roughness, F0, L, radiance);
            } else if (type == 2) {
                vec3 toL = gl.position.xyz - vWorldPos;
                float dist = length(toL);
                vec3 L = toL / max(dist, 1e-4);
                float atten = rangeAttenuation(dist, max(gl.position.w, 0.001));
                float cosAng = dot(-L, normalize(gl.direction.xyz));
                float spot = smoothstep(gl.color.w, gl.direction.w, cosAng);
                float sh = (hasShadow && shadowIdx == 1)
                    ? shadowPCF2D(spotShadowMap, u.spotLightViewProj, vWorldPos, N, L,
                                  u.shadowParamsSpot.x, u.shadowParamsSpot.y,
                                  u.shadowParamsSpot.z)
                    : 1.0;
                vec3 radiance = gl.color.rgb * atten * spot * sh;
                direct += evaluateLight(N, V, Ta, Ba, aniso, albedo, metallic,
                                        roughness, F0, L, radiance);
            } else if (type == 3) {
                vec3 center = gl.position.xyz;
                vec3 an = normalize(gl.direction.xyz);
                vec3 up = abs(an.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
                vec3 tangent = normalize(cross(up, an));
                float halfW = max(gl.params.w, 0.01) * 0.5;
                float halfH = max(gl.extra.x, 0.01) * 0.5;
                vec3 toP = vWorldPos - center;
                vec3 bitangent = cross(an, tangent);
                float x = clamp(dot(toP, tangent), -halfW, halfW);
                float y = clamp(dot(toP, bitangent), -halfH, halfH);
                vec3 closest = center + tangent * x + bitangent * y;
                vec3 toL = closest - vWorldPos;
                float dist = length(toL);
                vec3 L = toL / max(dist, 1e-4);
                float area = (halfW * 2.0) * (halfH * 2.0);
                float atten = area / (dist * dist + area);
                float front = max(dot(-L, an), 0.0);
                vec3 radiance = gl.color.rgb * atten * front;
                direct += evaluateLight(N, V, Ta, Ba, aniso, albedo, metallic,
                                        roughness, F0, L, radiance);
            }
        }
    } else {
    vec3 dirL = normalize(-u.directionalLight.xyz);
    vec3 toPoint = u.pointLight.xyz - vWorldPos;
    vec3 pointL = normalize(toPoint);
    vec3 toSpot = u.spotLight.xyz - vWorldPos;
    vec3 spotL = normalize(toSpot);

    shDir = shadowDirectional(vWorldPos, N, dirL);
    float shSpot = shadowPCF2D(spotShadowMap, u.spotLightViewProj, vWorldPos, N, spotL,
                               u.shadowParamsSpot.x, u.shadowParamsSpot.y, u.shadowParamsSpot.z);
    float shPoint = shadowPointPCF(u.pointLight.xyz, vWorldPos, N);

    if (u.directionalLight.w > 0.0) {
        vec3 radiance = u.directionalColor.rgb * u.directionalLight.w * shDir;
        direct += evaluateLight(N, V, Ta, Ba, aniso, albedo, metallic, roughness, F0, dirL, radiance);

        /* Clearcoat specular for the sun. */
        if (cc > 0.001) {
            float NdotL = max(dot(N, dirL), 0.0);
            float NdotV = max(dot(N, V), 1e-4);
            vec3 H = normalize(V + dirL);
            float NdotH = max(dot(N, H), 0.0);
            float VdotH = max(dot(V, H), 0.0);
            float alphaC = ccr * ccr;
            float Dc = NDF_GGX(NdotH, alphaC);
            float Vc = V_SmithGGXCorrelated(NdotL, NdotV, alphaC);
            vec3 Fc = F_Schlick(vec3(0.04), VdotH);
            direct += Dc * Vc * Fc * radiance * NdotL * cc;
        }
        /* Sheen for the sun. */
        if (dot(sheenColor, sheenColor) > 1e-6) {
            float NdotL = max(dot(N, dirL), 0.0);
            float NdotV = max(dot(N, V), 1e-4);
            vec3 H = normalize(V + dirL);
            float NdotH = max(dot(N, H), 0.0);
            float Ds = D_Charlie(NdotH, sheenRoughness);
            float Vs = V_Ashikhmin(NdotL, NdotV);
            direct += sheenColor * Ds * Vs * radiance * NdotL * (1.0 - metallic);
        }
    }

    if (u.pointLight.w > 0.0) {
        float dist = length(toPoint);
        float range = max(u.pointLight.w, 0.001);
        float atten = rangeAttenuation(dist, range);
        vec3 radiance = u.pointColor.rgb * u.pointColor.a * atten * shPoint;
        direct += evaluateLight(N, V, Ta, Ba, aniso, albedo, metallic, roughness, F0, pointL, radiance);
    }

    if (u.spotColor.a > 0.0) {
        float dist = length(toSpot);
        float range = max(u.spotLight.w, 0.001);
        float atten = rangeAttenuation(dist, range);
        float cosOuter = u.spotParams.y;
        float cosInner = u.spotParams.x;
        float cosAng = dot(-spotL, normalize(u.spotDirection.xyz));
        float spot = smoothstep(cosOuter, cosInner, cosAng);
        vec3 radiance = u.spotColor.rgb * u.spotColor.a * atten * spot * shSpot;
        direct += evaluateLight(N, V, Ta, Ba, aniso, albedo, metallic, roughness, F0, spotL, radiance);
    }

    if (u.areaLightPos.w > 0.5) {
        vec3 center = u.areaLightPos.xyz;
        vec3 an = normalize(u.areaLightNormal.xyz);
        vec3 up = abs(an.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vec3 tangent = normalize(cross(up, an));
        float halfW = max(u.areaLightSize.x, 0.01) * 0.5;
        float halfH = max(u.areaLightSize.y, 0.01) * 0.5;

        vec3 toP = vWorldPos - center;
        vec3 bitangent = cross(an, tangent);
        float x = clamp(dot(toP, tangent), -halfW, halfW);
        float y = clamp(dot(toP, bitangent), -halfH, halfH);
        vec3 closest = center + tangent * x + bitangent * y;

        vec3 toL = closest - vWorldPos;
        float dist = length(toL);
        vec3 L = toL / max(dist, 1e-4);
        float area = (halfW * 2.0) * (halfH * 2.0);
        float atten = area / (dist * dist + area);
        float front = max(dot(-L, an), 0.0);
        vec3 radiance = u.areaLightColor.rgb * u.areaLightColor.a * atten * front;
        direct += evaluateLight(N, V, Ta, Ba, aniso, albedo, metallic, roughness, F0, L, radiance);
    }
    } /* lightMeta.x == 0: legacy fixed-light path */

    /* ---- ambient: IBL + optional uniform fill ---- */
    float ssao = 1.0;
    if (u.spotParams.z > 0.001) {
        vec2 ssaoSize = vec2(textureSize(ssaoTex, 0));
        vec2 screenUV = gl_FragCoord.xy / max(ssaoSize * 2.0, vec2(1.0));
        ssao = clamp(texture(ssaoTex, screenUV).r, 0.0, 1.0);
    }
    float aoTotal = clamp(ao * ssao, 0.0, 1.0);

    vec3 ambient = evaluateIBL(N, V, albedo, metallic, roughness, F0, aoTotal);
    ambient += albedo * u.ambientLight.rgb * u.ambientLight.a * (1.0 - metallic) * aoTotal;

    vec3 R = reflect(-V, N);
    float NdotV = max(dot(N, V), 1e-4);

    /* Clearcoat ambient layer. */
    if (cc > 0.001) {
        bool haveMaps = u.materialParams.z > 0.01;
        float maxLod = haveMaps ? max(float(textureQueryLevels(prefilteredMap) - 1), 0.0) : 0.0;
        vec2 ccBrdf = haveMaps ? texture(brdfLUT, vec2(NdotV, ccr)).rg
                               : envBRDFApprox(NdotV, ccr);
        vec3 ccEnv = haveMaps ? textureLod(prefilteredMap, R, ccr * maxLod).rgb
                              : sampleStudioEnvironment(R, ccr);
        float specOcc = clamp(pow(NdotV + aoTotal, exp2(-16.0 * ccr - 1.0)) - 1.0 + aoTotal,
                              0.0, 1.0);
        vec3 Fcc = F_Schlick(vec3(0.04), NdotV);
        vec3 ccSpec = ccEnv * (vec3(0.04) * ccBrdf.x + ccBrdf.y) * specOcc * cc;

        ambient = ambient * (1.0 - cc * Fcc) + ccSpec;
        direct *= (1.0 - 0.5 * cc);
    }

    /* Sheen ambient lobe (approximate environment sheen). */
    if (dot(sheenColor, sheenColor) > 1e-6) {
        float sheenF = pow(1.0 - NdotV, 2.0 + sheenRoughness * 4.0);
        vec3 sheenEnv = max(ambient, vec3(0.0)) * 0.5 + vec3(0.05);
        ambient += sheenColor * sheenF * sheenEnv * (1.0 - metallic);
    }

    /* ---- KHR_materials_transmission (thin-walled approximation) ---- */
    float transmission = clamp(u.khrClearcoat.w, 0.0, 1.0);
    if (transmission > 0.001 && metallic < 0.9) {
        vec3 atten = u.khrVolume.rgb;
        float thick = max(u.khrSheenColor.a, 0.0);
        float attenDist = u.khrVolume.a;
        if (attenDist > 0.001 && thick > 0.0)
            atten = pow(max(atten, vec3(1e-4)), vec3(thick / attenDist));

        vec3 transmitted = albedo * atten * (ambient * 0.85 + 0.05);
        direct *= (1.0 - transmission);
        ambient = mix(ambient, transmitted, transmission * (1.0 - metallic));
        /* Keep transmissive surfaces readable in the raster path: full
           physical transparency would erase the volume tint almost entirely. */
        alpha = mix(alpha, alpha * (1.0 - transmission * 0.35), 1.0 - metallic);
    }

    vec3 color = direct + ambient + emissive;

    /* ---- debug views (raw, linear) ---- */
    int dbg = int(u.textureFlags2.y + 0.5);
    if (dbg != 0) {
        if (dbg == 1)      color = albedo;
        else if (dbg == 2) color = N * 0.5 + 0.5;
        else if (dbg == 3) color = vec3(roughness);
        else if (dbg == 4) color = vec3(metallic);
        else if (dbg == 5) color = vec3(aoTotal);
        else if (dbg == 6) color = emissive;
        else if (dbg == 7) color = vec3(shDir);
        outColor = vec4(color, isBlend ? alpha : 1.0);
        return;
    }

    /* ---- exposure, bloom, fog, tone map ---- */
    float exposure = max(u.textureFlags2.z, 0.01);
    color *= exposure;

    float bloomIntensity = max(u.areaLightSize.w, 0.0);
    if (bloomIntensity > 0.001) {
        float threshold = max(u.areaLightSize.z, 0.1);
        vec3 bright = max(color - vec3(threshold), vec3(0.0));
        color += bright * bloomIntensity;
    }

    color = applyDistanceFog(color, vWorldPos);

    if (!isBlend) alpha = 1.0;
    outColor = vec4(tonemap_ACES(color), alpha);
}
