/* ==================================================================
 *  vfastgltf_lib_stub.c — link-time fallback for the fastgltf wrapper.
 * ==================================================================
 *
 * vfastgltf_lib.h documents vfastgltf_lib_version() as ALWAYS linkable:
 *
 *     "when the wrapper was not compiled, this returns
 *      'fastgltf backend not compiled' so a caller can log which backend
 *      is actually present without a link error."
 *
 * Nothing implemented that promise, so any program that logged the backend
 * (including scripts/probe_gltf.c) failed to link in a default build.
 *
 * VLCRLIB_HAS_FASTGLTF comes from the generated vlcrlib_config.h. CMake
 * compiles this file only when the real wrapper (vfastgltf_lib.cpp) is NOT
 * part of the build, so the two definitions can never collide.
 */
#include "vfastgltf_lib.h"

#if !defined(VLCRLIB_HAS_FASTGLTF) || !VLCRLIB_HAS_FASTGLTF

const char *vfastgltf_lib_version(void) {
    return "fastgltf backend not compiled";
}

#endif /* !VLCRLIB_HAS_FASTGLTF */
