/*
 * raytracer_pbr.c — real-time CPU PBR ray tracer (C11).
 *
 * A C port/upgrade of the original PowerShell/C# Whitted tracer:
 *   - metallic-roughness PBR: GGX NDF, height-correlated Smith, Schlick Fresnel
 *   - area light (disk) with soft shadows
 *   - Fresnel reflection + Snell refraction for glass (IOR, TIR)
 *   - jittered anti-aliasing
 *   - HDR sky + ACES tone mapping + sRGB transfer
 *   - multithreaded row rendering
 *
 * The window is an SDL/vlcrlib window; the image is rendered on the CPU into
 * an RGBA8 buffer and uploaded to a texture.
 *
 * Controls:
 *   mouse drag   orbit
 *   wheel        zoom
 *   + / -        internal resolution
 *   A            AA samples (1x1, 2x2, 4x4)
 *   L            light samples (1, 4, 8)
 *   R            reflection depth 0..4
 *   Space        pause animation
 *   Esc          quit
 *
 * Headless benchmark:
 *   raytracer_pbr.exe --bench out.bmp [width height]
 *   prints render time and writes a 24-bit BMP.
 */
#include <vlcrlib.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#define PBR_PI       3.14159265358979323846
#define PBR_PLANE_Y  (-1.0)
#define PBR_EPS      1e-4
#define PBR_SPHERES  5   /* 4 material spheres + visible area-light marker */

/* ------------------------------------------------------------------ */
/*  Small vector / material / scene types                              */
/* ------------------------------------------------------------------ */

typedef struct { double x, y, z; } V3;

