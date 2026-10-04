# Differential pass for the LZX WOF decompressor.
#
# Enumerates every LZX compressed file under -Root with WofIsExternalFile, collects the same subtree with GetThis
# in one pass over -Volume, and compares each collected sample with Get-FileHash of the live path: the filesystem's
# own decompression is the ground truth. Needs the image mounted (read only is fine), so it stays out of CI.
#
# Usage: powershell -File tests\Test-WofLzx.ps1 -Out C:\tmp\wof-lzx [-Volume D:] [-Root "D:\Program Files (x86)\Microsoft"]
#        [-Orc S:\...\DFIR-ORC_x64.exe]

[cmdletbinding()]
Param (
    [Parameter(Mandatory = $True)]
    [ValidateNotNullOrEmpty()]
    [string]
    $Out,

    [string]
    $Volume = "D:",

    [string]
    $Root = "D:\Program Files (x86)\Microsoft",

    [string]
    $Orc = "",

    [int]
    $MaxMismatchesShown = 10
)

$ErrorActionPreference = "Stop"

# $PSScriptRoot is empty while parameter defaults are bound under Windows PowerShell, so resolve it here.
if (-not $Orc) { $Orc = Join-Path $PSScriptRoot "..\build\dfir-orc-x64-ninja\Debug\DFIR-ORC_x64.exe" }

# FSCTL_GET_EXTERNAL_BACKING returns the reparse data ORC itself parses: WOF_EXTERNAL_INFO {Version, Provider}
# then FILE_PROVIDER_EXTERNAL_INFO_V1 {Version, Algorithm, Flags}, 20 bytes. WofUtil's WofIsExternalFile was
# tried first and reports algorithm 0 for files compact /exe:lzx produced on Windows 11 26200, so it is not used.
$WofHelper = @'
    using System;
    using System.Runtime.InteropServices;

    public static class WofHelper
    {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr CreateFileW(
            string name, uint access, uint share, IntPtr securityAttributes, uint disposition, uint flags, IntPtr template);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool DeviceIoControl(
            IntPtr handle, uint code, IntPtr inBuffer, uint inSize, IntPtr outBuffer, uint outSize, out uint returned, IntPtr overlapped);

        [DllImport("kernel32.dll")]
        private static extern bool CloseHandle(IntPtr handle);

        // Returns the file provider's algorithm (FILE_PROVIDER_COMPRESSION_LZX is 1), or -1 for a plain file,
        // another provider, or a failure.
        public static int FileProviderAlgorithm(string path)
        {
            const uint genericRead = 0x80000000;
            const uint shareAll = 7;
            const uint openExisting = 3;
            const uint backupSemantics = 0x02000000;
            const uint fsctlGetExternalBacking = 0x90310;
            const uint providerFile = 2;
            const int outputSize = 20;

            IntPtr handle = CreateFileW(path, genericRead, shareAll, IntPtr.Zero, openExisting, backupSemantics, IntPtr.Zero);
            if (handle == new IntPtr(-1))
            {
                return -1;
            }

            IntPtr output = Marshal.AllocHGlobal(outputSize);
            try
            {
                uint returned;
                bool ok = DeviceIoControl(handle, fsctlGetExternalBacking, IntPtr.Zero, 0, output, outputSize, out returned, IntPtr.Zero);
                if (!ok || returned < outputSize || Marshal.ReadInt32(output, 4) != providerFile)
                {
                    return -1;
                }

                return Marshal.ReadInt32(output, 12);
            }
            finally
            {
                Marshal.FreeHGlobal(output);
                CloseHandle(handle);
            }
        }
    }
'@

Add-Type -TypeDefinition $WofHelper -Language CSharp

function Get-FirstDifferingOffset([string]$Left, [string]$Right)
{
    $a = [IO.File]::ReadAllBytes($Left)
    $b = [IO.File]::ReadAllBytes($Right)
    $n = [Math]::Min($a.Length, $b.Length)
    for ($i = 0; $i -lt $n; $i++)
    {
        if ($a[$i] -ne $b[$i]) { return $i }
    }

    if ($a.Length -ne $b.Length) { return $n }
    return -1
}

if (-not (Test-Path $Orc)) { throw "DFIR-ORC executable not found: $Orc" }
if (-not (Test-Path $Root)) { throw "Root not found: $Root" }
New-Item -ItemType Directory -Force $Out | Out-Null

