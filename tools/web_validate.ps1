# web_validate.ps1 — build and run the WebGPU runtime validation page.
#
# Requires Emscripten on PATH (or C:\emsdk\emsdk_env.ps1 already sourced),
# Python 3, and Chrome/Edge with WebGPU enabled. The page embeds JS+wasm in the
# HTML (SINGLE_FILE) so endpoint security cannot lock a separate .js artifact.
param(
    [string]$BuildDir = "build-web",
    [int]$Port = 8103,
    [string]$Chrome = "C:\Program Files\Google\Chrome\Application\chrome.exe",
    [int]$VirtualTimeMs = 40000,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $root $BuildDir
}

if (-not $NoBuild) {
    if (-not (Get-Command emcmake -ErrorAction SilentlyContinue)) {
        throw "emcmake not found. Source C:\emsdk\emsdk_env.ps1 first."
    }
    if (-not (Test-Path (Join-Path $BuildDir "build.ninja"))) {
        & emcmake cmake -S $root -B $BuildDir -G Ninja `
            -DCMAKE_BUILD_TYPE=Release -DVLCRLIB_SKIP_VCPKG=ON `
            -DVLCRLIB_BUILD_TOOLS=OFF
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }
    }
    & cmake --build $BuildDir --target web_validate_example
    if ($LASTEXITCODE -ne 0) { throw "web_validate_example build failed." }
}

$exampleDir = Join-Path $BuildDir "example"
$page = Join-Path $exampleDir "web_validate_example.html"
if (-not (Test-Path $page)) {
    throw "Missing $page (build target web_validate_example first)."
}
if (-not (Test-Path $Chrome)) {
    throw "Chrome not found: $Chrome"
}

$python = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $python) { throw "python not found." }

$server = Start-Process -FilePath $python `
    -ArgumentList @((Join-Path $root "web/serve_web.py"), $exampleDir, "$Port") `
    -PassThru -WindowStyle Hidden
$profile = Join-Path $env:TEMP ("vlcr-webgpu-profile-" + [guid]::NewGuid().ToString("N"))
$shot = Join-Path $root "docs/webgpu_validation.png"
$log = Join-Path $env:TEMP "vlcr-webgpu-validation.log"
New-Item -ItemType Directory -Force -Path $profile | Out-Null
Remove-Item $log, $shot -ErrorAction SilentlyContinue

try {
    $ready = $false
    for ($i = 0; $i -lt 50; $i++) {
        try {
            $client = [System.Net.Sockets.TcpClient]::new()
            $client.Connect("127.0.0.1", $Port)
            $client.Close()
            $ready = $true
            break
        } catch { Start-Sleep -Milliseconds 100 }
    }
    if (-not $ready) { throw "HTTP server did not start on port $Port." }

    $url = "http://127.0.0.1:$Port/web_validate_example.html"
    $chromeArgs = @(
        "--headless=new",
        "--no-sandbox",
        "--disable-gpu-sandbox",
        "--enable-unsafe-webgpu",
        "--enable-unsafe-swiftshader",
        "--use-angle=d3d11",
        "--ignore-gpu-blocklist",
        "--disable-dev-shm-usage",
        "--user-data-dir=$profile",
        "--virtual-time-budget=$VirtualTimeMs",
        "--enable-logging=stderr",
        "--v=1",
        "--screenshot=$shot",
        $url
    )
    $chromeProc = Start-Process -FilePath $Chrome -ArgumentList $chromeArgs `
        -PassThru -RedirectStandardError $log -RedirectStandardOutput ($log + ".out") `
        -WindowStyle Hidden
    if (-not $chromeProc.WaitForExit(90000)) {
        Stop-Process -Id $chromeProc.Id -Force
        throw "Chrome timed out."
    }

    $text = ""
    if (Test-Path $log) { $text += Get-Content $log -Raw }
    if (Test-Path ($log + ".out")) { $text += Get-Content ($log + ".out") -Raw }
    $match = [regex]::Match($text, "\[webgpu-validation\]\s+frames=(\d+)\s+errors=(\d+)")
    if (-not $match.Success) {
        Write-Host "--- Chrome log tail ---"
        $text -split "`n" | Select-Object -Last 40
        if ($text -match "init-failed|No available adapters|Failed to query ID3D11Device") {
            throw "Chrome launched but exposed no WebGPU adapter on this host (see log). Run this on a GPU-enabled CI/desktop session."
        }
        throw "Validation result not found in Chrome output."
    }

    $frames = [int]$match.Groups[1].Value
    $errors = [int]$match.Groups[2].Value
    Write-Host "WebGPU validation: frames=$frames errors=$errors"
    if (Test-Path $shot) { Write-Host "Screenshot: $shot" }
    if ($errors -ne 0 -or $frames -le 0) { exit 1 }
}
finally {
    if ($server -and -not $server.HasExited) { Stop-Process -Id $server.Id -Force }
    Remove-Item $profile -Recurse -Force -ErrorAction SilentlyContinue
}
