@echo off
setlocal enableextensions
cd /d "%~dp0"

echo === vclrlib Windows build ===
echo.

where cmake >nul 2>&1 || (echo [ERROR] cmake not on PATH. & echo Open this from "Developer Command Prompt for VS". & exit /b 1)
where cl    >nul 2>&1 || (echo [ERROR] cl.exe not found. & echo Open this from "x64 Native Tools Command Prompt for VS". & exit /b 1)

:: --- Проверка glslc ---
where glslc >nul 2>&1
if errorlevel 1 (
    if defined VULKAN_SDK (
        echo [INFO] glslc not on PATH, but VULKAN_SDK=%VULKAN_SDK%
        set "PATH=%VULKAN_SDK%\Bin;%PATH%"
    ) else (
        echo [WARN] glslc not found. Install Vulkan SDK from lunarg.com
    )
)

:: --- Выбор генератора ---
set "GEN="
for /f "tokens=*" %%G in ('cmake -G 2^>^&1 ^| findstr /C:"Visual Studio"') do (
    echo %%G | findstr /C:"Visual Studio 18 2026" >nul && if not defined GEN set "GEN=Visual Studio 18 2026"
    echo %%G | findstr /C:"Visual Studio 17 2022" >nul && if not defined GEN set "GEN=Visual Studio 17 2022"
    echo %%G | findstr /C:"Visual Studio 16 2019" >nul && if not defined GEN set "GEN=Visual Studio 16 2019"
)
if not defined GEN (echo [ERROR] No supported VS generator found. & exit /b 1)
echo Using generator: %GEN%
echo.

:: --- vcpkg (если есть) ---
set "VCPKG_ARG="
if defined VCPKG_ROOT (
    if exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
        echo [INFO] vcpkg found at %VCPKG_ROOT%
        set "VCPKG_ARG=-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows"
    )
)

:: --- Подготовка папки сборки ---
if not exist build mkdir build
cd build

:: Снос кэша при смене генератора
if exist CMakeCache.txt (
    for /f "tokens=2 delims==" %%V in ('findstr /B /C:"CMAKE_GENERATOR:INTERNAL=" CMakeCache.txt 2^>nul') do set "CACHED_GEN=%%V"
    if defined CACHED_GEN if /I not "%CACHED_GEN%"=="%GEN%" (
        echo [INFO] Cached generator "%CACHED_GEN%" != "%GEN%". Wiping cache...
        del /q CMakeCache.txt >nul 2>&1
        if exist CMakeFiles rmdir /s /q CMakeFiles
    )
)

:: --- Конфигурация ---
echo [INFO] Configuring with "%GEN%" (x64)...
cmake .. -G "%GEN%" -A x64 ^
    %VCPKG_ARG% ^
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ^
    -DVLCRLIB_BUILD_EXAMPLES=ON ^
    -DVLCRLIB_ENABLE_VULKAN=ON
if errorlevel 1 (echo. & echo [ERROR] CMake configure failed. & exit /b 1)

:: --- Сборка ---
echo.
echo [INFO] Building (Release)...
cmake --build . --config Release --parallel
if errorlevel 1 (echo [ERROR] Build failed. & exit /b 1)

echo.
echo Done. Binaries under: %~dp0build\Release\
echo Shaders:            %~dp0build\shaders\
endlocal