$ErrorActionPreference = "Stop"

$buildDir = Join-Path $PSScriptRoot "build"
if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

$output = Join-Path $buildDir "PulseHarbor.exe"

g++ -std=c++17 -O2 -mwindows `
    (Join-Path $PSScriptRoot "src\\main.cpp") `
    -lgdi32 `
    -o $output

Write-Host "Built:" $output
