#version 450

/*
 * Environment background:
 *   - atmosphere disabled: sample the HDR environment cubemap;
 *   - atmosphere enabled:  procedural time-of-day sky with sun/moon disks,
 *     stars, clouds and horizon haze.
 */

layout(location = 0) in vec3 vDir;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform samplerCube environmentMap;

layout(push_constant) uniform PC {
    vec4 camRight;
    vec4 camUp;
    vec4 camFwd;
    vec4 sun;      /* xyz = direction, w = intensity */
    vec4 moon;     /* xyz = direction, w = intensity */
    vec4 sky;      /* x = time, y = cloud, z = haze, w = stars */
    vec4 extra;    /* x = atmosphereEnabled, y = weather,
                      z = sunRadius, w = moonRadius */
} pc;

const float PI = 3.14159265358979323846;

vec3 tonemap_ACES(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 345.45));
    p += dot(p, p + 34.345);
    return fract(p.x * p.y);
}

float hash31(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p) {
    float sum = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; ++i) {
        sum += amp * valueNoise(p);
        p *= 2.03;
        amp *= 0.5;
    }
    return sum;
}

vec3 sunTint(float elevation) {
    return mix(vec3(1.00, 0.28, 0.06),
               vec3(1.00, 0.96, 0.88),
               smoothstep(0.0, 0.45, elevation));
}

void main() {
    vec3 dir = normalize(vDir);
    vec3 color;

    if (pc.extra.x < 0.5) {
        color = texture(environmentMap, dir).rgb;
    } else {
        vec3 sunN = normalize(pc.sun.xyz);
        vec3 moonN = normalize(pc.moon.xyz);
        float sunY = sunN.y;
        float day = smoothstep(-0.14, 0.10, sunY);
        float night = 1.0 - day;

        vec3 zenith = mix(vec3(0.004, 0.007, 0.020),
                          vec3(0.10, 0.28, 0.62), day);
        vec3 horizon = mix(vec3(0.015, 0.025, 0.060),
                           vec3(0.55, 0.73, 0.96), day);
        float h = pow(1.0 - clamp(dir.y, 0.0, 1.0), 5.0);
        color = mix(zenith, horizon, h);

        /* Warm band around the horizon at sunrise/sunset. */
        float sunset = exp(-abs(sunY) * 10.0) * smoothstep(-0.25, 0.05, sunY);
        color = mix(color, vec3(1.0, 0.30, 0.08),
                    sunset * (0.25 + 0.55 * h));

        vec3 sunCol = sunTint(sunY);
        float cosSun = dot(dir, sunN);
        float clearSky = 1.0 - pc.sky.y * 0.85;

        /* Sun disk + glow. The HDR disk is intentionally much brighter
           than the sky so tone mapping produces a real highlight. */
        float sunDisk = smoothstep(cos(pc.extra.z * 1.15),
                                   cos(pc.extra.z * 0.75), cosSun);
        float sunGlow = pow(max(cosSun, 0.0), 220.0) * 2.0
                      + pow(max(cosSun, 0.0), 12.0) * 0.10;
        color += sunCol * (sunDisk * 24.0 + sunGlow)
               * max(pc.sun.w, 0.0) * 0.25 * clearSky
               * smoothstep(-0.20, 0.02, sunY);

        /* Moon disk + soft halo. */
        float cosMoon = dot(dir, moonN);
        float moonDisk = smoothstep(cos(pc.extra.w * 1.15),
                                    cos(pc.extra.w * 0.75), cosMoon);
        float moonGlow = pow(max(cosMoon, 0.0), 260.0);
        color += vec3(0.62, 0.72, 0.95)
               * (moonDisk * 2.5 + moonGlow * 0.35)
               * pc.moon.w * night * clearSky;

        /* Sparse twinkling stars, only at night. */
        if (pc.sky.w > 0.001) {
            vec3 cell = floor(dir * 300.0);
            float star = hash31(cell);
            float s = smoothstep(0.9975, 0.9999, star);
            float tw = 0.55 + 0.45 * sin(pc.sky.x * 2.5 + star * 80.0);
            color += vec3(0.85, 0.92, 1.0) * s * tw * pc.sky.w
                   * night * (1.0 - pc.sky.z) * 1.5;
        }

        /* Cloud layer projected on a conceptual plane above the camera. */
        if (dir.y > 0.005 && pc.sky.y > 0.001) {
            vec2 uv = dir.xz / (dir.y + 0.22) * 0.75;
            uv += vec2(pc.sky.x * 0.010, pc.sky.x * 0.004);
            float f = fbm(uv * 1.6 + 3.1);
            float cover = smoothstep(1.0 - pc.sky.y,
                                     1.0 - pc.sky.y + 0.35, f);
            float fade = smoothstep(0.0, 0.18, dir.y);
            vec3 cloudCol = mix(vec3(0.28, 0.30, 0.35),
                                vec3(1.0, 0.98, 0.96), day);
            cloudCol *= 0.55 + 0.45 * smoothstep(0.0, 0.8, dir.y);
            cloudCol += sunCol * pow(max(cosSun, 0.0), 6.0) * 0.25 * day;
            color = mix(color, cloudCol, cover * fade * 0.92);
        }

        /* Aerosol haze / fog: pull the sky toward the horizon colour. */
        color = mix(color, horizon * 1.05,
                    pc.sky.z * (0.25 + 0.65 * h));
    }

    color *= max(pc.camFwd.w, 0.01);
    outColor = vec4(tonemap_ACES(color), 1.0);
}
