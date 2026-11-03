#version 450
layout(location = 0) in vec3 inPos;
layout(location = 1) in uvec4 inJoints;
layout(location = 2) in vec4 inWeights;
layout(set = 0, binding = 0) uniform VelocityVP {
    mat4 curVP;
    mat4 prevVP;
} vp;
layout(set = 0, binding = 1) uniform BoneBlock {
    mat4 curBones[256];
    mat4 prevBones[256];
} bb;
layout(push_constant) uniform PC {
    mat4 curModel;
    mat4 prevModel;
} pc;
layout(location = 0) out vec2 vVelocity;
void main() {
    uvec4 j = min(inJoints, uvec4(255u));
    vec4 localCur = bb.curBones[j.x] * vec4(inPos, 1.0) * inWeights.x +
                    bb.curBones[j.y] * vec4(inPos, 1.0) * inWeights.y +
                    bb.curBones[j.z] * vec4(inPos, 1.0) * inWeights.z +
                    bb.curBones[j.w] * vec4(inPos, 1.0) * inWeights.w;
    vec4 localPrev = bb.prevBones[j.x] * vec4(inPos, 1.0) * inWeights.x +
                     bb.prevBones[j.y] * vec4(inPos, 1.0) * inWeights.y +
                     bb.prevBones[j.z] * vec4(inPos, 1.0) * inWeights.z +
                     bb.prevBones[j.w] * vec4(inPos, 1.0) * inWeights.w;
    vec4 c = vp.curVP * pc.curModel * localCur;
    vec4 p = vp.prevVP * pc.prevModel * localPrev;
    vVelocity = (c.xy / c.w - p.xy / p.w) * 0.5;
    gl_Position = c;
}
