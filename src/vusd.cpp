/*
 * vusd.cpp — optional OpenUSD loader.
 *
 * When OpenUSD is available (VLCRLIB_HAS_USD=1), traverses UsdGeomMesh
 * prims, triangulates faces and packs them into the engine's Vertex3D.
 * Otherwise safe stubs are compiled.
 *
 * Textures/material binding are intentionally minimal: the USD path builds
 * default materials until the USD shading network is mapped to the engine.
 */
#include "vloaders.h"

#if VLCRLIB_HAS_USD
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec2f.h>

#include <cmath>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

static GfVec3f xformPoint(const GfMatrix4d& m, const GfVec3f& p) {
    GfVec4d h(p[0], p[1], p[2], 1.0);
    GfVec4d r = h * m; // row-vector convention
    return GfVec3f((float)r[0], (float)r[1], (float)r[2]);
}

static GfVec3f xformNormal(const GfMatrix4d& m, const GfVec3f& n) {
    GfVec4d r = GfVec4d(n[0], n[1], n[2], 0.0) * m;
    GfVec3f out((float)r[0], (float)r[1], (float)r[2]);
    float len = out.GetLength();
    return len > 1e-8f ? out / len : GfVec3f(0, 1, 0);
}
#endif

bool vloaders_HasUSD(void) {
#if VLCRLIB_HAS_USD
    return true;
#else
    return false;
#endif
}

Model LoadUSD(const char *path) {
#if VLCRLIB_HAS_USD
    Model empty = {0};
    if (!path || !path[0]) return empty;

    UsdStageRefPtr stage = UsdStage::Open(path);
    if (!stage) return empty;

    std::vector<Vertex3D> verts;
    std::vector<uint32_t> indices;
    Matrix identity = MatrixIdentity();

    for (const UsdPrim& prim : stage->Traverse()) {
        if (!prim.IsA<UsdGeomMesh>()) continue;
        UsdGeomMesh mesh(prim);

        VtArray<GfVec3f> points;
        VtArray<int> counts, faceIndices;
        if (!mesh.GetPointsAttr().Get(&points)) continue;
        if (!mesh.GetFaceVertexCountsAttr().Get(&counts)) continue;
        if (!mesh.GetFaceVertexIndicesAttr().Get(&faceIndices)) continue;

        VtArray<GfVec2f> uvs;
        bool hasUV = false;
        UsdGeomPrimvarsAPI pv(prim);
        if (UsdGeomPrimvar st = pv.GetPrimvar(TfToken("st"))) {
            if (st.ComputeFlattened(&uvs) && uvs.size() == points.size())
                hasUV = true;
        }

        GfMatrix4d xf = mesh.ComputeLocalToWorldTransform(UsdTimeCode::Default());
        const size_t base = verts.size();

        for (const GfVec3f& p : points) {
            GfVec3f wp = xformPoint(xf, p);
            Vertex3D v = {0};
            v.x = wp[0]; v.y = wp[1]; v.z = wp[2];
            v.ny = 1.0f;
            v.cr = v.cg = v.cb = v.ca = 1.0f;
            verts.push_back(v);
        }
        if (hasUV) {
            for (size_t i = 0; i < uvs.size(); ++i) {
                verts[base + i].u = uvs[i][0];
                verts[base + i].v = uvs[i][1];
            }
        }

        // face normals (USD mesh may provide authored normals; this is the
        // safe fallback so the object is not black)
        size_t off = 0;
        for (int c : counts) {
            if (c >= 3) {
                GfVec3f a = points[faceIndices[off + 0]];
                GfVec3f b = points[faceIndices[off + 1]];
                GfVec3f d = points[faceIndices[off + 2]];
                GfVec3f n = GfCross(b - a, d - a);
                n.Normalize();
                GfVec3f wn = xformNormal(xf, n);
                for (int k = 0; k < c; ++k) {
                    uint32_t vi = base + (uint32_t)faceIndices[off + k];
                    verts[vi].nx = wn[0]; verts[vi].ny = wn[1]; verts[vi].nz = wn[2];
                }
                for (int k = 1; k + 1 < c; ++k) {
                    indices.push_back(base + (uint32_t)faceIndices[off]);
                    indices.push_back(base + (uint32_t)faceIndices[off + k]);
                    indices.push_back(base + (uint32_t)faceIndices[off + k + 1]);
                }
            }
            off += c;
        }
    }

    if (verts.empty() || indices.empty()) return empty;
    Mesh gpu = vgraphics_RegisterMesh(verts.data(), (int)verts.size(),
                                      indices.data(), (int)indices.size());
    if (!gpu.id) return empty;

    Material mat = LoadMaterialDefault();
    Mesh meshes[1] = { gpu };
    int meshMats[1] = { 0 };
    Matrix transforms[1] = { identity };
    return vgraphics_CreateModelFromMeshesEx(meshes, meshMats, transforms,
                                             1, &mat, 1);
#else
    (void)path;
    Model empty = {0};
    return empty;
#endif
}
