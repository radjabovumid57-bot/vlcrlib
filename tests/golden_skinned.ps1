param(
    [string]$BuildDir = "build-check"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
$exe = Join-Path $build "example\Release\golden_skinned_capture.exe"
$vdiff = Join-Path $build "Release\vdiff.exe"
$asset = Join-Path $build "example\Release\skinned_anim.glb"
$baseline = Join-Path $root "docs\images\golden_skinned.png"
$actual = Join-Path $build "example\Release\golden_skinned_actual.png"

if (!(Test-Path $exe)) { throw "missing $exe" }
if (!(Test-Path $vdiff)) { throw "missing $vdiff" }
if (!(Test-Path $asset)) {
    Copy-Item (Join-Path $root "assets\skinned_anim.glb") $asset -Force
}
& $exe $actual $asset
if ($LASTEXITCODE -ne 0) { throw "capture failed" }
& $vdiff $baseline $actual --threshold 2
if ($LASTEXITCODE -ne 0) { throw "golden mismatch" }
Write-Host "golden_skinned: OK"
