/*
 * vibl.c — CPU generation of BRDF LUT, irradiance & prefiltered cubemaps for IBL.
 */
#include "vibl.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* stbi_loadf declared in stb_image.h — implementation in vtextures.c */
#include "stb_image.h"

static float clampf(float v, float a, float b) {
    return v < a ? a : (v > b ? b : v);
}

static float radicalInverse_VdC(unsigned int bits) {
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
    bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
    bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);
    return (float)bits * 2.3283064365386963e-10f;
}

static void hammersley(unsigned int i, unsigned int N, float *out) {
    out[0] = (float)i / (float)N;
    out[1] = radicalInverse_VdC(i);
}

static void importanceSampleGGX(float Xi0, float Xi1, float roughness, float N[3], float H[3]) {
    float a = roughness * roughness;
    float phi = 2.f * (float)M_PI * Xi0;
    float cosTheta = sqrtf((1.f - Xi1) / (1.f + (a * a - 1.f) * Xi1));
    float sinTheta = sqrtf(1.f - cosTheta * cosTheta);
    float Htmp[3] = { cosf(phi) * sinTheta, sinf(phi) * sinTheta, cosTheta };
    float up[3] = { fabsf(N[2]) < 0.999f ? 0.f : 1.f, 0.f, fabsf(N[2]) < 0.999f ? 1.f : 0.f };
    if (fabsf(N[2]) < 0.999f) { up[0] = 0; up[1] = 1; up[2] = 0; }
    else { up[0] = 1; up[1] = 0; up[2] = 0; }
    float T[3] = {
        up[1]*N[2] - up[2]*N[1],
        up[2]*N[0] - up[0]*N[2],
        up[0]*N[1] - up[1]*N[0]
    };
    float tlen = sqrtf(T[0]*T[0]+T[1]*T[1]+T[2]*T[2]);
    if (tlen > 1e-6f) { T[0]/=tlen; T[1]/=tlen; T[2]/=tlen; }
    float B[3] = { N[1]*T[2]-N[2]*T[1], N[2]*T[0]-N[0]*T[2], N[0]*T[1]-N[1]*T[0] };
    H[0] = T[0]*Htmp[0] + B[0]*Htmp[1] + N[0]*Htmp[2];
    H[1] = T[1]*Htmp[0] + B[1]*Htmp[1] + N[1]*Htmp[2];
    H[2] = T[2]*Htmp[0] + B[2]*Htmp[1] + N[2]*Htmp[2];
    float hl = sqrtf(H[0]*H[0]+H[1]*H[1]+H[2]*H[2]);
    H[0]/=hl; H[1]/=hl; H[2]/=hl;
}

static float GeometrySchlickGGX(float NdotV, float roughness) {
    float a = roughness;
    float k = (a * a) / 2.f;
    return NdotV / (NdotV * (1.f - k) + k);
}

static float GeometrySmith(float NdotV, float NdotL, float roughness) {
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

VIBL_LUT VIBL_GenerateBRDFLUT(int size) {
    VIBL_LUT lut = {0};
    if (size < 16) size = 64;
    lut.size = size;
    lut.data = (float *)malloc(sizeof(float) * (size_t)size * size * 2);
    if (!lut.data) return lut;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float NdotV = ((float)x + 0.5f) / (float)size;
            float roughness = ((float)y + 0.5f) / (float)size;
            NdotV = clampf(NdotV, 0.001f, 1.f);
            float scale, bias;
            /* inline integrate to avoid maxf order issue */
            float V[3] = { sqrtf(1.f - NdotV * NdotV), 0.f, NdotV };
            float A = 0.f, B = 0.f;
            const unsigned SAMPLE = 64;
            float N[3] = { 0, 0, 1 };
            for (unsigned i = 0; i < SAMPLE; ++i) {
                float Xi[2]; hammersley(i, SAMPLE, Xi);
                float H[3]; importanceSampleGGX(Xi[0], Xi[1], roughness, N, H);
                float VdotH = V[0]*H[0]+V[1]*H[1]+V[2]*H[2];
                float L[3] = { 2.f*VdotH*H[0]-V[0], 2.f*VdotH*H[1]-V[1], 2.f*VdotH*H[2]-V[2] };
                float NdotL = clampf(L[2], 0.f, 1.f);
                float NdotH = clampf(H[2], 0.f, 1.f);
                float vdoth = clampf(VdotH, 0.f, 1.f);
                if (NdotL > 0.f) {
                    float G = GeometrySmith(NdotV, NdotL, roughness);
                    float G_Vis = (G * vdoth) / (NdotH * NdotV < 1e-4f ? 1e-4f : NdotH * NdotV);
                    float Fc = powf(1.f - vdoth, 5.f);
                    A += (1.f - Fc) * G_Vis;
                    B += Fc * G_Vis;
                }
            }
            scale = A / (float)SAMPLE;
            bias  = B / (float)SAMPLE;
            int idx = (y * size + x) * 2;
            lut.data[idx] = scale;
            lut.data[idx + 1] = bias;
        }
    }
    fprintf(stderr, "[vibl] BRDF LUT %dx%d generated\n", size, size);
    return lut;
}

