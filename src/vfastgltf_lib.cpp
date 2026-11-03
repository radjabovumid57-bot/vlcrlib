/* ==================================================================
 *  vfastgltf_lib.cpp — spnda/fastgltf behind a C ABI
 * ==================================================================
 *
 * Mirrors the pattern used by vphysics.cpp (Bullet), vphysics2d.cpp
 * (Box2D) and vjolt.cpp (Jolt): the real library is C++, this file is
 * the only translation unit that sees it, and everything the C side
 * touches is a plain C struct or function pointer.
 *
 * Enable with -DVLCRLIB_ENABLE_FASTGLTF=ON after installing the port:
 *     vcpkg install fastgltf   (or --x-feature=fastgltf)
 *
 * Why this exists alongside vfastgltf.c:
 *   vfastgltf.c  — zero dependencies, hand-rolled, static geometry only.
 *   this file    — spec-complete (extensions, sparse accessors, real
 *                  SIMD parsing) but needs fastgltf + simdjson.
 * They coexist; pick per project.
 *
 * fastgltf does NOT decode images (its docs are explicit about this),
 * so PNG/JPEG bytes are handed to stb_image, exactly as the other
 * loaders do.
 *
 * ------------------------------------------------------------------
 *  API surface verified against upstream v0.9.0
 * ------------------------------------------------------------------
 *  Confirmed two ways: by reading the upstream v0.9.0 sources/example,
 *  and by compiling this file against the real headers from vcpkg
 *  (fastgltf:x64-windows@0.9.0). The compile caught two things that
 *  reading alone got wrong, both noted below.
 *
 *    fastgltf::MappedGltfFile::FromPath(path)  -> Expected<MappedGltfFile>
 *      (not GltfDataBuffer: GLB parsing seeks into the binary chunk)
 *    fastgltf::Parser::loadGltf(GltfDataGetter&, path, Options, Category)
 *      — takes the getter by REFERENCE in 0.9.0. The 1.0 docs show a
 *        pointer; passing &getter failed with C2664.
 *    fastgltf::Options::{DontRequireValidAssetMember, AllowDouble,
 *                        LoadExternalBuffers, LoadExternalImages,
 *                        GenerateMeshIndices}
 *    Expected<T>::error() / .get()
 *    iterateSceneNodes(asset, sceneIndex, initial, callback)
 *      — takes a SCENE index (it walks the hierarchy itself; do not loop
 *        over scene.nodeIndices) and the callback is
 *        (Node&, const math::fmat4x4&).
 *      — BUG in 0.9.0: it does not compile when handed a `const Asset&`
 *        (minimal repro: non-const compiles, const fails with C2672
 *        "std::invoke: no matching overloaded function"). We pass a
 *        const_cast; the function only reads the asset.
 *    Primitive::findAttribute(name) -> attribute with ->accessorIndex
 *    iterateAccessor<math::fvec3|fvec2|uint32_t>(asset, acc, lambda)
 *      — requires Element<ElementType>; fastgltf ships ElementTraits for
 *        its own math types, so no user specialization is needed here.
 *    Accessor::bufferViewIndex must be checked before reading (done in
 *      readVec3Floats / readUVFloats / the index path below).
 *    sources::{BufferView,URI,Array,Vector,ByteView} shapes; URI wraps a
 *      fastgltf::URI whose path accessor is .path().
 *
 *  Requires C++17 (fastgltf's own FASTGLTF_COMPILE_TARGET default),
 *  which is what this project already sets.
 *
 *  Verified end to end: parses example/DamagedHelmet.glb, reporting
 *  1 mesh / 1 material / 5 textures / 5 images / 1 node / 1 scene, which
 *  matches the dependency-free FastLoadGltf() on the same file.
 *
 * Ownership: one fastgltf::Asset per loaded file, held in a registry
 * keyed by the handle returned to C. All GPU resources are owned by the
 * resulting Model, so UnloadModel() releases everything; the registry
 * entry is dropped by vfastgltf_lib_release().
 *
 * Feature gate: the entire body of this file is inside
 *   #if VLCRLIB_HAS_FASTGLTF
 * When fastgltf was not found at configure time we instead compile a single
 * stub for vfastgltf_lib_version(). That keeps this translation unit valid
 * (no #error, no missing <fastgltf/...> includes) while still making a call
 * to any other vfastgltf_lib_* function a link error, which is the loudest
 * possible failure mode for "you used the backend you didn't build".
 */

#include "vfastgltf_lib.h"

#if !VLCRLIB_HAS_FASTGLTF

/* ------------------------------------------------------------------ */
/*  Stub build: fastgltf is not available                              */
/* ------------------------------------------------------------------ */

extern "C" const char *vfastgltf_lib_version(void) {
    return "fastgltf backend not compiled";
}

#else  /* VLCRLIB_HAS_FASTGLTF */

#include "vgraphics.h"
#include "vtextures.h"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/* ================================================================== */
/*  Registry                                                           */
/* ================================================================== */

struct FgEntry {
    std::unique_ptr<fastgltf::Asset> asset;
    std::string                      dir;      /* trailing separator */
    bool                             inUse = false;
};

static std::vector<std::unique_ptr<FgEntry>> g_entries;

static FgEntry *entryOf(FastGltfLibAsset handle) {
    if (handle < 1 || handle > (FastGltfLibAsset)g_entries.size()) return nullptr;
    return g_entries[(size_t)(handle - 1)].get();
}

/* ================================================================== */
/*  Small helpers                                                      */
/* ================================================================== */

static unsigned char to_srgb8(float v) {
    if (!(v > 0.0f)) return 0;
    if (v >= 1.0f)   return 255;
    float s = (v <= 0.0031308f) ? v * 12.92f
                                : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    if (s < 0.0f) s = 0.0f;
    if (s > 1.0f) s = 1.0f;
    return (unsigned char)(s * 255.0f + 0.5f);
}

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

