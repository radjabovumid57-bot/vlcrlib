# cross_backend_golden.ps1 — Vulkan vs WebGPU golden-image comparison.
#
# Renders the same deterministic scene with the native Vulkan backend and the
# WebGPU backend (headless Chrome), then compares the two captures with vdiff.
# Both runs use example/cross_backend_example.c with all post-processing off.
# Chrome is driven over CDP (Node 22+ WebSocket) so the viewport is exactly
# Width x Height; --window-size alone gives a smaller canvas and a false diff.
param(
    [string]$BuildDir = "build-web",
    [string]$NativeBuildDir = "build-check",
    [string]$NativeExe = "build-check/example/Release/cross_backend_example.exe",
    [string]$Vdiff = "build-check/Release/vdiff.exe",
    [string]$VulkanShot = "docs/cross_backend_vulkan.png",
    [string]$WebShot = "docs/cross_backend_webgpu.png",
    [int]$Port = 8104,
    [string]$Chrome = "C:\Program Files\Google\Chrome\Application\chrome.exe",
    [string]$Node = "node",
    [int]$CdpPort = 9222,
    [int]$VirtualTimeMs = 20000,
    [float]$Threshold = 6.0,
    [float]$MaxOver = 5.0,
    [int]$Width = 960,
    [int]$Height = 540,
    [switch]$SkipNative,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
function FullPath([string]$p) {
    if ([System.IO.Path]::IsPathRooted($p)) { return $p }
    return Join-Path $root $p
}
$BuildDir = FullPath $BuildDir
$NativeBuildDir = FullPath $NativeBuildDir
$NativeExe = FullPath $NativeExe
$Vdiff = FullPath $Vdiff
$VulkanShot = FullPath $VulkanShot
$WebShot = FullPath $WebShot

if (-not $NoBuild) {
    if (-not (Get-Command emcmake -ErrorAction SilentlyContinue)) {
        throw "emcmake not found. Source C:\emsdk\emsdk_env.ps1 first."
    }
    & cmake --build $BuildDir --target cross_backend_example
    if ($LASTEXITCODE -ne 0) { throw "Web cross_backend_example build failed." }
    & cmake --build $NativeBuildDir --target cross_backend_example --config Release
    if ($LASTEXITCODE -ne 0) { throw "Native cross_backend_example build failed." }
}

if (-not $SkipNative) {
    Remove-Item $VulkanShot -ErrorAction SilentlyContinue
    Push-Location (Split-Path -Parent $NativeExe)
    try {
        & $NativeExe $VulkanShot
        if ($LASTEXITCODE -ne 0) { throw "Native capture failed ($LASTEXITCODE)." }
    } finally { Pop-Location }
    if (-not (Test-Path $VulkanShot)) { throw "Missing $VulkanShot." }
}

$page = Join-Path $BuildDir "example/cross_backend_example.html"
if (-not (Test-Path $page)) { throw "Missing $page." }
if (-not (Test-Path $Chrome)) { throw "Chrome not found: $Chrome" }
$python = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $python) { throw "python not found." }

$server = Start-Process -FilePath $python `
    -ArgumentList @((Join-Path $root "web/serve_web.py"), (Join-Path $BuildDir "example"), "$Port") `
    -PassThru -WindowStyle Hidden
$profile = Join-Path $env:TEMP ("vlcr-cross-profile-" + [guid]::NewGuid().ToString("N"))
$log = Join-Path $env:TEMP "vlcr-cross-golden.log"
New-Item -ItemType Directory -Force -Path $profile | Out-Null
Remove-Item $log, $WebShot -ErrorAction SilentlyContinue

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

    $url = "http://127.0.0.1:$Port/cross_backend_example.html"
    $chromeArgs = @(
        "--headless=new",
        "--no-sandbox",
        "--disable-gpu-sandbox",
        "--enable-unsafe-webgpu",
        "--enable-unsafe-swiftshader",
        "--use-angle=d3d11",
        "--ignore-gpu-blocklist",
        "--disable-dev-shm-usage",
        "--hide-scrollbars",
        "--force-device-scale-factor=1",
        "--window-size=$Width,$Height",
        "--user-data-dir=$profile",
        "--remote-debugging-port=$CdpPort",
        "--enable-logging=stderr",
        "--v=1",
        "about:blank"
    )
    $chromeProc = Start-Process -FilePath $Chrome -ArgumentList $chromeArgs `
        -PassThru -RedirectStandardError $log -RedirectStandardOutput ($log + ".out") `
        -WindowStyle Hidden
    try {
        $cdp = Join-Path $PSScriptRoot "cdp_capture.mjs"
        $cdpOut = & $Node $cdp $url $WebShot $Width $Height $CdpPort 2>&1
        $text = ($cdpOut | Out-String)
        if (Test-Path $log) { $text += Get-Content $log -Raw }
        if (Test-Path ($log + ".out")) { $text += Get-Content ($log + ".out") -Raw }
    } finally {
        if ($chromeProc -and -not $chromeProc.HasExited) { Stop-Process -Id $chromeProc.Id -Force }
    }
    $match = [regex]::Match($text, "\[cross-golden\]\s+frames=(\d+)\s+shot=(\d+)\s+errors=(\d+)")
    if (-not $match.Success) {
        Write-Host "--- Chrome log tail ---"
        $text -split "`n" | Select-Object -Last 40
        throw "Cross-backend result not found in Chrome output."
    }
    $frames = [int]$match.Groups[1].Value
    $shot = [int]$match.Groups[2].Value
    $errors = [int]$match.Groups[3].Value
    Write-Host "WebGPU cross capture: frames=$frames shot=$shot errors=$errors"
    if ($frames -le 0 -or $shot -ne 1 -or $errors -ne 0) { throw "WebGPU capture failed (errors=$errors)." }
    if (-not (Test-Path $WebShot)) { throw "Missing $WebShot." }
} finally {
    if ($server -and -not $server.HasExited) { Stop-Process -Id $server.Id -Force }
    Remove-Item $profile -Recurse -Force -ErrorAction SilentlyContinue
}

if (-not (Test-Path $Vdiff)) { throw "Missing vdiff: $Vdiff" }
& $Vdiff $VulkanShot $WebShot --threshold $Threshold --max-over $MaxOver
if ($LASTEXITCODE -ne 0) { throw "Cross-backend golden mismatch." }
Write-Host "Cross-backend golden: OK (threshold=$Threshold max-over=$MaxOver%)"