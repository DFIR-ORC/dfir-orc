#Requires -Version 5.1
<#
.SYNOPSIS
    Download a VS Build Tools offline layout, or install from one.

.DESCRIPTION
    Download mode : downloads vs_buildtools.exe, runs the layout, fetches the
                    code-signing certificate, then copies this script into
                    OutputDir ready for transfer to an air-gapped host.

    Install mode  : installs the certificate then installs VS Build Tools from
                    the local layout. Requires an elevated session.

.EXAMPLE
    # Internet-connected machine:
    .\VSBuildTools.ps1 -OutputDir .\toolchain -Add @(
        "Microsoft.VisualStudio.Workload.VCTools",
        "Microsoft.VisualStudio.Component.VC.CMake.Project"
    )

    # Air-gapped host (from the transferred directory):
    .\VSBuildTools.ps1 -Install
#>
[CmdletBinding(DefaultParameterSetName = "Download")]
param (
    [Parameter(ParameterSetName = "Download", Mandatory,
               HelpMessage = "Output directory (created if absent).")]
    [ValidateNotNullOrEmpty()]
    [string] $OutputDir,

    [Parameter(ParameterSetName = "Download", Mandatory,
               HelpMessage = "VS workload/component IDs to include in the layout.")]
    [ValidateNotNullOrEmpty()]
    [string[]] $Add,

    [Parameter(ParameterSetName = "Install", Mandatory,
               HelpMessage = "Install VS Build Tools from a local layout.")]
    [switch] $Install,

    [Parameter(ParameterSetName = "Download",
               HelpMessage = "Suppress the final transfer instructions.")]
    [switch] $NoFooter
)

Set-StrictMode -Version Latest

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

$VS_BUILDTOOLS_URL  = "https://aka.ms/vs/17/release/vs_buildtools.exe"
$VS_BUILDTOOLS_NAME = "vs_buildtools.exe"

$CA_CERT_URL  = "https://www.microsoft.com/pkiops/certs/Microsoft%20Windows%20Code%20Signing%20PCA%202024.crt"
$CA_CERT_NAME = "Microsoft Windows Code Signing PCA 2024.crt"

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
        $hasValue = ($i + 1 -lt $ArgumentList.Count) -and ($ArgumentList[$i + 1] -notlike '--*')
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
        Invoke-WebRequest -Uri $Uri -OutFile $OutFile -UseBasicParsing
        return $true
    } catch {
        Write-Error "Failed to download ${Description}: $_"
        return $false
    }
}

function Invoke-VSLayout {
    param (
        [Parameter(Mandatory)] [string]   $Installer,
        [Parameter(Mandatory)] [string]   $LayoutDir,
        [Parameter(Mandatory)] [string[]] $Components
    )
    $addArgs = [System.Collections.Generic.List[string]]::new()
    foreach ($c in $Components) { $addArgs.Add("--add"); $addArgs.Add($c) }

    $layoutArgs = @(
        "--layout",   $LayoutDir,
        "--lang",     "en-US",
        "--norestart",
        "--passive",
        "--wait"
    ) + $addArgs.ToArray()

    Write-Host ""
    Write-Host "Running VS Build Tools layout ..."
    Write-CommandLine $Installer $layoutArgs

    $exitCode = Invoke-NativeProcess $Installer $layoutArgs
    if ($exitCode -ne 0) {
        Write-Error "vs_buildtools.exe layout exited with code $exitCode"
        return $false
    }
    return $true
}

