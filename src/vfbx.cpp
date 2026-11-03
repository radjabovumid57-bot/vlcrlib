/*
 * vfbx.cpp — optional FBX loader.
 *
 * Uses Assimp when available (VLCRLIB_HAS_ASSIMP=1). Without Assimp the
 * functions compile to safe stubs, so the engine has no hard FBX dependency.
 */
#include "vloaders.h"
#include "vtextures.h"

#if VLCRLIB_HAS_ASSIMP
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

static Matrix fromAiMatrix(const aiMatrix4x4& a) {
    Matrix m;
    m.m0 = a.a1; m.m1 = a.b1; m.m2 = a.c1; m.m3 = a.d1;
    m.m4 = a.a2; m.m5 = a.b2; m.m6 = a.c2; m.m7 = a.d2;
    m.m8 = a.a3; m.m9 = a.b3; m.m10 = a.c3; m.m11 = a.d3;
    m.m12 = a.a4; m.m13 = a.b4; m.m14 = a.c4; m.m15 = a.d4;
    return m;
}

static Texture2D loadAssimpTexture(const aiScene* scene, const char* path,
                                   const std::string& baseDir, bool srgb) {
    if (!path || !path[0]) return Texture2D{};

    Image image = {0};
    bool fromEmbedded = false;
    if (const aiTexture* embedded = scene->GetEmbeddedTexture(path)) {
        fromEmbedded = true;
        int w = 0, h = 0, comp = 0;
        stbi_uc* px = stbi_load_from_memory(
            reinterpret_cast<const stbi_uc*>(embedded->pcData),
            embedded->mHeight == 0 ? (int)embedded->mWidth
                                   : (int)(embedded->mWidth * embedded->mHeight * 4),
            &w, &h, &comp, 4);
        if (!px) return Texture2D{};
        image.data = px;
        image.width = w;
        image.height = h;
    } else {
        std::string full = path;
        if (full.size() > 1 && full[1] != ':' && full[0] != '/')
            full = baseDir + "/" + full;
        image = LoadImage(full.c_str());
        if (!image.data) return Texture2D{};
    }

    TextureOptions opts = {0};
    opts.srgb = srgb ? 1 : 0;
    opts.mipmaps = 1;
    Texture2D result = LoadTextureFromImageEx(image, &opts);
    (void)fromEmbedded;
    stbi_image_free(image.data);
    return result;
}

static Material makeAssimpMaterial(const aiScene* scene, unsigned index,
                                   const std::string& baseDir,
                                   std::vector<Texture2D>& owned) {
    Material mat = LoadMaterialDefault();
    if (index >= scene->mNumMaterials) return mat;
    const aiMaterial* am = scene->mMaterials[index];

    aiColor4D base;
    if (am->Get(AI_MATKEY_COLOR_DIFFUSE, base) == AI_SUCCESS) {
        mat.baseColorFactor[0] = base.r;
        mat.baseColorFactor[1] = base.g;
        mat.baseColorFactor[2] = base.b;
        mat.baseColorFactor[3] = base.a;
        mat.baseColor = Color{
            (unsigned char)(base.r * 255.0f + 0.5f),
            (unsigned char)(base.g * 255.0f + 0.5f),
            (unsigned char)(base.b * 255.0f + 0.5f),
            (unsigned char)(base.a * 255.0f + 0.5f)};
    }
    float metallic = 0.0f, roughness = 1.0f;
    am->Get(AI_MATKEY_METALLIC_FACTOR, metallic);
    am->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness);
    mat.metallic = metallic;
    mat.roughness = roughness;

    aiString tex;
    auto add = [&](aiTextureType type, int map, bool srgb) {
        aiString p;
        if (am->GetTexture(type, 0, &p) == AI_SUCCESS) {
            Texture2D t = loadAssimpTexture(scene, p.C_Str(), baseDir, srgb);
            if (t.id) { SetMaterialTexture(&mat, map, t); owned.push_back(t); }
        }
    };
    add(aiTextureType_BASE_COLOR, MATERIAL_MAP_ALBEDO, true);
    add(aiTextureType_DIFFUSE, MATERIAL_MAP_ALBEDO, true);
    add(aiTextureType_NORMALS, MATERIAL_MAP_NORMAL, false);
    add(aiTextureType_METALNESS, MATERIAL_MAP_METALLICROUGHNESS, false);
    add(aiTextureType_EMISSIVE, MATERIAL_MAP_EMISSION, true);

    int twoSided = 0;
    if (am->Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS && twoSided)
        mat.doubleSided = 1;
    return mat;
}
#endif

