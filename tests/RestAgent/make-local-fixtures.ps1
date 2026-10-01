# Generate a private copy of the vm-*.xml fixtures pinned to YOUR sink's certificate.
#
# The committed fixtures pin the throwaway 'localhost' certificate rest_test_server.py embeds
# (server.crt), so they work unchanged against a python sink on the default VmHost (127.0.0.1). Any
# other sink serves its own certificate, which that pin cannot match. This script copies the
# fixtures into a git-ignored directory, rewriting server_cert_pin with the certificate your own
# sink actually serves. Point run-vmtest.ps1 at the result with ConfigDir in vmtest.local.psd1.
#
# The generated copies are yours to edit further - upload credentials, proxy credentials, extra
# headers. They are git-ignored precisely so that bench-specific secrets never reach a commit.
#
# Usage:
#   .\make-local-fixtures.ps1                                  # cert from the ssh host in vmtest.local.psd1
#   .\make-local-fixtures.ps1 -CertFile .\my-sink.crt          # cert from a local PEM file
#   .\make-local-fixtures.ps1 -OutDir D:\bench\fixtures -Force
param(
    [string]$SettingsFile,
    [string]$SshHost,
    [string]$RemoteCert = '/root/server.crt',
    [string]$CertFile,
    [string]$OutDir,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$scriptDir = $PSScriptRoot

if (-not $SettingsFile) {
    $SettingsFile = Join-Path $scriptDir 'vmtest.local.psd1'
}

$settings = @{}
if (Test-Path $SettingsFile) {
    $settings = Import-PowerShellDataFile -Path $SettingsFile
}

if (-not $SshHost)  { $SshHost = [string]$settings.SshHost }
if (-not $OutDir)   { $OutDir  = [string]$settings.ConfigDir }
if (-not $OutDir)   { $OutDir  = Join-Path $scriptDir 'local' }

# Compared without requiring the directory to exist yet: this must refuse before anything is created.
if (-not [System.IO.Path]::IsPathRooted($OutDir)) {
    $OutDir = Join-Path (Get-Location).Path $OutDir
}

$OutDir = [System.IO.Path]::GetFullPath($OutDir).TrimEnd('\')
if ($OutDir -eq [System.IO.Path]::GetFullPath($scriptDir).TrimEnd('\')) {
    throw "OutDir must not be the checked-in fixture directory ($scriptDir) - it would overwrite committed files."
}

# The certificate, as single-line base64 DER: what server_cert_pin accepts (it also takes PEM, but
# one line keeps it inside an XML attribute).
if ($CertFile) {
    # Write the DER to a file rather than capturing it: PowerShell decodes a native command's stdout as
    # text and hands back a String[], which ToBase64String cannot bind ("cannot convert argument
    # 'inArray'"). The ssh branch below sidesteps this by base64-encoding on the remote side.
    $derFile = [System.IO.Path]::GetTempFileName()
    try {
        & openssl x509 -in $CertFile -outform DER -out $derFile
        if ($LASTEXITCODE -ne 0) { throw "openssl failed to read $CertFile" }

        $pin = [Convert]::ToBase64String([System.IO.File]::ReadAllBytes($derFile))
    }
    finally {
        Remove-Item $derFile -Force -ErrorAction SilentlyContinue
    }
}
else {
    if (-not $SshHost) {
        throw "no certificate source: pass -CertFile, or set SshHost in $SettingsFile"
    }

    $pin = (ssh $SshHost "openssl x509 -in $RemoteCert -outform DER | base64 -w0" | Out-String).Trim()
    if (-not $pin) { throw "could not read $RemoteCert from $SshHost" }
}

# The pin every positive https fixture shares, taken from the canonical one. vm-https-pin-bad.xml
# carries a DIFFERENT certificate on purpose - it is the negative test - so it never matches this
# search and keeps its deliberately-wrong pin.
$canonical = Get-Content -Raw (Join-Path $scriptDir 'vm-https-pin.xml')
if ($canonical -notmatch 'server_cert_pin="([^"]+)"') {
    throw "could not find server_cert_pin in vm-https-pin.xml"
}

$oldPin = $Matches[1]
if ($oldPin -eq $pin) {
    Write-Host "note  : the committed fixtures already pin this certificate"
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$written = 0
foreach ($src in Get-ChildItem -Path $scriptDir -Filter 'vm-*.xml') {
    $dest = Join-Path $OutDir $src.Name
    if ((Test-Path $dest) -and -not $Force) {
        Write-Host ("skip  : {0} exists (use -Force to overwrite)" -f $src.Name)
        continue
    }

    $text = (Get-Content -Raw $src.FullName).Replace($oldPin, $pin)
    Set-Content -Path $dest -Value $text -Encoding UTF8 -NoNewline
    $written++
}

Write-Host ""
Write-Host "wrote $written fixture(s) to $OutDir"
Write-Host "set   ConfigDir = '$OutDir'   in $SettingsFile"
Write-Host "check that '$OutDir' is git-ignored before adding any credential to those files"
