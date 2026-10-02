"""The VM target: restore, start, push the exe, run ORC in the guest, pull the results back."""
import os
from dataclasses import dataclass

from . import powershell

CLIXML_EXTENSIONS = (".clixml", ".xml")
JSON_EXTENSIONS = (".json",)


@dataclass(frozen=True)
class GuestPaths:
    exe: str = r"C:\Windows\Temp\orc.exe"
    config: str = r"C:\Windows\Temp\local.xml"
    out: str = r"C:\Windows\Temp\orc-out"
    temp: str = r"C:\Windows\Temp\orc-temp"
    # What ORC printed, in the GUEST. Deliberately NOT under 'out': an upload with
    # operation="move" empties that directory, and what ORC said about a run is exactly what is
    # wanted when it did. It is also kept out of the pulled result set, which the file-by-file
    # comparison of 'orca diff' would otherwise take in. The two files it holds are copied
    # into the result directory itself, beside the archives.
    console: str = r"C:\Windows\Temp\orc-console"


def windows_join(directory: str, name: str) -> str:
    """Join a Windows path. Not os.path.join: both sides here are Windows (the guest always,
    the Hyper-V host by definition) and the unit tests also run on Linux, where os.path.join
    would build the path with a forward slash."""
    return directory.rstrip("\\") + "\\" + name


def windows_basename(path: str) -> str:
    """The last component of a Windows path. Not os.path.basename, for the same reason."""
    return path.rstrip("\\").split("\\")[-1]


def credential_expression(path: str | None) -> str:
    """The PowerShell expression that yields the PSCredential.

    orca never reads the file: PowerShell does, so a password never passes through orca nor
    through a command line. With no file, Windows asks.
    """
    if not path:
        return "Get-Credential"

    extension = os.path.splitext(path)[1].lower()
    quoted = powershell.quote(path)
    if extension in CLIXML_EXTENSIONS:
        return f"Import-Clixml -Path {quoted}"

    if extension in JSON_EXTENSIONS:
        return (f"$(& {{ $c = Get-Content -Raw {quoted} | ConvertFrom-Json; "
                f"New-Object System.Management.Automation.PSCredential("
                f"$c.user, (ConvertTo-SecureString $c.password -AsPlainText -Force)) }})")

    raise RuntimeError(f"unsupported credential format '{extension}'; "
                       f"known extensions are .clixml, .xml and .json")


def restore_checkpoint_script(vm: str, checkpoint: str) -> str:
    name = powershell.quote(vm)
    return (f"$ErrorActionPreference = 'Stop'; "
            f"if ((Get-VM -Name {name}).State -ne 'Off') "
            f"{{ Stop-VM -Name {name} -Force -TurnOff }}; "
            f"Restore-VMCheckpoint -VMName {name} -Name {powershell.quote(checkpoint)} "
            f"-Confirm:$false")


def start_script(vm: str) -> str:
    name = powershell.quote(vm)
    return (f"$ErrorActionPreference = 'Stop'; "
            f"if ((Get-VM -Name {name}).State -ne 'Running') {{ Start-VM -Name {name} }}; "
            f"Wait-VM -Name {name}")


def copy_exe_script(vm: str, local_exe: str, guest_exe: str) -> str:
    return (f"$ErrorActionPreference = 'Stop'; "
            f"Copy-VMFile -Name {powershell.quote(vm)} -Force "
            f"-SourcePath {powershell.quote(local_exe)} "
            f"-DestinationPath {powershell.quote(guest_exe)} -FileSource Host")


