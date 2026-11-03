#version 450
// Half-res bright pass + simple blur (Sprint F)
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneTex;

layout(push_constant) uniform PC {
    float threshold;
    float intensity;
    vec2  invRes;
    int   mode; // 0=bright, 1=blur H, 2=blur V
} pc;

void main() {
    vec3 c = texture(sceneTex, vUV).rgb;
    if (pc.mode == 0) {
        float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
        float bright = max(lum - pc.threshold, 0.0);
        outColor = vec4(c * bright * pc.intensity, 1.0);
    } else {
        vec3 acc = vec3(0.0);
        float wsum = 0.0;
        for (int i = -2; i <= 2; ++i) {
            vec2 off = (pc.mode == 1) ? vec2(float(i) * pc.invRes.x, 0.0)
                                      : vec2(0.0, float(i) * pc.invRes.y);
            float w = 1.0 - abs(float(i)) * 0.2;
            acc += texture(sceneTex, vUV + off).rgb * w;
            wsum += w;
        }
        outColor = vec4(acc / max(wsum, 1.0), 1.0);
    }
}
