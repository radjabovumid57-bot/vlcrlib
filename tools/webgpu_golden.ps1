# webgpu_golden.ps1 - deterministic WebGPU golden-image check.
#
# Runs the 120-frame validation page, then compares docs/webgpu_validation.png
# against docs/webgpu_golden.png with vdiff. This is a WebGPU self-regression
# baseline; it is not yet a Vulkan-vs-WebGPU cross-backend diff.
param(
    [string]$BuildDir = "build-web",
    [string]$Vdiff = "build-check/Release/vdiff.exe",
    [float]$Threshold = 2.0,
    [switch]$UpdateGolden,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $root $BuildDir
}
if (-not [System.IO.Path]::IsPathRooted($Vdiff)) {
    $Vdiff = Join-Path $root $Vdiff
}

$validate = Join-Path $PSScriptRoot "web_validate.ps1"
$actual = Join-Path $root "docs/webgpu_validation.png"
$golden = Join-Path $root "docs/webgpu_golden.png"

$validateArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $validate,
                 "-BuildDir", $BuildDir)
if ($NoBuild) { $validateArgs += "-NoBuild" }
$validated = $false
for ($attempt = 1; $attempt -le 2; ++$attempt) {
    & powershell.exe @validateArgs
    if ($LASTEXITCODE -eq 0) { $validated = $true; break }
    Write-Host "WebGPU validation attempt $attempt failed; retrying..."
    Start-Sleep -Seconds 1
}
if (-not $validated) { throw "WebGPU validation failed after 2 attempts." }
if (-not (Test-Path $actual)) { throw "Missing $actual." }

if ($UpdateGolden) {
    Copy-Item $actual $golden -Force
    Write-Host "Updated golden: $golden"
    exit 0
}
if (-not (Test-Path $golden)) {
    throw "Missing $golden. Run with -UpdateGolden once on a known-good GPU."
}
if (-not (Test-Path $Vdiff)) {
    throw "Missing vdiff: $Vdiff (build the desktop vdiff target first)."
}

& $Vdiff $golden $actual --threshold $Threshold
if ($LASTEXITCODE -ne 0) { throw "WebGPU golden mismatch." }
Write-Host "WebGPU golden: OK"