def guest_script(vm: str, *, credential_expr: str, guest: GuestPaths, local_config: str,
                 orc_args: list[str], out_dir: str, result_path: str, pull: bool) -> str:
    """One script, because a PSSession does not outlive the process that created it.

    It reports through a JSON file rather than stdout: it runs with the console attached so
    'Get-Credential' can prompt, which makes its stdout unusable as a channel.

    'orc_args' is the complete argument list, built by the caller with the same 'orc_argv' the
    run record stores, so what ran and what was recorded cannot drift apart.
    """
    arguments = ", ".join(powershell.quote_argument(argument) for argument in orc_args)

    # ORC runs under Start-Process in a remote session, so nothing it prints reaches this
    # console: without these two files a guest run that failed says only that the results are
    # missing. Separate files because Start-Process refuses one path for both streams.
    stdout_path = powershell.quote(windows_join(guest.console, "stdout.log"))
    stderr_path = powershell.quote(windows_join(guest.console, "stderr.log"))

    # Brought back BEFORE the results, and unconditionally: it is the diagnosis for a run that
    # produced nothing, so it must not be lost to the pull below failing, nor withheld by
    # '--no-pull', which is about the collected artifacts. A diagnostic must never be the step
    # that fails the run either, hence the catch.
    #
    # The directory's CONTENTS, so the two files land beside the archives: one directory holds
    # one collection. Copy-Item resolves the wildcard in the guest and fails when it matches
    # nothing, which here is a capture directory that stayed empty because ORC never started -
    # worth the warning the catch prints, and the reason the files are named individually in
    # the messages rather than by their directory.
    landed = windows_join(out_dir, "stdout.log")
    console_line = (
        f"  try {{ Copy-Item -FromSession $s -Recurse -Force "
        f"-Path {powershell.quote(windows_join(guest.console, '*'))} "
        f"-Destination {powershell.quote(out_dir)};\n"
        f"    Write-Host ('ORC console output: ' + {powershell.quote(landed)}) }}\n"
        f"  catch {{ Write-Warning \"could not bring back ORC's console output: $_\" }};\n")

    pull_line = ""
    if pull:
        # The list is taken from the GUEST's output directory, not from '--out': '--out'
        # already holds the local.xml this run wrote, and under '--force' a previous run's
        # archives too, so enumerating it would make collect.json claim files nobody pulled.
        # Paths are relative to the guest output directory, so two same-named files in
        # different subtrees stay distinguishable.
        #
        # The copy is guarded by that list because 'Copy-Item -FromSession' resolves the
        # wildcard in the guest and fails - with the same message for an empty directory as
        # for an absent one - when it matches nothing. A collection that produced no file is
        # not orca failing: it is an upload with operation="move" having taken everything, or
        # an ORC that stopped early, and both belong in a warning, not in a traceback.
        # 'Get-ChildItem -Recurse -File' is itself silent on an empty and on a missing
        # directory alike, so the warning says what the pull found rather than why, and points
        # at the console capture, which is what tells the two apart.
        nothing_pulled = (f"'nothing to pull from ' + {powershell.quote(guest.out)} + "
                          f"': it holds no file - see ' + "
                          f"{powershell.quote(landed)}")
        pull_line = (f"  $pulled = @(Invoke-Command -Session $s -ScriptBlock {{ param($o) "
                     f"Get-ChildItem -Recurse -File $o | ForEach-Object "
                     f"{{ $_.FullName.Substring($o.Length).TrimStart('\\') }} }} "
                     f"-ArgumentList {powershell.quote(guest.out)});\n"
                     f"  if ($pulled.Count -gt 0) {{ Copy-Item -FromSession $s -Recurse -Force "
                     f"-Path {powershell.quote(guest.out + chr(92) + '*')} "
                     f"-Destination {powershell.quote(out_dir)} }}\n"
                     f"  else {{ Write-Warning ({nothing_pulled}) }};\n")

    return (
        f"$ErrorActionPreference = 'Stop';\n"
        f"$cred = {credential_expr};\n"
        f"$s = New-PSSession -VMName {powershell.quote(vm)} -Credential $cred;\n"
        f"$pulled = @();\n"
        f"try {{\n"
        f"  Copy-Item -ToSession $s -Force -Path {powershell.quote(local_config)} "
        f"-Destination {powershell.quote(guest.config)};\n"
        f"  $code = Invoke-Command -Session $s -ScriptBlock {{\n"
        f"    New-Item -ItemType Directory -Force {powershell.quote(guest.out)} | Out-Null;\n"
        f"    New-Item -ItemType Directory -Force {powershell.quote(guest.temp)} | Out-Null;\n"
        f"    New-Item -ItemType Directory -Force {powershell.quote(guest.console)} "
        f"| Out-Null;\n"
        f"    $p = Start-Process -FilePath {powershell.quote(guest.exe)} "
        f"-ArgumentList @({arguments}) -Wait -PassThru -NoNewWindow "
        f"-RedirectStandardOutput {stdout_path} -RedirectStandardError {stderr_path};\n"
        f"    $p.ExitCode\n"
        f"  }};\n"
        # Cast locally: anything Invoke-Command brings back carries PSComputerName, RunspaceId
        # and PSShowComputerName as note properties, and ConvertTo-Json writes those out, so
        # the exit code would reach orca as an object rather than a number. [int] strips them,
        # and [int]$null is 0, which is what an unreported exit code already meant.
        f"  $code = [int]$code;\n"
        f"{console_line}"
        f"{pull_line}"
        f"}} finally {{\n"
        f"  Remove-PSSession $s;\n"
        f"}}\n"
        f"@{{ exit_code = $code; pulled = $pulled }} | ConvertTo-Json "
        f"| Set-Content -Encoding utf8 {powershell.quote(result_path)}\n"
    )


def run_vm_collect(*, vm: str, checkpoint: str | None, local_exe: str, local_config: str,
                   credential: str | None, orc_args: list[str], out_dir: str,
                   result_path: str, pull: bool, guest: GuestPaths = GuestPaths(),
                   run_step=powershell.run) -> dict:
    """The whole VM sequence. 'run_step' is the seam the tests replace."""
    # Also called by collect before the VM is touched, so an unsupported format is refused
    # without having restored a checkpoint. Keep both: this one is what the script uses.
    expression = credential_expression(credential)

    if checkpoint:
        run_step(restore_checkpoint_script(vm, checkpoint), step="restore-checkpoint")

    run_step(start_script(vm), step="start-vm")
    run_step(copy_exe_script(vm, local_exe, guest.exe), step="copy-exe")

    script = guest_script(vm, credential_expr=expression, guest=guest,
                          local_config=local_config, orc_args=orc_args, out_dir=out_dir,
                          result_path=result_path, pull=pull)
    return run_step(script, step="guest-run", interactive=True, result_path=result_path) or {}
