#version 450
// Fullscreen triangle for half-res SSAO pass
layout(location = 0) out vec2 vUV;

void main() {
    // 3 vertices covering the screen
    vec2 pos = vec2(
        (gl_VertexIndex == 0) ? -1.0 : (gl_VertexIndex == 1) ? 3.0 : -1.0,
        (gl_VertexIndex == 0) ? -1.0 : (gl_VertexIndex == 1) ? -1.0 : 3.0
    );
    vUV = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
