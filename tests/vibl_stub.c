/* Link stubs for the standalone CPU raster unit test.
 *
 * vcpu.c owns an optional VIBL bundle but the raster test never loads IBL; the
 * real implementations live in src/vibl.c, which pulls in stb_image. */
#include "vibl.h"

void VIBL_FreeLUT(VIBL_LUT *lut) { (void)lut; }
void VIBL_FreeCube(VIBL_Cube *cube) { (void)cube; }
void VIBL_FreeBundle(VIBL_Bundle *bundle) { (void)bundle; }