/* One entry in the flat draw list: a mesh primitive at a world transform. */
struct DrawSlot {
    size_t      meshIndex;
    size_t      primIndex;
    size_t      materialIndex;   /* SIZE_MAX = none */
    fastgltf::math::fmat4x4 world;
};

/* Takes a non-const Asset because fastgltf 0.9.0's iterateSceneNodes rejects
 * a const one; see the note at the call site. It performs no mutation. */
static void collectDrawSlots(fastgltf::Asset &asset, size_t sceneIndex,
                             std::vector<DrawSlot> &out)
{
    if (sceneIndex >= asset.scenes.size()) return;

    /* One call covers the whole scene: iterateSceneNodes walks the node
     * hierarchy itself (scene.nodeIndices are the roots) and hands us the
     * accumulated world matrix for each node. */
    fastgltf::iterateSceneNodes(asset, sceneIndex, fastgltf::math::fmat4x4(),
        [&](fastgltf::Node &node, const fastgltf::math::fmat4x4 &matrix) {
            if (!node.meshIndex.has_value()) return;
            size_t mi = node.meshIndex.value();
            if (mi >= asset.meshes.size()) return;

            const fastgltf::Mesh &mesh = asset.meshes[mi];
            for (size_t pi = 0; pi < mesh.primitives.size(); ++pi) {
                const fastgltf::Primitive &prim = mesh.primitives[pi];
                if (prim.type != fastgltf::PrimitiveType::Triangles) continue;

                DrawSlot slot;
                slot.meshIndex     = mi;
                slot.primIndex     = pi;
                slot.materialIndex = prim.materialIndex.has_value()
                                   ? prim.materialIndex.value()
                                   : SIZE_MAX;
                slot.world         = matrix;
                out.push_back(slot);
            }
        });
}

/* ================================================================== */
/*  Texture loading                                                    */
/* ================================================================== */

/* Pull the encoded bytes of an image out of the asset.
 *
 * fastgltf does not decode images, so we hand the raw PNG/JPEG bytes to
 * stb_image via LoadImageFromMemory. With LoadExternalImages the bytes are
 * already materialised, so the URI case is only a safety net. */
static bool imageBytes(const fastgltf::Asset &asset, const fastgltf::Image &image,
                       std::vector<std::byte> &scratch,
                       const unsigned char **out, size_t *outLen)
{
    *out = nullptr;
    *outLen = 0;

    /* In-asset byte ranges first (embedded GLB images). */
    if (const auto *bv = std::get_if<fastgltf::sources::BufferView>(&image.data)) {
        if (bv->bufferViewIndex >= asset.bufferViews.size()) return false;
        const fastgltf::BufferView &view = asset.bufferViews[bv->bufferViewIndex];
        if (view.bufferIndex >= asset.buffers.size()) return false;
        const fastgltf::Buffer &buf = asset.buffers[view.bufferIndex];

        if (const auto *bytes = std::get_if<fastgltf::sources::ByteView>(&buf.data)) {
            if (view.byteOffset > bytes->bytes.size()) return false;
            if (view.byteLength > bytes->bytes.size() - view.byteOffset) return false;
            *out = reinterpret_cast<const unsigned char *>(bytes->bytes.data()
                                                           + view.byteOffset);
            *outLen = view.byteLength;
            return true;
        }
        if (const auto *arr = std::get_if<fastgltf::sources::Array>(&buf.data)) {
            if (view.byteOffset > arr->bytes.size()) return false;
            if (view.byteLength > arr->bytes.size() - view.byteOffset) return false;
            *out = reinterpret_cast<const unsigned char *>(arr->bytes.data()
                                                           + view.byteOffset);
            *outLen = view.byteLength;
            return true;
        }
        if (const auto *vec = std::get_if<fastgltf::sources::Vector>(&buf.data)) {
            if (view.byteOffset > vec->bytes.size()) return false;
            if (view.byteLength > vec->bytes.size() - view.byteOffset) return false;
            *out = reinterpret_cast<const unsigned char *>(vec->bytes.data()
                                                           + view.byteOffset);
            *outLen = view.byteLength;
            return true;
        }
        return false;
    }

    /* Images fastgltf already loaded into memory. */
    if (const auto *arr = std::get_if<fastgltf::sources::Array>(&image.data)) {
        *out = reinterpret_cast<const unsigned char *>(arr->bytes.data());
        *outLen = arr->bytes.size();
        return true;
    }
    if (const auto *vec = std::get_if<fastgltf::sources::Vector>(&image.data)) {
        *out = reinterpret_cast<const unsigned char *>(vec->bytes.data());
        *outLen = vec->bytes.size();
        return true;
    }
    if (const auto *view = std::get_if<fastgltf::sources::ByteView>(&image.data)) {
        *out = reinterpret_cast<const unsigned char *>(view->bytes.data());
        *outLen = view->bytes.size();
        return true;
    }

    /* External file, when it was not preloaded. */
    if (const auto *uri = std::get_if<fastgltf::sources::URI>(&image.data)) {
        std::string path(uri->uri.path().begin(), uri->uri.path().end());
        if (path.empty()) return false;

        FILE *f = fopen(path.c_str(), "rb");
        if (!f) return false;
        if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
        long sz = ftell(f);
        if (sz <= 0) { fclose(f); return false; }
        if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return false; }

        scratch.resize((size_t)sz);
        size_t got = fread(scratch.data(), 1, (size_t)sz, f);
        fclose(f);
        if (got != (size_t)sz) return false;

        *out = reinterpret_cast<const unsigned char *>(scratch.data());
        *outLen = (size_t)sz;
        return true;
    }

    return false;
}

struct TexCache {
    std::vector<size_t>    gltfIndex;
    std::vector<Texture2D> tex;

    Texture2D find(size_t idx) const {
        for (size_t i = 0; i < gltfIndex.size(); ++i)
            if (gltfIndex[i] == idx) return tex[i];
        Texture2D none; std::memset(&none, 0, sizeof(none));
        return none;
    }
    void add(size_t idx, Texture2D t) { gltfIndex.push_back(idx); tex.push_back(t); }
};

