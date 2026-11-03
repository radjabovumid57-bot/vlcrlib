#version 450

/*
 * Phase 3 — 2D fragment shader.
 * Texture * tint color (умножение).
 * Если текстура не задана — используется белая 1x1 (см. vgraphics.c).
 */

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 tex = texture(texSampler, fragUV);
    outColor = tex * fragColor;
}