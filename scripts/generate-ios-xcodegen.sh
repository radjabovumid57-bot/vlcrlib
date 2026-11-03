#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "${root}/platform/ios"

if ! command -v xcodegen >/dev/null 2>&1; then
  echo "xcodegen not found: brew install xcodegen" >&2
  exit 1
fi

# The app links prebuilt iOS libraries from the CMake build.
"${root}/scripts/generate-ios.sh"

xcodegen generate --spec project.yml --project vlcrlib-mobile.xcodeproj
echo "Generated ${root}/platform/ios/vlcrlib-mobile.xcodeproj"
