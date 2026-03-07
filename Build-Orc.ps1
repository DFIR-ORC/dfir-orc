<#
.SYNOPSIS
    Build DFIR-Orc and package it using OrcCapsule.

.PARAMETER Target
    CMake build configuration to use. One of: Debug, MinSizeRel, RelWithDebInfo.

.PARAMETER BuildDir
    Directory where CMake places build artifacts. Defaults to '.\build' relative to the current directory.

.PARAMETER Output
    Path of the packaged output file. Defaults to 'DFIR-Orc.exe' in the current directory.

.PARAMETER Force
    Overwrite the output file if it already exists.

.EXAMPLE
    .\build.ps1 -Target RelWithDebInfo -BuildDir C:\work\build -Output MyOrc.exe -Force
#>
[CmdletBinding()]
param(
    [Parameter()]
    [ValidateSet('Debug', 'MinSizeRel', 'RelWithDebInfo')]
    [string] $Target = 'MinSizeRel',

    [Parameter()]
    [string] $BuildDir = '.\build',

    [Parameter()]
    [string] $Output = 'DFIR-Orc.exe',

    [Parameter()]
    [switch] $Force
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Resolve paths to absolute (always relative to $PWD, as the user expects)
# ---------------------------------------------------------------------------

$Output   = [IO.Path]::GetFullPath($Output)
$BuildDir = [IO.Path]::GetFullPath($BuildDir)

$capsule = "$BuildDir\capsule-x86-xp\$Target\OrcCapsule.exe"
$orc64   = "$BuildDir\dfir-orc-x64-xp\$Target\DFIR-Orc_x64.exe"
$orc86   = "$BuildDir\dfir-orc-x86-xp\$Target\DFIR-Orc_x86.exe"

# ---------------------------------------------------------------------------
# Guard: output file
# ---------------------------------------------------------------------------

if (Test-Path $Output) {
    if (-not $Force) {
        throw "Output file '$Output' already exists. Use -Force to overwrite."
    }
    Write-Warning "Output file '$Output' exists and will be overwritten (-Force)."
}

# ---------------------------------------------------------------------------
# Configure and build
# ---------------------------------------------------------------------------

$presets = @('capsule-x86-xp', 'dfir-orc-x64-xp', 'dfir-orc-x86-xp')

foreach ($preset in $presets) {
    Write-Host ">>> cmake configure: $preset"
    cmake --preset $preset
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed for '$preset' (exit $LASTEXITCODE)" }

    Write-Host ">>> cmake build: $preset-$Target"
    cmake --build --preset "$preset-$Target"
    if ($LASTEXITCODE -ne 0) { throw "cmake build failed for '$preset-$Target' (exit $LASTEXITCODE)" }
}

# ---------------------------------------------------------------------------
# Guard: artifacts
# ---------------------------------------------------------------------------

foreach ($artifact in @($capsule, $orc64, $orc86)) {
    if (-not (Test-Path $artifact)) { throw "Expected artifact not found: '$artifact'" }
}

# ---------------------------------------------------------------------------
# Package
# ---------------------------------------------------------------------------

Write-Host ">>> Packaging -> $Output"
$cmdArgs = @(
    'capsule'
    'add'
    $orc64
    $orc86
    '--output'
    $Output
)

Write-Host "Executing: $capsule $($cmdArgs -join ' ')"
& $capsule @cmdArgs
if ($LASTEXITCODE -ne 0) { throw "OrcCapsule packaging failed (exit $LASTEXITCODE)" }

Write-Host "Done: $Output"
