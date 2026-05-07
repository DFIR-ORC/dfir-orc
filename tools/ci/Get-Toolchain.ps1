#Requires -Version 5.1
<#
.SYNOPSIS
    Download or install the full toolchain (VS Build Tools, VCRedist, Git).

.DESCRIPTION
    Download mode : downloads the VS Build Tools layout, the code-signing
                    certificate, VCRedist and Git, then copies this script into
                    OutputDir ready for transfer to an air-gapped host.

    Install mode  : installs VS Build Tools, VCRedist and Git from the local
                    files. Requires an elevated session.

.EXAMPLE
    # Internet-connected machine:
    .\Get-Toolchain.ps1 -OutputDir .\toolchain

    # Air-gapped host (from the transferred directory):
    .\Get-Toolchain.ps1 -Install
#>
[CmdletBinding(DefaultParameterSetName = "Download")]
param (
    [Parameter(ParameterSetName = "Download", Mandatory,
               HelpMessage = "Output directory (created if absent).")]
    [ValidateNotNullOrEmpty()]
    [string] $OutputDir,

    [Parameter(ParameterSetName = "Install", Mandatory,
               HelpMessage = "Install the toolchain from a local layout.")]
    [switch] $Install,

    [Parameter(ParameterSetName = "Download",
               HelpMessage = "Suppress the final transfer instructions.")]
    [switch] $NoFooter
)

Set-StrictMode -Version Latest

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

$VS_COMPONENTS = @(
    "Microsoft.VisualStudio.Workload.VCTools",
    "Microsoft.VisualStudio.Component.VC.CMake.Project",
    "Microsoft.VisualStudio.Component.VC.v141.x86.x64",
    "Microsoft.VisualStudio.Component.VC.v141.ATL",
    "Microsoft.VisualStudio.Component.WinXP",
    "Microsoft.VisualStudio.Component.Windows11SDK.22621"
)

$VCREDIST_NAME = "vc_redist.x86.exe"
$GIT_NAME = "Git-Installer.exe"

# ---------------------------------------------------------------------------
# Shared helpers
# ---------------------------------------------------------------------------

function ConvertTo-QuotedArg {
    param ([Parameter(Mandatory)] [string] $Arg)
    if ($Arg -notmatch '[ "\t]') { return $Arg }
    return '"' + $Arg.Replace('"', '\"') + '"'
}

function Write-CommandLine {
    param (
        [Parameter(Mandatory)] [string]   $FilePath,
        [Parameter(Mandatory)] [string[]] $ArgumentList
    )
    $indent = "    "
    Write-Host "  & $(ConvertTo-QuotedArg $FilePath) ``"

    $i = 0
    while ($i -lt $ArgumentList.Count) {
        $arg      = $ArgumentList[$i]
        $hasValue = ($i + 1 -lt $ArgumentList.Count) -and ($ArgumentList[$i + 1] -notmatch '^[-/]')
        $advance  = if ($hasValue) { 1 } else { 0 }
        $isLast   = ($i + $advance) -ge ($ArgumentList.Count - 1)
        $tail     = if (-not $isLast) { " ``" } else { "" }

        if ($hasValue) {
            Write-Host "$indent$arg $(ConvertTo-QuotedArg $ArgumentList[$i + 1])$tail"
            $i += 2
        } else {
            Write-Host "$indent$(ConvertTo-QuotedArg $arg)$tail"
            $i += 1
        }
    }
    Write-Host ""
}

function Invoke-NativeProcess {
    param (
        [Parameter(Mandatory)] [string]   $FilePath,
        [Parameter(Mandatory)] [string[]] $ArgumentList
    )
    $argString = ($ArgumentList | ForEach-Object { ConvertTo-QuotedArg $_ }) -join ' '

    $psi                       = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName              = $FilePath
    $psi.Arguments             = $argString
    $psi.UseShellExecute       = $false
    $psi.RedirectStandardInput = $true

    $proc = [System.Diagnostics.Process]::Start($psi)
    if ($null -eq $proc) {
        Write-Error "Failed to start process: $FilePath"
        return -1
    }
    $proc.StandardInput.Close()
    $proc.WaitForExit()

    return $proc.ExitCode
}

# ---------------------------------------------------------------------------
# Download mode
# ---------------------------------------------------------------------------

function New-DirectoryIfAbsent {
    param ([Parameter(Mandatory)] [string] $Path)
    if (Test-Path -LiteralPath $Path -PathType Container) { return $true }
    try {
        New-Item -ItemType Directory -Path $Path | Out-Null
        Write-Host "  Created : $Path"
        return $true
    } catch {
        Write-Error "Cannot create directory '$Path': $_"
        return $false
    }
}

