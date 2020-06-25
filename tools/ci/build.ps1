function Build-Orc
{
    <#
    .SYNOPSIS
        Build wrapper around cmake to facilitate CI integration.

    .PARAMETER Source
        Path to DFIR-ORC source root directory to build.

    .PARAMETER BuildDirectory
        Output directory. Must be a subdirectory of Path. Relative path will be treated as relative to $Path.
        Default value: '$Source/build'.

    .PARAMETER Output
        Build artifacts output directory

    .PARAMETER Architecture
        Target architecture (x86, x64).

    .PARAMETER Configuration
        Target configuration (Debug, MinSizeRel, Release, RelWithDebInfo).

    .PARAMETER Runtime
        Target runtime (static, dynamic). Default value: 'static'.

    .PARAMETER Clean
        Clean build directory

    .OUTPUTS
        None or error on failure.

    .EXAMPLE
        Build DFIR-Orc in 'F:\dfir-orc\build-x64' and place artefacts in 'F:\dfir-orc\build-x64\Artefacts'

        . F:\Orc\tools\ci\build.ps1
        Build-Orc -Path F:\dfir-orc -Clean -Output build-x64 -Configuration Debug,MinSizeRel -Architecture x64 -Runtime static
    #>

    [cmdletbinding()]
    Param (
        [Parameter(Mandatory = $True)]
        [ValidateNotNullOrEmpty()]
        [System.IO.DirectoryInfo]
        $Source,
        [Parameter(Mandatory = $False)]
        [ValidateNotNullOrEmpty()]
        [System.IO.DirectoryInfo]
        $BuildDirectory = "$Source/build",
        [Parameter(Mandatory = $False)]
        [ValidateNotNullOrEmpty()]
        [System.IO.DirectoryInfo]
        $Output,
        [Parameter(Mandatory = $True)]
        [ValidateSet('x86', 'x64')]
        [String]
        $Architecture,
        [Parameter(Mandatory = $False)]
        [ValidateSet('vs2017', 'vs2019')]
        [String]
        $Toolchain = 'vs2019',
        [Parameter(Mandatory = $True)]
        [ValidateSet('Debug', 'MinSizeRel', 'RelWithDebInfo')]
        [String[]]
        $Configuration,
        [Parameter(Mandatory = $False)]
        [ValidateSet('static', 'dynamic')]
        [String]
        $Runtime = 'static',
        [Parameter(Mandatory = $False)]
        [switch]
        $Clean
    )

    $OrcPath = (Resolve-Path -Path $Source).Path

    # Map cmake architecture option against $Architecture
    $CMakeArch = @{
        "x86" = "Win32";
        "x64" = "x64";
    }

    if(-not [System.IO.Path]::IsPathRooted($BuildDirectory))
    {
        $BuildDirectory = "$OrcPath/$BuildDirectory"
    }

    if(-not [System.IO.Path]::IsPathRooted($Output))
    {
        $Output = "$OrcPath/$Output"
    }

    $BuildDir = "$BuildDirectory/$Architecture"

    $Generators = @{
        "vs2017_x86" = @(("-G", "`"Visual Studio 15 2017`""))
        "vs2017_x64" = @(("-G", "`"Visual Studio 15 2017 Win64`""))
        "vs2019_x86" = @(("-G", "`"Visual Studio 16 2019`""), ("-A", "Win32"))
        "vs2019_x64" = @(("-G", "`"Visual Studio 16 2019`""), ("-A", "x64"))
    }

    $Generator = $Generators[$Toolchain + "_" + $Architecture]

    if($Clean)
    {
        Remove-Item -Force -Recurse -Path $BuildDir -ErrorAction Ignore
    }

    New-Item -Force -ItemType Directory -Path $BuildDir | Out-Null

    Push-Location $BuildDir
    try
    {
        $CMakeExe = Find-CMake
        if(-not $CMakeExe)
        {
            Write-Error "Cannot find 'cmake.exe'"
            return
        }

        foreach($Config in $Configuration)
        {

            . $CMakeExe `
                @Generator `
                -T v141_xp `
                -DORC_BUILD_VCPKG=ON `
                -DVCPKG_TARGET_TRIPLET="${Architecture}-windows-static" `
                -DCMAKE_TOOLCHAIN_FILE="${OrcPath}\external\vcpkg\scripts\buildsystems\vcpkg.cmake" `
                $OrcPath

            . $CMakeExe --build . --config $Config -- -maxcpucount

            . $CMakeExe --install . --prefix $Output --config $Config
        }
    }
    finally
    {
        Pop-Location
    }
}

function Find-CMake
{
    $CMakeExe = Get-Command "cmake.exe" -ErrorAction SilentlyContinue
    if($CMakeExe)
    {
        return $CMakeExe
    }

    $Locations = @(
        "c:\Program Files (x86)\Microsoft Visual Studio\2019\*\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        "c:\Program Files (x86)\Microsoft Visual Studio\*\*\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    )

    foreach($Location in $Locations)
    {
        $Path = Get-Item -Path $Location
        if($Path)
        {
            return $Path
        }
    }
}