static Texture2D loadTexture(const fastgltf::Asset &asset, size_t textureIndex,
                             bool srgb, TexCache &cache)
{
    Texture2D none; std::memset(&none, 0, sizeof(none));
    if (textureIndex >= asset.textures.size()) return none;

    Texture2D cached = cache.find(textureIndex);
    if (cached.id) return cached;

    const fastgltf::Texture &tex = asset.textures[textureIndex];
    if (!tex.imageIndex.has_value() || tex.imageIndex.value() >= asset.images.size()) {
        cache.add(textureIndex, none);
        return none;
    }

    std::vector<std::byte> scratch;
    const unsigned char *bytes = nullptr;
    size_t len = 0;
    if (!imageBytes(asset, asset.images[tex.imageIndex.value()], scratch, &bytes, &len)) {
        cache.add(textureIndex, none);
        return none;
    }
    if (len == 0 || len > (size_t)INT32_MAX) {
        cache.add(textureIndex, none);
        return none;
    }

    Image img = LoadImageFromMemory(".png", bytes, (int)len);
    Texture2D result = none;
    if (img.data) {
        TextureOptions topt;
        std::memset(&topt, 0, sizeof(topt));
        topt.srgb = srgb ? 1 : 0;
        topt.mipmaps = 1;
        topt.minFilter = 5; /* LINEAR_MIPMAP_LINEAR */
        topt.magFilter = 1; /* LINEAR */
        topt.wrapS = 0;     /* glTF default is REPEAT */
        topt.wrapT = 0;
        if (tex.samplerIndex.has_value() && tex.samplerIndex.value() < asset.samplers.size()) {
            const auto &samp = asset.samplers[tex.samplerIndex.value()];
            if (samp.wrapS == fastgltf::Wrap::ClampToEdge) topt.wrapS = 1;
            else if (samp.wrapS == fastgltf::Wrap::MirroredRepeat) topt.wrapS = 2;
            else topt.wrapS = 0;

            if (samp.wrapT == fastgltf::Wrap::ClampToEdge) topt.wrapT = 1;
            else if (samp.wrapT == fastgltf::Wrap::MirroredRepeat) topt.wrapT = 2;
            else topt.wrapT = 0;

            if (samp.magFilter.has_value() && samp.magFilter.value() == fastgltf::Filter::Nearest)
                topt.magFilter = 0;
            if (samp.minFilter.has_value()) {
                switch (samp.minFilter.value()) {
                    case fastgltf::Filter::Nearest: topt.minFilter = 0; topt.mipmaps = 0; break;
                    case fastgltf::Filter::Linear:  topt.minFilter = 1; topt.mipmaps = 0; break;
                    case fastgltf::Filter::NearestMipMapNearest: topt.minFilter = 2; break;
                    case fastgltf::Filter::LinearMipMapNearest:  topt.minFilter = 3; break;
                    case fastgltf::Filter::NearestMipMapLinear:  topt.minFilter = 4; break;
                    case fastgltf::Filter::LinearMipMapLinear:   topt.minFilter = 5; break;
                }
            }
        }
        result = LoadTextureFromImageEx(img, &topt);
        UnloadImage(img);
    }
    cache.add(textureIndex, result);
    return result;
}

/* ================================================================== */
/*  Mesh building                                                      */
/* ================================================================== */

static size_t componentByteSize(fastgltf::ComponentType t);

/* Read one scalar component of an accessor element, normalized the way glTF
 * specifies. `p` points at the element; `index` selects the component. */
static float componentToFloat(const fastgltf::Accessor &acc,
                              const unsigned char *p, int index)
{
    const unsigned char *q = p + (size_t)index * componentByteSize(acc.componentType);
    switch (acc.componentType) {
        case fastgltf::ComponentType::Float: {
            float v; std::memcpy(&v, q, sizeof(v)); return v;
        }
        case fastgltf::ComponentType::Double: {
            double v; std::memcpy(&v, q, sizeof(v)); return (float)v;
        }
        case fastgltf::ComponentType::UnsignedByte:
            return acc.normalized ? (float)(*q) / 255.0f : (float)(*q);
        case fastgltf::ComponentType::Byte: {
            int8_t v; std::memcpy(&v, q, 1);
            return acc.normalized ? (float)v / 127.0f : (float)v;
        }
        case fastgltf::ComponentType::UnsignedShort: {
            uint16_t v; std::memcpy(&v, q, 2);
            return acc.normalized ? (float)v / 65535.0f : (float)v;
        }
        case fastgltf::ComponentType::Short: {
            int16_t v; std::memcpy(&v, q, 2);
            return acc.normalized ? (float)v / 32767.0f : (float)v;
        }
        case fastgltf::ComponentType::UnsignedInt: {
            uint32_t v; std::memcpy(&v, q, 4); return (float)v;
        }
        case fastgltf::ComponentType::Int: {
            int32_t v; std::memcpy(&v, q, 4); return (float)v;
        }
        default: return 0.0f;
    }
}

static size_t componentByteSize(fastgltf::ComponentType t)
{
    switch (t) {
        case fastgltf::ComponentType::Byte:
        case fastgltf::ComponentType::UnsignedByte:  return 1;
        case fastgltf::ComponentType::Short:
        case fastgltf::ComponentType::UnsignedShort: return 2;
        case fastgltf::ComponentType::Int:
        case fastgltf::ComponentType::UnsignedInt:
        case fastgltf::ComponentType::Float:         return 4;
        case fastgltf::ComponentType::Double:        return 8;
        default: return 0;
    }
}

static int accessorComponentCount(fastgltf::AccessorType t)
{
    switch (t) {
        case fastgltf::AccessorType::Scalar: return 1;
        case fastgltf::AccessorType::Vec2:   return 2;
        case fastgltf::AccessorType::Vec3:   return 3;
        case fastgltf::AccessorType::Vec4:   return 4;
        case fastgltf::AccessorType::Mat2:   return 4;
        case fastgltf::AccessorType::Mat3:   return 9;
        case fastgltf::AccessorType::Mat4:   return 16;
        default: return 0;
    }
}

