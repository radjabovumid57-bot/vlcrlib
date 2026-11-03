#version 450

/*
 * Phase 3 — 2D vertex shader.
 * Input:  Vertex2D { vec2 position, vec2 uv, vec4 color } из vgraphics.h
 * Output: UV и цвет для fragment shader.
 *
 * Матрица ортопроекции передаётся через push constants:
 *   MatrixOrtho(0, width, height, 0, -1, 1)
 * Это даёт пиксельные координаты: (0,0) — левый верх, (W,H) — правый низ.
 */

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(push_constant) uniform Push {
    mat4 mvp;
} pc;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 0.0, 1.0);
    fragUV      = inUV;
    fragColor   = inColor;
}