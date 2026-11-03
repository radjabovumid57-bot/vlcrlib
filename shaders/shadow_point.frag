#version 450
layout(location = 0) in vec3 vWorldPos;
layout(push_constant) uniform PC {
    mat4 lightMVP;
    mat4 model;
    vec4 lightPosFar;
} pc;
layout(location = 0) out float outDepth;

void main() {
    float d = length(vWorldPos - pc.lightPosFar.xyz);
    outDepth = d / max(pc.lightPosFar.w, 0.001);
}
