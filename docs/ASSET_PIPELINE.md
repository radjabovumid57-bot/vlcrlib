# vlcrlib asset pipeline

Status: current runtime loaders + target offline cooker design.

## 1. Principle

The shipping runtime should load **cooked, validated, GPU-ready assets**.
DCC formats (FBX, USD, source glTF) are import-time formats and must not be
parsed on the critical path.

```text
FBX / USD / source glTF
        │
        ▼
   importer (Assimp / OpenUSD / tinygltf)
        │
        ▼
   validator (units, UV, normals, tangents, scale, materials)
        │
        ▼
   optimizer (meshoptimizer)
        │
        ├─ LOD generation
        ├─ vertex cache / overdraw optimization
        ├─ texture compression (KTX2 + BC7/BC6H/ASTC)
        └─ animation compression
        │
        ▼
   cooked asset / pack
        │
        ▼
   runtime streaming loader
```

## 2. Current runtime

| Format | Loader | Status |
|---|---|---|
| `.glb`, `.gltf` | tinygltf | static, skinned, animations |
| `.fbx` | Assimp (optional) | static + first skinned mesh |
| `.obj` | Assimp / built-in | MTL through Assimp |
| `.usd/.usda/.usdc/.usdz` | OpenUSD (optional) | static geometry, material stub |

Current limitations:

- images decode to RGBA8 at load time;
- no KTX2/BC7/BC6H/ASTC;
- no Draco/meshopt;
- no LOD generation;
- no async streaming;
- no asset database;
- only `TEXCOORD_0`;
- morph targets and USD animation are not implemented.

## 3. Target cooked format

### Mesh

```text
CookedMesh
  positions        f32x3
  normals          snorm16x3 (or f32x3 for debug)
  uvs              f16x2
  tangents         snorm16x4
  joints           u8x4 (optional)
  weights          unorm16x4 (optional)
  indices          u16 or u32
  bounds           AABB
  lodCount
  lods[]           index ranges + screen thresholds
```

### Texture

- KTX2 container;
- BC7 for color/albedo/emissive;
- BC5 for two-channel normals;
- BC4 for single-channel masks;
- BC6H for HDR environment maps;
- ASTC for mobile;
- mip chain baked offline;
- sRGB flag per semantic.

### Material

```text
CookedMaterial
  baseColorFactor
  metallic / roughness / ior
  alphaMode / alphaCutoff
  doubleSided
  texture indices: albedo, normal, MR, AO, emissive, specular, clearcoat
  KHR feature flags/factors
```

### Animation

- compressed keyframes (quantized time + value);
- per-bone tracks;
- clip names/events;
- root-motion flag.

## 4. Cooker CLI (planned)

```powershell
vlcrc --input character.fbx `
      --out build/assets/character.vasset `
      --textures ktx2 `
      --compression bc7 `
      --lod 0,1,2 `
      --validate
```

Outputs:

- `.vasset` — mesh/material/skeleton/animation data;
- `.vtex` or KTX2 sidecars;
- `.vmanifest` — dependencies and hashes;
- validation report.

## 5. Validation rules

Reject or warn on:

- non-uniform/negative scale;
- zero-length normals;
- missing tangents on normal-mapped materials;
- UVs outside expected range (warn);
- unit mismatch (FBX cm vs glTF m);
- duplicate meshes at incompatible scales (the `Walking.fbx` case);
- missing textures;
- >4 bone influences per vertex;
- unsupported primitive modes (strip/fan are converted);
- >64 bones (current limit);
- invalid alpha mode / cutoff.

## 6. Runtime streaming

Target:

```c
AssetHandle vassets_LoadAsync(const char *path, AssetPriority priority);
bool        vassets_IsReady(AssetHandle h);
void        vassets_Release(AssetHandle h);
```

- IO thread → decode thread → upload queue;
- budgeted upload per frame (e.g. 4 MB);
- ref-counted GPU resources;
- fallback texture/material while loading;
- VRAM budget and eviction for open worlds.

## 7. USD / FBX policy

- **FBX**: import to cooked asset; do not ship Assimp to the game runtime.
- **USD/USDZ**: use OpenUSD in the cooker; runtime reads cooked data.
- **glTF**: optional direct runtime for tools/prototypes; shipping uses cooked.
- Preserve source hashes for incremental cooking.

## 8. Milestones

### A. Cooker MVP

- [x] `vlcrc --input <model> --out <manifest.json>` validator;
- [x] glTF/GLB metadata via tinygltf;
- [x] FBX/OBJ metadata via Assimp (optional);
- [x] mesh/material/skeleton/animation counts;
- [x] bounds + unit/scale warnings;
- [x] JSON manifest;
- [x] mesh binary cooking: `vlcrc --mesh out.vmesh`;
- [x] runtime `LoadCookedModel` for `.vmesh`;
- [x] PBR material constants in `.vmesh` v2;
- [x] albedo/normal/MR/AO/emissive texture references;
- [x] glTF node transform baked into cooked vertices;
- [x] multi-mesh / multi-material `.vmesh` v3;
- [x] scene editor-lite: node list, transforms, visibility, parent index, save/load;
- [x] editor undo/redo (16 snapshots);
- [x] parent/child scene hierarchy with world-transform resolution;
- [x] bone-space additive overlay + pose API;
- [ ] sample environment.

```powershell
# Put KTX2 files next to the .vmesh (paths are relative).
vlcrc --input assets\DamagedHelmet.glb `
      --out assets\cooked\helmet.vmanifest.json `
      --mesh assets\cooked\helmet.vmesh `
      --ktx assets\cooked
```

### B. Texture compression

- [x] KTX2 container cooking via KTX-Software (`vlcrc --ktx <dir>`);
- [x] BC7 for color images (`--ktx`);
- [x] BC5 for normal maps (`--ktx-bc5`);

```powershell
vlcrc --input assets\DamagedHelmet.glb `
      --out assets\helmet.vmanifest.json `
      --ktx assets\tex_bc7
vlcrc --input assets\DamagedHelmet.glb `
      --out assets\helmet.vmanifest.json `
      --ktx assets\tex_bc5 --ktx-bc5
```

- [x] runtime KTX2/BC7/BC5 upload (`LoadTextureKTX2`, `.ktx2` in `LoadTexture`);
- [x] generated mip chain (box filter) written into KTX2 and uploaded;
- [ ] material texture references stored in `.vmesh`.


- [ ] KTX2 encode/decode;
- [ ] BC7/BC5/BC4;
- [ ] sRGB/linear semantics;
- [ ] mip generation offline.

### C. Mesh optimization

- [ ] meshoptimizer vertex cache;
- [ ] overdraw optimization;
- [ ] LOD chain;
- [ ] bounds generation.

### D. Streaming

- [ ] async file IO;
- [ ] decode job queue;
- [ ] upload budget;
- [ ] VRAM budget;
- [ ] hot reload.

### E. Asset database

- [ ] GUID/hash;
- [ ] dependency graph;
- [ ] incremental cook;
- [ ] content-addressed pack files.

## 9. Acceptance criteria

- 2 GB scene loads in < 3 s from NVMe;
- streaming hitch < 16 ms;
- texture memory reduced by ≥ 60% versus RGBA8;
- no DCC parser linked into shipping runtime;
- zero missing-texture/material errors in validation;
- deterministic cooked output for the same source/toolchain.