static V3 v3(double x, double y, double z) { V3 r = { x, y, z }; return r; }
static V3 vadd(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 vsub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 vmul(V3 a, double s) { return v3(a.x * s, a.y * s, a.z * s); }
static V3 vmul3(V3 a, V3 b) { return v3(a.x * b.x, a.y * b.y, a.z * b.z); }
static double vdot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 vcross(V3 a, V3 b) {
    return v3(a.y * b.z - a.z * b.y,
              a.z * b.x - a.x * b.z,
              a.x * b.y - a.y * b.x);
}
static double vlen(V3 a) { return sqrt(vdot(a, a)); }
static V3 vnorm(V3 a) {
    double l = vlen(a);
    if (l < 1e-12) return v3(0, 0, 0);
    return vmul(a, 1.0 / l);
}
static V3 vlerp(V3 a, V3 b, double t) {
    return v3(a.x + (b.x - a.x) * t,
              a.y + (b.y - a.y) * t,
              a.z + (b.z - a.z) * t);
}
static double clampd(double v, double a, double b) {
    return v < a ? a : (v > b ? b : v);
}
static double luminance(V3 c) {
    return 0.2126 * c.x + 0.7152 * c.y + 0.0722 * c.z;
}

typedef struct {
    V3     albedo;       /* linear RGB */
    double metallic;
    double roughness;    /* perceptual 0..1 */
    double ior;
    double transmission; /* 0 = opaque, 1 = glass */
    double emissive;
} Mat;

typedef struct { V3 c; double r; Mat m; } Sphere;

typedef struct {
    double t;
    V3     p;
    V3     n;    /* shading normal, oriented against the ray */
    V3     gn;   /* geometric normal */
    Mat    m;
    int    found;
} Hit;

/* ------------------------------------------------------------------ */
/*  Scene state                                                       */
/* ------------------------------------------------------------------ */

static Sphere g_spheres[PBR_SPHERES];
static V3     g_lightPos;
static V3     g_lightColor;
static double g_lightRadius = 1.60;
static double g_lightIntensity = 220.0;
static Mat    g_planeA, g_planeB;

static int    g_aa = 2;            /* samples per axis: 1, 2, 4 */
static int    g_lightSamples = 8;  /* soft-shadow samples on primary hit */
static int    g_depth = 3;         /* reflection/refraction depth */
static double g_exposure = 0.68;

/* Per-frame render globals (workers only read them). */
static float   *g_hdr;             /* linear RGB, w*h*3 */
static int      g_w, g_h;
static uint32_t g_frameSeed;
static V3       g_camPos, g_camFwd, g_camRight, g_camUp;
static double   g_camScale, g_camAspect;

/* ------------------------------------------------------------------ */
/*  RNG                                                               */
/* ------------------------------------------------------------------ */

static uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static double rnd01(uint32_t *s) {
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return (double)(*s & 0x00ffffffu) * (1.0 / 16777216.0);
}

/* ------------------------------------------------------------------ */
/*  Intersections                                                     */
/* ------------------------------------------------------------------ */

static int hit_sphere(V3 o, V3 d, const Sphere *s, double *outT) {
    V3 oc = vsub(o, s->c);
    double b = vdot(oc, d);
    double c = vdot(oc, oc) - s->r * s->r;
    double disc = b * b - c;
    if (disc < 0.0) return 0;
    double sq = sqrt(disc);
    double t = -b - sq;
    if (t < PBR_EPS) t = -b + sq;
    if (t < PBR_EPS) return 0;
    *outT = t;
    return 1;
}

static Mat plane_material(V3 p) {
    int chk = ((int)floor(p.x * 0.75) + (int)floor(p.z * 0.75)) & 1;
    return chk ? g_planeB : g_planeA;
}

static void consider(Hit *h, double t, V3 n, V3 gn, Mat m) {
    if (t > PBR_EPS && t < h->t) {
        h->t = t;
        h->n = n;
        h->gn = gn;
        h->m = m;
        h->found = 1;
    }
}

static void intersect_scene(V3 o, V3 d, Hit *h) {
    memset(h, 0, sizeof(*h));
    h->t = 1e30;

    for (int i = 0; i < PBR_SPHERES; ++i) {
        double t;
        if (hit_sphere(o, d, &g_spheres[i], &t)) {
            V3 p = vadd(o, vmul(d, t));
            V3 n = vnorm(vsub(p, g_spheres[i].c));
            V3 gn = n;
            if (vdot(n, d) > 0.0) n = vmul(n, -1.0);
            consider(h, t, n, gn, g_spheres[i].m);
        }
    }

    if (fabs(d.y) > 1e-9) {
        double t = (PBR_PLANE_Y - o.y) / d.y;
        if (t > PBR_EPS && t < h->t) {
            V3 p = vadd(o, vmul(d, t));
            V3 n = v3(0, 1, 0);
            if (vdot(n, d) > 0.0) n = vmul(n, -1.0);
            Mat pm = plane_material(p);
            double fade = clampd(t / 70.0, 0.0, 1.0);
            pm.albedo = vlerp(pm.albedo, v3(0.26, 0.28, 0.32), fade);
            consider(h, t, n, v3(0, 1, 0), pm);
        }
    }

    if (h->found) h->p = vadd(o, vmul(d, h->t));
}

static int occluded(V3 o, V3 d, double maxT) {
    for (int i = 0; i < PBR_SPHERES; ++i) {
        /* Glass is not treated as an opaque blocker here; a real path tracer
           would attenuate/refract the shadow ray. */
        if (g_spheres[i].m.transmission > 0.5) continue;
        if (g_spheres[i].m.emissive > 0.5) continue;
        double t;
        if (hit_sphere(o, d, &g_spheres[i], &t) && t < maxT) return 1;
    }
    if (fabs(d.y) > 1e-9) {
        double t = (PBR_PLANE_Y - o.y) / d.y;
        if (t > PBR_EPS && t < maxT) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Sky / light sampling                                              */
/* ------------------------------------------------------------------ */

static V3 sky_color(V3 d) {
    double t = clampd(d.y * 0.5 + 0.5, 0.0, 1.0);
    V3 horizon = v3(0.95, 0.80, 0.66);
    V3 zenith  = v3(0.10, 0.28, 0.80);
    V3 c = vlerp(horizon, zenith, pow(t, 0.60));
    c = vmul(c, 0.90);

    V3 sd = vnorm(g_lightPos);
    double s = clampd(vdot(d, sd), 0.0, 1.0);
    c = vadd(c, vmul(v3(1.0, 0.86, 0.66), pow(s, 220.0) * 5.0));
    /* Small HDR sun disk around the area-light direction. */
    c = vadd(c, vmul(v3(1.0, 0.92, 0.78), pow(s, 2500.0) * 28.0));
    return c;
}

static void basis(V3 n, V3 *t, V3 *b) {
    V3 up = fabs(n.y) < 0.99 ? v3(0, 1, 0) : v3(1, 0, 0);
    *t = vnorm(vcross(up, n));
    *b = vcross(n, *t);
}

static void sample_light(V3 p, int sample, int count, uint32_t *rng,
                         V3 *outL, double *outDist) {
    V3 n = vnorm(vmul(g_lightPos, -1.0)); /* disk faces the scene origin */
    V3 t, b;
    basis(n, &t, &b);

    double u1 = ((double)sample + rnd01(rng)) / (double)count;
    double u2 = fmod((double)sample * 0.61803398875 + rnd01(rng) * 0.37, 1.0);
    double r  = sqrt(u1) * g_lightRadius;
    double a  = 2.0 * PBR_PI * u2;
    V3 sp = vadd(g_lightPos, vadd(vmul(t, cos(a) * r), vmul(b, sin(a) * r)));

    V3 to = vsub(sp, p);
    *outDist = vlen(to);
    *outL = vnorm(to);
}

/* ------------------------------------------------------------------ */
/*  PBR BRDF                                                          */
/* ------------------------------------------------------------------ */

static double D_GGX(double NdotH, double a) {
    double a2 = a * a;
    double d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PBR_PI * d * d + 1e-12);
}

/* Height-correlated Smith visibility term (already divided by 4 NdotL NdotV). */
static double V_SmithGGX(double NdotL, double NdotV, double a) {
    double a2 = a * a;
    double gv = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    double gl = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    return 0.5 / (gv + gl + 1e-12);
}

static V3 F_Schlick(V3 F0, double u) {
    double f = pow(1.0 - clampd(u, 0.0, 1.0), 5.0);
    return vadd(F0, vmul(vsub(v3(1, 1, 1), F0), f));
}

static double fresnel_dielectric(double cosI, double ior) {
    cosI = clampd(cosI, 0.0, 1.0);
    double sinT2 = (1.0 - cosI * cosI) / (ior * ior);
    if (sinT2 >= 1.0) return 1.0; /* total internal reflection */
    double cosT = sqrt(1.0 - sinT2);
    double rs = (cosI - ior * cosT) / (cosI + ior * cosT);
    double rp = (cosT - ior * cosI) / (cosT + ior * cosI);
    return 0.5 * (rs * rs + rp * rp);
}

static V3 reflect_dir(V3 d, V3 n) {
    return vsub(d, vmul(n, 2.0 * vdot(d, n)));
}

static int refract_dir(V3 d, V3 n, double eta, V3 *outT) {
    double cosI = -vdot(n, d);
    double sinT2 = eta * eta * (1.0 - cosI * cosI);
    if (sinT2 > 1.0) return 0;
    double cosT = sqrt(1.0 - sinT2);
    *outT = vadd(vmul(d, eta), vmul(n, eta * cosI - cosT));
    return 1;
}

static V3 jitter_dir(V3 d, double roughness, uint32_t *rng) {
    if (roughness < 0.02) return d;
    V3 t, b;
    basis(d, &t, &b);
    double cone = roughness * roughness * 0.9;
    double a = 2.0 * PBR_PI * rnd01(rng);
    double r = sqrt(rnd01(rng)) * cone;
    return vnorm(vadd(d, vadd(vmul(t, cos(a) * r), vmul(b, sin(a) * r))));
}

/* ------------------------------------------------------------------ */
/*  Shading                                                           */
/* ------------------------------------------------------------------ */

static V3 direct_light(const Hit *h, V3 V, uint32_t *rng, int samples) {
    V3 N = h->n;
    V3 F0 = vlerp(v3(0.04, 0.04, 0.04), h->m.albedo, h->m.metallic);
    double a = h->m.roughness * h->m.roughness;
    if (a < 0.002) a = 0.002;

    double NdotV = fmax(vdot(N, V), 1e-4);
    V3 total = v3(0, 0, 0);

    for (int s = 0; s < samples; ++s) {
        V3 L;
        double dist;
        sample_light(h->p, s, samples, rng, &L, &dist);

        double NdotL = vdot(N, L);
        if (NdotL <= 0.0) continue;

        V3 H = vnorm(vadd(V, L));
        double NdotH = fmax(vdot(N, H), 0.0);
        double VdotH = fmax(vdot(V, H), 0.0);

        V3 F = F_Schlick(F0, VdotH);
        double D = D_GGX(NdotH, a);
        double Vis = V_SmithGGX(NdotL, NdotV, a);

        V3 spec = vmul(F, D * Vis);
        V3 kd = h->m.transmission > 0.01
              ? v3(0, 0, 0)
              : vmul(vsub(v3(1, 1, 1), F), 1.0 - h->m.metallic);
        V3 diff = vmul(vmul3(h->m.albedo, kd), 1.0 / PBR_PI);

        V3 Li = vmul(g_lightColor, g_lightIntensity / (dist * dist + 1e-4));
        V3 origin = vadd(h->p, vmul(N, 0.002));
        if (occluded(origin, L, dist - 0.004)) continue;

        total = vadd(total, vmul3(vadd(diff, spec), vmul(Li, NdotL)));
    }
    return total;
}

static V3 ambient_light(const Hit *h) {
    V3 sky = sky_color(h->n);
    V3 amb = vmul3(h->m.albedo, sky);
    return vmul(amb, 0.22 * (1.0 - h->m.metallic));
}

static V3 trace_ray(V3 o, V3 d, int depth, int insideGlass, uint32_t *rng) {
    Hit h;
    intersect_scene(o, d, &h);
    if (!h.found) return sky_color(d);

    V3 V = vmul(d, -1.0);
    int primary = (depth == g_depth);
    V3 direct = direct_light(&h, V, rng, primary ? g_lightSamples : 1);

    if (h.m.emissive > 0.0)
        return vmul(h.m.albedo, h.m.emissive);

    /* Glass: Fresnel reflection + Snell refraction. */
    if (h.m.transmission > 0.01 && depth > 0) {
        double cosI = fmax(vdot(V, h.n), 0.0);
        double F = fresnel_dielectric(cosI, h.m.ior);

        V3 R = reflect_dir(d, h.n);
        V3 refl = trace_ray(vadd(h.p, vmul(h.n, 0.002)), R,
                           depth - 1, insideGlass, rng);

        double eta = insideGlass ? h.m.ior : 1.0 / h.m.ior;
        V3 T;
        if (refract_dir(d, h.n, eta, &T)) {
            V3 trans = trace_ray(vadd(h.p, vmul(T, 0.002)), T,
                                depth - 1, !insideGlass, rng);
            /* Slight blue-green glass tint; a full implementation would use
               Beer-Lambert with the actual path length through the medium. */
            trans = vmul3(trans, v3(0.92, 0.97, 1.00));
            return vadd(vadd(vmul(refl, F), vmul(trans, 1.0 - F)), direct);
        }
        return vadd(refl, direct); /* total internal reflection */
    }

    V3 color = vadd(direct, ambient_light(&h));

    if (depth > 0) {
        V3 R = reflect_dir(d, h.n);
        if (h.m.metallic > 0.5)
            R = jitter_dir(R, h.m.roughness, rng);
        V3 refl = trace_ray(vadd(h.p, vmul(h.n, 0.002)), R,
                           depth - 1, insideGlass, rng);

        V3 F0 = vlerp(v3(0.04, 0.04, 0.04), h.m.albedo, h.m.metallic);
        V3 F = F_Schlick(F0, fmax(vdot(V, h.n), 0.0));
        double w = luminance(F);
        color = vadd(vmul(color, 1.0 - w), vmul3(refl, F));
    }
    return color;
}

/* ------------------------------------------------------------------ */
/*  Tone mapping                                                      */
/* ------------------------------------------------------------------ */

static double aces_film(double x) {
    double a = x * (2.51 * x + 0.03);
    double b = x * (2.43 * x + 0.59) + 0.14;
    return clampd(a / (b + 1e-12), 0.0, 1.0);
}

static int to_byte(double v) {
    v = aces_film(v * g_exposure);
    double s = v <= 0.0031308 ? 12.92 * v
                              : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
    return (int)(clampd(s, 0.0, 1.0) * 255.0 + 0.5);
}

/* ------------------------------------------------------------------ */
/*  Scene animation                                                   */
/* ------------------------------------------------------------------ */

static Mat make_mat(V3 albedo, double metallic, double roughness,
                    double ior, double transmission) {
    Mat m;
    m.albedo = albedo;
    m.metallic = metallic;
    m.roughness = roughness;
    m.ior = ior;
    m.transmission = transmission;
    m.emissive = 0.0;
    return m;
}

static void scene_setup(double t) {
    double jump = fabs(sin(t * 2.2));
    double a = t * 0.7;

    /* Same four spheres as the original PowerShell tracer, but with PBR
       materials: red center, green orbit, glass opposite, blue small. */
    Mat red   = make_mat(v3(0.40, 0.022, 0.018), 0.0, 0.22, 1.5, 0.0);
    Mat green = make_mat(v3(0.025, 0.36, 0.055), 0.0, 0.40, 1.5, 0.0);
    Mat glass = make_mat(v3(0.96, 0.98, 1.00), 0.0, 0.025, 1.50, 0.96);
    Mat blue  = make_mat(v3(0.020, 0.065, 0.60), 0.0, 0.30, 1.5, 0.0);

    g_spheres[0].c = v3(0, 0.9 * jump, 0);
    g_spheres[0].r = 1.0;
    g_spheres[0].m = red;

    g_spheres[1].c = v3(3.3 * cos(a), -0.30, 3.3 * sin(a));
    g_spheres[1].r = 0.70;
    g_spheres[1].m = green;

    g_spheres[2].c = v3(3.3 * cos(a + PBR_PI), -0.20, 3.3 * sin(a + PBR_PI));
    g_spheres[2].r = 0.80;
    g_spheres[2].m = glass;

    g_spheres[3].c = v3(1.9 * cos(-t * 1.3 + 2.0), -0.65,
                        2.2 + 1.4 * sin(-t * 1.3 + 2.0));
    g_spheres[3].r = 0.35;
    g_spheres[3].m = blue;

    g_lightPos = v3(7.0 * cos(t * 0.4), 6.0, 7.0 * sin(t * 0.4));
    g_lightColor = v3(1.00, 0.82, 0.62);

    Mat bulb = make_mat(v3(1.0, 0.88, 0.68), 0.0, 0.4, 1.5, 0.0);
    bulb.emissive = 9.0;
    g_spheres[4].c = g_lightPos;
    g_spheres[4].r = 0.32;
    g_spheres[4].m = bulb;

    g_planeA = make_mat(v3(0.55, 0.55, 0.57), 0.0, 0.55, 1.5, 0.0);
    g_planeB = make_mat(v3(0.055, 0.06, 0.07), 0.0, 0.70, 1.5, 0.0);
}

/* ------------------------------------------------------------------ */
/*  Camera + row rendering                                            */
/* ------------------------------------------------------------------ */

static void render_rows(int y0, int y1) {
    double aspect = g_camAspect;
    double scale = g_camScale;
    int aa = g_aa;

    for (int y = y0; y < y1; ++y) {
        for (int x = 0; x < g_w; ++x) {
            uint32_t rng = hash32((uint32_t)(x * 1973 + y * 9277) ^
                                  (g_frameSeed * 26699u) ^ 0x9e3779b9u);
            V3 col = v3(0, 0, 0);

            for (int sy = 0; sy < aa; ++sy) {
                for (int sx = 0; sx < aa; ++sx) {
                    double jx = (aa == 1) ? 0.5 : ((double)sx + rnd01(&rng)) / aa;
                    double jy = (aa == 1) ? 0.5 : ((double)sy + rnd01(&rng)) / aa;
                    double u = (2.0 * ((double)x + jx) / g_w - 1.0) * aspect * scale;
                    double v = (1.0 - 2.0 * ((double)y + jy) / g_h) * scale;
                    V3 dir = vnorm(vadd(g_camFwd,
                                        vadd(vmul(g_camRight, u), vmul(g_camUp, v))));
                    col = vadd(col, trace_ray(g_camPos, dir, g_depth, 0, &rng));
                }
            }

            double inv = 1.0 / (double)(aa * aa);
            col = vmul(col, inv);
            size_t o = ((size_t)y * g_w + (size_t)x) * 3u;
            g_hdr[o + 0] = (float)col.x;
            g_hdr[o + 1] = (float)col.y;
            g_hdr[o + 2] = (float)col.z;
        }
    }
}

typedef struct { int y0, y1; } RowJob;

static int row_worker(void *p) {
    RowJob *j = (RowJob *)p;
    render_rows(j->y0, j->y1);
    return 0;
}

static void render_image(float *hdr, int w, int h,
                         double t, int depth, double yaw, double pitch,
                         double dist, int frame) {
    g_hdr = hdr;
    g_w = w;
    g_h = h;
    g_depth = depth;
    g_frameSeed = (uint32_t)frame;

    scene_setup(t);

    V3 target = v3(0, 0.3, 0);
    g_camPos = vadd(target, v3(dist * cos(pitch) * sin(yaw),
                               dist * sin(pitch),
                               dist * cos(pitch) * cos(yaw)));
    g_camFwd = vnorm(vsub(target, g_camPos));
    g_camRight = vnorm(vcross(g_camFwd, v3(0, 1, 0)));
    g_camUp = vcross(g_camRight, g_camFwd);
    g_camAspect = (double)w / (double)h;
    g_camScale = tan(25.0 * PBR_PI / 180.0);

    int threads = SDL_GetNumLogicalCPUCores();
    if (threads < 1) threads = 1;
    if (threads > 32) threads = 32;
    if (threads > h) threads = h;

    if (threads > 1) {
        SDL_Thread *hs[32];
        RowJob jobs[32];
        for (int i = 0; i < threads; ++i) {
            jobs[i].y0 = (h * i) / threads;
            jobs[i].y1 = (h * (i + 1)) / threads;
            hs[i] = SDL_CreateThread(row_worker, "pbr-row", &jobs[i]);
            if (!hs[i]) render_rows(jobs[i].y0, jobs[i].y1);
        }
        for (int i = 0; i < threads; ++i)
            if (hs[i]) SDL_WaitThread(hs[i], NULL);
    } else {
        render_rows(0, h);
    }
}

/* Resolve linear HDR (optionally averaged over `count` frames) to RGBA8. */
static void tonemap_hdr(const float *src, int count, unsigned char *dst,
                        int w, int h) {
    double inv = 1.0 / (double)(count > 0 ? count : 1);
    size_t n = (size_t)w * (size_t)h;
    for (size_t i = 0; i < n; ++i) {
        double r = (double)src[i * 3 + 0] * inv;
        double g = (double)src[i * 3 + 1] * inv;
        double b = (double)src[i * 3 + 2] * inv;
        /* Clamp fireflies; accumulation already averages the rest. */
        r = fmin(r, 32.0); g = fmin(g, 32.0); b = fmin(b, 32.0);
        dst[i * 4 + 0] = (unsigned char)to_byte(r);
        dst[i * 4 + 1] = (unsigned char)to_byte(g);
        dst[i * 4 + 2] = (unsigned char)to_byte(b);
        dst[i * 4 + 3] = 255;
    }
}

/* ------------------------------------------------------------------ */
/*  BMP writer (headless benchmark)                                   */
/* ------------------------------------------------------------------ */

#pragma pack(push, 1)
typedef struct {
    uint16_t type;
    uint32_t size;
    uint16_t r1, r2;
    uint32_t off;
} BmpFileHeader;
typedef struct {
    uint32_t size;
    int32_t  w, h;
    uint16_t planes, bpp;
    uint32_t comp, imageSize;
    int32_t  xppm, yppm;
    uint32_t clrUsed, clrImportant;
} BmpInfoHeader;
#pragma pack(pop)

static int write_bmp(const char *path, const unsigned char *rgba, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int rowSize = (w * 3 + 3) & ~3;
    unsigned char *row = (unsigned char *)calloc((size_t)rowSize, 1);
    if (!row) { fclose(f); return 0; }

    BmpFileHeader fh;
    BmpInfoHeader ih;
    memset(&fh, 0, sizeof(fh));
    memset(&ih, 0, sizeof(ih));
    fh.type = 0x4D42;
    fh.off = 14 + 40;
    fh.size = fh.off + (uint32_t)(rowSize * h);
    ih.size = 40;
    ih.w = w;
    ih.h = h;
    ih.planes = 1;
    ih.bpp = 24;
    ih.imageSize = (uint32_t)(rowSize * h);

    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);
    for (int y = h - 1; y >= 0; --y) {
        for (int x = 0; x < w; ++x) {
            const unsigned char *p = rgba + ((size_t)y * w + x) * 4;
            row[x * 3 + 0] = p[2];
            row[x * 3 + 1] = p[1];
            row[x * 3 + 2] = p[0];
        }
        fwrite(row, (size_t)rowSize, 1, f);
    }
    free(row);
    fclose(f);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

static const int kHeights[] = { 45, 90, 135, 180, 270, 360 };

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "--bench") == 0) {
        const char *out = (argc >= 3) ? argv[2] : "raytracer_pbr.bmp";
        int w = (argc >= 4) ? atoi(argv[3]) : 640;
        int h = (argc >= 5) ? atoi(argv[4]) : 360;
        if (w < 32) w = 32;
        if (h < 32) h = 32;
        unsigned char *pixels = (unsigned char *)malloc((size_t)w * h * 4);
        float *hdr = (float *)malloc((size_t)w * h * 3 * sizeof(float));
        float *accum = (float *)calloc((size_t)w * h * 3, sizeof(float));
        if (!pixels || !hdr || !accum) return 1;
        /* Optional quality override: --bench out.bmp W H AA LIGHTS FRAMES */
        g_aa = (argc >= 6) ? atoi(argv[5]) : 2;
        g_lightSamples = (argc >= 7) ? atoi(argv[6]) : 4;
        int frames = (argc >= 8) ? atoi(argv[7]) : 1;
        if (g_aa < 1) g_aa = 1;
        if (g_aa > 4) g_aa = 4;
        if (g_lightSamples < 1) g_lightSamples = 1;
        if (g_lightSamples > 32) g_lightSamples = 32;
        if (frames < 1) frames = 1;
        if (frames > 256) frames = 256;
        Uint64 start = SDL_GetTicks();
        for (int f = 0; f < frames; ++f) {
            render_image(hdr, w, h, 3.2, 3, 0.35, 0.28, 8.5, f);
            size_t n = (size_t)w * h * 3;
            for (size_t i = 0; i < n; ++i) accum[i] += hdr[i];
        }
        tonemap_hdr(accum, frames, pixels, w, h);
        Uint64 ms = SDL_GetTicks() - start;
        int ok = write_bmp(out, pixels, w, h);
        printf("[pbr] %dx%d %d passes in %llu ms (AA %dx%d, light %d) -> %s (%s)\n",
               w, h, frames, (unsigned long long)ms,
               g_aa, g_aa, g_lightSamples, out, ok ? "ok" : "write failed");
        free(pixels); free(hdr); free(accum);
        return ok ? 0 : 1;
    }

    if (!InitWindow(960, 540, "vlcrlib - PBR ray tracer (C)")) return 1;
    SetTargetFPS(60);

