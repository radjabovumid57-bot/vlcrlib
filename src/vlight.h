#ifndef VLCRLIB_VLIGHT_H
#define VLCRLIB_VLIGHT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "vcore.h"

typedef enum VLightType {
    VLIGHT_DIRECTIONAL = 0,
    VLIGHT_POINT,
    VLIGHT_SPOT,
    VLIGHT_AMBIENT
} VLightType;

typedef struct VLight {
    VLightType type;
    Vector3 position;
    Vector3 direction;
    Color color;
    float intensity;
    float range;
    float innerCone;
    float outerCone;
} VLight;

typedef struct VLight2D {
    Vector2 position;
    Vector2 direction;
    Color color;
    float intensity;
    float range;
    float innerCone;
    float outerCone;
} VLight2D;

VLight VLightDirectional(Vector3 direction, Color color, float intensity);
VLight VLightPoint(Vector3 position, Color color, float intensity, float range);
VLight VLightSpot(Vector3 position, Vector3 direction, Color color,
                  float intensity, float range, float innerCone, float outerCone);
VLight VLightAmbient(Color color, float intensity);

VLight2D VLight2DPoint(Vector2 position, Color color, float intensity, float range);
VLight2D VLight2DSpot(Vector2 position, Vector2 direction, Color color,
                      float intensity, float range, float innerCone, float outerCone);

float VLightAttenuation(float distance, float range);
float VLightSpotFactor(Vector3 lightToPoint, Vector3 direction,
                       float innerCone, float outerCone);
float VLight2DSpotFactor(Vector2 lightToPoint, Vector2 direction,
                         float innerCone, float outerCone);
Color VLightColorAt2D(VLight2D light, Vector2 point);
Color VLightColorAt3D(VLight light, Vector3 point, Vector3 normal);

#ifdef __cplusplus
}
#endif
#endif
