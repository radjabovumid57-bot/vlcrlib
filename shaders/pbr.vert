#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in vec4 inColor;

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

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) out vec4 vTangent; /* xyz = T world, w = handedness */
layout(location = 4) out vec4 vColor;

void main() {
    vec4 world  = u.model * vec4(inPos, 1.0);
    gl_Position = u.mvp * vec4(inPos, 1.0);
    vWorldPos = world.xyz;
    mat3 nMat = mat3(u.normalMat);
    vNormal = nMat * inNormal;
    vUV = inUV;
    vec3 T = mat3(u.model) * inTangent.xyz;
    vTangent = vec4(T, inTangent.w);
    vColor = inColor;
}
