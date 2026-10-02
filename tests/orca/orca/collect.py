"""The 'collect' command: run ORC against a disk, a VHD or a VM, and keep what it produced."""
import datetime
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass

from . import hyperv, localconfig, scratch, vhd

RECORD_NAME = "collect.json"


@dataclass(frozen=True)
class Target:
    kind: str   # "directory" | "image" | "vhd" | "vm"
    value: str


def classify_offline_target(path: str) -> Target:
    """What ORC can be pointed at directly, and what has to be attached first.

    ORC reads a raw image itself ('/Offline=<FilePath>' is documented as "work on a disk
    image", and LocationSet sniffs the file), so only a VHD(X) container needs attaching.
    """
    if not os.path.exists(path):
        raise RuntimeError(f"offline target not found: '{path}'")

    if os.path.isdir(path):
        return Target("directory", os.path.abspath(path))

    if vhd.is_vhd(path):
        return Target("vhd", os.path.abspath(path))

    return Target("image", os.path.abspath(path))


def orc_argv(orc: str, *, out: str, temp: str, local_config: str, extra: list[str],
             overwrite: bool, offline: str | None = None) -> list[str]:
    """The ORC command line, in the order 'Invoke-OrcOffline' uses."""
    argv = [orc] + list(extra)
    if overwrite:
        argv.append("/overwrite")
    if offline:
        argv.append(f"/Offline={offline}")

    argv += [f"/TempDir={temp}", f"/Out={out}", f"/local={local_config}"]
    return argv


def prepare_out_dir(path: str, force: bool) -> str:
    """Create '--out' and return it absolute.

    Absolute because ORC resolves a relative path against its own working directory, not
    orca's. Refusing a non-empty one without '--force' keeps two collections from being
    mixed in one directory, which would make the result set undiffable.
    """
    absolute = os.path.abspath(path)
    if os.path.exists(absolute) and not os.path.isdir(absolute):
        raise RuntimeError(f"--out '{absolute}' is a file, not a directory")

    if os.path.isdir(absolute) and os.listdir(absolute) and not force:
        raise RuntimeError(f"'{absolute}' is not empty; pass --force to collect into it anyway")

    os.makedirs(absolute, exist_ok=True)
    return absolute


def copy_orc_into(out_dir: str, orc: str) -> str:
    """Put the ORC executable beside the results and return the copy's path.

    Two reasons this is the default. The result directory then holds the exact binary that
    produced it, so the run can be repeated after the build tree is gone; and the build output
    is only read once, for the copy, instead of being held for the length of the collection,
    which would stop a rebuild while one is running.

    The name is kept as it is ('DFIR-ORC_x64.exe', not renamed to 'orc.exe' as test.psm1 does)
    because it says which build this was.
    """
    destination = os.path.join(out_dir, os.path.basename(orc))
    try:
        shutil.copy2(orc, destination)
    except OSError as error:
        # On Windows a running executable cannot be overwritten, which is what a '--force'
        # rerun into an '--out' whose copy is still collecting runs into.
        raise RuntimeError(f"cannot copy ORC to '{destination}': {error}. A previous collect "
                           f"may still be running from it") from error

    return destination


def printable_argv(argv: list[str]) -> str:
    """The command line as something that can be copied and re-run: an argument carrying a
    space has to come back quoted, or what is printed is not what ran."""
    return subprocess.list2cmdline(argv)


