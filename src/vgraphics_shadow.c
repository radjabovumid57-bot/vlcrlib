/**
 * vgraphics_shadow.c — shadow light view-projection matrices (Sprint E)
 *
 * Resource create / recordShadowPass remain in vgraphics.c until mesh tables
 * are also modularized. This file owns CPU-side light matrices only.
 */
#include "vgraphics.h"
#include "vgraphics_internal.h"
#include <math.h>

void vgraphics_ComputeSpotShadowMatrix(float nearZ, float farZ) {
    Vector3 pos = VK.lightSpotPosition;
    Vector3 dir = VK.lightSpotDirection;
    float len = sqrtf(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
    if (len < 1e-6f) dir = (Vector3){0.f, -1.f, 0.f};
    else { dir.x /= len; dir.y /= len; dir.z /= len; }
    Vector3 target = { pos.x + dir.x, pos.y + dir.y, pos.z + dir.z };
    Vector3 up = {0, 1, 0};
    if (fabsf(dir.y) > 0.95f) up = (Vector3){0, 0, 1};
    Matrix view = MatrixLookAt(pos, target, up);
    float outer = VK.lightSpotOuterCone > 0.05f ? VK.lightSpotOuterCone : 0.6f;
    float fov = outer;
    if (fov < 0.2f) fov = 0.2f;
    if (fov > 2.5f) fov = 2.5f;
    if (farZ <= nearZ) farZ = nearZ + 40.f;
    Matrix proj = MatrixPerspective(fov, 1.0f, nearZ, farZ);
    VK.spotShadowLightVP = MatrixMultiply(proj, view);
}

void vgraphics_UpdateShadowMatrix(Matrix lightViewProj) {
    VK.shadowLightVP = lightViewProj;
}

void vgraphics_ComputeDirectionalShadowCascades(void) {
    /* Derive camera basis/FOV from the current view/proj (no extra state). */
    Matrix invView = MatrixInvert(VK.view3D);
    Vector3 right   = Vector3Normalize((Vector3){ invView.m0, invView.m1, invView.m2 });
    Vector3 up      = Vector3Normalize((Vector3){ invView.m4, invView.m5, invView.m6 });
    Vector3 forward = Vector3Normalize((Vector3){ -invView.m8, -invView.m9, -invView.m10 });
    Vector3 eye     = VK.cameraPos;

    float f = fabsf(VK.proj3D.m5);
    float aspect = (f > 1e-6f && fabsf(VK.proj3D.m0) > 1e-6f)
        ? f / fabsf(VK.proj3D.m0) : 1.0f;
    float nearD = VK.proj3D.m14 / VK.proj3D.m10;
    float farD  = VK.proj3D.m14 / (VK.proj3D.m10 + 1.0f);
    if (!(nearD > 0.001f) || !(farD > nearD + 0.1f)) {
        nearD = 0.1f; farD = 100.0f;
    }
    float tanHalf = 1.0f / (f > 1e-6f ? f : 1.0f);
    float frac[4] = { 0.10f, 0.25f, 0.50f, 1.0f };

    Vector3 lightDir = VK.lightDirectionalDirection;
    float ll = sqrtf(lightDir.x*lightDir.x + lightDir.y*lightDir.y + lightDir.z*lightDir.z);
    if (ll < 1e-6f) lightDir = (Vector3){0.4f, -1.0f, 0.3f};
    else { lightDir.x /= ll; lightDir.y /= ll; lightDir.z /= ll; }
    Vector3 lightUp = {0,1,0};
    if (fabsf(lightDir.y) > 0.95f) lightUp = (Vector3){0,0,1};

    float prevD = nearD;
    for (int c = 0; c < 4; ++c) {
        float d1 = nearD + (farD - nearD) * frac[c];
        float h0 = tanHalf * prevD;
        float h1 = tanHalf * d1;
        Vector3 c0 = Vector3Add(eye, Vector3Scale(forward, prevD));
        Vector3 c1 = Vector3Add(eye, Vector3Scale(forward, d1));

        Vector3 corners[8];
        int n = 0;
        for (int z = 0; z < 2; ++z) {
            Vector3 center = z ? c1 : c0;
            float hh = z ? h1 : h0;
            float hw = hh * aspect;
            corners[n++] = Vector3Add(center, Vector3Add(Vector3Scale(right,  hw), Vector3Scale(up,  hh)));
            corners[n++] = Vector3Add(center, Vector3Add(Vector3Scale(right, -hw), Vector3Scale(up,  hh)));
            corners[n++] = Vector3Add(center, Vector3Add(Vector3Scale(right,  hw), Vector3Scale(up, -hh)));
            corners[n++] = Vector3Add(center, Vector3Add(Vector3Scale(right, -hw), Vector3Scale(up, -hh)));
        }

        Vector3 center = {0,0,0};
        for (int i = 0; i < 8; ++i) center = Vector3Add(center, corners[i]);
        center = Vector3Scale(center, 1.0f/8.0f);

        float radius = 0.0f;
        for (int i = 0; i < 8; ++i)
            radius = fmaxf(radius, Vector3Distance(center, corners[i]));
        radius = fmaxf(radius, 0.25f);

        Vector3 lz = Vector3Normalize(Vector3Scale(lightDir, -1.0f));
        Vector3 lx = Vector3Normalize(Vector3Cross(lightUp, lz));
        Vector3 ly = Vector3Normalize(Vector3Cross(lz, lx));
        Vector3 lEye = Vector3Subtract(center, Vector3Scale(lightDir, radius * 2.0f));
        Matrix view = MatrixLookAt(lEye, center, ly);
        Matrix proj = MatrixOrtho(-radius, radius, -radius, radius, 0.0f, radius * 4.0f);
        VK.shadowCascadeVP[c] = MatrixMultiply(proj, view);
        VK.shadowCascadeSplits[c] = d1;
        prevD = d1;
    }
    VK.shadowCascadeCount = 4;
    VK.shadowLightVP = VK.shadowCascadeVP[0];
}

void vgraphics_ComputeDirectionalShadowMatrix(float radius, float nearZ, float farZ) {
    if (radius < 0.1f) radius = 0.1f;
    if (nearZ < 0.01f) nearZ = 0.01f;
    if (farZ <= nearZ) farZ = nearZ + radius * 4.0f;
    Vector3 d = VK.lightDirectionalDirection;
    float len = sqrtf(d.x*d.x+d.y*d.y+d.z*d.z);
    if (len < 1e-6f) d = (Vector3){0.4f, -1.f, 0.3f};
    else { d.x/=len; d.y/=len; d.z/=len; }
    /* Follow the camera so objects remain inside the directional shadow
     * frustum while exploring a large world. Snap in light-space texels to
     * prevent the map from swimming as the camera moves by sub-texel amounts. */
    Matrix cameraToWorld = MatrixInvert(VK.view3D);
    Vector3 target = { cameraToWorld.m12, cameraToWorld.m13, cameraToWorld.m14 };
    Vector3 up = {0,1,0};
    if (fabsf(d.y) > 0.95f) up = (Vector3){0,0,1};
    Vector3 lightZ = {-d.x,-d.y,-d.z};
    Vector3 right = Vector3Normalize(Vector3Cross(up, lightZ));
    Vector3 lightUp = Vector3Normalize(Vector3Cross(lightZ, right));
    float texelWorld = (2.0f * radius) / (float)(VK.shadowMapSize > 0 ? VK.shadowMapSize : 2048);
    float centerX = Vector3Dot(target, right);
    float centerY = Vector3Dot(target, lightUp);
    float snappedX = floorf(centerX / texelWorld + 0.5f) * texelWorld;
    float snappedY = floorf(centerY / texelWorld + 0.5f) * texelWorld;
    target = Vector3Add(target, Vector3Add(Vector3Scale(right, snappedX-centerX),
                                           Vector3Scale(lightUp, snappedY-centerY)));
    Vector3 eye = Vector3Subtract(target, Vector3Scale(d, radius * 1.5f));
    Matrix view = MatrixLookAt(eye, target, up);
    Matrix proj = MatrixOrtho(-radius, radius, -radius, radius, nearZ, farZ);
    VK.shadowLightVP = MatrixMultiply(proj, view);
}
