#version 450

/*
 * Phase 4 — 3D fragment shader без PBR.
 * Простая модель: ambient + диффузное освещение по одной направленной лампе.
 */

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec3 vWorld;

layout(set = 0, binding = 0) uniform UBO {
    mat4 mvp;
    mat4 model;
    mat4 normalMat;
    vec4 lightDir;
    vec4 lightColor;
    vec4 baseColor;
    vec4 _pad;
} u;

layout(set = 0, binding = 1) uniform sampler2D albedo;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(-u.lightDir.xyz);

    float diff = max(dot(N, L), 0.0);
    float ambient = 0.25;

    vec3 base = u.baseColor.rgb * texture(albedo, vUV).rgb;
    vec3 lit  = base * (ambient + (1.0 - ambient) * diff) * u.lightColor.rgb;

    outColor = vec4(lit, u.baseColor.a);
}