def file_sha256(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as source:
        for block in iter(lambda: source.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def _timestamp(epoch: float) -> str:
    return (datetime.datetime.fromtimestamp(epoch, datetime.timezone.utc).astimezone()
            .isoformat(timespec="seconds"))


def collect_record(*, target: Target, orc: str, argv: list[str], out_dir: str, temp_dir: str,
                   started: float, ended: float, exit_code: int, pulled: list[str],
                   target_detail: dict | None = None, copied_to: str | None = None) -> dict:
    """What produced this result set. When two ORC versions are compared, this is the only
    record of which binary wrote which side, so the binary is identified by content.

    'target_detail' carries the parameters that decide what was actually read: the checkpoint
    a VM was restored to, and for a VHD the volume taken plus whether orca is what attached
    it. Without them the record cannot answer "which volume, and was it already mounted?".
    """
    return {
        "target": {"kind": target.kind, "value": target.value, **(target_detail or {})},
        # 'path' is the binary that was asked for, 'copied_to' the one that actually ran (or
        # was pushed to the guest). Same bytes, so one sha256 covers both.
        "orc": {"path": os.path.abspath(orc), "size": os.path.getsize(orc),
                "sha256": file_sha256(orc), "copied_to": copied_to},
        "argv": argv,
        "out": out_dir,
        "temp": temp_dir,
        # Local time WITH its offset: two sides of a comparison can come from different hosts
        # or straddle a DST change, and a naive timestamp cannot be ordered across either.
        "started": _timestamp(started),
        "ended": _timestamp(ended),
        "duration_s": round(ended - started, 3),
        "exit_code": exit_code,
        "pulled": pulled,
    }


def write_record(out_dir: str, record: dict) -> str:
    path = os.path.join(out_dir, RECORD_NAME)
    with open(path, "w", encoding="utf-8") as sink:
        json.dump(record, sink, indent=2)
    return path


def build_local_config(*, temp_dir: str, public_key: str | None, upload: str | None,
                       bits: bool, allow_invalid_uri: bool, enable_keys, disable_keys, keys,
                       local_config: str | None, output: str | None = None) -> str:
    """The configuration text ORC will run with.

    'output' sets the <output> element, which only the VM target needs: a run whose results
    are pulled has to put them somewhere orca knows. An offline run gets '/Out=' on the
    command line instead and leaves the element out.
    """
    spec = None
    if upload:
        spec = localconfig.UploadSpec(uri=upload, bits=bits,
                                      allow_invalid_uri=allow_invalid_uri)

    generated = localconfig.build(output=output, temporary=temp_dir, public_key=public_key,
                                  upload=spec, enable_keys=enable_keys,
                                  disable_keys=disable_keys, keys=keys)
    if local_config:
        return localconfig.merge_file(local_config, generated)

    return generated


def write_local_config(out_dir: str, text: str) -> str:
    """Kept in the result directory: it is the record of what produced those results."""
    path = os.path.join(out_dir, "local.xml")
    with open(path, "w", encoding="utf-8") as sink:
        sink.write(text)
    return path


def run_collect(*, orc: str, out: str, offline: str | None = None, vm: str | None = None,
                checkpoint: str | None = None, temp: str | None = None,
                public_key: str | None = None, upload: str | None = None, bits: bool = False,
                allow_invalid_uri: bool = False, enable_keys=None, disable_keys=None,
                keys=None, local_config: str | None = None, credential: str | None = None,
                pull: bool = True, force: bool = False, copy_orc: bool = True, orc_args=None,
                run_process=subprocess.run, vm_runner=None) -> int:
    """Run a collection and leave a result directory 'orca diff' can read.

    Returns ORC's own exit code: a non-zero one is not orca's failure, and the archives ORC
    did produce are kept, because a partial collection is still diffable and is often the
    thing under test.
    """
    if bool(offline) == bool(vm):
        raise RuntimeError("collect needs exactly one target: --offline or --vm")

    if not os.path.isfile(orc):
        raise RuntimeError(f"ORC executable not found: '{orc}'")

    # Refuse an option that belongs to the other target rather than accepting it and doing
    # nothing with it: a silently ignored --checkpoint means collecting from a VM nobody
    # reverted, which looks like a successful run.
    if offline:
        for name, value in (("--checkpoint", checkpoint), ("--credential", credential),
                            ("--no-pull", not pull)):
            if value:
                raise RuntimeError(f"{name} applies to --vm, not to --offline")

    # Everything that can reject the run is checked BEFORE --out or a temp directory is
    # created: a run that never started must not leave a local.xml behind, or the corrected
    # retry is refused for a directory holding nothing but the failed attempt's leftovers.
    target = classify_offline_target(offline) if offline else Target("vm", vm)
    if vm:
        _check_vm_credential(credential)

    out_dir = prepare_out_dir(out, force)
    extra = list(orc_args or [])

    # The copy is the default: see copy_orc_into. Offline runs it; the VM target pushes it to
    # the guest. Either way the build output is touched once rather than held for the run.
    copied_to = copy_orc_into(out_dir, orc) if copy_orc else None
    orc_to_run = copied_to or os.path.abspath(orc)

    if vm:
        # The VM's own temp is a GUEST path, so '--temp' names a directory in the guest; the
        # host side only needs somewhere to drop the guest result json.
        guest = hyperv.GuestPaths(temp=temp) if temp else hyperv.GuestPaths()
        return _collect_vm(orc=orc, orc_to_run=orc_to_run, copied_to=copied_to, vm=vm,
                           checkpoint=checkpoint, out_dir=out_dir,
                           temp_dir=scratch.directory("collect_"), guest=guest, target=target,
                           public_key=public_key, upload=upload, bits=bits,
                           allow_invalid_uri=allow_invalid_uri, enable_keys=enable_keys,
                           disable_keys=disable_keys, keys=keys, local_config=local_config,
                           credential=credential, pull=pull, force=force, extra=extra,
                           vm_runner=vm_runner)

    temp_dir = os.path.abspath(temp) if temp else scratch.directory("collect_")
    os.makedirs(temp_dir, exist_ok=True)
    return _collect_offline(orc=orc, orc_to_run=orc_to_run, copied_to=copied_to,
                            target=target, out_dir=out_dir, temp_dir=temp_dir,
                            public_key=public_key, upload=upload, bits=bits,
                            allow_invalid_uri=allow_invalid_uri, enable_keys=enable_keys,
                            disable_keys=disable_keys, keys=keys, local_config=local_config,
                            force=force, extra=extra, run_process=run_process)


def _collect_offline(*, orc, orc_to_run, copied_to, target, out_dir, temp_dir, public_key,
                     upload, bits, allow_invalid_uri, enable_keys, disable_keys, keys,
                     local_config, force, extra, run_process) -> int:
    config_text = build_local_config(temp_dir=temp_dir,
                                     public_key=public_key, upload=upload, bits=bits,
                                     allow_invalid_uri=allow_invalid_uri,
                                     enable_keys=enable_keys, disable_keys=disable_keys,
                                     keys=keys, local_config=local_config)
    config_path = write_local_config(out_dir, config_text)

    if target.kind != "vhd":
        return _run_orc(orc=orc, orc_to_run=orc_to_run, copied_to=copied_to, target=target,
                        location=target.value, out_dir=out_dir,
                        temp_dir=temp_dir, config_path=config_path, force=force, extra=extra,
                        run_process=run_process)

    with vhd.attached(target.value) as disk:
        print(f"attached '{target.value}' as {disk.drive_letter} "
              f"({disk.physical_path}, attached by orca: {disk.attached_by_orca})")
        return _run_orc(orc=orc, orc_to_run=orc_to_run, copied_to=copied_to, target=target,
                        location=disk.drive_letter, out_dir=out_dir,
                        temp_dir=temp_dir, config_path=config_path, force=force, extra=extra,
                        run_process=run_process,
                        target_detail={"drive_letter": disk.drive_letter,
                                       "physical_path": disk.physical_path,
                                       "attached_by_orca": disk.attached_by_orca})


def _run_orc(*, orc, orc_to_run, copied_to, target, location, out_dir, temp_dir,
             config_path, force, extra,
             run_process, target_detail=None) -> int:
    argv = orc_argv(orc_to_run, out=out_dir, temp=temp_dir,
                    local_config=config_path, extra=extra, overwrite=force, offline=location)
    print(printable_argv(argv))
    started = time.time()
    completed = run_process(argv)
    ended = time.time()

    record = collect_record(target=target, orc=orc, argv=argv, out_dir=out_dir,
                            temp_dir=temp_dir, started=started, ended=ended,
                            exit_code=completed.returncode, pulled=[],
                            target_detail=target_detail, copied_to=copied_to)
    write_record(out_dir, record)

    if completed.returncode != 0:
        print(f"ORC exited {completed.returncode}; results kept in '{out_dir}'")

    return completed.returncode


def _check_vm_credential(credential: str | None) -> None:
    """Both ways a VM run can be unusable before anything is started.

    Get-Credential prompts on a console: without one and without a credential file the run
    would hang where nobody can answer. And an unsupported credential format must be rejected
    before a checkpoint is restored and the guest booted, not after.
    """
    if not credential and not sys.stdin.isatty():
        raise RuntimeError("collect --vm has no console to prompt on; pass --credential")

    hyperv.credential_expression(credential)


def _collect_vm(*, orc, orc_to_run, copied_to, vm, checkpoint, out_dir, temp_dir, guest,
                target, public_key, upload, bits, allow_invalid_uri, enable_keys,
                disable_keys, keys, local_config, credential, pull, force, extra,
                vm_runner=None) -> int:
    # ORC must write where orca can find it: the generated <output> names a guest directory,
    # which is what makes pulling the results possible at all. 'Invoke-OrcVM' did not need
    # this because it never brought anything back.
    config_text = build_local_config(temp_dir=guest.temp,
                                     public_key=public_key, upload=upload, bits=bits,
                                     allow_invalid_uri=allow_invalid_uri,
                                     enable_keys=enable_keys, disable_keys=disable_keys,
                                     keys=keys, local_config=local_config,
                                     output=guest.out)
    config_path = write_local_config(out_dir, config_text)

    runner = vm_runner or hyperv.run_vm_collect

    # One argv, used both to launch ORC in the guest and to record what ran, so the record
    # cannot drift from the run. Every path in it is a GUEST path.
    argv = orc_argv(guest.exe, out=guest.out, temp=guest.temp, local_config=guest.config,
                    extra=extra, overwrite=force)
    started = time.time()
    result = runner(vm=vm, checkpoint=checkpoint, local_exe=orc_to_run,
                    local_config=config_path, credential=credential, orc_args=argv[1:],
                    out_dir=out_dir, result_path=os.path.join(temp_dir, "guest-result.json"),
                    pull=pull, guest=guest)
    ended = time.time()

    exit_code = int(result.get("exit_code") or 0)
    record = collect_record(target=target, orc=orc, argv=argv, out_dir=out_dir,
                            temp_dir=temp_dir, started=started, ended=ended,
                            exit_code=exit_code, pulled=list(result.get("pulled") or []),
                            target_detail={"checkpoint": checkpoint, "pulled_from_guest": pull},
                            copied_to=copied_to)
    write_record(out_dir, record)

    if exit_code != 0:
        print(f"ORC exited {exit_code} in '{vm}'; results kept in '{out_dir}'")

    return exit_code
