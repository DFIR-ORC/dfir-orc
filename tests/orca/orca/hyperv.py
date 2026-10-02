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
    pull_line = ""
    if pull:
        # The list is taken from the GUEST's output directory, not from '--out': '--out'
        # already holds the local.xml this run wrote, and under '--force' a previous run's
        # archives too, so enumerating it would make collect.json claim files nobody pulled.
        # Paths are relative to the guest output directory, so two same-named files in
        # different subtrees stay distinguishable.
        pull_line = (f"  $pulled = @(Invoke-Command -Session $s -ScriptBlock {{ param($o) "
                     f"Get-ChildItem -Recurse -File $o | ForEach-Object "
                     f"{{ $_.FullName.Substring($o.Length).TrimStart('\\') }} }} "
                     f"-ArgumentList {powershell.quote(guest.out)});\n"
                     f"  Copy-Item -FromSession $s -Recurse -Force "
                     f"-Path {powershell.quote(guest.out + chr(92) + '*')} "
                     f"-Destination {powershell.quote(out_dir)};\n")

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
        f"    $p = Start-Process -FilePath {powershell.quote(guest.exe)} "
        f"-ArgumentList @({arguments}) -Wait -PassThru -NoNewWindow;\n"
        f"    $p.ExitCode\n"
        f"  }};\n"
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
