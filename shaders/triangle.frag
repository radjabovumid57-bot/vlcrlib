#version 450

/*
 * Phase 1+2 — фрагментный шейдер тестового треугольника.
 * Просто выводит оранжевый цвет.
 */

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(0.95, 0.45, 0.20, 1.0);
}