/* Cubemap face directions: +X -X +Y -Y +Z -Z */
static void faceDir(int face, float u, float v, float d[3]) {
    /* u,v in [-1,1] */
    switch (face) {
    case 0: d[0]= 1; d[1]=-v; d[2]=-u; break;
    case 1: d[0]=-1; d[1]=-v; d[2]= u; break;
    case 2: d[0]= u; d[1]= 1; d[2]= v; break;
    case 3: d[0]= u; d[1]=-1; d[2]=-v; break;
    case 4: d[0]= u; d[1]=-v; d[2]= 1; break;
    default:d[0]=-u; d[1]=-v; d[2]=-1; break;
    }
    float len = sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    d[0]/=len; d[1]/=len; d[2]/=len;
}

static void sampleEquirect(const float *hdr, int w, int h, float dx, float dy, float dz, float rgb[3]) {
    float phi = atan2f(dz, dx);
    float theta = asinf(clampf(dy, -1.f, 1.f));
    float u = (phi / (2.f * (float)M_PI) + 0.5f);
    float v = 0.5f - theta / (float)M_PI;
    u = u - floorf(u);
    v = clampf(v, 0.f, 1.f);
    float x = u * (w - 1);
    float y = v * (h - 1);
    int x0 = (int)x, y0 = (int)y;
    int x1 = x0 + 1 < w ? x0 + 1 : x0;
    int y1 = y0 + 1 < h ? y0 + 1 : y0;
    float fx = x - x0, fy = y - y0;
    const float *p00 = hdr + ((y0 * w + x0) * 3);
    const float *p10 = hdr + ((y0 * w + x1) * 3);
    const float *p01 = hdr + ((y1 * w + x0) * 3);
    const float *p11 = hdr + ((y1 * w + x1) * 3);
    for (int c = 0; c < 3; ++c) {
        float a = p00[c] * (1-fx) + p10[c] * fx;
        float b = p01[c] * (1-fx) + p11[c] * fx;
        rgb[c] = a * (1-fy) + b * fy;
    }
}

static void proceduralEnv(float dx, float dy, float dz, float rgb[3]) {
    float horizon = clampf(dy * 0.5f + 0.5f, 0.f, 1.f);
    float ground[3] = {0.04f, 0.04f, 0.05f};
    float skyL[3] = {0.12f, 0.18f, 0.28f};
    float skyH[3] = {0.55f, 0.72f, 1.05f};
    float t = horizon < 0.5f ? horizon * 2.f : 1.f;
    float sky[3];
    for (int i=0;i<3;++i) sky[i] = skyL[i]*(1-t)+skyH[i]*t;
    float h = horizon;
    for (int i=0;i<3;++i)
        rgb[i] = ground[i]*(1.f-h) + sky[i]*h;
    /* key light */
    float kx=-0.4f, ky=0.85f, kz=-0.3f;
    float kd = dx*kx+dy*ky+dz*kz;
    if (kd < 0) kd = 0;
    float key = powf(kd, 8.f);
    rgb[0]+=8.f*key; rgb[1]+=7.5f*key; rgb[2]+=6.5f*key;
}

static float *allocFace(int size) {
    return (float *)calloc((size_t)size * size * 3, sizeof(float));
}

