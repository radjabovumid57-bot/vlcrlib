# Сборка example_fastgltf_lib → APK

## Статус в этой среде
Android SDK/NDK здесь нет — APK нужно собрать на вашем ПК (Android Studio).

## Что уже исправлено
1. `example/example_fastgltf_lib.c` — пути для desktop + Android assets
2. `android/app/src/main/assets/` — место для `DamagedHelmet.glb`
3. Manifest с Vulkan feature

## На Windows

```powershell
cd C:\Users\Admin\Documents\vlcrlib

# 1) Модель в assets APK
mkdir android\app\src\main\assets -Force
copy example\DamagedHelmet.glb android\app\src\main\assets\

# 2) Desktop-проверка (сначала это!)
cmake --build build --config Release --target example_fastgltf_lib
cd build\example\Release
.\example_fastgltf_lib.exe
```

Если на desktop шлем виден — логика import OK, дальше APK.

## APK через Android Studio
1. File → Open → `vlcrlib/android`
2. Дописать native CMake, линкующий `vlcrlib` + `example_fastgltf_lib.c`
3. Build APK → `adb install`

Минимальный `CMakeLists.txt` для app module (набросок):

```cmake
cmake_minimum_required(VERSION 3.22)
project(vlcrlib_android)
add_subdirectory(${VLCRLIB_ROOT} vlcrlib_build)
add_library(main SHARED
  ${VLCRLIB_ROOT}/example/example_fastgltf_lib.c)
target_link_libraries(main vlcrlib SDL3::SDL3 android log)
```

## Тест logcat
```
adb logcat | findstr /i "gltf SDL Vulkan vlcrlib"
```

Ожидаемо:
```
[gltf] start
[gltf] window ok
[gltf] extracted DamagedHelmet.glb -> ...
[gltf] LoadModel id=1
[gltf] first frame ok model=1
```
