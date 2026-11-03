# Model loaders (glTF, OBJ, FBX, USD)

## Native

| Format | Loader |
|---|---|
| `.glb`, `.gltf` | tinygltf (`src/vgltf.cpp`) |
| `.obj` | Assimp when available; otherwise built-in OBJ loader (`LoadModel`) |

## Optional

| Format | Backend | CMake |
|---|---|---|
| `.fbx` | Assimp | `find_package(assimp CONFIG)` |
| `.usd`, `.usda`, `.usdc`, `.usdz` | OpenUSD | `find_package(usd CONFIG)` |

If the dependency is not found, the corresponding loader compiles as a stub
returning `Model{id=0}`; the rest of the engine still builds.

### Install

With vcpkg:

```powershell
C:/vcpkg/vcpkg install assimp:x64-windows
# OpenUSD is large; install only if the USD runtime is required:
C:/vcpkg/vcpkg install usd:x64-windows
```

Then reconfigure:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

CMake prints:

```text
vlcrlib: FBX loader = Assimp
vlcrlib: USD loader = OpenUSD
```

## API

```c
#include "vloaders.h"

Model m = LoadModelAuto("model.fbx");   // or .usdz/.glb/.obj
if (!m.id) { /* backend missing or load failed */ }

UnloadModel(m);
```

`LoadModelAuto` dispatches by extension:

- `.glb`/`.gltf` → tinygltf;
- `.fbx` → Assimp;
- `.usd/.usda/.usdc/.usdz` → OpenUSD;
- `.obj` → `LoadOBJ` (Assimp with MTL support, built-in fallback);
- unknown → tries glTF, then OBJ.

## Example

```powershell
build-check\example\Release\model_viewer.exe path\to\model.fbx
```

`example/model_viewer.c` uses `LoadModelAuto`, the same IBL/lighting setup and
prints a hint when the FBX/USD backend was not compiled in.

## GUI loader + animation picker

```powershell
build-check\example\Release\model_animation_viewer.exe
```

Features:

- **Browse...** opens a native file dialog filtered to
  `glb;gltf;fbx;obj;usd;usda;usdc;usdz`;
- loads skinned glTF through `LoadSkinnedModel` (tinygltf);
- loads skinned FBX through `LoadFBXSkinned` (Assimp);
- falls back to static `LoadFBX` / `LoadUSD` / `LoadModelGLTFTiny`;
- lists animation clips by name;
- automatically selects a clip named **Idle** (case-insensitive), otherwise
  the first clip;
- Play/Pause, Rewind, Loop, speed, depth/IBL/bloom toggles, orbit camera.

For FBX animations, Assimp channel times are converted using
`mTicksPerSecond`; glTF animation times are already seconds.

Animation clips are added through the engine API:

```c
int vgraphics_AddSkinnedAnimation(SkinnedModel model, const char *name,
                                  float duration,
                                  const AnimTrackData *tracks, int trackCount);
const char *GetSkinnedModelAnimationName(SkinnedModel model, int animIndex);
```

USD animation is not implemented yet; USD/USDZ is loaded as a static mesh.

## Notes and limitations

- FBX units are often centimeters. If a model is 100× too large, scale it by
  `0.01` before drawing.
- FBX/OBJ may use non-PBR (Phong) materials; Assimp factors are mapped to the
  engine's metallic-roughness material, but metalness/roughness may need
  manual tuning.
- USD material/shading networks are only minimally mapped: the current USD
  path builds a default material, not `UsdPreviewSurface` textures.
- Skinning/animation is not implemented for FBX/USD.
- USD is heavy. For runtime rendering, converting USD to glTF offline is
  usually preferable.
- Assimp does not support USD; OpenUSD does not read FBX.