static VIBL_Cube buildCubeFromSampler(int size, int mips,
    void (*sample)(float,float,float,float*),
    const float *hdr, int hw, int hh, int useHdr) {
    VIBL_Cube c = {0};
    c.size = size;
    c.mips = mips;
    c.faces = (float **)calloc((size_t)mips * 6, sizeof(float *));
    if (!c.faces) return c;
    for (int m = 0; m < mips; ++m) {
        int s = size >> m;
        if (s < 1) s = 1;
        float roughness = mips > 1 ? (float)m / (float)(mips - 1) : 0.f;
        for (int face = 0; face < 6; ++face) {
            float *data = allocFace(s);
            c.faces[m * 6 + face] = data;
            for (int y = 0; y < s; ++y) {
                for (int x = 0; x < s; ++x) {
                    float u = (2.f * ((float)x + 0.5f) / (float)s) - 1.f;
                    float v = (2.f * ((float)y + 0.5f) / (float)s) - 1.f;
                    float d[3]; faceDir(face, u, v, d);
                    float rgb[3] = {0,0,0};
                    if (m == 0 || mips == 1) {
                        if (useHdr) sampleEquirect(hdr, hw, hh, d[0], d[1], d[2], rgb);
                        else proceduralEnv(d[0], d[1], d[2], rgb);
                    } else {
                        /* cheap prefilter: average neighborhood in dir space */
                        int samples = 16;
                        for (int i = 0; i < samples; ++i) {
                            float Xi[2]; hammersley((unsigned)i, (unsigned)samples, Xi);
                            float H[3]; importanceSampleGGX(Xi[0], Xi[1], roughness, d, H);
                            float VdotH = d[0]*H[0]+d[1]*H[1]+d[2]*H[2];
                            float L[3] = {2.f*VdotH*H[0]-d[0], 2.f*VdotH*H[1]-d[1], 2.f*VdotH*H[2]-d[2]};
                            float llen = sqrtf(L[0]*L[0]+L[1]*L[1]+L[2]*L[2]);
                            L[0]/=llen; L[1]/=llen; L[2]/=llen;
                            float srgb[3];
                            if (useHdr) sampleEquirect(hdr, hw, hh, L[0], L[1], L[2], srgb);
                            else proceduralEnv(L[0], L[1], L[2], srgb);
                            rgb[0]+=srgb[0]; rgb[1]+=srgb[1]; rgb[2]+=srgb[2];
                        }
                        rgb[0]/=samples; rgb[1]/=samples; rgb[2]/=samples;
                    }
                    int idx = (y * s + x) * 3;
                    data[idx]=rgb[0]; data[idx+1]=rgb[1]; data[idx+2]=rgb[2];
                }
            }
        }
    }
    return c;
}

static VIBL_Cube buildIrradiance(int irrSize, int srcSize, float **srcFaces) {
    VIBL_Cube c = {0};
    c.size = irrSize;
    c.mips = 1;
    c.faces = (float **)calloc(6, sizeof(float *));
    const int samples = 32;
    for (int face = 0; face < 6; ++face) {
        float *data = allocFace(irrSize);
        c.faces[face] = data;
        for (int y = 0; y < irrSize; ++y) {
            for (int x = 0; x < irrSize; ++x) {
                float u = (2.f * ((float)x + 0.5f) / (float)irrSize) - 1.f;
                float v = (2.f * ((float)y + 0.5f) / (float)irrSize) - 1.f;
                float N[3]; faceDir(face, u, v, N);
                float rgb[3] = {0,0,0};
                float weight = 0.f;
                for (int i = 0; i < samples; ++i) {
                    float Xi[2]; hammersley((unsigned)i, (unsigned)samples, Xi);
                    float phi = Xi[0] * 2.f * (float)M_PI;
                    float cosTheta = 1.f - Xi[1];
                    float sinTheta = sqrtf(1.f - cosTheta * cosTheta);
                    float up[3] = {0,1,0};
                    if (fabsf(N[1]) > 0.9f) { up[0]=1; up[1]=0; up[2]=0; }
                    float T[3] = { up[1]*N[2]-up[2]*N[1], up[2]*N[0]-up[0]*N[2], up[0]*N[1]-up[1]*N[0] };
                    float tl = sqrtf(T[0]*T[0]+T[1]*T[1]+T[2]*T[2]);
                    T[0]/=tl; T[1]/=tl; T[2]/=tl;
                    float B[3] = { N[1]*T[2]-N[2]*T[1], N[2]*T[0]-N[0]*T[2], N[0]*T[1]-N[1]*T[0] };
                    float L[3] = {
                        T[0]*cosf(phi)*sinTheta + B[0]*sinf(phi)*sinTheta + N[0]*cosTheta,
                        T[1]*cosf(phi)*sinTheta + B[1]*sinf(phi)*sinTheta + N[1]*cosTheta,
                        T[2]*cosf(phi)*sinTheta + B[2]*sinf(phi)*sinTheta + N[2]*cosTheta
                    };
                    /* sample from +Z face approx using equirect-style from cube face 0 mip */
                    /* use procedural/env via direction - sample src face by dir */
                    /* Find dominant face */
                    float ax = fabsf(L[0]), ay = fabsf(L[1]), az = fabsf(L[2]);
                    int sf = 0;
                    float sc = L[0];
                    if (ay > ax && ay > az) { sf = L[1]>0?2:3; sc = L[1]; }
                    else if (az > ax && az > ay) { sf = L[2]>0?4:5; sc = L[2]; }
                    else { sf = L[0]>0?0:1; sc = L[0]; }
                    float inv = 1.f / (fabsf(sc) + 1e-6f);
                    float uu, vv;
                    if (sf == 0) { uu = -L[2]*inv; vv = -L[1]*inv; }
                    else if (sf == 1) { uu =  L[2]*inv; vv = -L[1]*inv; }
                    else if (sf == 2) { uu =  L[0]*inv; vv =  L[2]*inv; }
                    else if (sf == 3) { uu =  L[0]*inv; vv = -L[2]*inv; }
                    else if (sf == 4) { uu =  L[0]*inv; vv = -L[1]*inv; }
                    else { uu = -L[0]*inv; vv = -L[1]*inv; }
                    int px = (int)((uu * 0.5f + 0.5f) * (srcSize - 1));
                    int py = (int)((vv * 0.5f + 0.5f) * (srcSize - 1));
                    px = px < 0 ? 0 : (px >= srcSize ? srcSize-1 : px);
                    py = py < 0 ? 0 : (py >= srcSize ? srcSize-1 : py);
                    const float *sp = srcFaces[sf] + (py * srcSize + px) * 3;
                    float ndl = cosTheta;
                    rgb[0]+=sp[0]*ndl; rgb[1]+=sp[1]*ndl; rgb[2]+=sp[2]*ndl;
                    weight += ndl;
                }
                if (weight > 1e-4f) { rgb[0]/=weight; rgb[1]/=weight; rgb[2]/=weight; }
                rgb[0]*=(float)M_PI; rgb[1]*=(float)M_PI; rgb[2]*=(float)M_PI;
                int idx = (y * irrSize + x) * 3;
                data[idx]=rgb[0]; data[idx+1]=rgb[1]; data[idx+2]=rgb[2];
            }
        }
    }
    return c;
}


