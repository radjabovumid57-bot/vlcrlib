/*
 * vlcrc — minimal asset validator / manifest cooker.
 *
 * Reads glTF/GLB through tinygltf and FBX/OBJ through Assimp, reports mesh,
 * material, skeleton and animation statistics, and writes a JSON manifest.
 * Texture compression/LOD generation are intentionally left to the next
 * cooker milestone.
 */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "vcooked_format.h"

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>

#if defined(VLCRLIB_HAS_ASSIMP) && VLCRLIB_HAS_ASSIMP
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#endif

#if defined(VLCRLIB_HAS_KTX) && VLCRLIB_HAS_KTX
#include <ktx.h>
#include <vulkan/vulkan.h>
#include <stb_image_write.h>
#endif

struct Manifest {
    std::string source;
    std::string format;
    int meshes = 0;
    int materials = 0;
    int images = 0;
    int skins = 0;
    int bones = 0;
    int animations = 0;
    long long vertices = 0;
    long long triangles = 0;
    float boundsMin[3] = { 1e30f, 1e30f, 1e30f };
    float boundsMax[3] = { -1e30f, -1e30f, -1e30f };
    std::vector<std::string> meshNames;
    std::vector<std::string> materialNames;
    std::vector<std::string> animationNames;
    std::vector<std::string> warnings;
    std::string error;

    void include(float x, float y, float z) {
        boundsMin[0] = std::min(boundsMin[0], x);
        boundsMin[1] = std::min(boundsMin[1], y);
        boundsMin[2] = std::min(boundsMin[2], z);
        boundsMax[0] = std::max(boundsMax[0], x);
        boundsMax[1] = std::max(boundsMax[1], y);
        boundsMax[2] = std::max(boundsMax[2], z);
    }
    bool hasBounds() const { return boundsMin[0] <= boundsMax[0]; }
    float height() const { return hasBounds() ? boundsMax[1] - boundsMin[1] : 0.0f; }
};

static std::string jsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default: o += c;
        }
    }
    return o;
}

static std::string extOf(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return ext;
}

static bool loadGltf(const std::string& path, Manifest& m) {
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;
    bool ok = extOf(path) == ".glb"
        ? loader.LoadBinaryFromFile(&model, &err, &warn, path)
        : loader.LoadASCIIFromFile(&model, &err, &warn, path);
    if (!ok) {
        m.error = err.empty() ? "glTF load failed" : err;
        return false;
    }
    m.format = "gltf";
    m.meshes = (int)model.meshes.size();
    m.materials = (int)model.materials.size();
    m.images = (int)model.images.size();
    m.skins = (int)model.skins.size();
    m.animations = (int)model.animations.size();

    for (const auto& mesh : model.meshes) m.meshNames.push_back(mesh.name);
    for (const auto& mat : model.materials) m.materialNames.push_back(mat.name);
    for (const auto& anim : model.animations) m.animationNames.push_back(anim.name);
    for (const auto& skin : model.skins) m.bones += (int)skin.joints.size();

    for (const auto& mesh : model.meshes) {
        for (const auto& prim : mesh.primitives) {
            auto it = prim.attributes.find("POSITION");
            if (it == prim.attributes.end()) continue;
            const auto& acc = model.accessors[it->second];
            m.vertices += (long long)acc.count;
            if (acc.minValues.size() >= 3 && acc.maxValues.size() >= 3)
                m.include((float)acc.minValues[0], (float)acc.minValues[1],
                          (float)acc.minValues[2]);
            if (acc.minValues.size() >= 3 && acc.maxValues.size() >= 3)
                m.include((float)acc.maxValues[0], (float)acc.maxValues[1],
                          (float)acc.maxValues[2]);
            if (prim.indices >= 0)
                m.triangles += (long long)(model.accessors[prim.indices].count / 3);
            else
                m.triangles += (long long)(acc.count / 3);

            if (prim.attributes.find("NORMAL") == prim.attributes.end())
                m.warnings.push_back("mesh has no NORMAL attribute");
            if (prim.attributes.find("TEXCOORD_0") == prim.attributes.end())
                m.warnings.push_back("mesh has no TEXCOORD_0");
        }
    }
    if (!warn.empty()) m.warnings.push_back("tinygltf: " + warn);
    return true;
}

