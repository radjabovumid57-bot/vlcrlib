#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Build the vlcrlib language bindings and run their smoke tests.

.DESCRIPTION
    Configures and builds build-bindings (vlcr_ffi, vlcr_jni, the C++ example),
    stages the native libraries plus the compiled shaders into bindings/dist,
    builds the C# and Java packages, then runs one smoke test per language.

.PARAMETER Configuration
    CMake/dotnet configuration: Release (default) or Debug.

.PARAMETER BuildDir
    CMake build directory (default: build-bindings).

.PARAMETER Cpu
    Configure the GPU-free CPU backend (no Vulkan). Audio is compiled out.

.PARAMETER SkipTests
    Only build; do not run the smoke tests.

.PARAMETER SkipDotnet
    Skip the C# package (no .NET SDK installed).

.PARAMETER SkipJava
    Skip the Java package (no JDK installed).

.EXAMPLE
    scripts/build_bindings.ps1
    scripts/build_bindings.ps1 -Configuration Debug -SkipJava
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [string]$BuildDir = 'build-bindings',
    [switch]$Cpu,
    [switch]$SkipTests,
    [switch]$SkipDotnet,
    [switch]$SkipJava
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $root $BuildDir
$dist = Join-Path $root 'bindings\dist'
$results = [System.Collections.Generic.List[object]]::new()

function Write-Step([string]$message) {
    Write-Host ""
    Write-Host "==> $message" -ForegroundColor Cyan
}

function Add-Result([string]$name, [bool]$ok, [string]$detail = '') {
    $results.Add([pscustomobject]@{ Name = $name; Ok = $ok; Detail = $detail })
    $color = if ($ok) { 'Green' } else { 'Red' }
    $mark = if ($ok) { 'PASS' } else { 'FAIL' }
    Write-Host ("  [{0}] {1} {2}" -f $mark, $name, $detail) -ForegroundColor $color
}

function Require-Command([string]$name) {
    $command = Get-Command $name -ErrorAction SilentlyContinue
    if (-not $command) {
        throw "required tool '$name' was not found on PATH"
    }
    return $command.Source
}

# ------------------------------------------------------------------------------
# Configure
# ------------------------------------------------------------------------------
Write-Step "Configuring $BuildDir"

$cmake = Require-Command cmake
$configureArgs = @(
    '-S', $root,
    '-B', $buildPath,
    '-DVLCRLIB_BUILD_BINDINGS=ON',
    '-DVLCRLIB_BUILD_EXAMPLES=OFF',
    '-DVLCRLIB_BUILD_TESTS=OFF',
    '-DVLCRLIB_BUILD_TOOLS=OFF',
    '-DVLCRLIB_ENABLE_VLC=OFF',
    '-DVLCRLIB_BUILD_BINDING_EXAMPLES=ON'
)

# Prefer the repository's pre-installed vcpkg tree; fall back to a vcpkg root.
$vcpkgPrefix = Join-Path $root 'vcpkg_installed\x64-windows'
$vcpkgToolchain = Join-Path $root 'vcpkg_installed\vcpkg\scripts\buildsystems\vcpkg.cmake'
if (Test-Path $vcpkgPrefix) {
    $configureArgs += '-DVLCRLIB_SKIP_VCPKG=ON'
    $configureArgs += "-DCMAKE_PREFIX_PATH=$vcpkgPrefix"
    $configureArgs += "-DSDL3_DIR=$vcpkgPrefix\share\sdl3"
} elseif ($env:VCPKG_ROOT -and (Test-Path "$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake")) {
    $configureArgs += "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
} elseif (Test-Path 'C:\vcpkg\scripts\buildsystems\vcpkg.cmake') {
    $configureArgs += '-DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake'
} elseif (Test-Path $vcpkgToolchain) {
    $configureArgs += "-DCMAKE_TOOLCHAIN_FILE=$vcpkgToolchain"
}

if ($Cpu) {
    $configureArgs += '-DVLCRLIB_GRAPHICS_CPU=ON'
}

if (-not (Test-Path (Join-Path $buildPath 'CMakeCache.txt'))) {
    & $cmake @configureArgs
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
} else {
    Write-Host "  (existing cache in $buildPath)"
}

# ------------------------------------------------------------------------------
# Build native libraries
# ------------------------------------------------------------------------------
Write-Step "Building native libraries"

