#version 450
layout(location = 0) in vec2 vVelocity;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vec4(vVelocity, 0.0, 1.0);
}