#if defined(VLCRLIB_HAS_ASSIMP) && VLCRLIB_HAS_ASSIMP
static bool loadAssimp(const std::string& path, Manifest& m) {
    Assimp::Importer imp;
    const aiScene* scene = imp.ReadFile(path,
        aiProcess_Triangulate | aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace | aiProcess_JoinIdenticalVertices);
    if (!scene) {
        m.error = imp.GetErrorString();
        return false;
    }
    m.format = extOf(path).empty() ? "assimp" : extOf(path).substr(1);
    m.meshes = (int)scene->mNumMeshes;
    m.materials = (int)scene->mNumMaterials;
    m.animations = (int)scene->mNumAnimations;

    std::vector<float> heights;
    for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
        const aiMesh* mesh = scene->mMeshes[i];
        m.meshNames.push_back(mesh->mName.C_Str());
        m.vertices += mesh->mNumVertices;
        m.triangles += (long long)mesh->mNumFaces;
        m.bones += (int)mesh->mNumBones;
        float hMin = 1e30f, hMax = -1e30f;
        for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
            const aiVector3D& p = mesh->mVertices[v];
            m.include(p.x, p.y, p.z);
            hMin = std::min(hMin, p.y);
            hMax = std::max(hMax, p.y);
        }
        if (hMax > hMin) heights.push_back(hMax - hMin);
        if (!mesh->HasNormals()) m.warnings.push_back("mesh has no normals");
        if (!mesh->HasTextureCoords(0)) m.warnings.push_back("mesh has no UV0");
    }
    for (unsigned i = 0; i < scene->mNumMaterials; ++i)
        m.materialNames.push_back(scene->mMaterials[i]->GetName().C_Str());
    for (unsigned i = 0; i < scene->mNumAnimations; ++i)
        m.animationNames.push_back(scene->mAnimations[i]->mName.C_Str());

    if (heights.size() > 1) {
        float lo = *std::min_element(heights.begin(), heights.end());
        float hi = *std::max_element(heights.begin(), heights.end());
        if (lo > 1e-4f && hi > lo * 8.0f)
            m.warnings.push_back("meshes have incompatible scales (likely FBX units)");
    }
    if (m.height() > 10.0f)
        m.warnings.push_back("asset height > 10 m; check FBX centimeter units");
    return true;
}
#endif

static bool readFloats(const tinygltf::Model& model, int index,
                       int comps, std::vector<float>& out) {
    if (index < 0 || index >= (int)model.accessors.size()) return false;
    const auto& a = model.accessors[index];
    if (a.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) return false;
    if (a.bufferView < 0) return false;
    const auto& bv = model.bufferViews[a.bufferView];
    const auto& buf = model.buffers[bv.buffer];
    int ac = a.type == TINYGLTF_TYPE_VEC2 ? 2 :
             a.type == TINYGLTF_TYPE_VEC3 ? 3 :
             a.type == TINYGLTF_TYPE_VEC4 ? 4 : 0;
    if (ac < comps) return false;
    size_t stride = bv.byteStride ? bv.byteStride : (size_t)ac * sizeof(float);
    size_t base = (size_t)bv.byteOffset + a.byteOffset;
    out.resize((size_t)a.count * comps);
    for (size_t i = 0; i < a.count; ++i) {
        size_t off = base + i * stride;
        if (off + (size_t)comps * sizeof(float) > buf.data.size()) return false;
        const float *p = (const float *)(buf.data.data() + off);
        for (int c = 0; c < comps; ++c) out[i*comps+c] = p[c];
    }
    return true;
}

static bool readIndices(const tinygltf::Model& model, int index,
                        std::vector<uint32_t>& out) {
    if (index < 0 || index >= (int)model.accessors.size()) return false;
    const auto& a = model.accessors[index];
    if (a.bufferView < 0) return false;
    const auto& bv = model.bufferViews[a.bufferView];
    const auto& buf = model.buffers[bv.buffer];
    size_t stride = bv.byteStride ? bv.byteStride : 0;
    if (!stride) stride = a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT ? 2 : 4;
    size_t base = (size_t)bv.byteOffset + a.byteOffset;
    out.resize(a.count);
    for (size_t i = 0; i < a.count; ++i) {
        size_t off = base + i * stride;
        if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            if (off + 1 > buf.data.size()) return false;
            out[i] = buf.data[off];
        } else if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            if (off + 2 > buf.data.size()) return false;
            uint16_t v; std::memcpy(&v, buf.data.data()+off, 2); out[i] = v;
        } else if (a.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
            if (off + 4 > buf.data.size()) return false;
            uint32_t v; std::memcpy(&v, buf.data.data()+off, 4); out[i] = v;
        } else return false;
    }
    return true;
}