function Invoke-Download {
    param (
        [Parameter(Mandatory)] [string] $Uri,
        [Parameter(Mandatory)] [string] $OutFile,
        [Parameter(Mandatory)] [string] $Description
    )
    Write-Host ""
    Write-Host "Downloading $Description ..."
    Write-Host "  From : $Uri"
    Write-Host "  To   : $OutFile"
    try {
        curl.exe -L $Uri -o $OutFile
        return $true
    } catch {
        Write-Error "Failed to download ${Description}: $_"
        return $false
    }
}

function Invoke-DownloadMode {
    $resolvedOutput     = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)
    $vcRedistDest       = Join-Path $resolvedOutput $VCREDIST_NAME
    $gitDest            = Join-Path $resolvedOutput $GIT_NAME
    $vsBuildToolsScript = Join-Path (Split-Path -Parent $PSCommandPath) "Get-VsBuildTools.ps1"

    Write-Host "Output directory : $resolvedOutput"

    if (-not (New-DirectoryIfAbsent $resolvedOutput)) { return $false }

    # VS Build Tools layout (Get-VsBuildTools.ps1 copies itself into OutputDir)
    & $vsBuildToolsScript -OutputDir $OutputDir -NoFooter -Add $VS_COMPONENTS

    $VCREDIST_URL  = "https://aka.ms/vc14/vc_redist.x86.exe"
    if (-not (Invoke-Download $VCREDIST_URL $vcRedistDest "VCRedist x86"))    { return $false }

    $GIT_URL  = (Invoke-RestMethod https://api.github.com/repos/git-for-windows/git/releases/latest).assets `
    | Where-Object { $_.browser_download_url -match '64-bit.exe$' } `
    | ForEach-Object { $_.browser_download_url }
    if (-not (Invoke-Download $GIT_URL      $gitDest      "Git for Windows")) { return $false }

    Copy-Item -LiteralPath $PSCommandPath -Destination $resolvedOutput -Force

    if (-not $NoFooter) {
        Write-Host ""
        Write-Host "Toolchain ready. Transfer '$resolvedOutput' to the air-gapped host and run:"
        Write-Host "  .\$(Split-Path -Leaf $PSCommandPath) -Install"
    }
    return $true
}

# ---------------------------------------------------------------------------
# Install mode
# ---------------------------------------------------------------------------

function Assert-Administrator {
    $identity  = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal $identity
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Write-Error "Install mode requires an elevated (Administrator) session."
        return $false
    }
    return $true
}

function Install-VCRedist {
    param ([Parameter(Mandatory)] [string] $Installer)
    Write-Host ""
    Write-Host "Installing VC Redistributable (x86) ..."

    $procArgs = @("/quiet", "/norestart")
    Write-CommandLine $Installer $procArgs

    $exitCode = Invoke-NativeProcess $Installer $procArgs
    if ($exitCode -ne 0) {
        Write-Error "vc_redist.x86.exe exited with code $exitCode"
        return $false
    }
    return $true
}

function Install-Git {
    param ([Parameter(Mandatory)] [string] $Installer)
    Write-Host ""
    Write-Host "Installing Git for Windows ..."

    $procArgs = @("/VERYSILENT", "/NORESTART", "/SUPPRESSMSGBOXES", "/SP-")
    Write-CommandLine $Installer $procArgs

    $exitCode = Invoke-NativeProcess $Installer $procArgs
    if ($exitCode -ne 0) {
        Write-Error "Git installer exited with code $exitCode"
        return $false
    }
    return $true
}

function Invoke-InstallMode {
    $scriptDir          = Split-Path -Parent $PSCommandPath
    $vsBuildToolsScript = Join-Path $scriptDir "Get-VsBuildTools.ps1"
    $vcRedist           = Join-Path $scriptDir $VCREDIST_NAME
    $gitInstaller       = Join-Path $scriptDir $GIT_NAME

    if (-not (Assert-Administrator)) { return $false }

    if (-not (Test-Path -LiteralPath $vsBuildToolsScript)) {
        Write-Error "VS Build Tools script not found: $vsBuildToolsScript"; return $false
    }
    if (-not (Test-Path -LiteralPath $vcRedist)) {
        Write-Error "VCRedist installer not found: $vcRedist"; return $false
    }
    if (-not (Test-Path -LiteralPath $gitInstaller)) {
        Write-Error "Git installer not found: $gitInstaller"; return $false
    }

    Write-Host "Step 1: Visual Studio Build Tools"
    $psArgs = @("-ExecutionPolicy", "Bypass", "-File", $vsBuildToolsScript, "-Install")
    Write-CommandLine "powershell.exe" $psArgs
    $exitCode = Invoke-NativeProcess "powershell.exe" $psArgs
    if ($exitCode -ne 0) { Write-Host "Exit code: $exitCode" }

    if (-not (Install-VCRedist $vcRedist))    { return $false }
    if (-not (Install-Git      $gitInstaller)) { return $false }

    Write-Host ""
    Write-Host "Done."
    return $true
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

$ok = if ($Install) { Invoke-InstallMode } else { Invoke-DownloadMode }
if (-not $ok) { exit 1 }
