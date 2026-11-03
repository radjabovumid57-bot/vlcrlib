#!/usr/bin/env bash
# Build vlcrlib on macOS (MoltenVK via Vulkan SDK or vcpkg)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

if [[ -z "${CMAKE_TOOLCHAIN_FILE:-}" && -n "${VCPKG_ROOT:-}" ]]; then
  export CMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
fi

mkdir -p build-macos
cd build-macos
cmake .. -DCMAKE_BUILD_TYPE=Release \
  -DVLCRLIB_BUILD_EXAMPLES=ON \
  -DVLCRLIB_ENABLE_JOLT=ON \
  ${CMAKE_TOOLCHAIN_FILE:+-DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE"}
cmake --build . -j"$(sysctl -n hw.ncpu)"
echo "Done. Examples: $ROOT/build-macos/example/"