/* Locate the bytes backing an accessor.
 *
 * This deliberately does NOT use fastgltf::iterateAccessor<math::fvec3>:
 * ElementTraits binds one component type to each element type (fvec3 means
 * float), so iterating a quantized accessor as fvec3 silently produces
 * nothing. glTF permits any component type plus normalized integers, and
 * KHR_mesh_quantization makes that common, so we read the raw storage and
 * convert ourselves. */
static bool accessorBytes(const fastgltf::Asset &asset, const fastgltf::Accessor &acc,
                          const std::byte **out, size_t *outLen)
{
    *out = nullptr;
    *outLen = 0;

    if (!acc.bufferViewIndex.has_value()) return false;   /* sparse or zero-filled */
    if (acc.bufferViewIndex.value() >= asset.bufferViews.size()) return false;

    const fastgltf::BufferView &view = asset.bufferViews[acc.bufferViewIndex.value()];
    if (view.bufferIndex >= asset.buffers.size()) return false;
    const fastgltf::Buffer &buf = asset.buffers[view.bufferIndex];

    const std::byte *base = nullptr;
    size_t baseLen = 0;
    if (const auto *bv = std::get_if<fastgltf::sources::ByteView>(&buf.data)) {
        base = bv->bytes.data();
        baseLen = bv->bytes.size();
    } else if (const auto *arr = std::get_if<fastgltf::sources::Array>(&buf.data)) {
        base = arr->bytes.data();
        baseLen = arr->bytes.size();
    } else if (const auto *vec = std::get_if<fastgltf::sources::Vector>(&buf.data)) {
        base = vec->bytes.data();
        baseLen = vec->bytes.size();
    } else {
        /* External buffers are expected to be materialised by
         * Options::LoadExternalBuffers, so URI here means "not available". */
        return false;
    }

    if (view.byteOffset > baseLen) return false;
    *out = base + view.byteOffset;
    *outLen = baseLen - view.byteOffset;   /* clamp to the buffer, not byteLength */
    (void)view.byteLength;
    return true;
}

/* Read `components` floats per element, converting any component type. */
static bool readFloats(const fastgltf::Asset &asset, const fastgltf::Accessor &acc,
                       float *out, int components, size_t count)
{
    if (acc.count < count) return false;

    const size_t csize = componentByteSize(acc.componentType);
    if (csize == 0) return false;

    const int actual = accessorComponentCount(acc.type);
    if (actual < components) return false;

    const std::byte *data = nullptr;
    size_t dataLen = 0;
    if (!accessorBytes(asset, acc, &data, &dataLen)) return false;

    const size_t viewIdx = acc.bufferViewIndex.value();
    const size_t stride = asset.bufferViews[viewIdx].byteStride.value_or(0)
                        ? asset.bufferViews[viewIdx].byteStride.value()
                        : (size_t)actual * csize;
    if (stride < (size_t)actual * csize) return false;

    for (size_t i = 0; i < count; ++i) {
        if (i > (SIZE_MAX - acc.byteOffset) / stride) return false;
        const size_t off = acc.byteOffset + stride * i;
        if (off > dataLen) return false;
        if ((size_t)components * csize > dataLen - off) return false;

        const unsigned char *p = reinterpret_cast<const unsigned char *>(data + off);
        for (int c = 0; c < components; ++c)
            out[i * (size_t)components + (size_t)c] = componentToFloat(acc, p, c);
    }
    return true;
}

/* Read `count` indices, converting 8/16/32-bit storage to uint32_t.
 *
 * fastgltf::iterateAccessor<uint32_t> cannot be used here: its
 * ElementTraits require a component type of exactly UnsignedInt, so a
 * 16-bit index accessor (very common) yields no elements at all and the
 * primitive ends up with an empty index buffer. */
static bool readIndices(const fastgltf::Asset &asset, const fastgltf::Accessor &acc,
                        uint32_t *out, size_t count)
{
    if (acc.count < count) return false;

    const size_t csize = componentByteSize(acc.componentType);
    if (csize == 0 || acc.componentType == fastgltf::ComponentType::Float ||
        acc.componentType == fastgltf::ComponentType::Double) {
        return false;
    }

    const std::byte *data = nullptr;
    size_t dataLen = 0;
    if (!accessorBytes(asset, acc, &data, &dataLen)) return false;

    const size_t viewIdx = acc.bufferViewIndex.value();
    const size_t stride = asset.bufferViews[viewIdx].byteStride.value_or(0)
                        ? asset.bufferViews[viewIdx].byteStride.value()
                        : csize;
    if (stride < csize) return false;

    for (size_t i = 0; i < count; ++i) {
        if (i > (SIZE_MAX - acc.byteOffset) / stride) return false;
        const size_t off = acc.byteOffset + stride * i;
        if (csize > dataLen || off > dataLen - csize) return false;

        const unsigned char *p = reinterpret_cast<const unsigned char *>(data + off);
        uint32_t v = 0;
        switch (acc.componentType) {
            case fastgltf::ComponentType::UnsignedByte:
            case fastgltf::ComponentType::Byte: {
                uint8_t t; std::memcpy(&t, p, 1); v = t; break;
            }
            case fastgltf::ComponentType::UnsignedShort:
            case fastgltf::ComponentType::Short: {
                uint16_t t; std::memcpy(&t, p, 2); v = t; break;
            }
            default: {
                uint32_t t; std::memcpy(&t, p, 4); v = t; break;
            }
        }
        out[i] = v;
    }
    return true;
}