/* ================================================================== */
/*  Public HDR loaders                                                 */
/* ================================================================== */

float *VIBL_LoadHDRFile(const char *path, int *outW, int *outH) {
    if (outW) *outW = 0;
    if (outH) *outH = 0;
    if (!path || !path[0]) return NULL;
    int w = 0, h = 0, n = 0;
    float *hdr = stbi_loadf(path, &w, &h, &n, 3);
    if (!hdr) {
        fprintf(stderr, "[vibl] stbi_loadf failed: %s\n", path);
        return NULL;
    }
    if (outW) *outW = w;
    if (outH) *outH = h;
    fprintf(stderr, "[vibl] HDR loaded %s (%dx%d, %d ch -> RGB)\n", path, w, h, n);
    return hdr;
}

float *VIBL_LoadHDRMemory(const void *data, int dataSize, int *outW, int *outH) {
    if (outW) *outW = 0;
    if (outH) *outH = 0;
    if (!data || dataSize <= 0) return NULL;
    int w = 0, h = 0, n = 0;
    float *hdr = stbi_loadf_from_memory((const stbi_uc *)data, dataSize, &w, &h, &n, 3);
    if (!hdr) {
        fprintf(stderr, "[vibl] stbi_loadf_from_memory failed (%d bytes)\n", dataSize);
        return NULL;
    }
    if (outW) *outW = w;
    if (outH) *outH = h;
    return hdr;
}

void VIBL_FreeHDR(float *pixels) {
    if (pixels) stbi_image_free(pixels);
}

VIBL_Cube VIBL_EquirectToCubemap(const float *hdr, int hdrW, int hdrH, int cubeSize) {
    if (!hdr || hdrW < 2 || hdrH < 2) {
        VIBL_Cube z = {0};
        return z;
    }
    if (cubeSize < 8) cubeSize = 64;
    return buildCubeFromSampler(cubeSize, 1, NULL, hdr, hdrW, hdrH, 1);
}

VIBL_Cube VIBL_LoadHDRCubemap(const char *path, int cubeSize) {
    int w = 0, h = 0;
    float *hdr = VIBL_LoadHDRFile(path, &w, &h);
    if (!hdr) {
        VIBL_Cube z = {0};
        return z;
    }
    VIBL_Cube cube = VIBL_EquirectToCubemap(hdr, w, h, cubeSize);
    VIBL_FreeHDR(hdr);
    if (cube.faces)
        fprintf(stderr, "[vibl] HDR cubemap OK: %s face=%d\n", path, cube.size);
    return cube;
}

