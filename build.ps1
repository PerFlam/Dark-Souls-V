$ErrorActionPreference = "Stop"

Push-Location $PSScriptRoot

$buildDir = Join-Path "." "build"
if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

$output = Join-Path "build" "Dark Souls V.exe"

g++ -std=c++17 -O2 -mwindows `
    "src/main.cpp" `
    -lgdi32 `
    -lgdiplus `
    -o $output

Write-Host "Built:" $output

Pop-Location
