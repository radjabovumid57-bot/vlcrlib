#version 450
/* Depth-only shadow caster. Push constant = lightViewProj * model */
layout(location = 0) in vec3 inPos;

layout(push_constant) uniform PC {
    mat4 lightMVP;
} pc;

void main() {
    gl_Position = pc.lightMVP * vec4(inPos, 1.0);
}
