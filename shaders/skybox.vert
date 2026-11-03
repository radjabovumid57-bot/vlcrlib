#version 450

/*
 * Fullscreen environment background.
 *
 * A single oversized triangle covers the viewport at the far plane. The
 * view ray is reconstructed from the camera basis and the projection's
 * half-FOV tangents (taken from the view-projection matrix on the CPU),
 * so no inverse matrix is needed.
 *
 * Push constants (112 bytes):
 *   camRight.xyz = camera right,  .w = tan(fovX/2)
 *   camUp.xyz    = camera up,     .w = tan(fovY/2)
 *   camFwd.xyz   = camera forward,.w = exposure
 *   sun.xyz      = direction to sun,   .w = sun intensity
 *   moon.xyz     = direction to moon,  .w = moon intensity
 *   sky          = time, cloud, haze, stars
 *   extra        = atmosphereEnabled, weather, sunRadius, moonRadius
 */

layout(push_constant) uniform PC {
    vec4 camRight;
    vec4 camUp;
    vec4 camFwd;
    vec4 sun;
    vec4 moon;
    vec4 sky;
    vec4 extra;
} pc;

layout(location = 0) out vec3 vDir;

void main() {
    /* (-1,-1), (3,-1), (-1,3) */
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2),
                  float(gl_VertexIndex & 2)) * 2.0 - 1.0;

    vDir = pc.camFwd.xyz
         + pc.camRight.xyz * (p.x * pc.camRight.w)
         + pc.camUp.xyz    * (p.y * pc.camUp.w);

    /* z = 1 with w = 1 -> depth 1.0 (far plane), so any rendered
       geometry with depth < 1 wins the LEQUAL depth test. */
    gl_Position = vec4(p, 1.0, 1.0);
}