static bool writeCooked(const std::string& path,
                        const std::vector<CookVertex>& verts,
                        const std::vector<uint32_t>& indices,
                        const std::vector<CookMaterial>& materials,
                        const std::vector<CookTextureRef>& textures,
                        const std::vector<CookMesh>& meshes,
                        const std::string& strings) {
    if (verts.empty() || indices.empty() || materials.empty() || meshes.empty())
        return false;
    CookedHeader h = {};
    std::memcpy(h.magic, "VLCRMSH", 7);
    h.version = 3;
    h.vertexCount = (uint32_t)verts.size();
    h.indexCount = (uint32_t)indices.size();
    h.indexType = 32;
    h.materialCount = (uint32_t)materials.size();
    h.textureCount = (uint32_t)textures.size();
    h.stringBytes = (uint32_t)strings.size();
    h.meshCount = (uint32_t)meshes.size();
    h.bounds[0] = h.bounds[1] = h.bounds[2] = 1e30f;
    h.bounds[3] = h.bounds[4] = h.bounds[5] = -1e30f;
    for (const auto& v : verts) {
        for (int k = 0; k < 3; ++k) {
            h.bounds[k] = std::min(h.bounds[k], v.p[k]);
            h.bounds[3+k] = std::max(h.bounds[3+k], v.p[k]);
        }
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write((const char *)&h, sizeof(h));
    f.write((const char *)verts.data(), (std::streamsize)(verts.size() * sizeof(CookVertex)));
    f.write((const char *)indices.data(), (std::streamsize)(indices.size() * sizeof(uint32_t)));
    f.write(strings.data(), (std::streamsize)strings.size());
    if (!textures.empty())
        f.write((const char *)textures.data(),
                (std::streamsize)(textures.size() * sizeof(CookTextureRef)));
    f.write((const char *)materials.data(),
            (std::streamsize)(materials.size() * sizeof(CookMaterial)));
    f.write((const char *)meshes.data(),
            (std::streamsize)(meshes.size() * sizeof(CookMesh)));
    return (bool)f;
}

struct Mat4 { float m[16]; };

static Mat4 matIdentity(void) {
    Mat4 r = {};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

static Mat4 matMul(const Mat4& a, const Mat4& b) {
    Mat4 r = {};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[k*4+row] * b.m[c*4+k];
            r.m[c*4+row] = s;
        }
    return r;
}

static Mat4 matFromNode(const tinygltf::Node& n) {
    Mat4 r = matIdentity();
    if (n.matrix.size() == 16) {
        for (int i = 0; i < 16; ++i) r.m[i] = (float)n.matrix[i];
        return r;
    }
    float tx = n.translation.size() > 0 ? (float)n.translation[0] : 0.0f;
    float ty = n.translation.size() > 1 ? (float)n.translation[1] : 0.0f;
    float tz = n.translation.size() > 2 ? (float)n.translation[2] : 0.0f;
    float qx = n.rotation.size() > 0 ? (float)n.rotation[0] : 0.0f;
    float qy = n.rotation.size() > 1 ? (float)n.rotation[1] : 0.0f;
    float qz = n.rotation.size() > 2 ? (float)n.rotation[2] : 0.0f;
    float qw = n.rotation.size() > 3 ? (float)n.rotation[3] : 1.0f;
    float sx = n.scale.size() > 0 ? (float)n.scale[0] : 1.0f;
    float sy = n.scale.size() > 1 ? (float)n.scale[1] : 1.0f;
    float sz = n.scale.size() > 2 ? (float)n.scale[2] : 1.0f;
    float x2 = qx+qx, y2 = qy+qy, z2 = qz+qz;
    float xx = qx*x2, xy = qx*y2, xz = qx*z2;
    float yy = qy*y2, yz = qy*z2, zz = qz*z2;
    float wx = qw*x2, wy = qw*y2, wz = qw*z2;
    Mat4 rot = matIdentity();
    rot.m[0] = 1-(yy+zz); rot.m[1] = xy+wz;     rot.m[2] = xz-wy;
    rot.m[4] = xy-wz;     rot.m[5] = 1-(xx+zz); rot.m[6] = yz+wx;
    rot.m[8] = xz+wy;     rot.m[9] = yz-wx;     rot.m[10] = 1-(xx+yy);
    Mat4 scl = matIdentity();
    scl.m[0] = sx; scl.m[5] = sy; scl.m[10] = sz;
    Mat4 tr = matIdentity();
    tr.m[12] = tx; tr.m[13] = ty; tr.m[14] = tz;
    return matMul(tr, matMul(rot, scl));
}

static bool findNodeWorld(const tinygltf::Model& model, int nodeIndex, int meshIndex,
                          const Mat4& parent, Mat4& out) {
    if (nodeIndex < 0 || nodeIndex >= (int)model.nodes.size()) return false;
    const tinygltf::Node& n = model.nodes[nodeIndex];
    Mat4 world = matMul(parent, matFromNode(n));
    if (n.mesh == meshIndex) { out = world; return true; }
    for (int child : n.children)
        if (findNodeWorld(model, child, meshIndex, world, out)) return true;
    return false;
}

static bool meshNodeWorld(const tinygltf::Model& model, int meshIndex, Mat4& out) {
    int scene = model.defaultScene >= 0 ? model.defaultScene :
                (!model.scenes.empty() ? 0 : -1);
    if (scene >= 0 && scene < (int)model.scenes.size()) {
        for (int root : model.scenes[scene].nodes)
            if (findNodeWorld(model, root, meshIndex, matIdentity(), out)) return true;
    }
    for (int i = 0; i < (int)model.nodes.size(); ++i)
        if (model.nodes[i].mesh == meshIndex) { out = matFromNode(model.nodes[i]); return true; }
    return false;
}

static void transformPoint(const Mat4& m, float p[3]) {
    float x = p[0], y = p[1], z = p[2];
    p[0] = m.m[0]*x + m.m[4]*y + m.m[8]*z + m.m[12];
    p[1] = m.m[1]*x + m.m[5]*y + m.m[9]*z + m.m[13];
    p[2] = m.m[2]*x + m.m[6]*y + m.m[10]*z + m.m[14];
}

static void transformDir(const Mat4& m, float p[3]) {
    float x = p[0], y = p[1], z = p[2];
    float ox = m.m[0]*x + m.m[4]*y + m.m[8]*z;
    float oy = m.m[1]*x + m.m[5]*y + m.m[9]*z;
    float oz = m.m[2]*x + m.m[6]*y + m.m[10]*z;
    float len = std::sqrt(ox*ox + oy*oy + oz*oz);
    if (len > 1e-8f) { ox /= len; oy /= len; oz /= len; }
    p[0] = ox; p[1] = oy; p[2] = oz;
}

static CookMaterial defaultCookMaterial(void) {
    CookMaterial m = {};
    m.baseColorFactor[0] = m.baseColorFactor[1] = m.baseColorFactor[2] = m.baseColorFactor[3] = 1.0f;
    m.metallic = 1.0f;
    m.roughness = 1.0f;
    m.aoStrength = 1.0f;
    m.alphaCutoff = 0.5f;
    for (int i = 0; i < COOK_TEXTURE_SLOT_COUNT; ++i) m.mapIndex[i] = 0xFFFFFFFFu;
    std::snprintf(m.name, sizeof(m.name), "default");
    return m;
}

static CookMaterial buildGltfMaterial(const tinygltf::Model& model,
                                      const tinygltf::Primitive& prim,
                                      const std::string& ktxDir, bool bc5,
                                      std::vector<CookTextureRef>& textures,
                                      std::string& strings) {
    CookMaterial cm = defaultCookMaterial();
    if (prim.material < 0 || prim.material >= (int)model.materials.size())
        return cm;
    const auto& gm = model.materials[prim.material];
    const auto& pbr = gm.pbrMetallicRoughness;
    for (int i = 0; i < 4; ++i) cm.baseColorFactor[i] = (float)pbr.baseColorFactor[i];
    cm.metallic = (float)pbr.metallicFactor;
    cm.roughness = (float)pbr.roughnessFactor;
    for (int i = 0; i < 3; ++i) cm.emissive[i] = (float)gm.emissiveFactor[i];
    cm.alphaCutoff = (float)gm.alphaCutoff;
    cm.flags = (gm.doubleSided ? 1u : 0u) |
               (gm.extensions.count("KHR_materials_unlit") ? 2u : 0u);
    std::snprintf(cm.name, sizeof(cm.name), "%s", gm.name.c_str());

    auto addTexture = [&](int textureInfoIndex, uint32_t semantic) {
        if (textureInfoIndex < 0 || textureInfoIndex >= (int)model.textures.size()) return;
        int src = model.textures[textureInfoIndex].source;
        if (src < 0 || src >= (int)model.images.size()) return;
        std::string rel = !ktxDir.empty()
            ? ("image_" + std::to_string(src) + ".ktx2")
            : model.images[src].uri;
        if (rel.empty()) return;
        CookTextureRef ref = {};
        ref.semantic = semantic;
        ref.format = bc5 ? COOK_TEX_FORMAT_BC5 : COOK_TEX_FORMAT_BC7;
        ref.width = (uint32_t)model.images[src].width;
        ref.height = (uint32_t)model.images[src].height;
        ref.pathOffset = (uint32_t)strings.size();
        ref.pathLength = (uint32_t)rel.size();
        strings.append(rel);
        cm.mapIndex[semantic] = (uint32_t)textures.size();
        textures.push_back(ref);
    };
    addTexture(pbr.baseColorTexture.index, COOK_TEXTURE_ALBEDO);
    addTexture(gm.normalTexture.index, COOK_TEXTURE_NORMAL);
    addTexture(pbr.metallicRoughnessTexture.index, COOK_TEXTURE_METALLIC_ROUGHNESS);
    addTexture(gm.occlusionTexture.index, COOK_TEXTURE_AO);
    addTexture(gm.emissiveTexture.index, COOK_TEXTURE_EMISSIVE);
    return cm;
}

static bool cookGltfMesh(const std::string& path, const std::string& out,
                         const std::string& ktxDir, bool bc5) {
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;
    if (!(extOf(path) == ".glb"
            ? loader.LoadBinaryFromFile(&model, &err, &warn, path)
            : loader.LoadASCIIFromFile(&model, &err, &warn, path)))
        return false;

    std::vector<CookVertex> allVerts;
    std::vector<uint32_t> allIndices;
    std::vector<CookMaterial> materials;
    std::vector<CookTextureRef> textures;
    std::vector<CookMesh> meshes;
    std::string strings;

    for (int meshIndex = 0; meshIndex < (int)model.meshes.size(); ++meshIndex) {
        const auto& mesh = model.meshes[meshIndex];
        Mat4 world = matIdentity();
        meshNodeWorld(model, meshIndex, world);
        for (const auto& prim : mesh.primitives) {
            auto pit = prim.attributes.find("POSITION");
            if (pit == prim.attributes.end()) continue;
            std::vector<float> pos, nrm, uv, tan;
            if (!readFloats(model, pit->second, 3, pos)) continue;
            readFloats(model, prim.attributes.count("NORMAL") ? prim.attributes.at("NORMAL") : -1, 3, nrm);
            readFloats(model, prim.attributes.count("TEXCOORD_0") ? prim.attributes.at("TEXCOORD_0") : -1, 2, uv);
            readFloats(model, prim.attributes.count("TANGENT") ? prim.attributes.at("TANGENT") : -1, 4, tan);

            std::vector<uint32_t> indices;
            if (prim.indices >= 0) {
                if (!readIndices(model, prim.indices, indices)) continue;
            } else {
                indices.resize(pos.size()/3);
                for (size_t i = 0; i < indices.size(); ++i) indices[i] = (uint32_t)i;
            }

            uint32_t vertexOffset = (uint32_t)allVerts.size();
            uint32_t indexOffset = (uint32_t)allIndices.size();
            CookMesh rec = {};
            rec.vertexOffset = vertexOffset;
            rec.vertexCount = (uint32_t)(pos.size()/3);
            rec.indexOffset = indexOffset;
            rec.indexCount = (uint32_t)indices.size();
            rec.materialIndex = (int32_t)materials.size();
            rec.boundsMin[0] = rec.boundsMin[1] = rec.boundsMin[2] = 1e30f;
            rec.boundsMax[0] = rec.boundsMax[1] = rec.boundsMax[2] = -1e30f;

            for (size_t i = 0; i < pos.size()/3; ++i) {
                CookVertex v = {};
                v.p[0]=pos[i*3]; v.p[1]=pos[i*3+1]; v.p[2]=pos[i*3+2];
                v.n[0]=0; v.n[1]=1; v.n[2]=0;
                if (nrm.size() >= (i+1)*3) { v.n[0]=nrm[i*3]; v.n[1]=nrm[i*3+1]; v.n[2]=nrm[i*3+2]; }
                v.uv[0]=0; v.uv[1]=0;
                if (uv.size() >= (i+1)*2) { v.uv[0]=uv[i*2]; v.uv[1]=uv[i*2+1]; }
                v.t[0]=1; v.t[1]=0; v.t[2]=0; v.t[3]=1;
                if (tan.size() >= (i+1)*4) { v.t[0]=tan[i*4]; v.t[1]=tan[i*4+1]; v.t[2]=tan[i*4+2]; v.t[3]=tan[i*4+3]; }
                v.c[0]=v.c[1]=v.c[2]=v.c[3]=1.0f;
                transformPoint(world, v.p);
                transformDir(world, v.n);
                float tang[3] = { v.t[0], v.t[1], v.t[2] };
                transformDir(world, tang);
                v.t[0] = tang[0]; v.t[1] = tang[1]; v.t[2] = tang[2];
                for (int k = 0; k < 3; ++k) {
                    rec.boundsMin[k] = std::min(rec.boundsMin[k], v.p[k]);
                    rec.boundsMax[k] = std::max(rec.boundsMax[k], v.p[k]);
                }
                allVerts.push_back(v);
            }
            allIndices.insert(allIndices.end(), indices.begin(), indices.end());
            materials.push_back(buildGltfMaterial(model, prim, ktxDir, bc5,
                                                  textures, strings));
            meshes.push_back(rec);
        }
    }
    if (strings.empty()) strings = "default";
    return writeCooked(out, allVerts, allIndices, materials, textures, meshes, strings);
}

#if defined(VLCRLIB_HAS_ASSIMP) && VLCRLIB_HAS_ASSIMP
static bool cookAssimpMesh(const std::string& path, const std::string& out) {
    Assimp::Importer imp;
    const aiScene* scene = imp.ReadFile(path,
        aiProcess_Triangulate | aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace | aiProcess_JoinIdenticalVertices);
    if (!scene || scene->mNumMeshes == 0) return false;
    std::vector<CookVertex> allVerts;
    std::vector<uint32_t> allIndices;
    std::vector<CookMaterial> mats;
    std::vector<CookMesh> recs;

    for (unsigned mi = 0; mi < scene->mNumMeshes; ++mi) {
        const aiMesh* mesh = scene->mMeshes[mi];
        if (!mesh || !mesh->HasPositions()) continue;
        CookMesh rec = {};
        rec.vertexOffset = (uint32_t)allVerts.size();
        rec.vertexCount = mesh->mNumVertices;
        rec.indexOffset = (uint32_t)allIndices.size();
        rec.materialIndex = (int32_t)mats.size();
        rec.boundsMin[0] = rec.boundsMin[1] = rec.boundsMin[2] = 1e30f;
        rec.boundsMax[0] = rec.boundsMax[1] = rec.boundsMax[2] = -1e30f;

        for (unsigned i = 0; i < mesh->mNumVertices; ++i) {
            CookVertex v = {};
            v.p[0]=mesh->mVertices[i].x; v.p[1]=mesh->mVertices[i].y; v.p[2]=mesh->mVertices[i].z;
            v.n[0]=0; v.n[1]=1; v.n[2]=0;
            if (mesh->HasNormals()) { v.n[0]=mesh->mNormals[i].x; v.n[1]=mesh->mNormals[i].y; v.n[2]=mesh->mNormals[i].z; }
            v.uv[0]=0; v.uv[1]=0;
            if (mesh->HasTextureCoords(0)) { v.uv[0]=mesh->mTextureCoords[0][i].x; v.uv[1]=mesh->mTextureCoords[0][i].y; }
            v.t[0]=1; v.t[1]=0; v.t[2]=0; v.t[3]=1;
            if (mesh->HasTangentsAndBitangents()) { v.t[0]=mesh->mTangents[i].x; v.t[1]=mesh->mTangents[i].y; v.t[2]=mesh->mTangents[i].z; }
            v.c[0]=v.c[1]=v.c[2]=v.c[3]=1.0f;
            for (int k = 0; k < 3; ++k) {
                rec.boundsMin[k] = std::min(rec.boundsMin[k], v.p[k]);
                rec.boundsMax[k] = std::max(rec.boundsMax[k], v.p[k]);
            }
            allVerts.push_back(v);
        }
        for (unsigned f = 0; f < mesh->mNumFaces; ++f)
            for (unsigned k = 0; k < mesh->mFaces[f].mNumIndices; ++k)
                allIndices.push_back(mesh->mFaces[f].mIndices[k]);
        rec.indexCount = (uint32_t)allIndices.size() - rec.indexOffset;

        CookMaterial cm = defaultCookMaterial();
        if (mesh->mMaterialIndex < scene->mNumMaterials) {
            const aiMaterial* am = scene->mMaterials[mesh->mMaterialIndex];
            aiColor4D diffuse;
            if (am->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS) {
                cm.baseColorFactor[0] = diffuse.r; cm.baseColorFactor[1] = diffuse.g;
                cm.baseColorFactor[2] = diffuse.b; cm.baseColorFactor[3] = diffuse.a;
            }
            std::snprintf(cm.name, sizeof(cm.name), "%s", "assimp");
        }
        mats.push_back(cm);
        recs.push_back(rec);
    }
    return writeCooked(out, allVerts, allIndices, mats, {}, recs, "default");
}
#endif

#if defined(VLCRLIB_HAS_KTX) && VLCRLIB_HAS_KTX
static void downsampleRgba(const std::vector<unsigned char>& src, int sw, int sh,
                           std::vector<unsigned char>& dst, int& dw, int& dh) {
    dw = sw > 1 ? sw / 2 : 1;
    dh = sh > 1 ? sh / 2 : 1;
    dst.assign((size_t)dw * dh * 4, 0);
    for (int y = 0; y < dh; ++y) {
        for (int x = 0; x < dw; ++x) {
            for (int c = 0; c < 4; ++c) {
                int sum = 0, n = 0;
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        int sx = std::min(sw - 1, x * 2 + dx);
                        int sy = std::min(sh - 1, y * 2 + dy);
                        sum += src[((size_t)sy * sw + sx) * 4 + c];
                        n++;
                    }
                }
                dst[((size_t)y * dw + x) * 4 + c] = (unsigned char)(sum / n);
            }
        }
    }
}

