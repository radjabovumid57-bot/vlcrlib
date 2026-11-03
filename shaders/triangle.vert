#version 450

/*
 * Phase 1+2 — тестовый шейдер треугольника.
 * Позиции задаются прямо в шейдере — вершинный буфер не нужен.
 * Используется только для проверки, что Vulkan pipeline работает.
 */

void main() {
    vec2 positions[3] = vec2[](
        vec2( 0.0, -0.5),
        vec2( 0.5,  0.5),
        vec2(-0.5,  0.5)
    );

    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}