& $cmake --build $buildPath --config $Configuration --target vlcr_ffi
if ($LASTEXITCODE -ne 0) { throw 'building vlcr_ffi failed' }

$jniBuilt = $false
if (-not $SkipJava) {
    & $cmake --build $buildPath --config $Configuration --target vlcr_jni
    $jniBuilt = ($LASTEXITCODE -eq 0)
    if (-not $jniBuilt) {
        Write-Warning 'vlcr_jni was not built (is a JDK available? set JAVA_HOME)'
    }
}

$cppBuilt = $false
& $cmake --build $buildPath --config $Configuration --target vlcr_cpp_hello
$cppBuilt = ($LASTEXITCODE -eq 0)

# ------------------------------------------------------------------------------
# Stage runtime files
# ------------------------------------------------------------------------------
Write-Step "Staging bindings/dist"

New-Item -ItemType Directory -Force -Path $dist | Out-Null

$nativeCandidates = @(
    (Join-Path $buildPath "bindings\$Configuration"),
    (Join-Path $buildPath 'bindings')
)
$nativeDir = $nativeCandidates | Where-Object { Test-Path (Join-Path $_ 'vlcr_ffi.dll') } | Select-Object -First 1
if (-not $nativeDir) {
    $nativeDir = $nativeCandidates | Where-Object {
        (Test-Path (Join-Path $_ 'libvlcr_ffi.so')) -or (Test-Path (Join-Path $_ 'libvlcr_ffi.dylib'))
    } | Select-Object -First 1
}
if (-not $nativeDir) { throw "cannot find the built vlcr_ffi library under $buildPath" }

Get-ChildItem $nativeDir -File |
    Where-Object { $_.Extension -in '.dll', '.so', '.dylib' } |
    Copy-Item -Destination $dist -Force

# Runtime dependencies (SDL3, Vulkan loader) and the compiled shaders.
$vcpkgBin = Join-Path $root 'vcpkg_installed\x64-windows\bin'
if (Test-Path $vcpkgBin) {
    foreach ($dll in 'SDL3.dll', 'vulkan-1.dll') {
        $source = Join-Path $vcpkgBin $dll
        if (Test-Path $source) { Copy-Item $source $dist -Force }
    }
}

$shaderCandidates = @(
    (Join-Path $buildPath "$Configuration\shaders"),
    (Join-Path $buildPath 'shaders')
)
$shaderDir = $shaderCandidates | Where-Object {
    Test-Path (Join-Path $_ '2d.vert.spv')
} | Select-Object -First 1
if ($shaderDir) {
    Copy-Item $shaderDir (Join-Path $dist 'shaders') -Recurse -Force
} else {
    Write-Warning "compiled shaders not found; run without -Cpu or copy shaders/ manually"
}

Write-Host "  native files -> $dist"

# The C++ example is an executable; dropping it in dist lets the smoke test run
# from the same working directory as everything else.
foreach ($name in 'vlcr_cpp_hello.exe', 'vlcr_cpp_hello') {
    $exe = Join-Path $nativeDir $name
    if (Test-Path $exe) { Copy-Item $exe $dist -Force }
}

# ------------------------------------------------------------------------------
# C#
# ------------------------------------------------------------------------------
$dotnetAvailable = (-not $SkipDotnet) -and (Get-Command dotnet -ErrorAction SilentlyContinue)
if ($dotnetAvailable) {
    Write-Step 'Building the C# package'
    if (-not $env:DOTNET_CLI_HOME) { $env:DOTNET_CLI_HOME = Join-Path $root '.dotnet-home' }
    $env:DOTNET_NOLOGO = '1'

    & dotnet build (Join-Path $root 'bindings\csharp\Vlcrlib\Vlcrlib.csproj') -c $Configuration --nologo
    if ($LASTEXITCODE -ne 0) { throw 'building the C# library failed' }

    & dotnet build (Join-Path $root 'bindings\csharp\tests\Smoke\Smoke.csproj') -c $Configuration --nologo
    if ($LASTEXITCODE -ne 0) { throw 'building the C# smoke test failed' }

    & dotnet build (Join-Path $root 'bindings\csharp\examples\Hello\Hello.csproj') -c $Configuration --nologo
    if ($LASTEXITCODE -ne 0) { throw 'building the C# example failed' }
}