static bool cookGltfKtx(const std::string& path, const std::string& outDir, bool bc5) {
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;
    if (!(extOf(path) == ".glb"
            ? loader.LoadBinaryFromFile(&model, &err, &warn, path)
            : loader.LoadASCIIFromFile(&model, &err, &warn, path)))
        return false;
    if (model.images.empty()) return false;
    std::filesystem::create_directories(outDir);

    for (size_t i = 0; i < model.images.size(); ++i) {
        const auto& img = model.images[i];
        if (img.width <= 0 || img.height <= 0 || img.image.empty()) continue;
        int comp = img.component > 0 ? img.component : 4;
        std::vector<unsigned char> rgba((size_t)img.width * img.height * 4, 255);
        for (size_t p = 0; p < (size_t)img.width * img.height; ++p) {
            const unsigned char* s = img.image.data() + p * (size_t)comp;
            rgba[p*4+0] = s[0];
            rgba[p*4+1] = comp > 1 ? s[1] : 0;
            rgba[p*4+2] = comp > 2 ? s[2] : 0;
            rgba[p*4+3] = comp > 3 ? s[3] : 255;
        }

        std::vector<std::vector<unsigned char>> mips;
        mips.push_back(rgba);
        int mw = img.width, mh = img.height;
        while (mw > 1 || mh > 1) {
            std::vector<unsigned char> next;
            int nw = 1, nh = 1;
            downsampleRgba(mips.back(), mw, mh, next, nw, nh);
            mips.push_back(std::move(next));
            mw = nw; mh = nh;
        }

        ktxTextureCreateInfo ci = {};
        ci.glInternalformat = bc5 ? 0x8058u /* GL_RGBA8 */ : 0x8C43u /* GL_SRGB8_ALPHA8 */;
        ci.vkFormat = bc5 ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB;
        ci.baseWidth = (uint32_t)img.width;
        ci.baseHeight = (uint32_t)img.height;
        ci.baseDepth = 1;
        ci.numDimensions = 2;
        ci.numLevels = (uint32_t)mips.size();
        ci.numLayers = 1;
        ci.numFaces = 1;
        ktxTexture2* tex = nullptr;
        KTX_error_code rc = ktxTexture2_Create(&ci, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &tex);
        if (rc != KTX_SUCCESS || !tex) {
            fprintf(stderr, "vlcrc: ktx create failed: %s\n", ktxErrorString(rc));
            continue;
        }
        for (size_t level = 0; level < mips.size(); ++level) {
            ktx_size_t offset = 0;
            rc = ktxTexture_GetImageOffset((ktxTexture*)tex, (ktx_uint32_t)level, 0, 0, &offset);
            if (rc != KTX_SUCCESS) break;
            std::memcpy(tex->pData + offset, mips[level].data(), mips[level].size());
        }
        if (rc != KTX_SUCCESS) {
            ktxTexture_Destroy((ktxTexture*)tex);
            continue;
        }
        rc = ktxTexture2_CompressBasis(tex, 160);
        if (rc == KTX_SUCCESS)
            rc = ktxTexture2_TranscodeBasis(tex, bc5 ? KTX_TTF_BC5_RG : KTX_TTF_BC7_RGBA, 0);
        if (rc != KTX_SUCCESS) {
            fprintf(stderr, "vlcrc: ktx compress/transcode failed: %s\n", ktxErrorString(rc));
            ktxTexture_Destroy((ktxTexture*)tex);
            continue;
        }
        std::string out = (std::filesystem::path(outDir) /
                           ("image_" + std::to_string(i) + ".ktx2")).string();
        rc = ktxTexture_WriteToNamedFile((ktxTexture*)tex, out.c_str());
        if (rc != KTX_SUCCESS)
            fprintf(stderr, "vlcrc: ktx write failed: %s\n", ktxErrorString(rc));
        else
            printf("ktx2: %s\n", out.c_str());
        ktxTexture_Destroy((ktxTexture*)tex);
    }
    return true;
}
#endif