function Invoke-DownloadMode {
    $resolvedOutput = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)
    $layoutDir      = Join-Path $resolvedOutput "vslayout"
    $installerTemp  = Join-Path $ENV:TEMP $VS_BUILDTOOLS_NAME
    $certDest       = Join-Path $resolvedOutput $CA_CERT_NAME
    $scriptDest     = Join-Path $resolvedOutput (Split-Path -Leaf $PSCommandPath)

    Write-Host "Output directory : $resolvedOutput"

    if (-not (New-DirectoryIfAbsent $resolvedOutput))                                              { return $false }
    if (-not (Invoke-Download $VS_BUILDTOOLS_URL $installerTemp "VS Build Tools bootstrapper"))    { return $false }

    try {
        if (-not (Invoke-VSLayout $installerTemp $layoutDir $Add))                                 { return $false }
    } finally {
        Remove-Item -LiteralPath $installerTemp -ErrorAction SilentlyContinue
    }

    if (-not (Invoke-Download $CA_CERT_URL $certDest "Microsoft Windows Code Signing PCA 2024"))   { return $false }

    Copy-Item -LiteralPath $PSCommandPath -Destination $scriptDest -Force
    Write-Host "  Copied script : $scriptDest"

    if (-not $NoFooter) {
        Write-Host ""
        Write-Host "Layout ready. Transfer '$resolvedOutput' to the air-gapped host and run:"
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

function Install-Certificate {
    param ([Parameter(Mandatory)] [string] $CertPath)
    Write-Host "Installing certificate: $(Split-Path -Leaf $CertPath) ..."

    $certArgs = @("-addstore", "-f", "CA", $CertPath)
    Write-CommandLine "certutil" $certArgs

    $exitCode = Invoke-NativeProcess "certutil" $certArgs
    if ($exitCode -ne 0) {
        Write-Error "certutil failed with exit code $exitCode"
        return $false
    }
    return $true
}

function Install-VSBuildTools {
    param (
        [Parameter(Mandatory)] [string] $Installer,
        [Parameter(Mandatory)] [string] $Response,
        [Parameter(Mandatory)] [string] $LayoutPath
    )
    Write-Host ""
    Write-Host "Installing Visual Studio Build Tools ..."

    $installArgs = @(
        "install",
        "--in",         $Response,
        "--noweb",
        "--norestart",
        "--passive",
        "--wait"
    )
    Write-CommandLine $Installer $installArgs

    $exitCode = Invoke-NativeProcess $Installer $installArgs
    if ($exitCode -ne 0) {
        Write-Error "vs_buildtools.exe install exited with code $exitCode"
        return $false
    }
    return $true
}

function Install-OptionalCACerts {
    param ([Parameter(Mandatory)] [string] $CertDir)
    if (-not (Test-Path -LiteralPath $CertDir -PathType Container)) { return }

    $certs = Get-ChildItem -LiteralPath $CertDir -Filter "*.cer"
    if ($certs.Count -eq 0) { return }

    Write-Host ""
    Write-Host "Installing optional CA certificates from layout ..."
    foreach ($cert in $certs) {
        $certArgs = @("-addstore", "-f", "CA", $cert.FullName)
        Write-CommandLine "certutil" $certArgs
        $exitCode = Invoke-NativeProcess "certutil" $certArgs
        if ($exitCode -ne 0) {
            Write-Warning "certutil failed for '$($cert.Name)' with exit code $exitCode"
        }
    }
}

function Invoke-InstallMode {
    $scriptDir  = Split-Path -Parent $PSCommandPath
    $certFile   = Join-Path $scriptDir $CA_CERT_NAME
    $layoutDir  = Join-Path $scriptDir "vslayout"
    $installer  = Join-Path $layoutDir $VS_BUILDTOOLS_NAME
    $response   = Join-Path $layoutDir "Response.json"
    $extraCerts = Join-Path $layoutDir "certificates"

    if (-not (Assert-Administrator))                                              { return $false }
    if (-not (Test-Path -LiteralPath $certFile))   { Write-Error "Certificate not found: $certFile";   return $false }
    if (-not (Test-Path -LiteralPath $installer))  { Write-Error "Installer not found: $installer";    return $false }
    if (-not (Test-Path -LiteralPath $response))   { Write-Error "Response file not found: $response"; return $false }

    if (-not (Install-Certificate $certFile))                                     { return $false }
    if (-not (Install-VSBuildTools $installer $response $layoutDir))              { return $false }

    Install-OptionalCACerts $extraCerts

    Write-Host ""
    Write-Host "Done."
    return $true
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

$ok = if ($Install) { Invoke-InstallMode } else { Invoke-DownloadMode }
if (-not $ok) { exit 1 }
