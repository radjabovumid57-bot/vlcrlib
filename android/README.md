# vlcrlib Android project

This directory is a working Gradle application for the native `mobile_gltf`
viewer. It builds `app-debug.apk` with `libmain.so`, `libSDL3.so` and
`libc++_shared.so` for `arm64-v8a`.

## Requirements

- Android Studio Ladybug+ / SDK 35
- NDK r26+ (`ndkVersion` in `app/build.gradle.kts`)
- CMake 3.22+
- SDL3 source in `external/SDL3` (vendored) **or** an SDL3 CMake package
- `VLCRLIB_ENABLE_PHYSICS=OFF` is set by the app's native CMake args
- minSdk 28 (Vulkan 1.1 entry points)

The local SDK is installed at `C:\Android\Sdk` (NDK `26.1.10909125`, Android
platform 35, build-tools 35.0.0, CMake 3.22.1, Gradle 8.9). `local.properties`
points Gradle at the SDK/CMake; restart the shell/Android Studio after the
user-level environment variables were added.

Built APK:

```text
android/app/build/outputs/apk/debug/app-debug.apk
```

`aapt2` reports:

```text
package: org.vlcrlib.mobile
minSdkVersion: 28
native-code: arm64-v8a
lib/arm64-v8a/libmain.so
lib/arm64-v8a/libSDL3.so
lib/arm64-v8a/libc++_shared.so
```

## Build

1. Put SDL3 source at `external/SDL3` (the checked-in CMake wrapper runs
   `add_subdirectory`), or install SDL3 as a CMake package.
2. Put models/textures under `android/app/src/main/assets/`.
3. Set `local.properties`:
   ```properties
   sdk.dir=C:\\Users\\<you>\\AppData\\Local\\Android\\Sdk
   ndk.dir=C:\\Users\\<you>\\AppData\\Local\\Android\\Sdk\\ndk\\26.1.10909125
   ```
4. Open `android/` in Android Studio, or:
   ```bash
   cd android
   gradle :app:assembleDebug
   ```
5. Install:
   ```bash
   adb install -r app/build/outputs/apk/debug/app-debug.apk
   adb logcat -s SDL vlcrlib
   ```

The Gradle wrapper properties are pinned to Gradle 8.9. Android Studio can
generate the wrapper jar/scripts on first import; alternatively install Gradle
and run the command above.

## Native shape

- `app/src/main/cpp/CMakeLists.txt` builds `libmain.so` from
  `example/mobile_gltf.c` (`-DMOBILE_ENTRY=...` switches the entry point);
  the viewer loads `assets/DamagedHelmet.glb` from the APK;
- `vlcrlib` is added as a subdirectory;
- SDL3's Android Java classes come from `external/SDL3/android-project/...`
  via the Gradle `sourceSets` entry;
- the manifest uses `org.libsdl.app.SDLActivity` and requests Vulkan.

The Android APK still must be signed/packaged by Gradle; this directory does not
contain a checked-in `.apk`.