#if defined(VLCRLIB_HAS_KTX) && VLCRLIB_HAS_KTX
static bool dumpKtxPng(const std::string& in, const std::string& outPng) {
    ktxTexture2* tex = nullptr;
    KTX_error_code rc = ktxTexture2_CreateFromNamedFile(
        in.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &tex);
    if (rc != KTX_SUCCESS || !tex) {
        fprintf(stderr, "vlcrc: ktx open failed: %s\n", ktxErrorString(rc));
        return false;
    }
    rc = ktxTexture2_TranscodeBasis(tex, KTX_TTF_RGBA32, 0);
    if (rc != KTX_SUCCESS) {
        fprintf(stderr, "vlcrc: ktx transcode failed: %s\n", ktxErrorString(rc));
        ktxTexture_Destroy((ktxTexture*)tex);
        return false;
    }
    ktx_size_t offset = 0;
    ktxTexture_GetImageOffset((ktxTexture*)tex, 0, 0, 0, &offset);
    unsigned char* rgba = tex->pData + offset;
    bool ok = stbi_write_png(outPng.c_str(), (int)tex->baseWidth,
                             (int)tex->baseHeight, 4, rgba,
                             (int)tex->baseWidth * 4) != 0;
    ktxTexture_Destroy((ktxTexture*)tex);
    return ok;
}
#endif

