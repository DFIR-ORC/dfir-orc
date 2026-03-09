<#
.SYNOPSIS
    Build DFIR-ORC and package it using OrcCapsule.

.PARAMETER Source
    Root of the DFIR-ORC source tree. Defaults to the current working directory.

.PARAMETER BuildConfig
    One or more CMake build configurations to compile.
    Accepted values: Debug, MinSizeRel, RelWithDebInfo.
    Defaults to MinSizeRel.

.PARAMETER Platform
    One or more target platforms to build. Each value must match a CMake
    preset of the form 'dfir-orc-<platform>' (e.g. dfir-orc-x64-xp).
    Defaults to x64-xp and x86-xp.

.PARAMETER BuildDir
    Directory that receives all cmake binary trees and packaged output.
    Defaults to .\build relative to the current working directory.

.PARAMETER ConfigureOnly
    Run cmake configure for all presets and then stop.
    Neither the build nor the packaging step is executed.

.PARAMETER BuildOnly
    Skip packaging; only run the cmake configure and build steps.

.PARAMETER ToolEmbed
    Run ToolEmbed on the packaged H to embed an additional tool and configuration.

.PARAMETER FastFind
    When set, also builds and packages FastFind.exe in addition to DFIR-ORC.exe.

.EXAMPLE
    .\Build-Orc.ps1 -BuildConfig Debug,MinSizeRel -Platform x64-xp,x86-xp,x64,x86

    Builds two configurations for both platforms in one pass, producing a Debug
    and a MinSizeRel package under .\build\.
#>
[CmdletBinding()]
param(
    [Parameter()]
    [string] $Source = $PWD,

    [Parameter()]
    [ValidateSet('Debug', 'MinSizeRel', 'RelWithDebInfo')]
    [string[]] $BuildConfig = @('MinSizeRel'),

    [Parameter()]
    [string[]] $Platform = @('x64-xp', 'x86-xp'),

    [Parameter()]
    [string] $BuildDir = '.\build',

    [Parameter()]
    [switch] $ConfigureOnly,

    [Parameter()]
    [switch] $BuildOnly,

    [Parameter()]
    [string] $ToolEmbed = '',

    [Parameter()]
    [switch] $FastFind
)

function Resolve-InputPath {
    param([string] $Path)

    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $PWD.Path $Path))
}

function Join-Paths {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory, ValueFromRemainingArguments)]
        [AllowNull()]
        [AllowEmptyString()]
        [string[]]$Parts
    )

    $Parts = $Parts | Where-Object { -not [string]::IsNullOrEmpty($_) }
    if (-not $Parts -or $Parts.Count -eq 0) {
        return $null
    }

    $result = $Parts[0]

    for ($i = 1; $i -lt $Parts.Count; $i++) {
        $result = [System.IO.Path]::Combine($result, $Parts[$i])
    }

    return $result
}

$ErrorActionPreference = 'Stop'
$initialLocation = $PWD.Path

$SourceDir = Resolve-InputPath $Source
$BuildDir = Resolve-InputPath $BuildDir

if ($ToolEmbed) {
    $ToolEmbed = Resolve-InputPath $ToolEmbed
}

Set-Location $SourceDir