Model LoadOBJ(const char *path) {
#if VLCRLIB_HAS_ASSIMP
    // Assimp's OBJ importer handles .mtl, groups and textures with the same
    // vertex/material pipeline as FBX.
    return LoadFBX(path);
#else
    return LoadModel(path);
#endif
}

bool vloaders_HasFBX(void) {
#if VLCRLIB_HAS_ASSIMP
    return true;
#else
    return false;
#endif
}

SkinnedModel LoadFBXSkinned(const char *path) {
#if VLCRLIB_HAS_ASSIMP
    SkinnedModel empty = {0};
    if (!path || !path[0]) return empty;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate | aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace | aiProcess_JoinIdenticalVertices);
    if (!scene || !scene->mRootNode) return empty;

    // Prefer the bone-bound mesh whose height is closest to a human-scale
    // character. FBX files such as Walking.fbx contain the same character
    // twice at 1x and 100x; picking the first mesh can select the giant copy.
    const aiMesh* am = nullptr;
    float bestScore = 1e30f;
    for (unsigned m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* candidate = scene->mMeshes[m];
        if (!candidate || candidate->mNumBones == 0 || candidate->mNumVertices == 0)
            continue;
        float mn = 1e30f, mx = -1e30f;
        for (unsigned v = 0; v < candidate->mNumVertices; ++v) {
            mn = std::min(mn, candidate->mVertices[v].y);
            mx = std::max(mx, candidate->mVertices[v].y);
        }
        float h = mx - mn;
        if (h < 0.05f) continue;
        float score = std::fabs(h - 2.0f);
        if (score < bestScore) { bestScore = score; am = candidate; }
    }
    if (!am) {
        for (unsigned m = 0; m < scene->mNumMeshes; ++m) {
            const aiMesh* candidate = scene->mMeshes[m];
            if (candidate && candidate->mNumBones > 0 &&
                candidate->mNumBones <= 256) { am = candidate; break; }
        }
    }
    if (!am || am->mNumBones > 256) return empty;

    std::vector<const aiBone*> bones;
    std::unordered_map<std::string, int> boneByName;
    for (unsigned b = 0; b < am->mNumBones; ++b) {
        bones.push_back(am->mBones[b]);
        boneByName[am->mBones[b]->mName.C_Str()] = (int)b;
    }

    std::vector<int> parents(bones.size(), -1);
    std::vector<Matrix> inverseBind(bones.size(), MatrixIdentity());
    std::vector<Vector3> restT(bones.size(), Vector3{0,0,0});
    std::vector<Quaternion> restR(bones.size(), Quaternion{0,0,0,1});
    std::vector<Vector3> restS(bones.size(), Vector3{1,1,1});
    std::vector<const char*> boneNames(bones.size(), nullptr);
    for (size_t b = 0; b < bones.size(); ++b)
        boneNames[b] = bones[b]->mName.C_Str();

    std::function<const aiNode*(const aiNode*, const std::string&)> findNode =
        [&](const aiNode* n, const std::string& name) -> const aiNode* {
            if (!n) return nullptr;
            if (name == n->mName.C_Str()) return n;
            for (unsigned c = 0; c < n->mNumChildren; ++c) {
                if (const aiNode* r = findNode(n->mChildren[c], name)) return r;
            }
            return nullptr;
        };

    for (size_t b = 0; b < bones.size(); ++b) {
        inverseBind[b] = fromAiMatrix(bones[b]->mOffsetMatrix);
        if (const aiNode* node = findNode(scene->mRootNode,
                                          bones[b]->mName.C_Str())) {
            if (const aiNode* p = node->mParent) {
                auto it = boneByName.find(p->mName.C_Str());
                if (it != boneByName.end()) parents[b] = it->second;
            }
            aiVector3D s, t;
            aiQuaternion r;
            node->mTransformation.Decompose(s, r, t);
            restT[b] = Vector3{t.x, t.y, t.z};
            restR[b] = Quaternion{r.x, r.y, r.z, r.w};
            restS[b] = Vector3{s.x, s.y, s.z};
        }
    }

    std::vector<SkinnedVertex3D> verts(am->mNumVertices);
    for (unsigned i = 0; i < am->mNumVertices; ++i) {
        SkinnedVertex3D& v = verts[i];
        v.x = am->mVertices[i].x;
        v.y = am->mVertices[i].y;
        v.z = am->mVertices[i].z;
        v.ny = 1.0f;
        if (am->HasNormals()) {
            v.nx = am->mNormals[i].x;
            v.ny = am->mNormals[i].y;
            v.nz = am->mNormals[i].z;
        }
        if (am->HasTextureCoords(0)) {
            v.u = am->mTextureCoords[0][i].x;
            v.v = am->mTextureCoords[0][i].y;
        }
        if (am->HasTangentsAndBitangents()) {
            v.tx = am->mTangents[i].x;
            v.ty = am->mTangents[i].y;
            v.tz = am->mTangents[i].z;
            v.tw = 1.0f;
        }
    }

    for (size_t b = 0; b < bones.size(); ++b) {
        for (unsigned w = 0; w < bones[b]->mNumWeights; ++w) {
            const aiVertexWeight& vw = bones[b]->mWeights[w];
            if (vw.mVertexId >= am->mNumVertices) continue;
            SkinnedVertex3D& v = verts[vw.mVertexId];
            int slot = -1;
            for (int k = 0; k < 4; ++k) if (v.weights[k] == 0.0f) { slot = k; break; }
            if (slot < 0) continue;
            v.joints[slot] = (unsigned char)b;
            v.weights[slot] = vw.mWeight;
        }
    }
    for (auto& v : verts) {
        float sum = v.weights[0] + v.weights[1] + v.weights[2] + v.weights[3];
        if (sum > 1e-6f) for (int k = 0; k < 4; ++k) v.weights[k] /= sum;
        else { v.joints[0] = 0; v.weights[0] = 1.0f; }
    }

    std::vector<uint32_t> indices;
    for (unsigned f = 0; f < am->mNumFaces; ++f) {
        const aiFace& face = am->mFaces[f];
        for (unsigned k = 0; k < face.mNumIndices; ++k)
            indices.push_back(face.mIndices[k]);
    }
    if (indices.empty()) return empty;

    std::string baseDir = std::string(path);
    size_t slash = baseDir.find_last_of("/\\");
    baseDir = slash == std::string::npos ? "." : baseDir.substr(0, slash);

    std::vector<Texture2D> owned;
    Material mat = makeAssimpMaterial(scene, am->mMaterialIndex, baseDir, owned);
    SkinnedModel result = vgraphics_CreateSkinnedModel(
        verts.data(), (int)verts.size(), indices.data(), (int)indices.size(),
        &mat, inverseBind.data(), parents.data(),
        restT.data(), restR.data(), restS.data(), (int)bones.size(),
        boneNames.data());
    if (!result.id) return empty;

    for (unsigned a = 0; a < scene->mNumAnimations; ++a) {
        const aiAnimation* anim = scene->mAnimations[a];
        const double tps = anim->mTicksPerSecond > 0.0 ? anim->mTicksPerSecond : 1.0;
        std::vector<std::vector<AnimVec3Key>> pos(bones.size());
        std::vector<std::vector<AnimQuatKey>> rot(bones.size());
        std::vector<std::vector<AnimVec3Key>> scl(bones.size());

        for (unsigned c = 0; c < anim->mNumChannels; ++c) {
            const aiNodeAnim* ch = anim->mChannels[c];
            auto it = boneByName.find(ch->mNodeName.C_Str());
            if (it == boneByName.end()) continue;
            int b = it->second;
            for (unsigned k = 0; k < ch->mNumPositionKeys; ++k) {
                const auto& key = ch->mPositionKeys[k];
                pos[b].push_back({(float)(key.mTime / tps),
                                  Vector3{key.mValue.x, key.mValue.y, key.mValue.z}});
            }
            for (unsigned k = 0; k < ch->mNumRotationKeys; ++k) {
                const auto& key = ch->mRotationKeys[k];
                rot[b].push_back({(float)(key.mTime / tps),
                                  Quaternion{key.mValue.x, key.mValue.y,
                                             key.mValue.z, key.mValue.w}});
            }
            for (unsigned k = 0; k < ch->mNumScalingKeys; ++k) {
                const auto& key = ch->mScalingKeys[k];
                scl[b].push_back({(float)(key.mTime / tps),
                                  Vector3{key.mValue.x, key.mValue.y, key.mValue.z}});
            }
        }

        std::vector<AnimTrackData> tracks(bones.size());
        for (size_t b = 0; b < bones.size(); ++b) {
            tracks[b].pos = pos[b].empty() ? nullptr : pos[b].data();
            tracks[b].posCount = (int)pos[b].size();
            tracks[b].rot = rot[b].empty() ? nullptr : rot[b].data();
            tracks[b].rotCount = (int)rot[b].size();
            tracks[b].scale = scl[b].empty() ? nullptr : scl[b].data();
            tracks[b].scaleCount = (int)scl[b].size();
        }
        vgraphics_AddSkinnedAnimation(result,
            anim->mName.length ? anim->mName.C_Str() : "clip",
            (float)(anim->mDuration > 0 ? anim->mDuration / tps : 1.0),
            tracks.data(), (int)bones.size());
    }

    (void)owned;
    return result;