static bool writeManifest(const Manifest& m, const std::string& out) {
    std::ofstream f(out);
    if (!f) return false;
    f << "{\n";
    f << "  \"source\": \"" << jsonEscape(m.source) << "\",\n";
    f << "  \"format\": \"" << jsonEscape(m.format) << "\",\n";
    f << "  \"meshes\": " << m.meshes << ",\n";
    f << "  \"materials\": " << m.materials << ",\n";
    f << "  \"images\": " << m.images << ",\n";
    f << "  \"skins\": " << m.skins << ",\n";
    f << "  \"bones\": " << m.bones << ",\n";
    f << "  \"animations\": " << m.animations << ",\n";
    f << "  \"vertices\": " << m.vertices << ",\n";
    f << "  \"triangles\": " << m.triangles << ",\n";
    if (m.hasBounds()) {
        f << "  \"bounds\": { \"min\": [" << m.boundsMin[0] << ", " << m.boundsMin[1]
          << ", " << m.boundsMin[2] << "], \"max\": [" << m.boundsMax[0]
          << ", " << m.boundsMax[1] << ", " << m.boundsMax[2] << "] },\n";
    }
    f << "  \"warnings\": [";
    for (size_t i = 0; i < m.warnings.size(); ++i)
        f << (i ? ", " : "") << "\"" << jsonEscape(m.warnings[i]) << "\"";
    f << "]\n}\n";
    return true;
}