static void generateTangents(std::vector<Vertex3D> &verts,
                             const std::vector<uint32_t> &indices)
{
    const size_t n = verts.size();
    std::vector<float> tanAcc(n * 3, 0.0f);
    std::vector<float> bitAcc(n * 3, 0.0f);

    for (size_t t = 0; t + 2 < indices.size(); t += 3) {
        uint32_t i0 = indices[t], i1 = indices[t + 1], i2 = indices[t + 2];
        if (i0 >= n || i1 >= n || i2 >= n) continue;
        Vertex3D &v0 = verts[i0], &v1 = verts[i1], &v2 = verts[i2];

        float x1 = v1.x - v0.x, y1 = v1.y - v0.y, z1 = v1.z - v0.z;
        float x2 = v2.x - v0.x, y2 = v2.y - v0.y, z2 = v2.z - v0.z;
        float s1 = v1.u - v0.u, t1 = v1.v - v0.v;
        float s2 = v2.u - v0.u, t2 = v2.v - v0.v;
        float r = s1 * t2 - s2 * t1;
        if (fabsf(r) < 1e-8f) continue;
        r = 1.0f / r;

        float sx = (t2 * x1 - t1 * x2) * r;
        float sy = (t2 * y1 - t1 * y2) * r;
        float sz = (t2 * z1 - t1 * z2) * r;
        float bx = (s1 * x2 - s2 * x1) * r;
        float by = (s1 * y2 - s2 * y1) * r;
        float bz = (s1 * z2 - s2 * z1) * r;

        uint32_t ii[3] = { i0, i1, i2 };
        for (int k = 0; k < 3; ++k) {
            tanAcc[ii[k] * 3 + 0] += sx;
            tanAcc[ii[k] * 3 + 1] += sy;
            tanAcc[ii[k] * 3 + 2] += sz;
            bitAcc[ii[k] * 3 + 0] += bx;
            bitAcc[ii[k] * 3 + 1] += by;
            bitAcc[ii[k] * 3 + 2] += bz;
        }
    }

    for (size_t i = 0; i < n; ++i) {
        float nx = verts[i].nx, ny = verts[i].ny, nz = verts[i].nz;
        float tx = tanAcc[i * 3 + 0], ty = tanAcc[i * 3 + 1], tz = tanAcc[i * 3 + 2];
        float d = nx * tx + ny * ty + nz * tz;
        tx -= nx * d; ty -= ny * d; tz -= nz * d;
        float len = sqrtf(tx * tx + ty * ty + tz * tz);
        if (len < 1e-8f) { tx = 1.0f; ty = 0.0f; tz = 0.0f; }
        else { tx /= len; ty /= len; tz /= len; }

        float cx = ny * tz - nz * ty;
        float cy = nz * tx - nx * tz;
        float cz = nx * ty - ny * tx;
        float w = (cx * bitAcc[i * 3 + 0] + cy * bitAcc[i * 3 + 1] +
                   cz * bitAcc[i * 3 + 2]) < 0.0f ? -1.0f : 1.0f;

        verts[i].tx = tx; verts[i].ty = ty; verts[i].tz = tz; verts[i].tw = w;
    }
}

static Mesh buildMesh(const fastgltf::Asset &asset, const fastgltf::Primitive &prim)
{
    Mesh none; std::memset(&none, 0, sizeof(none));

    const auto *posAttr = prim.findAttribute("POSITION");
    if (!posAttr) return none;

    size_t vertexCount = asset.accessors[posAttr->accessorIndex].count;
    if (vertexCount == 0 || vertexCount > 100000000u) return none;

    std::vector<Vertex3D> verts(vertexCount);
    std::vector<float> scratch(vertexCount * 3);

    /* POSITION */
    if (!readFloats(asset, asset.accessors[posAttr->accessorIndex],
                    scratch.data(), 3, vertexCount)) {
        return none;
    }
    for (size_t i = 0; i < vertexCount; ++i) {
        verts[i].x = scratch[i * 3 + 0];
        verts[i].y = scratch[i * 3 + 1];
        verts[i].z = scratch[i * 3 + 2];
    }

    /* NORMAL */
    bool haveNormal = false;
    if (const auto *nAttr = prim.findAttribute("NORMAL")) {
        const fastgltf::Accessor &acc = asset.accessors[nAttr->accessorIndex];
        if (acc.count == vertexCount && readFloats(asset, acc, scratch.data(), 3, vertexCount)) {
            for (size_t i = 0; i < vertexCount; ++i) {
                verts[i].nx = scratch[i * 3 + 0];
                verts[i].ny = scratch[i * 3 + 1];
                verts[i].nz = scratch[i * 3 + 2];
            }
            haveNormal = true;
        }
    }
    if (!haveNormal) {
        for (size_t i = 0; i < vertexCount; ++i) {
            verts[i].nx = 0.0f; verts[i].ny = 1.0f; verts[i].nz = 0.0f;
        }
    }

    /* TEXCOORD_0 */
    bool haveUV = false;
    if (const auto *tAttr = prim.findAttribute("TEXCOORD_0")) {
        const fastgltf::Accessor &acc = asset.accessors[tAttr->accessorIndex];
        if (acc.count == vertexCount) {
            std::vector<float> uv(vertexCount * 2);
            if (readFloats(asset, acc, uv.data(), 2, vertexCount)) {
                for (size_t i = 0; i < vertexCount; ++i) {
                    verts[i].u = uv[i * 2 + 0];
                    verts[i].v = uv[i * 2 + 1];
                }
                haveUV = true;
            }
        }
    }

    /* TANGENT (glTF stores xyz + handedness w). If absent, generate the
       Lengyel basis after indices are known; otherwise normal maps are
       sampled with a zero basis. */
    bool haveTangent = false;
    if (const auto *gAttr = prim.findAttribute("TANGENT")) {
        const fastgltf::Accessor &acc = asset.accessors[gAttr->accessorIndex];
        if (acc.count == vertexCount) {
            std::vector<float> tan(vertexCount * 4);
            if (readFloats(asset, acc, tan.data(), 4, vertexCount)) {
                for (size_t i = 0; i < vertexCount; ++i) {
                    verts[i].tx = tan[i * 4 + 0];
                    verts[i].ty = tan[i * 4 + 1];
                    verts[i].tz = tan[i * 4 + 2];
                    verts[i].tw = tan[i * 4 + 3];
                }
                haveTangent = true;
            }
        }
    }

    /* COLOR_0 (vec3 or vec4, normalized ints allowed). Missing alpha = 1. */
    if (const auto *cAttr = prim.findAttribute("COLOR_0")) {
        const fastgltf::Accessor &acc = asset.accessors[cAttr->accessorIndex];
        const int comps = accessorComponentCount(acc.type);
        if (comps >= 3 && acc.count == vertexCount) {
            std::vector<float> col(vertexCount * (size_t)comps);
            if (readFloats(asset, acc, col.data(), comps, vertexCount)) {
                for (size_t i = 0; i < vertexCount; ++i) {
                    verts[i].cr = col[i * (size_t)comps + 0];
                    verts[i].cg = col[i * (size_t)comps + 1];
                    verts[i].cb = col[i * (size_t)comps + 2];
                    verts[i].ca = (comps >= 4) ? col[i * (size_t)comps + 3] : 1.0f;
                }
            }
        }
    }

    /* Indices: component-type agnostic (8/16/32-bit all supported). */
    std::vector<uint32_t> indices;
    if (prim.indicesAccessor.has_value()) {
        const fastgltf::Accessor &acc = asset.accessors[prim.indicesAccessor.value()];
        if (acc.count == 0) return none;
        indices.resize(acc.count);
        if (!readIndices(asset, acc, indices.data(), indices.size())) return none;
    } else {
        indices.resize(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) indices[i] = (uint32_t)i;
    }

    for (uint32_t idx : indices) {
        if (idx >= (uint32_t)vertexCount) return none;
    }

    if (!haveTangent && haveUV) generateTangents(verts, indices);

    return vgraphics_RegisterMesh(verts.data(), (int)vertexCount,
                                  indices.data(), (int)indices.size());
}

