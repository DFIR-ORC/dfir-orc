# TODO: create a file >4GB to test the 64 bits chunk offsets
# TODO: lzx

[cmdletbinding()]
Param (
    [Parameter(Mandatory = $True)]
    [ValidateNotNullOrEmpty()]
    [System.IO.DirectoryInfo]
    $Path
)

[System.Collections.Generic.List[System.Object]]$DatasetSizes = `
        ( 256, 512, 1024, 4096, 8192, 16384, 1048576, (1048576 * 10) ) `
    | Sort-Object

$CustomHelper = @'
    using System.IO;

    public static class CustomHelper
    {
        public static void FillBufferWithInt32Sequence(byte[] buffer)
        {
            for (int i = 0; i < buffer.Length; i += 4)
            {
                int value = i / 4;
                buffer[i + 3] = (byte)value;
                buffer[i + 2] = (byte)(value >> 8);
                buffer[i + 1] = (byte)(value >> 16);
                buffer[i] = (byte)(value >> 24);
            }
        }
    }
'@

Add-Type -TypeDefinition $CustomHelper -Language CSharp

$Buffer = New-Object byte[] $DatasetSizes[-1]
[CustomHelper]::FillBufferWithInt32Sequence($Buffer)

$ControlGroup = @()
foreach ($Size in $DatasetSizes)
{
    $FilePath = Join-Path ${Path} "test_wof_seq_${Size}.bin"

    Write-Host "Write: $FilePath"
    [IO.File]::WriteAllBytes($FilePath, $Buffer[0..($Size-1)])

    $ControlGroup += [System.IO.FileInfo]$FilePath
}

# This will create some resident 'WofCompressedData'
$BufferZero = New-Object byte[] $DatasetSizes[-1]
foreach ($Size in $DatasetSizes)
{
    $FilePath = Join-Path ${Path} "test_wof_zero_${Size}.bin"

    Write-Host "Write: $FilePath"
    [IO.File]::WriteAllBytes($FilePath, $BufferZero[0..($Size-1)])

    $ControlGroup += [System.IO.FileInfo]$FilePath
}

$Algorithm = @("xpress4k", "xpress8k", "xpress16k")
foreach ($Algo in $Algorithm)
{
    foreach ($File in $ControlGroup)
    {
        $TestPath = Join-Path `
            $File.Directory `
            "$($File.BaseName)_$($Algo.ToLower())$($File.Extension)"

        Write-Host "Write: $TestPath"
        Copy-Item $File $TestPath
        compact.exe /c /exe:$Algo $TestPath
    }
}
