#version 450
layout(location = 0) in vec3 inPos;
layout(set = 0, binding = 0) uniform VelocityVP {
    mat4 curVP;
    mat4 prevVP;
} vp;
layout(push_constant) uniform PC {
    mat4 curModel;
    mat4 prevModel;
} pc;
layout(location = 0) out vec2 vVelocity;
void main() {
    vec4 c = vp.curVP * pc.curModel * vec4(inPos, 1.0);
    vec4 p = vp.prevVP * pc.prevModel * vec4(inPos, 1.0);
    vVelocity = (c.xy / c.w - p.xy / p.w) * 0.5;
    gl_Position = c;
}
