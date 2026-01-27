#
# Start a powershell session inside a job (with an acceptable race condition)
#
# . tests\tools\job.ps1
# New-TestJob
# Start-InJob -Program "C:\Program Files\PowerShell\7\pwsh.exe"
# ...
#

Add-Type @"
using System;
using System.Runtime.InteropServices;

public class JobTest {
    public const uint JOB_OBJECT_LIMIT_BREAKAWAY_OK = 0x00000800;
    public const int JobObjectBasicLimitInformation = 2;

    [StructLayout(LayoutKind.Sequential)]
    public struct JOBOBJECT_BASIC_LIMIT_INFORMATION {
        public long PerProcessUserTimeLimit;
        public long PerJobUserTimeLimit;
        public uint LimitFlags;
        public UIntPtr MinimumWorkingSetSize;
        public UIntPtr MaximumWorkingSetSize;
        public uint ActiveProcessLimit;
        public UIntPtr Affinity;
        public uint PriorityClass;
        public uint SchedulingClass;
    }

    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr CreateJobObject(IntPtr attr, string name);

    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);

    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool SetInformationJobObject(IntPtr job, int infoClass, ref JOBOBJECT_BASIC_LIMIT_INFORMATION info, uint cbInfoSize);

    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool QueryInformationJobObject(IntPtr job, int infoClass, ref JOBOBJECT_BASIC_LIMIT_INFORMATION info, uint cbInfoSize, IntPtr lpReturnLength);

    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool IsProcessInJob(IntPtr proc, IntPtr job, out bool result);

    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);

    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool CloseHandle(IntPtr handle);
}
"@

$script:job = [IntPtr]::Zero

function New-TestJob {
    $script:job = [JobTest]::CreateJobObject([IntPtr]::Zero, "DFIROrcTestJob")
    if ($script:job -eq [IntPtr]::Zero) { throw "CreateJobObject failed" }
    Write-Host "Job created. Breakaway blocked by default."
}

function Start-InJob {
    param(
        [Parameter(Mandatory)][string]$Program,
        [string[]]$Arguments = @()
    )

    if ($script:job -eq [IntPtr]::Zero) { throw "Call New-TestJob first" }

    # Start the process suspended so we can assign it to the job before it runs
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Program
    $psi.Arguments = $Arguments -join " "
    $psi.UseShellExecute = $true
    $psi.CreateNoWindow = $false

    $p = [System.Diagnostics.Process]::Start($psi)
    if ($null -eq $p) { throw "Failed to start process" }

    $PROCESS_ALL_ACCESS = 0x1F0FFF
    $handle = [JobTest]::OpenProcess($PROCESS_ALL_ACCESS, $false, $p.Id)
    if ($handle -eq [IntPtr]::Zero) { throw "OpenProcess failed" }

    try {
        $assigned = [JobTest]::AssignProcessToJobObject($script:job, $handle)
        if (!$assigned) { throw "AssignProcessToJobObject failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
        Write-Host "Process $($p.Id) ($Program) assigned to job."
    }
    finally {
        [JobTest]::CloseHandle($handle) | Out-Null
    }

    return $p
}

function Set-BreakawayAllowed {
    param(
        [Parameter(Mandatory)][bool]$Allow
    )

    if ($script:job -eq [IntPtr]::Zero) { throw "Call New-TestJob first" }

    $info = New-Object JobTest+JOBOBJECT_BASIC_LIMIT_INFORMATION
    $size = [Runtime.InteropServices.Marshal]::SizeOf($info)

    [JobTest]::QueryInformationJobObject($script:job, [JobTest]::JobObjectBasicLimitInformation, [ref]$info, $size, [IntPtr]::Zero) | Out-Null

    if ($Allow) {
        $info.LimitFlags = $info.LimitFlags -bor [JobTest]::JOB_OBJECT_LIMIT_BREAKAWAY_OK
    } else {
        $info.LimitFlags = $info.LimitFlags -band (-bnot [JobTest]::JOB_OBJECT_LIMIT_BREAKAWAY_OK)
    }

    $result = [JobTest]::SetInformationJobObject($script:job, [JobTest]::JobObjectBasicLimitInformation, [ref]$info, $size)
    if (!$result) { throw "SetInformationJobObject failed" }

    Write-Host "Breakaway allowed: $Allow"
}
