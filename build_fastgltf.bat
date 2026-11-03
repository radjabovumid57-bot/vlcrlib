@echo off
REM ==================================================================
REM  build_fastgltf.bat — configure + build vlcrlib with the
REM  spnda/fastgltf backend
REM ==================================================================
REM
REM  WHY THE EXTRA FLAGS
REM  -------------------
REM  1. TWO vcpkg COPIES EXIST on this machine:
REM
REM       C:\vcpkg         2871 ports, HAS .git   -> can resolve the
REM                        builtin-baseline in vcpkg.json. Use this one.
REM       C:\vcpkg-master  2867 ports, NO .git    -> "fatal: not a git
REM                        repository", and its downloads\ dir cannot
REM                        even be created (Access is denied).
REM
REM     "vcpkg" on PATH resolves to C:\vcpkg-master, so a bare
REM     `vcpkg install` fails before it reaches fastgltf. VCPKG_EXE below
REM     calls the correct one by full path.
REM
REM  2. CMake's automatic manifest install and a manual `vcpkg install`
REM     write to DIFFERENT trees:
REM
REM       manual : <project>\vcpkg_installed        (vcpkg's own default)
REM       CMake  : <project>\build\vcpkg_installed  (CMake's default)
REM
REM     So installing fastgltf by hand leaves CMake still unable to find
REM     it. This script points CMake at whichever tree actually has
REM     fastgltf instead of relying on that coincidence.
REM
REM  3. build\ accumulated state from earlier failed configures, so it is
REM     removed first.
REM
setlocal enableextensions
cd /d "%~dp0"

set "VCPKG_EXE="
if exist "%VCPKG_ROOT%\vcpkg.exe" if exist "%VCPKG_ROOT%\.git" set "VCPKG_EXE=%VCPKG_ROOT%\vcpkg.exe"
if not defined VCPKG_EXE if exist "C:\vcpkg\vcpkg.exe" if exist "C:\vcpkg\.git" set "VCPKG_EXE=C:\vcpkg\vcpkg.exe"
if not defined VCPKG_EXE (
    echo [ERROR] No vcpkg root with .git found ^(looked at VCPKG_ROOT and C:\vcpkg^).
    echo         A vcpkg without .git cannot resolve builtin-baseline.
    exit /b 1
)
set "VCPKG_TOOLCHAIN=%VCPKG_EXE:\vcpkg.exe=\scripts\buildsystems\vcpkg.cmake%"

echo [INFO] vcpkg     : %VCPKG_EXE%
echo [INFO] toolchain : %VCPKG_TOOLCHAIN%
echo [INFO] workspace : %CD%
echo.

REM --- 1. make sure fastgltf is installed somewhere ----------------------
echo === [1/4] vcpkg install --x-feature=fastgltf ===
"%VCPKG_EXE%" install --x-feature=fastgltf
if errorlevel 1 (
    echo [ERROR] vcpkg install failed. Scroll up for the first real error.
    exit /b 1
)

REM --- 2. clear stale build state ---------------------------------------
echo.
echo === [2/4] Removing stale build\ ===
if exist build rmdir /s /q build

REM --- 3. configure ------------------------------------------------------
REM Prefer a tree that already contains fastgltf; otherwise let CMake do a
REM normal manifest install (which now requests the feature via
REM VCPKG_MANIFEST_FEATURES, set from VLCRLIB_ENABLE_FASTGLTF in CMakeLists).
echo.
echo === [3/4] Configure ===
set "INSTALLED_ARG="
set "MANIFEST_ARG="
if exist "vcpkg_installed\x64-windows\share\fastgltf\fastgltfConfig.cmake" (
    echo [INFO] Using existing vcpkg tree: vcpkg_installed
    set "INSTALLED_ARG=-DVCPKG_INSTALLED_DIR=%CD:\=/%/vcpkg_installed"
    set "MANIFEST_ARG=-DVCPKG_MANIFEST_MODE=OFF"
) else (
    echo [INFO] No pre-populated tree with fastgltf; using CMake's manifest install
)

cmake -B build -G "Visual Studio 18 2026" -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN:\=/%" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows ^
    %INSTALLED_ARG% %MANIFEST_ARG% ^
    -DVLCRLIB_ENABLE_FASTGLTF=ON ^
    -DVLCRLIB_BUILD_EXAMPLES=ON
if errorlevel 1 (
    echo.
    echo [ERROR] CMake configure failed.
    exit /b 1
)

REM --- 4. build ----------------------------------------------------------
echo.
echo === [4/4] Build ===
cmake --build build --config Release --parallel
if errorlevel 1 (
    echo.
    echo [ERROR] Build failed.
    exit /b 1
)

echo.
echo ================================================================
echo  Done. Run the fastgltf example:
echo    .\build\example\Release\example_fastgltf_lib.exe example\DamagedHelmet.glb
echo ================================================================
endlocal