# 1. The live side: every LZX file under the root, with the filesystem's own view of its content.
Write-Host "Enumerating LZX files under $Root"
$lzxAlgorithm = 1
$live = @{}
foreach ($file in Get-ChildItem $Root -Recurse -File)
{
    if ([WofHelper]::FileProviderAlgorithm($file.FullName) -ne $lzxAlgorithm) { continue }

    # GetThis reports FullName relative to the volume, with a leading backslash.
    $relative = $file.FullName.Substring($Volume.Length)
    $live[$relative] = [pscustomobject]@{
        Path = $file.FullName
        Size = $file.Length
        Hash = (Get-FileHash $file.FullName -Algorithm SHA256).Hash
    }
}

Write-Host ("Live LZX files: {0}" -f $live.Count)
if ($live.Count -eq 0) { throw "No LZX file under ${Root}: is the image mounted and is this the right directory?" }

# 2. The collected side: GetThis over the same subtree, in one pass over the volume.
$template = Join-Path $PSScriptRoot "wof_lzx_getthis.xml"
$config = Join-Path $Out "wof_lzx_getthis.xml"
$pathMatch = $Root.Substring($Volume.Length).TrimEnd("\") + "\*"
(Get-Content $template -Raw) `
    -replace "<location>[^<]*</location>", "<location>$Volume</location>" `
    -replace 'path_match="[^"]*"', ('path_match="' + $pathMatch + '"') `
    | Set-Content $config -Encoding UTF8

$samples = Join-Path $Out "samples"
if (Test-Path $samples) { Remove-Item -Recurse -Force $samples }
New-Item -ItemType Directory -Force $samples | Out-Null

Write-Host "Running GetThis with $config"
# A native process writing to stderr is not a PowerShell error: keep Stop for everything else.
$ErrorActionPreference = "Continue"
& $Orc GetThis "/config=$config" /nolimits "/out=$samples" > (Join-Path $Out "getthis.log") 2>&1
$ErrorActionPreference = "Stop"
if ($LASTEXITCODE -ne 0) { Write-Warning "GetThis exited with $LASTEXITCODE; see $(Join-Path $Out 'getthis.log')" }

$csv = Get-ChildItem $samples -Recurse -Filter GetThis.csv | Select-Object -First 1
if (-not $csv) { throw "GetThis.csv not found under $samples" }

# SampleName is relative to the output directory ("<sample name>\<FRN>"). GetThis deduplicates hard links by
# FRN, so several FullName rows may share one SampleName.
$collected = @{}
foreach ($row in Import-Csv $csv.FullName)
{
    $samplePath = Join-Path $samples $row.SampleName
    if (-not (Test-Path $samplePath)) { continue }
    $collected[$row.FullName] = $samplePath
}

# 3. The comparison.
$matched = 0
$mismatches = @()
$missing = @()
$hashCache = @{}
foreach ($relative in ($live.Keys | Sort-Object))
{
    if (-not $collected.ContainsKey($relative))
    {
        $missing += $relative
        continue
    }

    $samplePath = $collected[$relative]
    if (-not $hashCache.ContainsKey($samplePath))
    {
        $hashCache[$samplePath] = (Get-FileHash $samplePath -Algorithm SHA256).Hash
    }

    if ($hashCache[$samplePath] -eq $live[$relative].Hash)
    {
        $matched++
        continue
    }

    $mismatches += [pscustomobject]@{
        Path = $live[$relative].Path
        LiveHash = $live[$relative].Hash
        SampleHash = $hashCache[$samplePath]
        FirstDifferingOffset = Get-FirstDifferingOffset $live[$relative].Path $samplePath
        Sample = $samplePath
    }
}

Write-Host ("total {0} / matched {1} / mismatched {2} / missing {3}" -f $live.Count, $matched, $mismatches.Count, $missing.Count)

foreach ($m in ($mismatches | Select-Object -First $MaxMismatchesShown))
{
    Write-Host ("MISMATCH {0}" -f $m.Path)
    Write-Host ("  live   {0}" -f $m.LiveHash)
    Write-Host ("  sample {0} ({1})" -f $m.SampleHash, $m.Sample)
    Write-Host ("  first differing offset {0} (chunk {1}, offset in chunk {2})" -f $m.FirstDifferingOffset, [Math]::Floor($m.FirstDifferingOffset / 32768), ($m.FirstDifferingOffset % 32768))
}

foreach ($m in ($missing | Select-Object -First $MaxMismatchesShown))
{
    Write-Host ("MISSING  {0}" -f $m)
}

if ($mismatches.Count -ne 0 -or $missing.Count -ne 0) { exit 1 }
exit 0