/* ================================================================== */
/*  Public C ABI                                                       */
/* ================================================================== */

FastGltfLibAsset vfastgltf_lib_load(const char *path, FastGltfLibOptions options,
                                    FastGltfLibInfo *outInfo)
{
    if (outInfo) std::memset(outInfo, 0, sizeof(*outInfo));
    if (!path) return 0;

    /* --- read the file ---
     * MappedGltfFile memory-maps the file and (per the upstream example) is
     * the right choice for both .gltf and .glb, since GLB parsing needs to
     * seek into the binary chunk rather than read it as a forward stream. */
    auto gltfFile = fastgltf::MappedGltfFile::FromPath(std::filesystem::path(path));
    if (!bool(gltfFile)) {
        if (options & FASTGLTF_LIB_VERBOSE) {
            std::fprintf(stderr, "[fastgltf] cannot open %s\n", path);
        }
        return 0;
    }

    fastgltf::Parser parser;

    /* Options is a bit-flag enum, so the combination needs an explicit cast
     * back to the enum type in C++.
     *
     * GenerateMeshIndices matters: buildMesh() prefers the index accessor and
     * only falls back to a sequential range when it is absent, so asking
     * fastgltf to synthesise indices keeps every primitive indexed. */
    const auto loadOptions = static_cast<fastgltf::Options>(
        fastgltf::Options::DontRequireValidAssetMember |
        fastgltf::Options::LoadExternalBuffers |
        fastgltf::Options::LoadExternalImages |
        fastgltf::Options::GenerateMeshIndices);

    /* loadGltf takes the getter by reference in v0.9.0 (some versions/docs
     * show a pointer), so bind the Expected payload to a reference and pass
     * it directly. `gltfFile` must outlive the parse, which it does. */
    fastgltf::MappedGltfFile &gltfGetter = gltfFile.get();
    auto assetExpected = parser.loadGltf(
        gltfGetter, std::filesystem::path(path).parent_path(), loadOptions);

    if (assetExpected.error() != fastgltf::Error::None) {
        if (options & FASTGLTF_LIB_VERBOSE) {
            std::fprintf(stderr, "[fastgltf] parse failed (%s): %s\n", path,
                         fastgltf::getErrorMessage(assetExpected.error()).data());
        }
        return 0;
    }

    std::unique_ptr<FgEntry> entry(new FgEntry());
    /* Move the parsed Asset out of Expected; Expected itself is not assumed
     * to be movable, only its payload. */
    entry->asset = std::make_unique<fastgltf::Asset>(
        std::move(assetExpected.get()));
    {
        std::string p(path);
        size_t cut = p.find_last_of("/\\");
        entry->dir = (cut == std::string::npos) ? std::string() : p.substr(0, cut + 1);
    }

    size_t slot = g_entries.size();
    for (size_t i = 0; i < g_entries.size(); ++i) {
        if (!g_entries[i] || !g_entries[i]->inUse) { slot = i; break; }
    }
    entry->inUse = true;
    if (slot == g_entries.size()) g_entries.push_back(std::move(entry));
    else                          g_entries[slot] = std::move(entry);

    if (outInfo) {
        const fastgltf::Asset &a = *g_entries[slot]->asset;
        outInfo->ok            = 1;
        outInfo->meshCount     = (int)a.meshes.size();
        outInfo->materialCount = (int)a.materials.size();
        outInfo->textureCount  = (int)a.textures.size();
        outInfo->nodeCount     = (int)a.nodes.size();
        outInfo->sceneCount    = (int)a.scenes.size();
        outInfo->animationCount = (int)a.animations.size();
        outInfo->imageCount    = (int)a.images.size();
        outInfo->skinnedMeshCount = (int)a.skins.size();
    }
    return (FastGltfLibAsset)(slot + 1);
}

void vfastgltf_lib_release(FastGltfLibAsset handle) {
    FgEntry *e = entryOf(handle);
    if (!e) return;
    e->asset.reset();
    e->inUse = false;
}

size_t vfastgltf_lib_mesh_count(FastGltfLibAsset handle) {
    FgEntry *e = entryOf(handle);
    return e ? e->asset->meshes.size() : 0;
}

