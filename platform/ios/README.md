# vlcrlib iOS project

Two generation paths are provided:

- CMake (`scripts/generate-ios.sh`) — builds the native library and target;
- XcodeGen (`scripts/generate-ios-xcodegen.sh`) — generates a checked-spec
  `.xcodeproj` that links the CMake-built iOS static libraries.

## CMake

```bash
./scripts/generate-ios.sh
open build-ios/vlcrlib.xcodeproj
```

## XcodeGen

```bash
brew install xcodegen
./scripts/generate-ios-xcodegen.sh
open platform/ios/vlcrlib-mobile.xcodeproj
```

The script configures:

- `CMAKE_SYSTEM_NAME=iOS`, deployment target 13.0;
- `mobile_hello` as a `MACOSX_BUNDLE`;
- MoltenVK portability support;
- UIKit/Foundation/CoreGraphics/QuartzCore/CoreMotion/AVFoundation/Metal/
  MetalKit/CoreAudio/AudioToolbox/CoreFoundation.

## Bundle resources

Add to the generated target:

- `assets/Roboto-Medium.ttf`;
- the `shaders/*.spv` directory;
- any glTF/GLB assets (prefer GLB with embedded textures).

`Info.plist` in this directory is a template; CMake/Xcode can use it via
`MACOSX_BUNDLE_INFO_PLIST`.

## Signing

Select your team in Xcode, set a bundle identifier, and run on a device. The
repository does not contain signing identities or a prebuilt `.ipa`.
