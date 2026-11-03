#ifndef VLCRLIB_VIBL_H
#define VLCRLIB_VIBL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/*  CPU-side IBL / HDR cubemap data (linear float RGB)                */
/* ------------------------------------------------------------------ */

typedef struct VIBL_Cube {
    int    size;     /* face width = height */
    int    mips;     /* 1 for env/irradiance; >1 for prefiltered */
    float **faces;   /* [mip * 6 + face] → size*size*3 floats */
} VIBL_Cube;

typedef struct VIBL_LUT {
    int    size;
    float *data;     /* size*size*2 → (scale, bias) */
} VIBL_LUT;

typedef struct VIBL_Bundle {
    VIBL_LUT  brdf;
    VIBL_Cube environment;  /* source env cubemap (mip 0 from HDR) */
    VIBL_Cube irradiance;
    VIBL_Cube prefiltered;
    bool      valid;
    char      sourcePath[512];
} VIBL_Bundle;

/* Face order: 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z (Vulkan/OpenGL style) */

/* --- HDR loading -------------------------------------------------- */

/* Load Radiance .hdr (equirectangular) via stbi_loadf.
 * Returns float RGB buffer; free with VIBL_FreeHDR.
 * outW/outH receive dimensions. */
float *VIBL_LoadHDRFile(const char *path, int *outW, int *outH);

float *VIBL_LoadHDRMemory(const void *data, int dataSize, int *outW, int *outH);

void VIBL_FreeHDR(float *pixels);

/* Convert equirectangular HDR → cubemap faces (float RGB). */
VIBL_Cube VIBL_EquirectToCubemap(const float *hdr, int hdrW, int hdrH, int cubeSize);

/* Load .hdr file and convert to cubemap in one call. */
VIBL_Cube VIBL_LoadHDRCubemap(const char *path, int cubeSize);

/* --- Full IBL bake ------------------------------------------------ */

VIBL_LUT    VIBL_GenerateBRDFLUT(int size);
VIBL_Bundle VIBL_BuildFromHDRFile(const char *path, int cubeSize, int irrSize);
VIBL_Bundle VIBL_BuildProcedural(int cubeSize, int irrSize);

void VIBL_FreeBundle(VIBL_Bundle *b);
void VIBL_FreeLUT(VIBL_LUT *lut);
void VIBL_FreeCube(VIBL_Cube *cube);

/* Debug: print face luminance stats */
void VIBL_PrintCubeInfo(const VIBL_Cube *cube, const char *name);

#ifdef __cplusplus
}
#endif
#endif
