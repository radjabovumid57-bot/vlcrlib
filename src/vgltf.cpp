/*
 * vgltf.cpp — glTF/GLB loading through tinygltf.
 *
 * tinygltf handles the container/JSON/accessors; images are decoded by the
 * engine's stb_image path through a custom image loader.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOGDI
#define NOUSER
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE

#include "vgltf.h"
#include "vtextures.h"
#include "vfiles.h"
#include "tiny_gltf.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

static int componentSize(int type) {
    switch (type) {
        case TINYGLTF_COMPONENT_TYPE_BYTE:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:  return 1;
        case TINYGLTF_COMPONENT_TYPE_SHORT:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: return 2;
        case TINYGLTF_COMPONENT_TYPE_INT:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        case TINYGLTF_COMPONENT_TYPE_FLOAT:          return 4;
        default: return 0;
    }
}

static int typeComponents(int type) {
    switch (type) {
        case TINYGLTF_TYPE_SCALAR: return 1;
        case TINYGLTF_TYPE_VEC2:   return 2;
        case TINYGLTF_TYPE_VEC3:   return 3;
        case TINYGLTF_TYPE_VEC4:   return 4;
        case TINYGLTF_TYPE_MAT2:   return 4;
        case TINYGLTF_TYPE_MAT3:   return 9;
        case TINYGLTF_TYPE_MAT4:   return 16;
        default: return 0;
    }
}

static float readComponent(const tinygltf::Accessor& a, const unsigned char* p, int c) {
    switch (a.componentType) {
        case TINYGLTF_COMPONENT_TYPE_FLOAT: {
            float v; std::memcpy(&v, p + c * 4, 4); return v;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            return a.normalized ? p[c] / 255.0f : (float)p[c];
        case TINYGLTF_COMPONENT_TYPE_BYTE:
            return a.normalized ? (float)(int8_t)p[c] / 127.0f : (float)(int8_t)p[c];
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            uint16_t v; std::memcpy(&v, p + c * 2, 2);
            return a.normalized ? v / 65535.0f : (float)v;
        }
        case TINYGLTF_COMPONENT_TYPE_SHORT: {
            int16_t v; std::memcpy(&v, p + c * 2, 2);
            return a.normalized ? (float)v / 32767.0f : (float)v;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
            uint32_t v; std::memcpy(&v, p + c * 4, 4); return (float)v;
        }
        default: return 0.0f;
    }
}

static bool accessorFloats(const tinygltf::Model& model, int index,
                           std::vector<float>& out, int wanted) {
    if (index < 0 || index >= (int)model.accessors.size()) return false;
    const auto& a = model.accessors[index];
    const int actual = typeComponents(a.type);
    const int csize = componentSize(a.componentType);
    if (actual < wanted || csize == 0 || a.bufferView < 0) return false;
    const auto& bv = model.bufferViews[a.bufferView];
    if (bv.buffer < 0) return false;
    const auto& buf = model.buffers[bv.buffer];
    const size_t stride = bv.byteStride ? bv.byteStride
                                        : (size_t)actual * csize;
    const size_t base = (size_t)bv.byteOffset + a.byteOffset;
    if (base > buf.data.size()) return false;
    out.resize((size_t)a.count * wanted);
    for (size_t i = 0; i < a.count; ++i) {
        const size_t off = base + i * stride;
        if (off + (size_t)actual * csize > buf.data.size()) return false;
        const unsigned char* p = buf.data.data() + off;
        for (int c = 0; c < wanted; ++c)
            out[i * wanted + c] = readComponent(a, p, c);
    }
    return true;
}

static bool accessorIndices(const tinygltf::Model& model, int index,
                            std::vector<uint32_t>& out) {
    if (index < 0 || index >= (int)model.accessors.size()) return false;
    const auto& a = model.accessors[index];
    const int csize = componentSize(a.componentType);
    if (a.bufferView < 0 || csize == 0) return false;
    const auto& bv = model.bufferViews[a.bufferView];
    const auto& buf = model.buffers[bv.buffer];
    const size_t stride = bv.byteStride ? bv.byteStride : (size_t)csize;
    const size_t base = (size_t)bv.byteOffset + a.byteOffset;
    out.resize(a.count);
    for (size_t i = 0; i < a.count; ++i) {
        const size_t off = base + i * stride;
        if (off + csize > buf.data.size()) return false;
        const unsigned char* p = buf.data.data() + off;
        switch (a.componentType) {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                out[i] = p[0]; break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                uint16_t v; std::memcpy(&v, p, 2); out[i] = v; break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
                uint32_t v; std::memcpy(&v, p, 4); out[i] = v; break;
            }
            default: return false;
        }
    }
    return true;
}

static bool tinyImageLoader(tinygltf::Image* image, const int image_idx,
                            std::string* err, std::string* warn,
                            int req_width, int req_height,
                            const unsigned char* bytes, int size, void* user) {
    (void)image_idx; (void)warn; (void)req_width; (void)req_height; (void)user;
    Image decoded = LoadImageFromMemory(nullptr, bytes, size);
    if (!decoded.data) {
        if (err) *err = "stb_image failed";
        return false;
    }
    image->width = decoded.width;
    image->height = decoded.height;
    image->component = 4;
    image->bits = 8;
    image->pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
    image->image.assign((unsigned char*)decoded.data,
                        (unsigned char*)decoded.data + (size_t)decoded.width * decoded.height * 4);
    UnloadImage(decoded);
    return true;
}

static int engineMinFilter(int f) {
    switch (f) {
        case TINYGLTF_TEXTURE_FILTER_NEAREST: return 0;
        case TINYGLTF_TEXTURE_FILTER_LINEAR: return 1;
        case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST: return 2;
        case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST: return 3;
        case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR: return 4;
        case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR: return 5;
        default: return 5;
    }
}
static int engineMagFilter(int f) {
    return f == TINYGLTF_TEXTURE_FILTER_NEAREST ? 0 : 1;
}
static int engineWrap(int w) {
    if (w == TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE) return 1;
    if (w == TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT) return 2;
    return 0;
}

static Texture2D loadTexture(const tinygltf::Model& model, int textureIndex,
                             bool srgb) {
    if (textureIndex < 0 || textureIndex >= (int)model.textures.size())
        return Texture2D{};
    const auto& tex = model.textures[textureIndex];
    if (tex.source < 0 || tex.source >= (int)model.images.size())
        return Texture2D{};
    const auto& img = model.images[tex.source];
    if (img.image.empty() || img.width <= 0 || img.height <= 0)
        return Texture2D{};

    Image image = {0};
    image.width = img.width;
    image.height = img.height;
    image.data = (void*)img.image.data();

    TextureOptions opts = {0};
    opts.srgb = srgb ? 1 : 0;
    opts.mipmaps = 1;
    opts.minFilter = 5;
    opts.magFilter = 1;
    opts.wrapS = 0;
    opts.wrapT = 0;
    if (tex.sampler >= 0 && tex.sampler < (int)model.samplers.size()) {
        const auto& s = model.samplers[tex.sampler];
        if (s.minFilter >= 0) opts.minFilter = engineMinFilter(s.minFilter);
        if (s.magFilter >= 0) opts.magFilter = engineMagFilter(s.magFilter);
        opts.wrapS = engineWrap(s.wrapS);
        opts.wrapT = engineWrap(s.wrapT);
    }
    return LoadTextureFromImageEx(image, &opts);
}

static void generateTangents(std::vector<Vertex3D>& verts,
                             const std::vector<uint32_t>& indices) {
    const size_t n = verts.size();
    std::vector<float> tanAcc(n * 3, 0.0f), bitAcc(n * 3, 0.0f);
    for (size_t t = 0; t + 2 < indices.size(); t += 3) {
        uint32_t i0 = indices[t], i1 = indices[t + 1], i2 = indices[t + 2];
        if (i0 >= n || i1 >= n || i2 >= n) continue;
        Vertex3D &v0 = verts[i0], &v1 = verts[i1], &v2 = verts[i2];
        float x1 = v1.x - v0.x, y1 = v1.y - v0.y, z1 = v1.z - v0.z;
        float x2 = v2.x - v0.x, y2 = v2.y - v0.y, z2 = v2.z - v0.z;
        float s1 = v1.u - v0.u, t1 = v1.v - v0.v;
        float s2 = v2.u - v0.u, t2 = v2.v - v0.v;
        float r = s1 * t2 - s2 * t1;
        if (std::fabs(r) < 1e-8f) continue;
        r = 1.0f / r;
        float sx = (t2 * x1 - t1 * x2) * r;
        float sy = (t2 * y1 - t1 * y2) * r;
        float sz = (t2 * z1 - t1 * z2) * r;
        float bx = (s1 * x2 - s2 * x1) * r;
        float by = (s1 * y2 - s2 * y1) * r;
        float bz = (s1 * z2 - s2 * z1) * r;
        for (uint32_t ii : {i0, i1, i2}) {
            tanAcc[ii * 3 + 0] += sx; tanAcc[ii * 3 + 1] += sy; tanAcc[ii * 3 + 2] += sz;
            bitAcc[ii * 3 + 0] += bx; bitAcc[ii * 3 + 1] += by; bitAcc[ii * 3 + 2] += bz;
        }
    }
    for (size_t i = 0; i < n; ++i) {
        float nx = verts[i].nx, ny = verts[i].ny, nz = verts[i].nz;
        float tx = tanAcc[i * 3 + 0], ty = tanAcc[i * 3 + 1], tz = tanAcc[i * 3 + 2];
        float d = nx * tx + ny * ty + nz * tz;
        tx -= nx * d; ty -= ny * d; tz -= nz * d;
        float len = std::sqrt(tx * tx + ty * ty + tz * tz);
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

static Matrix nodeLocalMatrix(const tinygltf::Node& node) {
    if (node.matrix.size() == 16) {
        Matrix m;
        std::memcpy(&m, node.matrix.data(), sizeof(Matrix));
        return MatrixTranspose(m);
    }
    const double tx = node.translation.size() > 0 ? node.translation[0] : 0.0;
    const double ty = node.translation.size() > 1 ? node.translation[1] : 0.0;
    const double tz = node.translation.size() > 2 ? node.translation[2] : 0.0;
    const double rx = node.rotation.size() > 0 ? node.rotation[0] : 0.0;
    const double ry = node.rotation.size() > 1 ? node.rotation[1] : 0.0;
    const double rz = node.rotation.size() > 2 ? node.rotation[2] : 0.0;
    const double rw = node.rotation.size() > 3 ? node.rotation[3] : 1.0;
    const double sx = node.scale.size() > 0 ? node.scale[0] : 1.0;
    const double sy = node.scale.size() > 1 ? node.scale[1] : 1.0;
    const double sz = node.scale.size() > 2 ? node.scale[2] : 1.0;

    Matrix T = MatrixTranslate((float)tx, (float)ty, (float)tz);
    Quaternion q = { (float)rx, (float)ry, (float)rz, (float)rw };
    Matrix R = QuaternionToMatrix(q);
    Matrix S = MatrixScale((float)sx, (float)sy, (float)sz);
    return MatrixMultiply(T, MatrixMultiply(R, S));
}

static Material makeMaterial(const tinygltf::Model& model, int index,
                             std::unordered_map<uint64_t, Texture2D>& cache) {
    Material mat = LoadMaterialDefault();
    if (index < 0 || index >= (int)model.materials.size()) return mat;
    const auto& src = model.materials[index];
    const auto& pbr = src.pbrMetallicRoughness;
    for (int i = 0; i < 4; ++i) mat.baseColorFactor[i] = (float)pbr.baseColorFactor[i];
    mat.baseColor = Color{
        (unsigned char)(mat.baseColorFactor[0] * 255.0f + 0.5f),
        (unsigned char)(mat.baseColorFactor[1] * 255.0f + 0.5f),
        (unsigned char)(mat.baseColorFactor[2] * 255.0f + 0.5f),
        (unsigned char)(mat.baseColorFactor[3] * 255.0f + 0.5f)};
    mat.metallic = (float)pbr.metallicFactor;
    mat.roughness = (float)pbr.roughnessFactor;
    mat.emissionColor = Color{
        (unsigned char)(src.emissiveFactor[0] * 255.0f + 0.5f),
        (unsigned char)(src.emissiveFactor[1] * 255.0f + 0.5f),
        (unsigned char)(src.emissiveFactor[2] * 255.0f + 0.5f), 255};
    float emax = (float)std::max({src.emissiveFactor[0], src.emissiveFactor[1], src.emissiveFactor[2]});
    mat.emissionStrength = emax > 0.0f ? 1.0f : 0.0f;

    auto setTex = [&](int textureIndex, int mapType, bool srgb) {
        if (textureIndex < 0) return;
        uint64_t key = ((uint64_t)(uint32_t)textureIndex << 1) | (srgb ? 1u : 0u);
        auto it = cache.find(key);
        Texture2D t;
        if (it == cache.end()) {
            t = loadTexture(model, textureIndex, srgb);
            if (t.id) cache[key] = t;
        } else t = it->second;
        if (t.id) SetMaterialTexture(&mat, mapType, t);
    };
    setTex(pbr.baseColorTexture.index, MATERIAL_MAP_ALBEDO, true);
    setTex(pbr.metallicRoughnessTexture.index, MATERIAL_MAP_METALLICROUGHNESS, false);
    setTex(src.normalTexture.index, MATERIAL_MAP_NORMAL, false);
    setTex(src.occlusionTexture.index, MATERIAL_MAP_AO, false);
    setTex(src.emissiveTexture.index, MATERIAL_MAP_EMISSION, true);
    mat.normalScale = (float)src.normalTexture.scale;
    mat.aoStrength = (float)src.occlusionTexture.strength;
    mat.alphaMode = src.alphaMode == "MASK" ? 1 : (src.alphaMode == "BLEND" ? 2 : 0);
    mat.alphaCutoff = (float)src.alphaCutoff;
    mat.doubleSided = src.doubleSided;
    return mat;
}

static void collectNode(const tinygltf::Model& model, int nodeIndex,
                        Matrix parent,
                        std::vector<Mesh>& meshes,
                        std::vector<int>& meshMats,
                        std::vector<Matrix>& transforms,
                        std::vector<Material>& materials,
                        std::unordered_map<int, int>& materialMap,
                        std::unordered_map<uint64_t, Texture2D>& textureCache) {
    if (nodeIndex < 0 || nodeIndex >= (int)model.nodes.size()) return;
    const auto& node = model.nodes[nodeIndex];
    Matrix world = MatrixMultiply(parent, nodeLocalMatrix(node));

    if (node.mesh >= 0) {
        const auto& mesh = model.meshes[node.mesh];
        for (const auto& prim : mesh.primitives) {
            auto itPos = prim.attributes.find("POSITION");
            if (itPos == prim.attributes.end()) continue;

            std::vector<float> pos;
            if (!accessorFloats(model, itPos->second, pos, 3)) continue;
            const size_t vcount = pos.size() / 3;
            if (!vcount) continue;

            std::vector<Vertex3D> verts(vcount);
            bool haveUV = false, haveTangent = false;
            for (size_t i = 0; i < vcount; ++i) {
                verts[i].x = pos[i * 3 + 0];
                verts[i].y = pos[i * 3 + 1];
                verts[i].z = pos[i * 3 + 2];
                verts[i].ny = 1.0f;
                verts[i].cr = verts[i].cg = verts[i].cb = verts[i].ca = 1.0f;
            }

            if (auto it = prim.attributes.find("NORMAL"); it != prim.attributes.end()) {
                std::vector<float> n;
                if (accessorFloats(model, it->second, n, 3)) {
                    for (size_t i = 0; i < vcount; ++i) {
                        verts[i].nx = n[i * 3 + 0];
                        verts[i].ny = n[i * 3 + 1];
                        verts[i].nz = n[i * 3 + 2];
                    }
                }
            }
            if (auto it = prim.attributes.find("TEXCOORD_0"); it != prim.attributes.end()) {
                std::vector<float> uv;
                if (accessorFloats(model, it->second, uv, 2)) {
                    for (size_t i = 0; i < vcount; ++i) {
                        verts[i].u = uv[i * 2 + 0];
                        verts[i].v = uv[i * 2 + 1];
                    }
                    haveUV = true;
                }
            }
            if (auto it = prim.attributes.find("TANGENT"); it != prim.attributes.end()) {
                std::vector<float> tan;
                if (accessorFloats(model, it->second, tan, 4)) {
                    for (size_t i = 0; i < vcount; ++i) {
                        verts[i].tx = tan[i * 4 + 0];
                        verts[i].ty = tan[i * 4 + 1];
                        verts[i].tz = tan[i * 4 + 2];
                        verts[i].tw = tan[i * 4 + 3];
                    }
                    haveTangent = true;
                }
            }
            if (auto it = prim.attributes.find("COLOR_0"); it != prim.attributes.end()) {
                const int comps = typeComponents(model.accessors[it->second].type);
                if (comps >= 3) {
                    std::vector<float> col;
                    if (accessorFloats(model, it->second, col, comps)) {
                        for (size_t i = 0; i < vcount; ++i) {
                            verts[i].cr = col[i * comps + 0];
                            verts[i].cg = col[i * comps + 1];
                            verts[i].cb = col[i * comps + 2];
                            verts[i].ca = comps >= 4 ? col[i * comps + 3] : 1.0f;
                        }
                    }
                }
            }

            std::vector<uint32_t> indices;
            if (prim.indices >= 0) {
                if (!accessorIndices(model, prim.indices, indices)) continue;
            } else {
                indices.resize(vcount);
                for (size_t i = 0; i < vcount; ++i) indices[i] = (uint32_t)i;
            }
            const int mode = prim.mode >= 0 ? prim.mode : TINYGLTF_MODE_TRIANGLES;
            if (mode == TINYGLTF_MODE_TRIANGLE_STRIP && indices.size() >= 3) {
                std::vector<uint32_t> tri;
                tri.reserve((indices.size() - 2) * 3);
                for (size_t i = 2; i < indices.size(); ++i) {
                    if (i & 1) { tri.push_back(indices[i-1]); tri.push_back(indices[i-2]); }
                    else       { tri.push_back(indices[i-2]); tri.push_back(indices[i-1]); }
                    tri.push_back(indices[i]);
                }
                indices.swap(tri);
            } else if (mode == TINYGLTF_MODE_TRIANGLE_FAN && indices.size() >= 3) {
                std::vector<uint32_t> tri;
                tri.reserve((indices.size() - 2) * 3);
                for (size_t i = 2; i < indices.size(); ++i) {
                    tri.push_back(indices[0]);
                    tri.push_back(indices[i-1]);
                    tri.push_back(indices[i]);
                }
                indices.swap(tri);
            } else if (mode != TINYGLTF_MODE_TRIANGLES) {
                continue; // points/lines are not part of the PBR mesh path
            }
            if (!haveTangent && haveUV) generateTangents(verts, indices);
            Mesh gpu = vgraphics_RegisterMesh(verts.data(), (int)vcount,
                                              indices.data(), (int)indices.size());
            if (!gpu.id) continue;

            int materialSlot = 0;
            if (prim.material >= 0) {
                auto found = materialMap.find(prim.material);
                if (found != materialMap.end()) materialSlot = found->second;
                else {
                    materialSlot = (int)materials.size();
                    materialMap[prim.material] = materialSlot;
                    materials.push_back(makeMaterial(model, prim.material, textureCache));
                }
            }
            meshes.push_back(gpu);
            meshMats.push_back(materialSlot);
            transforms.push_back(world);
        }
    }

    for (int child : node.children)
        collectNode(model, child, world, meshes, meshMats, transforms,
                    materials, materialMap, textureCache);
}

static bool vlcrGltfExists(const std::string &path, void *) {
    return FileExists(path.c_str());
}

static bool vlcrGltfRead(std::vector<unsigned char> *out, std::string *err,
                         const std::string &path, void *) {
    unsigned int size = 0;
    unsigned char *data = LoadFileData(path.c_str(), &size);
    if (!data) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    out->assign(data, data + size);
    UnloadFileData(data);
    return true;
}

static bool vlcrGltfWrite(std::string *err, const std::string &path,
                          const std::vector<unsigned char> &contents, void *) {
    if (!SaveFileData(path.c_str(), contents.data(),
                      (unsigned int)contents.size())) {
        if (err) *err = "cannot write " + path;
        return false;
    }
    return true;
}

static bool vlcrGltfSize(size_t *out, std::string *err,
                         const std::string &path, void *) {
    unsigned int size = GetFileSizeBytes(path.c_str());
    if (size == 0) {
        if (err) *err = "cannot stat " + path;
        return false;
    }
    *out = size;
    return true;
}

static std::string gltfBaseDir(const std::string &path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

static std::string vlcrGltfExpand(const std::string &path, void *) {
    return path;
}

static void configureGltfLoader(tinygltf::TinyGLTF &loader) {
    tinygltf::FsCallbacks fs;
    fs.FileExists = vlcrGltfExists;
    fs.ExpandFilePath = vlcrGltfExpand;
    fs.ReadWholeFile = vlcrGltfRead;
    fs.WriteWholeFile = vlcrGltfWrite;
    fs.GetFileSizeInBytes = vlcrGltfSize;
    fs.user_data = nullptr;
    std::string err;
    loader.SetFsCallbacks(fs, &err);
}

static bool loadGltfMemory(tinygltf::TinyGLTF &loader, tinygltf::Model &model,
                           std::string &err, std::string &warn,
                           const std::string &path, bool binary) {
    unsigned int length = 0;
    unsigned char *data = LoadFileData(path.c_str(), &length);
    if (!data || length == 0) return false;
    bool ok = binary
        ? loader.LoadBinaryFromMemory(&model, &err, &warn, data, length,
                                      gltfBaseDir(path))
        : loader.LoadASCIIFromString(&model, &err, &warn,
                                     (const char *)data, length,
                                     gltfBaseDir(path));
    UnloadFileData(data);
    return ok;
}

extern "C" Model LoadModelGLTFTiny(const char *path) {
    Model empty = {0};
    if (!path) return empty;

    tinygltf::TinyGLTF loader;
    configureGltfLoader(loader);
    loader.SetImageLoader(tinyImageLoader, nullptr);
    tinygltf::Model model;
    std::string err, warn;
    std::string p(path);
    bool binary = p.size() > 4 &&
        (p.substr(p.size() - 4) == ".glb" || p.substr(p.size() - 4) == ".GLB");
    bool ok = loadGltfMemory(loader, model, err, warn, p, binary);
    if (!ok) return empty;

    const int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (sceneIndex < 0 || sceneIndex >= (int)model.scenes.size()) return empty;

    std::vector<Mesh> meshes;
    std::vector<int> meshMats;
    std::vector<Matrix> transforms;
    std::vector<Material> materials;
    std::unordered_map<int, int> materialMap;
    std::unordered_map<uint64_t, Texture2D> textureCache;

    for (int node : model.scenes[sceneIndex].nodes)
        collectNode(model, node, MatrixIdentity(), meshes, meshMats, transforms,
                    materials, materialMap, textureCache);

    if (meshes.empty()) return empty;
    Model result = vgraphics_CreateModelFromMeshesEx(
        meshes.data(), meshMats.data(), transforms.data(),
        (int)meshes.size(), materials.data(), (int)materials.size());
    for (auto& kv : textureCache)
        if (!vgraphics_TrackModelTexture(result, kv.second))
            UnloadTexture(kv.second);
    return result;
}

extern "C" SkinnedModel vgltf_LoadSkinnedModel(const char *path) {
    SkinnedModel empty = {0};
    if (!path || !path[0]) return empty;

    tinygltf::TinyGLTF loader;
    configureGltfLoader(loader);
    loader.SetImageLoader(tinyImageLoader, nullptr);
    tinygltf::Model model;
    std::string err, warn;
    std::string p(path);
    bool binary = p.size() > 4 &&
        (p.substr(p.size() - 4) == ".glb" || p.substr(p.size() - 4) == ".GLB");
    bool ok = loadGltfMemory(loader, model, err, warn, p, binary);
    if (!ok) return empty;

    // First node with both a mesh and a skin.
    int nodeIndex = -1;
    for (int i = 0; i < (int)model.nodes.size(); ++i)
        if (model.nodes[i].skin >= 0 && model.nodes[i].mesh >= 0) {
            nodeIndex = i; break;
        }
    if (nodeIndex < 0) return empty;
    const auto& node = model.nodes[nodeIndex];
    if (node.skin < 0 || node.skin >= (int)model.skins.size()) return empty;
    const auto& skin = model.skins[node.skin];
    const size_t boneCount = skin.joints.size();
    if (boneCount == 0 || boneCount > 256) return empty;

    std::vector<Matrix> inverseBind(boneCount, MatrixIdentity());
    std::vector<int> parents(boneCount, -1);
    std::vector<Vector3> restT(boneCount, Vector3{0,0,0});
    std::vector<Quaternion> restR(boneCount, Quaternion{0,0,0,1});
    std::vector<Vector3> restS(boneCount, Vector3{1,1,1});
    std::vector<const char*> boneNames(boneCount, nullptr);
    for (size_t b = 0; b < boneCount; ++b) {
        int ni = skin.joints[b];
        boneNames[b] = ni >= 0 && ni < (int)model.nodes.size()
                     ? model.nodes[ni].name.c_str() : "";
    }

    if (skin.inverseBindMatrices >= 0) {
        std::vector<float> m;
        // glTF stores MAT4 accessors column-major, while vlcrlib's Matrix
        // keeps row-major memory order, so a raw copy would transpose them.
        if (accessorFloats(model, skin.inverseBindMatrices, m, 16))
            for (size_t i = 0; i < boneCount && i * 16 + 16 <= m.size(); ++i) {
                std::memcpy(&inverseBind[i], &m[i*16], sizeof(Matrix));
                inverseBind[i] = MatrixTranspose(inverseBind[i]);
            }
    }

    // parent lookup by walking the node tree
    std::function<void(int,int)> walk = [&](int ni, int parent) {
        if (ni < 0 || ni >= (int)model.nodes.size()) return;
        for (size_t b = 0; b < boneCount; ++b) {
            if (skin.joints[b] == ni) {
                parents[b] = parent;
                const auto& n = model.nodes[ni];
                const double tx = n.translation.size() > 0 ? n.translation[0] : 0;
                const double ty = n.translation.size() > 1 ? n.translation[1] : 0;
                const double tz = n.translation.size() > 2 ? n.translation[2] : 0;
                const double rx = n.rotation.size() > 0 ? n.rotation[0] : 0;
                const double ry = n.rotation.size() > 1 ? n.rotation[1] : 0;
                const double rz = n.rotation.size() > 2 ? n.rotation[2] : 0;
                const double rw = n.rotation.size() > 3 ? n.rotation[3] : 1;
                const double sx = n.scale.size() > 0 ? n.scale[0] : 1;
                const double sy = n.scale.size() > 1 ? n.scale[1] : 1;
                const double sz = n.scale.size() > 2 ? n.scale[2] : 1;
                restT[b] = Vector3{(float)tx,(float)ty,(float)tz};
                restR[b] = Quaternion{(float)rx,(float)ry,(float)rz,(float)rw};
                restS[b] = Vector3{(float)sx,(float)sy,(float)sz};
                break;
            }
        }
        for (int c : model.nodes[ni].children) walk(c, ni);
    };
    for (int root : model.scenes[model.defaultScene >= 0 ? model.defaultScene : 0].nodes)
        walk(root, -1);

    // The walk records parent *node* indices; the skinned model needs parent
    // *bone* indices. Nodes that are not skin joints become root bones.
    for (size_t b = 0; b < boneCount; ++b) {
        int parentNode = parents[b];
        parents[b] = -1;
        for (size_t pb = 0; pb < boneCount; ++pb)
            if (skin.joints[pb] == parentNode) { parents[b] = (int)pb; break; }
    }

    const auto& mesh = model.meshes[node.mesh];
    if (mesh.primitives.empty()) return empty;
    const auto& prim = mesh.primitives[0];
    auto posIt = prim.attributes.find("POSITION");
    if (posIt == prim.attributes.end()) return empty;
    std::vector<float> pos;
    if (!accessorFloats(model, posIt->second, pos, 3)) return empty;
    const size_t vcount = pos.size() / 3;
    if (!vcount) return empty;

    std::vector<SkinnedVertex3D> verts(vcount);
    for (size_t i = 0; i < vcount; ++i) {
        verts[i].x = pos[i*3+0];
        verts[i].y = pos[i*3+1];
        verts[i].z = pos[i*3+2];
        verts[i].ny = 1.0f;
    }
    if (auto it = prim.attributes.find("NORMAL"); it != prim.attributes.end()) {
        std::vector<float> n;
        if (accessorFloats(model, it->second, n, 3))
            for (size_t i = 0; i < vcount; ++i) {
                verts[i].nx = n[i*3+0]; verts[i].ny = n[i*3+1]; verts[i].nz = n[i*3+2];
            }
    }
    if (auto it = prim.attributes.find("TEXCOORD_0"); it != prim.attributes.end()) {
        std::vector<float> uv;
        if (accessorFloats(model, it->second, uv, 2))
            for (size_t i = 0; i < vcount; ++i) {
                verts[i].u = uv[i*2+0]; verts[i].v = uv[i*2+1];
            }
    }
    if (auto it = prim.attributes.find("TANGENT"); it != prim.attributes.end()) {
        std::vector<float> t;
        if (accessorFloats(model, it->second, t, 4))
            for (size_t i = 0; i < vcount; ++i) {
                verts[i].tx=t[i*4+0]; verts[i].ty=t[i*4+1];
                verts[i].tz=t[i*4+2]; verts[i].tw=t[i*4+3];
            }
    }
    if (auto it = prim.attributes.find("JOINTS_0"); it != prim.attributes.end()) {
        std::vector<float> j;
        if (accessorFloats(model, it->second, j, 4))
            for (size_t i = 0; i < vcount; ++i)
                for (int k = 0; k < 4; ++k) {
                    int v = (int)(j[i*4+k] + 0.5f);
                    verts[i].joints[k] = (unsigned char)(v >= 0 && v < (int)boneCount ? v : 0);
                }
    }
    if (auto it = prim.attributes.find("WEIGHTS_0"); it != prim.attributes.end()) {
        std::vector<float> w;
        if (accessorFloats(model, it->second, w, 4))
            for (size_t i = 0; i < vcount; ++i) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) { verts[i].weights[k] = w[i*4+k]; sum += verts[i].weights[k]; }
                if (sum > 1e-6f) for (int k = 0; k < 4; ++k) verts[i].weights[k] /= sum;
                else { verts[i].joints[0] = 0; verts[i].weights[0] = 1.0f; }
            }
    }

    std::vector<uint32_t> indices;
    if (prim.indices >= 0) {
        if (!accessorIndices(model, prim.indices, indices)) return empty;
    } else {
        indices.resize(vcount);
        for (size_t i = 0; i < vcount; ++i) indices[i] = (uint32_t)i;
    }

    std::unordered_map<uint64_t, Texture2D> cache;
    Material mat = prim.material >= 0 ? makeMaterial(model, prim.material, cache)
                                      : LoadMaterialDefault();
    SkinnedModel result = vgraphics_CreateSkinnedModel(
        verts.data(), (int)vcount, indices.data(), (int)indices.size(),
        &mat, inverseBind.data(), parents.data(),
        restT.data(), restR.data(), restS.data(), (int)boneCount,
        boneNames.data());
    if (!result.id) return empty;

    // Animations: map node index -> bone index.
    std::unordered_map<int,int> nodeToBone;
    for (size_t b = 0; b < boneCount; ++b) nodeToBone[(int)skin.joints[b]] = (int)b;

    for (const auto& anim : model.animations) {
        std::vector<std::vector<AnimVec3Key>> pos(boneCount);
        std::vector<std::vector<AnimQuatKey>> rot(boneCount);
        std::vector<std::vector<AnimVec3Key>> scl(boneCount);
        float duration = 0.0f;

        for (const auto& ch : anim.channels) {
            auto nb = nodeToBone.find(ch.target_node);
            if (nb == nodeToBone.end()) continue;
            if (ch.sampler < 0 || ch.sampler >= (int)anim.samplers.size()) continue;
            const auto& smp = anim.samplers[ch.sampler];
            std::vector<float> times, values;
            int comps = ch.target_path == "rotation" ? 4 : 3;
            if (!accessorFloats(model, smp.input, times, 1)) continue;
            if (!accessorFloats(model, smp.output, values, comps)) continue;
            size_t n = std::min(times.size(), values.size() / comps);
            if (n == 0) continue;
            duration = std::max(duration, times[n-1]);
            int b = nb->second;
            if (ch.target_path == "translation") {
                for (size_t i=0;i<n;++i)
                    pos[b].push_back({times[i], Vector3{values[i*3],values[i*3+1],values[i*3+2]}});
            } else if (ch.target_path == "scale") {
                for (size_t i=0;i<n;++i)
                    scl[b].push_back({times[i], Vector3{values[i*3],values[i*3+1],values[i*3+2]}});
            } else if (ch.target_path == "rotation") {
                for (size_t i=0;i<n;++i)
                    rot[b].push_back({times[i], Quaternion{values[i*4],values[i*4+1],
                                                          values[i*4+2],values[i*4+3]}});
            }
        }

        std::vector<AnimTrackData> tracks(boneCount);
        for (size_t b=0;b<boneCount;++b) {
            tracks[b].pos = pos[b].empty()?nullptr:pos[b].data();
            tracks[b].posCount = (int)pos[b].size();
            tracks[b].rot = rot[b].empty()?nullptr:rot[b].data();
            tracks[b].rotCount = (int)rot[b].size();
            tracks[b].scale = scl[b].empty()?nullptr:scl[b].data();
            tracks[b].scaleCount = (int)scl[b].size();
        }
        vgraphics_AddSkinnedAnimation(result,
            anim.name.empty() ? "clip" : anim.name.c_str(),
            duration > 0 ? duration : 1.0f, tracks.data(), (int)boneCount);
    }
    return result;
}