# ------------------------------------------------------------------------------
# Java
# ------------------------------------------------------------------------------
$javacAvailable = (-not $SkipJava) -and (Get-Command javac -ErrorAction SilentlyContinue)
$javaClasses = Join-Path $dist 'java\classes'
if ($javacAvailable) {
    Write-Step 'Compiling the Java package'
    New-Item -ItemType Directory -Force -Path $javaClasses | Out-Null

    $javaSources = @(
        Get-ChildItem (Join-Path $root 'bindings\java\src\main\java') -Recurse -Filter *.java
        Get-ChildItem (Join-Path $root 'bindings\java\examples') -Filter *.java
    ) | ForEach-Object FullName

    & javac -encoding UTF-8 -d $javaClasses @javaSources
    if ($LASTEXITCODE -ne 0) { throw 'compiling the Java package failed' }
}

# ------------------------------------------------------------------------------
# Smoke tests
# ------------------------------------------------------------------------------
# Native programs write progress to stderr; with $ErrorActionPreference = Stop
# PowerShell would treat that as a failure. Capture the output and only trust
# the exit code.
function Invoke-Native {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$Log
    )
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $FilePath @Arguments *> $Log
        return $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
}

if ($SkipTests) {
    Write-Step 'Skipping smoke tests (-SkipTests)'
} else {
    Write-Step 'Running smoke tests'
    Push-Location $dist
    try {
        # Python
        $python = Get-Command python -ErrorAction SilentlyContinue
        if ($python) {
            $env:PYTHONPATH = Join-Path $root 'bindings\python'
            $log = Join-Path $dist 'smoke-python.log'
            $code = Invoke-Native $python.Source @(
                (Join-Path $root 'bindings\python\tests\test_smoke.py')) $log
            Add-Result 'python' ($code -eq 0) $(if ($code -ne 0) { "see $log" } else { '' })
            Remove-Item Env:\PYTHONPATH -ErrorAction SilentlyContinue
        } else {
            Add-Result 'python' $false 'python not found'
        }

        # C++
        if ($cppBuilt) {
            $env:VLCR_FRAMES = '10'
            $cppExe = Join-Path $dist 'vlcr_cpp_hello.exe'
            if (-not (Test-Path $cppExe)) { $cppExe = Join-Path $dist 'vlcr_cpp_hello' }
            $log = Join-Path $dist 'smoke-cpp.log'
            $code = Invoke-Native $cppExe @() $log
            Add-Result 'c++' ($code -eq 0) $(if ($code -ne 0) { "see $log" } else { '' })
            Remove-Item Env:\VLCR_FRAMES -ErrorAction SilentlyContinue
        }

        # C#
        if ($dotnetAvailable) {
            $smokeExe = Join-Path $root "bindings\csharp\tests\Smoke\bin\$Configuration\net8.0\Smoke.exe"
            if (-not (Test-Path $smokeExe)) {
                $smokeExe = Join-Path $root "bindings\csharp\tests\Smoke\bin\$Configuration\net8.0\Smoke"
            }
            $log = Join-Path $dist 'smoke-csharp.log'
            $code = Invoke-Native $smokeExe @() $log
            Add-Result 'c#' ($code -eq 0) $(if ($code -ne 0) { "see $log" } else { '' })
        }

        # Java
        if ($javacAvailable) {
            $log = Join-Path $dist 'smoke-java.log'
            $code = Invoke-Native 'java' @('-cp', $javaClasses, 'SmokeJava') $log
            Add-Result 'java' ($code -eq 0) $(if ($code -ne 0) { "see $log" } else { '' })
        }
    } finally {
        Pop-Location
    }
}

# ------------------------------------------------------------------------------
# Summary
# ------------------------------------------------------------------------------
Write-Step 'Summary'

$passed = @($results | Where-Object { $_.Ok })
$failed = @($results | Where-Object { -not $_.Ok })

Write-Host ("  {0} passed, {1} failed, {2} skipped" -f $passed.Count, $failed.Count,
                                             (@(4 - $passed.Count - $failed.Count) | Measure-Object -Sum).Sum)
if ($failed.Count -gt 0) {
    Write-Host ""
    Write-Host "Bindings built, but $($failed.Count) smoke test(s) failed:" -ForegroundColor Red
    foreach ($result in $failed) {
        Write-Host "  - $($result.Name) $($result.Detail)" -ForegroundColor Red
    }
    exit 1
}

Write-Host ""
Write-Host "Bindings ready in $dist" -ForegroundColor Green
exit 0
