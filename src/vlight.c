#include "vlight.h"
#include <math.h>

static float clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static unsigned char lightChannel(unsigned char channel, float factor) {
    float value = (float)channel * clamp01(factor);
    if (value > 255.0f) value = 255.0f;
    return (unsigned char)value;
}

VLight VLightDirectional(Vector3 direction, Color color, float intensity) {
    return (VLight){ VLIGHT_DIRECTIONAL, {0, 0, 0}, Vector3Normalize(direction),
                     color, intensity, 0.0f, 0.0f, 0.0f };
}

VLight VLightPoint(Vector3 position, Color color, float intensity, float range) {
    return (VLight){ VLIGHT_POINT, position, {0, 0, 0},
                     color, intensity, range, 0.0f, 0.0f };
}

VLight VLightSpot(Vector3 position, Vector3 direction, Color color,
                  float intensity, float range, float innerCone, float outerCone) {
    return (VLight){ VLIGHT_SPOT, position, Vector3Normalize(direction),
                     color, intensity, range, innerCone, outerCone };
}

VLight VLightAmbient(Color color, float intensity) {
    return (VLight){ VLIGHT_AMBIENT, {0, 0, 0}, {0, 0, 0},
                     color, intensity, 0.0f, 0.0f, 0.0f };
}

VLight2D VLight2DPoint(Vector2 position, Color color, float intensity, float range) {
    return (VLight2D){ position, {0, 0}, color, intensity, range, 0.0f, 0.0f };
}

VLight2D VLight2DSpot(Vector2 position, Vector2 direction, Color color,
                      float intensity, float range, float innerCone, float outerCone) {
    return (VLight2D){ position, Vector2Normalize(direction), color,
                       intensity, range, innerCone, outerCone };
}

float VLightAttenuation(float distance, float range) {
    if (range <= 0.0f) return 1.0f;
    float normalized = clamp01(1.0f - distance / range);
    return normalized * normalized;
}

float VLightSpotFactor(Vector3 lightToPoint, Vector3 direction,
                       float innerCone, float outerCone) {
    Vector3 toPoint = Vector3Normalize(lightToPoint);
    Vector3 forward = Vector3Normalize(direction);
    float cosine = Vector3Dot(toPoint, forward);
    float inner = cosf(innerCone * 0.5f);
    float outer = cosf(outerCone * 0.5f);
    if (inner <= outer) return cosine >= outer ? 1.0f : 0.0f;
    return clamp01((cosine - outer) / (inner - outer));
}

float VLight2DSpotFactor(Vector2 lightToPoint, Vector2 direction,
                         float innerCone, float outerCone) {
    Vector2 toPoint = Vector2Normalize(lightToPoint);
    Vector2 forward = Vector2Normalize(direction);
    float cosine = Vector2Dot(toPoint, forward);
    float inner = cosf(innerCone * 0.5f);
    float outer = cosf(outerCone * 0.5f);
    if (inner <= outer) return cosine >= outer ? 1.0f : 0.0f;
    return clamp01((cosine - outer) / (inner - outer));
}

Color VLightColorAt2D(VLight2D light, Vector2 point) {
    Vector2 offset = Vector2Subtract(point, light.position);
    float factor = VLightAttenuation(Vector2Length(offset), light.range) * light.intensity;
    if (Vector2Length(light.direction) > 0.001f)
        factor *= VLight2DSpotFactor(offset, light.direction, light.innerCone, light.outerCone);
    return (Color){ lightChannel(light.color.r, factor),
                    lightChannel(light.color.g, factor),
                    lightChannel(light.color.b, factor), 255 };
}

Color VLightColorAt3D(VLight light, Vector3 point, Vector3 normal) {
    float factor = light.intensity;
    if (light.type == VLIGHT_AMBIENT)
        return (Color){ lightChannel(light.color.r, factor),
                        lightChannel(light.color.g, factor),
                        lightChannel(light.color.b, factor), 255 };
    if (light.type == VLIGHT_DIRECTIONAL) {
        factor *= clamp01(Vector3Dot(Vector3Normalize(normal),
                                     Vector3Scale(light.direction, -1.0f)));
    } else {
        Vector3 offset = Vector3Subtract(point, light.position);
        factor *= VLightAttenuation(Vector3Length(offset), light.range);
        if (light.type == VLIGHT_SPOT)
            factor *= VLightSpotFactor(offset, light.direction, light.innerCone, light.outerCone);
        factor *= clamp01(Vector3Dot(Vector3Normalize(normal), Vector3Normalize(offset)));
    }
    return (Color){ lightChannel(light.color.r, factor),
                    lightChannel(light.color.g, factor),
                    lightChannel(light.color.b, factor), 255 };
}
