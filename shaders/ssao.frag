#version 450
// Half-resolution SSAO from depth buffer (Sprint F)
layout(location = 0) in vec2 vUV;
layout(location = 0) out float outAO;

layout(set = 0, binding = 0) uniform sampler2D depthTex;
layout(set = 0, binding = 1) uniform sampler2D noiseTex; // optional white

layout(push_constant) uniform PC {
    float radius;
    float intensity;
    float nearZ;
    float farZ;
    vec2  invRes;
    vec2  noiseScale;
} pc;

/* Vulkan ZO depth: d = 0 at the near plane, d = 1 at the far plane.
   (The GL-style d*2-1 remap would give nonsense distances here.) */
float linearize(float d) {
    float n = pc.nearZ;
    float f = pc.farZ;
    return (n * f) / (f - d * (f - n));
}

void main() {
    float centerD = texture(depthTex, vUV).r;
    if (centerD >= 0.999) {
        outAO = 1.0;
        return;
    }
    float centerZ = linearize(centerD);

    // 8-tap kernel in screen space
    const vec2 k[8] = vec2[](
        vec2( 1, 0), vec2(-1, 0), vec2(0, 1), vec2(0,-1),
        vec2( 0.7, 0.7), vec2(-0.7, 0.7), vec2(0.7,-0.7), vec2(-0.7,-0.7)
    );

    float occ = 0.0;
    for (int i = 0; i < 8; ++i) {
        vec2 off = k[i] * pc.radius * pc.invRes;
        float sd = texture(depthTex, vUV + off).r;
        float sz = linearize(sd);
        float diff = centerZ - sz;
        // sample closer than center → occludes
        occ += clamp(diff * 8.0, 0.0, 1.0) * exp(-abs(diff) * 2.0);
    }
    occ /= 8.0;
    float ao = 1.0 - occ * pc.intensity;
    outAO = clamp(ao, 0.05, 1.0);
}
