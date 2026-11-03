#version 450

/*
 * Skinned vertex — UBO must match pbr.frag (same set0 binding0 layout).
 * Bone matrices: binding 12 (skin only; 0-11 match pbr.frag).
 */

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in uvec4 inJoints;
layout(location = 5) in vec4 inWeights;

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
} u;

layout(set = 0, binding = 12) uniform BoneBlock {
    mat4 bones[256];
} bb;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) out vec4 vTangent;
layout(location = 4) out vec4 vColor; /* pbr.frag declares location 4 */

void main() {
    uvec4 j = min(inJoints, uvec4(255u));
    vec4  w = inWeights;
    float wsum = w.x + w.y + w.z + w.w;
    if (wsum > 1e-5) w /= wsum; else w = vec4(1.0, 0.0, 0.0, 0.0);

    mat4 skin =
        bb.bones[j.x] * w.x +
        bb.bones[j.y] * w.y +
        bb.bones[j.z] * w.z +
        bb.bones[j.w] * w.w;

    vec4 local = skin * vec4(inPos, 1.0);
    vec4 world = u.model * local;
    gl_Position = u.mvp * local;

    mat3 skinN = mat3(skin);
    mat3 nMat = mat3(u.normalMat);
    vWorldPos = world.xyz;
    vNormal   = normalize(nMat * (skinN * inNormal));
    vUV       = inUV;
    vec3 T = nMat * (skinN * inTangent.xyz);
    vTangent = vec4(T, inTangent.w);
    vColor = vec4(1.0);
}