try {

# ---------------------------------------------------------------------------
# Configure
# ---------------------------------------------------------------------------

$presets = @()
$presets += [pscustomobject]@{ Name = "capsule"; Platform = "x86-xp" ; BinaryDir = $null }
foreach ($p in $Platform) {
    $presets += [pscustomobject]@{Name = "dfir-orc"; Platform = $p ; BinaryDir = $null }
}

foreach ($preset in $presets) {
    $PresetName = "$($preset.Name)-$($preset.Platform)"
    $BinaryDir = Join-Path $BuildDir $PresetName
    Write-Host ">>> cmake configure: $PresetName, build: $BinaryDir"
    New-Item -ItemType Directory -Force -Path $BinaryDir | Out-Null

    $cmdArgs = @(
        '--preset', $PresetName,
        '-B', $BinaryDir
    )

    if ("Debug" -notin $BuildConfig) {
        $arch, $suffix = ($preset.Platform -replace '^([^ -]+)(.*)$','$1,$2') -split ',',2
        $cmdArgs += "-DVCPKG_TARGET_TRIPLET=$arch-windows-static$suffix-release"
    }

    Write-Host "Executing: cmake $($cmdArgs -join ' ')"
    & cmake @cmdArgs
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed for '$PresetName' (exit $LASTEXITCODE)" }
    $preset.BinaryDir = $BinaryDir
}

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

if ($ConfigureOnly) { return }

foreach ($config in $BuildConfig)
{
    foreach ($preset in $presets) {
        # Build preset are not supported by cmake and preset value overrides build directory
        $cmdArgs = @('--build', $preset.BinaryDir, '--config', $config)

        Write-Host "Executing: cmake $($cmdArgs -join ' ')"
        & cmake @cmdArgs
        if ($LASTEXITCODE -ne 0) { throw "cmake build failed for '$($Preset.Name)-$config' (exit $LASTEXITCODE)" }
    }
}

if ($BuildOnly) { return }

# ---------------------------------------------------------------------------
# Package
# ---------------------------------------------------------------------------

function New-OrcPackages {
param(
    [string] $Target,
    [string] $BuildRoot,
    [string[]] $Configs,
    [string[]] $TargetPlatforms
)
    $packages = [ordered]@{}
    foreach ($config in $BuildConfig)
    {
        $PackageOutput = Join-Paths $BuildDir $config "${Target}.exe"
        Write-Host ">>> Packaging -> $PackageOutput"

        $dir = Split-Path -Parent $PackageOutput
        New-Item -ItemType Directory -Path $dir -Force | Out-Null

        $cmdArgs = @(
            'capsule'
            'add'
        )

        foreach ($p in $Platform)
        {
            $arch = $p -replace '-xp$', ''
            $cmdArgs += Join-Paths $BuildDir dfir-orc-$p $config "${Target}_$arch.exe"
        }

        $cmdArgs += "--output"
        $cmdArgs += $PackageOutput
        $cmdArgs += '--force'

        $capsule = Join-Paths $BuildDir "capsule-x86-xp" $config "OrcCapsule.exe"
        Write-Host "Executing: $capsule $($cmdArgs -join ' ')"
        & $capsule @cmdArgs | Write-Host
        if ($LASTEXITCODE -ne 0) { throw "OrcCapsule packaging failed (exit $LASTEXITCODE)" }

        $packages[$config] = $PackageOutput
    }

    return $packages
}

$packages = [ordered]@{}
if ($FastFind)
{
    $packages["FastFind"] = New-OrcPackages -Target "FastFind" -BuildRoot $BuildDir
}

$packages["DFIR-ORC"] = New-OrcPackages -Target "DFIR-ORC" -BuildRoot $BuildDir

if (-not $ToolEmbed)
{
    $packages.GetEnumerator() | ForEach-Object {
        $target = $_.Key
        $_.Value.GetEnumerator() | ForEach-Object {
            [PSCustomObject]@{
                Target = $target
                Config = $_.Key
                Path   = $_.Value
            }
        }
    }

    return
}

# ---------------------------------------------------------------------------
# ToolEmbed
# ---------------------------------------------------------------------------

$embeds = [ordered]@{}
foreach ($config in $BuildConfig)
{
    if ($config -eq "MinSizeRel") { $suffix = "" } else { $suffix = "-$config" }

    $EmbedOutput = Join-Paths $BuildDir "DFIR-ORC$suffix.exe"
    Write-Host ">>> Embedding configuration -> $EmbedOutput"

    $dir = Split-Path -Parent $EmbedOutput
    New-Item -ItemType Directory -Path $dir -Force | Out-Null

    $cmdArgs = @(
        'ToolEmbed',
        "/embed=$ToolEmbed",
        "/out=$EmbedOutput",
        "/force")

    Write-Host "Executing: $($packages["DFIR-ORC"][$config]) $($cmdArgs -join ' ')"
    & $packages["DFIR-ORC"][$config] @cmdArgs
    if ($LASTEXITCODE -ne 0) { throw "ToolEmbed failed." }

    $embeds[$config] = $EmbedOutput
}

} finally {
    Set-Location $initialLocation
}

$embeds.GetEnumerator() | ForEach-Object {
    [PSCustomObject]@{
        Target = "DFIR-ORC"
        Config = $_.Key
        Path   = $_.Value
    }
}