#ifdef _WIN32
    Font uiFont = LoadFontEx("C:/Windows/Fonts/segoeui.ttf", 32, 32, 96);
    if (!uiFont.ok)
        uiFont = LoadFontEx("C:/Windows/Fonts/arial.ttf", 32, 32, 96);
#else
    Font uiFont = LoadFontEx("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                             32, 32, 96);
#endif

    int hIdx = 4;              /* 270p default: less pixelation, still interactive */
    double yaw = 0.35, pitch = 0.28, dist = 8.5;
    /* Start paused so the first visible image is accumulated and clean.
       Space starts the animation (and temporarily disables accumulation). */
    bool paused = true;
    double animT = 3.2;
    int frame = 0, rw = 0, rh = 0;
    unsigned char *pixels = NULL;
    float *hdr = NULL, *accum = NULL;
    int accumCount = 0;
    Texture2D texture = { 0 };
    Vector2 lastMouse = { 0, 0 };
    bool dragging = false;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (!paused) animT += (dt > 0.1f ? 0.1f : dt);

        if (IsKeyPressed(SDL_SCANCODE_SPACE)) { paused = !paused; accumCount = 0; }
        if (IsKeyPressed(SDL_SCANCODE_EQUALS) && hIdx < 5) { hIdx++; accumCount = 0; }
        if (IsKeyPressed(SDL_SCANCODE_MINUS) && hIdx > 0) { hIdx--; accumCount = 0; }
        if (IsKeyPressed(SDL_SCANCODE_R)) { g_depth = (g_depth + 1) % 5; accumCount = 0; }
        if (IsKeyPressed(SDL_SCANCODE_A)) {
            g_aa = (g_aa == 1) ? 2 : (g_aa == 2 ? 4 : 1);
            accumCount = 0;
        }
        if (IsKeyPressed(SDL_SCANCODE_L)) {
            g_lightSamples = (g_lightSamples == 1) ? 4 : (g_lightSamples == 4 ? 8 : 1);
            accumCount = 0;
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            Vector2 m = GetMousePosition();
            if (!dragging) { lastMouse = m; dragging = true; }
            yaw -= (m.x - lastMouse.x) * 0.01f;
            pitch += (m.y - lastMouse.y) * 0.01f;
            if (pitch < 0.03) pitch = 0.03;
            if (pitch > 1.4) pitch = 1.4;
            lastMouse = m;
            accumCount = 0;
        } else {
            dragging = false;
        }
        float wheel = GetMouseWheelMove();
        bool zoomMod = IsKeyDown(SDL_SCANCODE_LSHIFT) || IsKeyDown(SDL_SCANCODE_RSHIFT);
        if (zoomMod && wheel != 0.0f) {
            dist *= (wheel > 0.0f) ? 0.9 : 1.1;
            if (dist < 3.0) dist = 3.0;
            if (dist > 20.0) dist = 20.0;
            accumCount = 0;
        }

        int sw = 0, sh = 0;
        vcore_GetScreenSize(&sw, &sh);
        if (sw < 32) sw = 32;
        if (sh < 32) sh = 32;

        int nh = kHeights[hIdx];
        int nw = (int)((double)nh * (double)sw / (double)sh + 0.5);
        if (nw < 16) nw = 16;
        if (nw != rw || nh != rh) {
            free(pixels); free(hdr); free(accum);
            pixels = (unsigned char *)malloc((size_t)nw * nh * 4);
            hdr = (float *)malloc((size_t)nw * nh * 3 * sizeof(float));
            accum = (float *)calloc((size_t)nw * nh * 3, sizeof(float));
            if (!pixels || !hdr || !accum) break;
            accumCount = 0;
            rw = nw;
            rh = nh;
            if (texture.id) UnloadTexture(texture);
            Image img;
            memset(&img, 0, sizeof(img));
            img.data = pixels;
            img.width = rw;
            img.height = rh;
            img.mipmaps = 1;
            img.format = 0;
            /* The CPU buffer already contains sRGB-encoded bytes. Binding it
               as an sRGB texture makes the sampler linearise before the
               swapchain's sRGB write, avoiding a double gamma. */
            texture = LoadTextureFromImageSRGB(img);
        }

        /* Animating: one frame, no reprojection. Paused: accumulate jittered
           AA/light samples until the image converges (up to 128 frames). */
        if (!paused) {
            render_image(hdr, rw, rh, animT, g_depth, yaw, pitch, dist, frame);
            tonemap_hdr(hdr, 1, pixels, rw, rh);
            accumCount = 0;
            ++frame;
        } else if (accumCount < 128) {
            render_image(hdr, rw, rh, animT, g_depth, yaw, pitch, dist, frame);
            if (accumCount == 0)
                memset(accum, 0, (size_t)rw * rh * 3 * sizeof(float));
            size_t n = (size_t)rw * rh * 3;
            for (size_t i = 0; i < n; ++i) accum[i] += hdr[i];
            ++accumCount;
            tonemap_hdr(accum, accumCount, pixels, rw, rh);
            ++frame;
        }
        if (texture.id) UpdateTexture(texture, pixels);

        BeginDrawing();
        ClearBackground((Color){ 8, 10, 15, 255 });

        if (texture.id) {
            double s = (double)sw / rw;
            double sy = (double)sh / rh;
            if (sy < s) s = sy;
            int dw = (int)(rw * s + 0.5);
            int dh = (int)(rh * s + 0.5);
            /* DrawTexturePro in this engine ignores dest size; DrawTextureEx
               scales the full texture, which is what this demo needs. */
            DrawTextureEx(texture,
                          (Vector2){ (float)((sw - dw) / 2), (float)((sh - dh) / 2) },
                          0.0f, (float)s, WHITE);
        }

        DrawRectangle(0, 0, 910, 86, (Color){ 0, 0, 0, 235 });
        DrawText("PBR RAY TRACER  (C, CPU)", 16, 10, 20, WHITE);

        char line[256];
        int spp = accumCount * g_aa * g_aa;
        if (paused) {
            snprintf(line, sizeof(line),
                     "%d FPS   %dx%d   AA %dx%d   light %d   depth %d   dist %.1f   PAUSED%s  %d spp",
                     GetFPS(), rw, rh, g_aa, g_aa, g_lightSamples, g_depth,
                     dist, accumCount >= 128 ? " (DONE)" : "", spp);
        } else {
            snprintf(line, sizeof(line),
                     "%d FPS   %dx%d   AA %dx%d   light %d   depth %d   ANIMATING",
                     GetFPS(), rw, rh, g_aa, g_aa, g_lightSamples, g_depth);
        }
        DrawText(line, 16, 38, 16, (Color){ 190, 210, 235, 255 });
        DrawText("drag: orbit   Shift+wheel: zoom   +/-: resolution   A: AA   L: light   R: depth   Space: animate/pause",
                 16, 60, 14, (Color){ 150, 168, 190, 255 });

        DrawRectangle(0, sh - 30, sw, 30, (Color){ 0, 0, 0, 235 });
        DrawText("GGX + Smith + Schlick | area light soft shadows | glass IOR | ACES",
                 16, sh - 23, 14, (Color){ 160, 176, 198, 255 });

        EndDrawing();
    }

    if (texture.id) UnloadTexture(texture);
    if (uiFont.ok) UnloadFont(uiFont);
    free(pixels); free(hdr); free(accum);
    CloseWindow();
    return 0;
}
