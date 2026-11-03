#ifndef VLCRLIB_H
#define VLCRLIB_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Modules ---------- */
#include "vplatform.h"
#include "vmath.h"
#include "vcore.h"
#include "vcpu.h"
#include "vgraphics.h"
#include "vanimator.h"
#include "vlmaterial.h"
#include "vtextures.h"
#include "vshapes.h"
#include "vtext.h"
#include "vgui.h"
#include "vbezier.h"
#include "vfiles.h"
#include "vparticles.h"
#include "vaudio3d.h"
#include "vlight.h"
#include "vaudio.h"
#include "vnetwork.h"
#include "vinput.h"

/* ---------- C++ С„РёР·РёРєР° вЂ” РїСѓР±Р»РёС‡РЅС‹Рµ C ABI Р·Р°РіРѕР»РѕРІРєРё ---------- */
#include "vphysics.h"
#include "vphysics2d.h"
#include "vjolt.h"   /* Jolt Physics (optional; needs vcpkg joltphysics) */

/* ---------- Named colors ---------- */
#define LIGHTGRAY  VLCRLIB_COMPOUND_LITERAL(Color){ 200,200,200,255 }
#define GRAY       VLCRLIB_COMPOUND_LITERAL(Color){ 130,130,130,255 }
#define DARKGRAY   VLCRLIB_COMPOUND_LITERAL(Color){  80, 80, 80,255 }
#define YELLOW     VLCRLIB_COMPOUND_LITERAL(Color){ 253,249,  0,255 }
#define GOLD       VLCRLIB_COMPOUND_LITERAL(Color){ 255,203,  0,255 }
#define ORANGE     VLCRLIB_COMPOUND_LITERAL(Color){ 255,161,  0,255 }
#define PINK       VLCRLIB_COMPOUND_LITERAL(Color){ 255,109,194,255 }
#define RED        VLCRLIB_COMPOUND_LITERAL(Color){ 230, 41, 55,255 }
#define MAROON     VLCRLIB_COMPOUND_LITERAL(Color){ 190, 33, 55,255 }
#define GREEN      VLCRLIB_COMPOUND_LITERAL(Color){   0,228, 48,255 }
#define LIME       VLCRLIB_COMPOUND_LITERAL(Color){   0,158, 47,255 }
#define DARKGREEN  VLCRLIB_COMPOUND_LITERAL(Color){   0,117, 44,255 }
#define SKYBLUE    VLCRLIB_COMPOUND_LITERAL(Color){ 102,191,255,255 }
#define BLUE       VLCRLIB_COMPOUND_LITERAL(Color){   0,121,241,255 }
#define DARKBLUE   VLCRLIB_COMPOUND_LITERAL(Color){   0, 82,172,255 }
#define PURPLE     VLCRLIB_COMPOUND_LITERAL(Color){ 200,122,255,255 }
#define VIOLET     VLCRLIB_COMPOUND_LITERAL(Color){ 135, 60,190,255 }
#define DARKPURPLE VLCRLIB_COMPOUND_LITERAL(Color){ 112, 31,126,255 }
#define BEIGE      VLCRLIB_COMPOUND_LITERAL(Color){ 211,176,131,255 }
#define BROWN      VLCRLIB_COMPOUND_LITERAL(Color){ 127,106, 79,255 }
#define DARKBROWN  VLCRLIB_COMPOUND_LITERAL(Color){  76, 63, 47,255 }
#define WHITE      VLCRLIB_COMPOUND_LITERAL(Color){ 255,255,255,255 }
#define BLACK      VLCRLIB_COMPOUND_LITERAL(Color){   0,  0,  0,255 }
#define BLANK      VLCRLIB_COMPOUND_LITERAL(Color){   0,  0,  0,  0 }
#define MAGENTA    VLCRLIB_COMPOUND_LITERAL(Color){ 255,  0,255,255 }
#define RAYWHITE   VLCRLIB_COMPOUND_LITERAL(Color){ 245,245,245,255 }

#ifdef __cplusplus
}
#endif
#endif /* VLCRLIB_H */