void vfastgltf_lib_get_mesh_info(FastGltfLibAsset handle, size_t meshIndex,
                                 FastGltfLibMeshInfo *out)
{
    if (!out) return;
    std::memset(out, 0, sizeof(*out));
    FgEntry *e = entryOf(handle);
    if (!e || meshIndex >= e->asset->meshes.size()) return;

    const fastgltf::Mesh &mesh = e->asset->meshes[meshIndex];
    out->primitiveCount = (int)mesh.primitives.size();
    int v = 0;
    for (const fastgltf::Primitive &p : mesh.primitives) {
        const auto *pos = p.findAttribute("POSITION");
        if (!pos) continue;
        size_t n = e->asset->accessors[pos->accessorIndex].count;
        if (n > (size_t)INT32_MAX - (size_t)v) return;
        v += (int)n;
    }
    out->vertexCount = v;
}

int vfastgltf_lib_get_material(FastGltfLibAsset handle, size_t materialIndex,
                               FastGltfLibMaterial *out)
{
    if (!out) return 0;
    std::memset(out, 0, sizeof(*out));
    out->baseColor[0] = out->baseColor[1] = out->baseColor[2] = out->baseColor[3] = 1.0f;
    out->metallic  = 1.0f;
    out->roughness = 1.0f;
    out->normalScale = 1.0f;
    out->occlusionStrength = 1.0f;
    for (int i = 0; i < FASTGLTF_LIB_MAX_MAPS; ++i) out->textureIndex[i] = -1;

    FgEntry *e = entryOf(handle);
    if (!e || materialIndex >= e->asset->materials.size()) return 0;

    const fastgltf::Material &m = e->asset->materials[materialIndex];
    const auto &pbr = m.pbrData;

    out->baseColor[0] = pbr.baseColorFactor[0];
    out->baseColor[1] = pbr.baseColorFactor[1];
    out->baseColor[2] = pbr.baseColorFactor[2];
    out->baseColor[3] = pbr.baseColorFactor[3];
    out->metallic  = pbr.metallicFactor;
    out->roughness = pbr.roughnessFactor;
    out->emissive[0] = m.emissiveFactor[0];
    out->emissive[1] = m.emissiveFactor[1];
    out->emissive[2] = m.emissiveFactor[2];
    if (m.normalTexture.has_value())    out->normalScale = m.normalTexture->scale;
    if (m.occlusionTexture.has_value()) out->occlusionStrength = m.occlusionTexture->strength;

    out->doubleSided = m.doubleSided ? 1 : 0;
    out->alphaMode   = (int)m.alphaMode;
    out->alphaCutoff = m.alphaCutoff;

    /* TextureInfo::textureIndex is a plain size_t; the optional is the
     * TextureInfo itself. */
    auto tex = [](const auto &info) -> int {
        return info.has_value() ? (int)info->textureIndex : -1;
    };
    out->textureIndex[FASTGLTF_LIB_MAP_ALBEDO]            = tex(pbr.baseColorTexture);
    out->textureIndex[FASTGLTF_LIB_MAP_NORMAL]            = tex(m.normalTexture);
    out->textureIndex[FASTGLTF_LIB_MAP_METALLICROUGHNESS] = tex(pbr.metallicRoughnessTexture);
    out->textureIndex[FASTGLTF_LIB_MAP_OCCLUSION]         = tex(m.occlusionTexture);
    out->textureIndex[FASTGLTF_LIB_MAP_EMISSIVE]          = tex(m.emissiveTexture);

    return 1;
}