#else
    (void)path;
    SkinnedModel empty = {0};
    return empty;
#endif
}

Model LoadFBX(const char *path) {
#if VLCRLIB_HAS_ASSIMP
    Model empty = {0};
    if (!path || !path[0]) return empty;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_JoinIdenticalVertices |
        aiProcess_PreTransformVertices);
    if (!scene || !scene->mRootNode) return empty;

    std::string baseDir = std::string(path);
    size_t slash = baseDir.find_last_of("/\\");
    baseDir = slash == std::string::npos ? "." : baseDir.substr(0, slash);

    std::vector<Mesh> meshes;
    std::vector<int> meshMats;
    std::vector<Matrix> transforms;
    std::vector<Material> materials;
    std::vector<Texture2D> owned;
    std::unordered_map<int, int> materialMap;

    /* Some FBX files contain the same character twice at wildly different
       unit scales (Walking.fbx has one 1.6 m mesh and one 180 m copy).
       Keep the smaller coherent set and drop obvious scale outliers. */
    float minHeight = 1e30f;
    for (unsigned m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* am = scene->mMeshes[m];
        if (!am->HasPositions() || am->mNumVertices == 0) continue;
        aiVector3D mn(1e30f), mx(-1e30f);
        for (unsigned i = 0; i < am->mNumVertices; ++i) {
            mn.x = std::min(mn.x, am->mVertices[i].x);
            mn.y = std::min(mn.y, am->mVertices[i].y);
            mn.z = std::min(mn.z, am->mVertices[i].z);
            mx.x = std::max(mx.x, am->mVertices[i].x);
            mx.y = std::max(mx.y, am->mVertices[i].y);
            mx.z = std::max(mx.z, am->mVertices[i].z);
        }
        float h = mx.y - mn.y;
        if (h > 1e-4f && h < minHeight) minHeight = h;
    }

    for (unsigned m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* am = scene->mMeshes[m];
        if (!am->HasPositions() || am->mNumVertices == 0) continue;

        aiVector3D mn(1e30f), mx(-1e30f);
        for (unsigned i = 0; i < am->mNumVertices; ++i) {
            mn.x = std::min(mn.x, am->mVertices[i].x);
            mn.y = std::min(mn.y, am->mVertices[i].y);
            mn.z = std::min(mn.z, am->mVertices[i].z);
            mx.x = std::max(mx.x, am->mVertices[i].x);
            mx.y = std::max(mx.y, am->mVertices[i].y);
            mx.z = std::max(mx.z, am->mVertices[i].z);
        }
        float meshScale = 1.0f;
        if (minHeight < 1e29f && (mx.y - mn.y) > minHeight * 8.0f)
            meshScale = minHeight / std::max(mx.y - mn.y, 1e-6f);

        std::vector<Vertex3D> verts(am->mNumVertices);
        for (unsigned i = 0; i < am->mNumVertices; ++i) {
            Vertex3D& v = verts[i];
            v.x = am->mVertices[i].x;
            v.y = am->mVertices[i].y;
            v.z = am->mVertices[i].z;
            v.ny = 1.0f;
            v.cr = v.cg = v.cb = v.ca = 1.0f;
            if (meshScale != 1.0f) {
                v.x *= meshScale;
                v.y *= meshScale;
                v.z *= meshScale;
            }
            if (am->HasNormals()) {
                v.nx = am->mNormals[i].x;
                v.ny = am->mNormals[i].y;
                v.nz = am->mNormals[i].z;
            }
            if (am->HasTextureCoords(0)) {
                v.u = am->mTextureCoords[0][i].x;
                v.v = am->mTextureCoords[0][i].y;
            }
            if (am->HasTangentsAndBitangents()) {
                v.tx = am->mTangents[i].x;
                v.ty = am->mTangents[i].y;
                v.tz = am->mTangents[i].z;
                v.tw = 1.0f;
            }
            if (am->HasVertexColors(0)) {
                v.cr = am->mColors[0][i].r;
                v.cg = am->mColors[0][i].g;
                v.cb = am->mColors[0][i].b;
                v.ca = am->mColors[0][i].a;
            }
        }

        std::vector<uint32_t> indices;
        for (unsigned f = 0; f < am->mNumFaces; ++f) {
            const aiFace& face = am->mFaces[f];
            for (unsigned k = 0; k < face.mNumIndices; ++k)
                indices.push_back(face.mIndices[k]);
        }
        if (indices.empty()) continue;

        Mesh gpu = vgraphics_RegisterMesh(verts.data(), (int)verts.size(),
                                          indices.data(), (int)indices.size());
        if (!gpu.id) continue;

        int slot = 0;
        if (am->mMaterialIndex < scene->mNumMaterials) {
            auto it = materialMap.find((int)am->mMaterialIndex);
            if (it != materialMap.end()) slot = it->second;
            else {
                slot = (int)materials.size();
                materialMap[(int)am->mMaterialIndex] = slot;
                materials.push_back(makeAssimpMaterial(scene, am->mMaterialIndex,
                                                       baseDir, owned));
            }
        }
        meshes.push_back(gpu);
        meshMats.push_back(slot);
        transforms.push_back(MatrixIdentity());
    }

    if (meshes.empty()) return empty;
    Model result = vgraphics_CreateModelFromMeshesEx(
        meshes.data(), meshMats.data(), transforms.data(), (int)meshes.size(),
        materials.data(), (int)materials.size());
    for (Texture2D t : owned)
        if (!vgraphics_TrackModelTexture(result, t)) UnloadTexture(t);
    return result;
#else
    (void)path;
    Model empty = {0};
    return empty;
#endif
}