void VIBL_PrintCubeInfo(const VIBL_Cube *cube, const char *name) {
    if (!cube || !cube->faces) {
        fprintf(stderr, "[vibl] %s: empty\n", name ? name : "cube");
        return;
    }
    fprintf(stderr, "[vibl] %s: size=%d mips=%d\n", name ? name : "cube", cube->size, cube->mips);
    for (int f = 0; f < 6; ++f) {
        const float *p = cube->faces[f];
        if (!p) continue;
        double sum = 0;
        int n = cube->size * cube->size;
        for (int i = 0; i < n; ++i)
            sum += p[i * 3] + p[i * 3 + 1] + p[i * 3 + 2];
        fprintf(stderr, "  face %d avg_lum=%.4f\n", f, sum / (3.0 * n));
    }
}


VIBL_Bundle VIBL_BuildProcedural(int cubeSize, int irrSize) {
    VIBL_Bundle b = {0};
    if (cubeSize < 16) cubeSize = 64;
    if (irrSize < 8) irrSize = 16;
    int mips = 1;
    int s = cubeSize;
    while (s > 4) { mips++; s >>= 1; }
    b.brdf = VIBL_GenerateBRDFLUT(128);
    b.environment = buildCubeFromSampler(cubeSize, 1, NULL, NULL, 0, 0, 0);
    b.prefiltered = buildCubeFromSampler(cubeSize, mips, NULL, NULL, 0, 0, 0);
    b.irradiance = buildIrradiance(irrSize, cubeSize,
        b.environment.faces ? b.environment.faces : b.prefiltered.faces);
    b.valid = b.brdf.data && b.prefiltered.faces && b.irradiance.faces;
    snprintf(b.sourcePath, sizeof(b.sourcePath), "procedural");
    fprintf(stderr, "[vibl] procedural IBL cube=%d irr=%d mips=%d\n", cubeSize, irrSize, mips);
    return b;
}

VIBL_Bundle VIBL_BuildFromHDRFile(const char *path, int cubeSize, int irrSize) {
    VIBL_Bundle b = {0};
    if (!path || !path[0]) return VIBL_BuildProcedural(cubeSize, irrSize);

    int w = 0, h = 0;
    float *hdr = VIBL_LoadHDRFile(path, &w, &h);
    if (!hdr) {
        fprintf(stderr, "[vibl] failed to load HDR: %s — using procedural\n", path);
        return VIBL_BuildProcedural(cubeSize, irrSize);
    }

    if (cubeSize < 16) cubeSize = 64;
    if (irrSize < 8) irrSize = 16;
    int mips = 1;
    int s = cubeSize;
    while (s > 4) { mips++; s >>= 1; }

    if (path) {
        size_t i = 0;
        for (; path[i] && i + 1 < sizeof(b.sourcePath); ++i)
            b.sourcePath[i] = path[i];
        b.sourcePath[i] = 0;
    }

    b.brdf = VIBL_GenerateBRDFLUT(128);
    /* env = sharp cubemap (1 mip) from HDR */
    b.environment = VIBL_EquirectToCubemap(hdr, w, h, cubeSize);
    /* prefiltered = multi-mip from same HDR */
    b.prefiltered = buildCubeFromSampler(cubeSize, mips, NULL, hdr, w, h, 1);
    b.irradiance = buildIrradiance(irrSize, cubeSize, b.environment.faces
                                   ? b.environment.faces : b.prefiltered.faces);
    b.valid = b.brdf.data && b.prefiltered.faces && b.irradiance.faces && b.environment.faces;

    VIBL_FreeHDR(hdr);
    VIBL_PrintCubeInfo(&b.environment, "environment");
    fprintf(stderr, "[vibl] HDR IBL complete: %s cube=%d irr=%d mips=%d\n",
            path, cubeSize, irrSize, mips);
    return b;
}

void VIBL_FreeLUT(VIBL_LUT *lut) {
    if (!lut) return;
    free(lut->data); lut->data = NULL; lut->size = 0;
}

void VIBL_FreeCube(VIBL_Cube *cube) {
    if (!cube || !cube->faces) return;
    int n = cube->mips * 6;
    if (cube->mips < 1) n = 6;
    for (int i = 0; i < n; ++i) free(cube->faces[i]);
    free(cube->faces); cube->faces = NULL; cube->size = 0; cube->mips = 0;
}

void VIBL_FreeBundle(VIBL_Bundle *b) {
    if (!b) return;
    VIBL_FreeLUT(&b->brdf);
    VIBL_FreeCube(&b->environment);
    VIBL_FreeCube(&b->irradiance);
    VIBL_FreeCube(&b->prefiltered);
    b->valid = false;
}
