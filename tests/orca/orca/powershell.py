"""The only module that knows PowerShell exists: locate it, run one script, read its result.

An offline collect never calls in here. The VM target does, because Hyper-V's only supported
automation surface is the PowerShell module (wmic.exe is gone on current Windows, and driving
WMI through COM by hand is not worth the maintenance).
"""
import json
import os
import shutil
import subprocess

WINDOWS_POWERSHELL = r"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe"


def find_powershell() -> str | None:
    for name in ("pwsh", "powershell"):
        found = shutil.which(name)
        if found:
            return found

    return WINDOWS_POWERSHELL if os.path.exists(WINDOWS_POWERSHELL) else None


def quote(value: str) -> str:
    """A PowerShell single-quoted literal. Doubling the quote is what keeps a path like
    C:\\it's\\k.json from ending the string and running the rest as code."""
    return "'" + value.replace("'", "''") + "'"


def quote_argument(value: str) -> str:
    """A PowerShell literal for one element of 'Start-Process -ArgumentList'.

    PowerShell joins those elements with spaces and does NOT re-quote them, so a value
    carrying whitespace would reach the process as several arguments. Wrapping it in embedded
    double quotes makes CommandLineToArgvW put it back together as one.
    """
    inner = f'"{value}"' if any(character.isspace() for character in value) else value
    return quote(inner)


def command_argv(executable: str, script: str, *, interactive: bool) -> list[str]:
    """'-NonInteractive' is dropped for the interactive run so 'Get-Credential' can prompt."""
    argv = [executable, "-NoProfile"]
    if not interactive:
        argv.append("-NonInteractive")

    return argv + ["-Command", script]


def run(script: str, *, step: str, interactive: bool = False, result_path: str | None = None,
        executable: str | None = None) -> dict | None:
    """Run one script. Returns the decoded 'result_path' JSON when one is expected.

    An interactive run keeps the console, so its stdout cannot be captured: it reports back
    through 'result_path' instead.
    """
    shell = executable or find_powershell()
    if not shell:
        raise RuntimeError(f"{step}: PowerShell not found (looked for pwsh and powershell)")

    argv = command_argv(shell, script, interactive=interactive)
    if interactive:
        completed = subprocess.run(argv)
    else:
        completed = subprocess.run(argv, capture_output=True, text=True)

    if completed.returncode != 0:
        detail = (getattr(completed, "stderr", "") or getattr(completed, "stdout", "")
                  or "").strip()
        raise RuntimeError(f"{step}: PowerShell exited {completed.returncode}: {detail}")

    if not result_path:
        return None

    # utf-8-sig, not utf-8: Windows PowerShell 5.1 writes 'Set-Content -Encoding utf8' WITH a
    # BOM, and 5.1 is the shell this falls back to when pwsh is absent. It also reads a
    # BOM-less file, so both shells work. Failing here would throw away a collection that
    # already ran.
    try:
        with open(result_path, "r", encoding="utf-8-sig") as source:
            return json.load(source)
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"{step}: no usable result at '{result_path}': {error}") from error
