#version 450

/*
 * Final composite: HDR/LDR scene + bloom -> swapchain.
 *
 * The scene pass (pbr.frag / skybox.frag) already applies exposure and
 * ACES tone mapping, so this pass must NOT tone map or apply exposure a
 * second time. It only adds the blurred bright pass and writes the result
 * to the (sRGB) swapchain image.
 */

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneTex;
layout(set = 0, binding = 1) uniform sampler2D bloomTex;
layout(set = 0, binding = 2) uniform sampler2D historyTex;
layout(set = 0, binding = 3) uniform sampler2D depthTex;
layout(set = 0, binding = 4) uniform TaaBlock {
    mat4 curVP;
    mat4 prevVP;
    vec4 params; /* x=enabled, y=feedback, z=reset */
} taa;
layout(set = 0, binding = 5) uniform sampler2D velocityTex;

layout(push_constant) uniform PC {
    float bloomStrength;
    float exposure;      /* kept for ABI compatibility; already applied */
    float fxaa;
} pc;

float luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

vec3 fxaaScene(vec2 uv) {
    vec2 texel = 1.0 / vec2(textureSize(sceneTex, 0));
    vec3 m  = texture(sceneTex, uv).rgb;
    vec3 nw = texture(sceneTex, uv + vec2(-1.0, -1.0) * texel).rgb;
    vec3 ne = texture(sceneTex, uv + vec2( 1.0, -1.0) * texel).rgb;
    vec3 sw = texture(sceneTex, uv + vec2(-1.0,  1.0) * texel).rgb;
    vec3 se = texture(sceneTex, uv + vec2( 1.0,  1.0) * texel).rgb;

    float lm  = luma(m);
    float lmin = min(lm, min(min(luma(nw), luma(ne)), min(luma(sw), luma(se))));
    float lmax = max(lm, max(max(luma(nw), luma(ne)), max(luma(sw), luma(se))));
    if (lmax - lmin < max(0.0312, lmax * 0.125)) return m;

    vec2 dir = vec2(
        -((luma(nw) + luma(ne)) - (luma(sw) + luma(se))),
         ((luma(nw) + luma(sw)) - (luma(ne) + luma(se))));
    float reduce = max((luma(nw) + luma(ne) + luma(sw) + luma(se)) * 0.03125, 1.0 / 128.0);
    float rcp = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcp, vec2(-8.0), vec2(8.0)) * texel;

    vec3 a = 0.5 * (texture(sceneTex, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
                    texture(sceneTex, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 b = a * 0.5 + 0.25 * (texture(sceneTex, uv - dir * 0.5).rgb +
                               texture(sceneTex, uv + dir * 0.5).rgb);
    return (luma(b) < lmin || luma(b) > lmax) ? a : b;
}

vec3 taaResolve(vec3 current, vec2 uv) {
    if (taa.params.x < 0.5 || taa.params.z > 0.5) return current;
    vec2 velocity = texture(velocityTex, uv).xy;
    vec2 prevUV;
    if (length(velocity) > 1e-6) {
        prevUV = uv - velocity;
    } else {
        float depth = texture(depthTex, uv).r;
        vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
        vec4 world = inverse(taa.curVP) * clip;
        if (abs(world.w) < 1e-6) return current;
        world /= world.w;
        vec4 prev = taa.prevVP * world;
        if (abs(prev.w) < 1e-6) return current;
        prevUV = prev.xy / prev.w * 0.5 + 0.5;
    }
    if (prevUV.x < 0.0 || prevUV.x > 1.0 || prevUV.y < 0.0 || prevUV.y > 1.0)
        return current;

    vec2 texel = 1.0 / vec2(textureSize(sceneTex, 0));
    vec3 nmin = current, nmax = current;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec3 c = texture(sceneTex, uv + vec2(x, y) * texel).rgb;
            nmin = min(nmin, c);
            nmax = max(nmax, c);
        }
    }
    vec3 history = clamp(texture(historyTex, prevUV).rgb, nmin, nmax);
    return mix(current, history, clamp(taa.params.y, 0.02, 0.5));
}

void main() {
    vec3 current = texture(sceneTex, vUV).rgb;
    current = taaResolve(current, vUV);
    vec3 scene = pc.fxaa > 0.5 ? fxaaScene(vUV) : current;
    vec3 bloom = texture(bloomTex, vUV).rgb;

    vec3 color = scene + bloom * pc.bloomStrength;
    outColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
