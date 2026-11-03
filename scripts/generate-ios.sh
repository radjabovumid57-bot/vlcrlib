#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${root}/build-ios"

cmake -S "${root}" -B "${build}" -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DVLCRLIB_SKIP_VCPKG=ON \
  -DVLCRLIB_ENABLE_VLC=OFF \
  -DVLCRLIB_BUILD_EXAMPLES=ON \
  -DVLCRLIB_BUILD_TESTS=OFF \
  -DVLCRLIB_BUILD_TOOLS=OFF

cmake --build "${build}" --config Release --target mobile_hello
echo "Xcode project: ${build}/vlcrlib.xcodeproj"