Model vfastgltf_lib_build_model(FastGltfLibAsset handle,
                                const FastGltfLibBuildOptions *options)
{
    Model empty; std::memset(&empty, 0, sizeof(empty));
    FgEntry *e = entryOf(handle);
    if (!e) return empty;

    const fastgltf::Asset &asset = *e->asset;

    FastGltfLibBuildOptions opt;
    if (options) opt = *options;
    else {
        std::memset(&opt, 0, sizeof(opt));
        opt.sceneIndex   = 0;
        opt.scale        = 1.0f;
        opt.srgbTextures = 1;
    }
    if (!(opt.scale > 0.0f)) opt.scale = 1.0f;

    size_t sceneIndex = (opt.sceneIndex < 0) ? 0 : (size_t)opt.sceneIndex;
    if (sceneIndex >= asset.scenes.size() && !asset.scenes.empty()) sceneIndex = 0;

    std::vector<DrawSlot> slots;
    /* const_cast is required, not stylistic: fastgltf 0.9.0's
     * iterateSceneNodes does not compile when handed a `const Asset&`
     * (verified by minimal repro: non-const compiles, const fails with
     * C2672 "std::invoke: no matching overloaded function" — the internal
     * recursion propagates const onto its own lambda). The function only
     * reads the asset, so removing const here is safe. */
    collectDrawSlots(const_cast<fastgltf::Asset &>(asset), sceneIndex, slots);
    if (slots.empty()) return empty;

    /* --- upload each glTF mesh's primitives exactly once --- */
    std::vector<std::vector<Mesh>> meshPrims(asset.meshes.size());
    std::vector<bool>              meshBuilt(asset.meshes.size(), false);

    std::vector<Mesh>   drawMeshes;
    std::vector<int>    drawMats;
    std::vector<Matrix> drawXf;
    drawMeshes.reserve(slots.size());
    drawMats.reserve(slots.size());
    drawXf.reserve(slots.size());

    /* --- materials actually referenced --- */
    std::vector<size_t> usedMats;
    for (const DrawSlot &s : slots) {
        if (s.materialIndex == SIZE_MAX) continue;
        bool seen = false;
        for (size_t u : usedMats) if (u == s.materialIndex) { seen = true; break; }
        if (!seen) usedMats.push_back(s.materialIndex);
    }

    TexCache cache;
    std::vector<Material> modelMats;
    modelMats.reserve(usedMats.empty() ? 1 : usedMats.size());

    for (size_t dm : usedMats) {
        FastGltfLibMaterial info;
        vfastgltf_lib_get_material(handle, dm, &info);

        Material mat = LoadMaterialDefault();
        mat.baseColor.r = to_srgb8(clamp01(info.baseColor[0]));
        mat.baseColor.g = to_srgb8(clamp01(info.baseColor[1]));
        mat.baseColor.b = to_srgb8(clamp01(info.baseColor[2]));
        mat.baseColor.a = (unsigned char)(clamp01(info.baseColor[3]) * 255.0f + 0.5f);
        mat.baseColorFactor[0] = clamp01(info.baseColor[0]);
        mat.baseColorFactor[1] = clamp01(info.baseColor[1]);
        mat.baseColorFactor[2] = clamp01(info.baseColor[2]);
        mat.baseColorFactor[3] = clamp01(info.baseColor[3]);
        mat.metallic    = clamp01(info.metallic);
        mat.roughness   = clamp01(info.roughness);
        mat.normalScale = info.normalScale;
        mat.aoStrength  = clamp01(info.occlusionStrength);
        mat.doubleSided = info.doubleSided;
        mat.alphaMode   = info.alphaMode;
        mat.alphaCutoff = info.alphaCutoff;

        mat.emissionColor.r = to_srgb8(clamp01(info.emissive[0]));
        mat.emissionColor.g = to_srgb8(clamp01(info.emissive[1]));
        mat.emissionColor.b = to_srgb8(clamp01(info.emissive[2]));
        mat.emissionColor.a = 255;
        float emax = info.emissive[0];
        if (info.emissive[1] > emax) emax = info.emissive[1];
        if (info.emissive[2] > emax) emax = info.emissive[2];
        mat.emissionStrength = (emax > 0.0f) ? 1.0f : 0.0f;

        struct { int map; int target; bool srgb; } maps[] = {
            { FASTGLTF_LIB_MAP_ALBEDO,            MATERIAL_MAP_ALBEDO,            opt.srgbTextures != 0 },
            { FASTGLTF_LIB_MAP_NORMAL,            MATERIAL_MAP_NORMAL,            false },
            { FASTGLTF_LIB_MAP_METALLICROUGHNESS, MATERIAL_MAP_METALLICROUGHNESS, false },
            { FASTGLTF_LIB_MAP_OCCLUSION,         MATERIAL_MAP_AO,                false },
            { FASTGLTF_LIB_MAP_EMISSIVE,          MATERIAL_MAP_EMISSION,          opt.srgbTextures != 0 },
        };
        for (const auto &m : maps) {
            int ti = info.textureIndex[m.map];
            if (ti < 0) continue;
            Texture2D t = loadTexture(asset, (size_t)ti, m.srgb, cache);
            if (t.id) SetMaterialTexture(&mat, m.target, t);
        }
        modelMats.push_back(mat);
    }
    if (modelMats.empty()) modelMats.push_back(LoadMaterialDefault());

    /* --- assemble draw slots --- */
    for (const DrawSlot &s : slots) {
        if (!meshBuilt[s.meshIndex]) {
            meshBuilt[s.meshIndex] = true;
            const fastgltf::Mesh &mesh = asset.meshes[s.meshIndex];
            meshPrims[s.meshIndex].resize(mesh.primitives.size());
            for (size_t pi = 0; pi < mesh.primitives.size(); ++pi) {
                meshPrims[s.meshIndex][pi] = buildMesh(asset, mesh.primitives[pi]);
            }
        }
        Mesh m = (s.primIndex < meshPrims[s.meshIndex].size())
               ? meshPrims[s.meshIndex][s.primIndex] : Mesh{};
        if (!m.id) continue;

        int matSlot = 0;
        for (size_t k = 0; k < usedMats.size(); ++k) {
            if (s.materialIndex != SIZE_MAX && usedMats[k] == s.materialIndex) {
                matSlot = (int)k;
                break;
            }
        }

        /* Assign column-by-column from fastgltf::math::fmat4x4 to engine Matrix.
           fastgltf stores column vectors: s.world[c][r]. */
        Matrix xf;
        xf.m0  = s.world[0][0]; xf.m1  = s.world[0][1]; xf.m2  = s.world[0][2]; xf.m3  = s.world[0][3];
        xf.m4  = s.world[1][0]; xf.m5  = s.world[1][1]; xf.m6  = s.world[1][2]; xf.m7  = s.world[1][3];
        xf.m8  = s.world[2][0]; xf.m9  = s.world[2][1]; xf.m10 = s.world[2][2]; xf.m11 = s.world[2][3];
        xf.m12 = s.world[3][0]; xf.m13 = s.world[3][1]; xf.m14 = s.world[3][2]; xf.m15 = s.world[3][3];
        if (opt.scale != 1.0f) {
            xf.m12 *= opt.scale;
            xf.m13 *= opt.scale;
            xf.m14 *= opt.scale;
        }

        drawMeshes.push_back(m);
        drawMats.push_back(matSlot);
        drawXf.push_back(xf);
    }

    if (drawMeshes.empty()) return empty;

    Model model = vgraphics_CreateModelFromMeshesEx(
        drawMeshes.data(), drawMats.data(), drawXf.data(),
        (int)drawMeshes.size(), modelMats.data(), (int)modelMats.size());

    /* Hand every texture to the model so UnloadModel frees it. */
    if (model.id) {
        for (size_t i = 0; i < cache.tex.size(); ++i) {
            Texture2D t = cache.tex[i];
            if (!t.id) continue;
            bool dup = false;
            for (size_t k = 0; k < i; ++k)
                if (cache.tex[k].id == t.id) { dup = true; break; }
            if (dup) continue;
            if (!vgraphics_TrackModelTexture(model, t)) UnloadTexture(t);
        }
    } else {
        for (size_t i = 0; i < cache.tex.size(); ++i) {
            Texture2D t = cache.tex[i];
            if (!t.id) continue;
            bool dup = false;
            for (size_t k = 0; k < i; ++k)
                if (cache.tex[k].id == t.id) { dup = true; break; }
            if (!dup) UnloadTexture(t);
        }
    }

    return model;
}

const char *vfastgltf_lib_version(void) {
    return "spnda/fastgltf (C++ wrapper)";
}

#endif /* VLCRLIB_HAS_FASTGLTF */