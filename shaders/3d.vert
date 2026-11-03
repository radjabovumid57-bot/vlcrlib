#version 450

/*
 * Phase 4 — базовый 3D vertex shader (fixed: normalMat).
 */

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform UBO {
    mat4 mvp;
    mat4 model;
    mat4 normalMat;
    vec4 lightDir;
    vec4 lightColor;
    vec4 baseColor;
    vec4 _pad;
} u;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec3 vWorld;

void main() {
    /* CPU: u.mvp = proj*view*model (or proj*view when model=I). Clip = mvp*pos once. */
    vec4 world  = u.model * vec4(inPos, 1.0);
    gl_Position = u.mvp * vec4(inPos, 1.0);

    vNormal = mat3(u.normalMat) * inNormal;
    vUV     = inUV;
    vWorld  = world.xyz;
}