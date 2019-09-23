#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright © 2011-2019 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl
#

param (
    [Parameter(Mandatory=$true)][string]$SourceRepository,
    [string]$Branch = "master",
    [Parameter(Mandatory=$true)][string]$Destination = "."
 )

function Export-Git
{
    if (-not (Test-Path -Path $Destination))
    {
        Write-Error "Output directory does not exists"
    }

    $TempDir = [System.IO.Path]::GetTempPath() + "orc_export-git"
    Write-Output "Using temporary directory: '$TempDir'"
    if (Test-Path -Path $TempDir)
    {
        Remove-Item -Force -Recurse -Path $TempDir
    }

    # Clone the repository
    $GitDir = "$TempDir\Source"
    New-Item -ItemType "directory" -Path $GitDir | Out-Null
    & git clone --recursive -b $Branch $SourceRepository $GitDir

    # Copy only required files
    $ExportDir = "$TempDir\Export"
    if (Test-Path -Path $ExportDir)
    {
        Remove-Item -Force -Recurse -Path $ExportDir
    }

    New-Item -ItemType "directory" -Path $ExportDir | Out-Null


    # Filter some path
    $AllItems = Get-ChildItem $GitDir -Recurse

    $ExcludeRegexList =
        "\.git$",
        "\.gitmodules$",
        "external\\XmlLite.*",
        "tools\\OrcBuild.*"

    $ExportedItems = `
        New-Object System.Collections.Generic.List[System.IO.FileSystemInfo]

    ForEach ($Item in $AllItems)
    {
        $Exported = 1

        ForEach ($ExcludeRegex in $ExcludeRegexList)
        {
            $Pattern = [Regex]::Escape($GitDir + "\") + $ExcludeRegex
            if ($Item.FullName -Match $Pattern)
            {
                $Exported = 0
                break
            }
        }

        if ($Exported)
        {
            $ExportedItems.Add($Item)
        }
    }

    # Copy files to export
    ForEach ($Item in $ExportedItems)
    {
        $OutputPath = `
            Join-Path $Destination $Item.FullName.Substring($GitDir.length)
        Copy-Item -Force $Item.FullName -Destination $OutputPath
    }

    # Add information about git commit and submodules
    Set-Location $GitDir
    $OrcInternalCommit = git describe --tag --abbrev=64 --always

    Set-Location $GitDir\external\vcpkg
    $VcpkgInternalCommit = git describe --tag --abbrev=64 --always
    $VcpkgMicrosoftCommit = git describe master --tag --abbrev=64 --always

    Write-Output "INTERNAL_ORC_REF=$OrcInternalCommit" | out-file -encoding utf8 "$Destination\VERSION.txt"
    Write-Output "INTERNAL_VCPKG_REF=$VcpkgInternalCommit" | out-file -Encoding utf8 -Append "$Destination\VERSION.txt"
    Write-Output "MICROSOFT_VCPKG_REF=$VcpkgMicrosoftCommit" | out-file -Encoding utf8 -Append "$Destination\VERSION.txt"

    # Some files like "vcpkg\script\buildsystem\vcpkg.cmake" were missing
    Write-Output "WARNING: Remember that a .gitignore files is present!"
    Write-Output "Use: 'git add -f --all' to stage files for a commit"
}

$ErrorActionPreference = "Stop"

try {
    Push-Location
    Export-Git
} finally {
    Pop-Location
}
