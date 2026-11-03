#!/usr/bin/env bash
# Build vlcrlib for Web (Emscripten → WASM + WebGPU)
# Prerequisites: emsdk activated (emcmake, emcc on PATH)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

if ! command -v emcmake >/dev/null 2>&1; then
  echo "emcmake not found. Install emsdk and run: source emsdk_env.sh"
  exit 1
fi

mkdir -p build-web
cd build-web
emcmake cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DVLCRLIB_BUILD_EXAMPLES=ON \
  -DVLCRLIB_ENABLE_JOLT=OFF \
  -DVLCRLIB_ENABLE_VLC=OFF \
  -DVLCRLIB_GRAPHICS_WEBGPU=ON \
  -DVLCRLIB_SKIP_VCPKG=ON
# Preload GLB if present
if [[ -f "$ROOT/example/DamagedHelmet.glb" ]]; then
  echo "Note: add --preload-file for DamagedHelmet in target link flags if needed"
fi
emmake cmake --build . -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo ""
echo "Web build done. Serve and open:"
echo "  cd $ROOT/build-web/example"
echo "  python3 -m http.server 8080"
echo "  → http://localhost:8080/web_hello.html"