int main(int argc, char** argv) {
    std::string input, output, meshOutput, ktxDir, dumpIn, dumpOut;
    bool ktxBc5 = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--input") && i + 1 < argc) input = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) output = argv[++i];
        else if (!strcmp(argv[i], "--mesh") && i + 1 < argc) meshOutput = argv[++i];
        else if (!strcmp(argv[i], "--ktx") && i + 1 < argc) ktxDir = argv[++i];
        else if (!strcmp(argv[i], "--ktx-bc5")) ktxBc5 = true;
        else if (!strcmp(argv[i], "--dump-ktx") && i + 2 < argc) {
            dumpIn = argv[++i];
            dumpOut = argv[++i];
        }
        else if (!strcmp(argv[i], "--help")) {
            printf("vlcrc --input <model> [--out <manifest.json>] [--mesh <out.vmesh>]\n"
                   "      [--ktx <texture-dir>] [--ktx-bc5]\n"
                   "      --dump-ktx <in.ktx2> <out.png>\n");
            return 0;
        }
    }
    if (!dumpIn.empty()) {
#if defined(VLCRLIB_HAS_KTX) && VLCRLIB_HAS_KTX
        return dumpKtxPng(dumpIn, dumpOut) ? 0 : 1;
#else
        fprintf(stderr, "vlcrc: KTX support not compiled in\n");
        return 1;
#endif
    }
    if (input.empty()) {
        fprintf(stderr, "vlcrc: --input required\n");
        return 2;
    }
    if (output.empty()) output = input + ".vmanifest.json";

    Manifest m;
    m.source = input;
    std::string ext = extOf(input);
    bool ok = false;
    if (ext == ".glb" || ext == ".gltf") {
        ok = loadGltf(input, m);
    } else {
#if defined(VLCRLIB_HAS_ASSIMP) && VLCRLIB_HAS_ASSIMP
        ok = loadAssimp(input, m);
#else
        m.error = "Assimp support not compiled in";
#endif
    }
    if (!ok) {
        fprintf(stderr, "vlcrc: %s\n", m.error.c_str());
        return 1;
    }
    if (!writeManifest(m, output)) {
        fprintf(stderr, "vlcrc: cannot write %s\n", output.c_str());
        return 1;
    }

    printf("cooked: %s\n", output.c_str());
    printf("  meshes=%d verts=%lld tris=%lld materials=%d bones=%d anims=%d\n",
           m.meshes, m.vertices, m.triangles, m.materials, m.bones, m.animations);

    if (!meshOutput.empty()) {
        bool cooked = false;
        if (ext == ".glb" || ext == ".gltf")
            cooked = cookGltfMesh(input, meshOutput, ktxDir, ktxBc5);
        else {
#if defined(VLCRLIB_HAS_ASSIMP) && VLCRLIB_HAS_ASSIMP
            cooked = cookAssimpMesh(input, meshOutput);
#endif
        }
        if (!cooked) {
            fprintf(stderr, "vlcrc: mesh cooking failed\n");
            return 1;
        }
        printf("mesh: %s\n", meshOutput.c_str());
    }

    if (!ktxDir.empty()) {
#if defined(VLCRLIB_HAS_KTX) && VLCRLIB_HAS_KTX
        if (ext != ".glb" && ext != ".gltf") {
            fprintf(stderr, "vlcrc: --ktx currently supports glTF/GLB images only\n");
            return 1;
        }
        if (!cookGltfKtx(input, ktxDir, ktxBc5)) {
            fprintf(stderr, "vlcrc: KTX2 cooking failed\n");
            return 1;
        }
#else
        fprintf(stderr, "vlcrc: KTX support not compiled in\n");
        return 1;
#endif
    }
    for (const auto& w : m.warnings)
        printf("  warning: %s\n", w.c_str());
    return 0;
}
