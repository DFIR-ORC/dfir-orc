"""orca CLI. Terse help: one line per option."""
import argparse
import sys

from . import (check, cms, collect, extract, ingest, localconfig, merge, outcome, outline,
               report, diff, getthis)


def orc_passthrough(remainder: list[str]) -> list[str]:
    """argparse keeps the '--' separator in REMAINDER; ORC must not see it."""
    items = list(remainder or [])
    if items and items[0] == "--":
        return items[1:]

    return items


REPORT_TABLE_MODES = ("outcome", "outline")


def report_table_mode(argv: list[str]) -> str | None:
    """'outcome' or 'outline' when this argv asks for 'orca report <mode>', else None.

    Those two produce row tables rather than per-host views, so they are modes of 'report'
    rather than flags on it. They cannot be argparse subparsers: 'report' already takes the run
    dirs as an optional positional, and argparse then reads 'orca report E:\\runs\\host' as a
    mode named E:\\runs\\host and refuses it. So the mode word is recognised here and handed to
    a parser of its own. A directory actually named 'outcome' has to be given as '.\\outcome'.
    """
    if len(argv) >= 2 and argv[0] == "report" and argv[1] in REPORT_TABLE_MODES:
        return argv[1]

    return None


def _build_mode_parser(mode: str) -> argparse.ArgumentParser:
    """The parser for one 'orca report <mode>' row table. 'prog' is set so its --help names
    the command the way it is actually typed."""
    parser = argparse.ArgumentParser(
        prog=f"orca report {mode}",
        description=f"One queryable row per {'command run' if mode == 'outcome' else 'ORC run'}"
                    f", across every run dir given.")
    parser.add_argument("run_dirs", nargs="*", help="one or more run dirs")
    parser.add_argument("--root", default=None, help="discover run dirs under this parent")
    parser.add_argument("--out", default=None,
                        help=f"output parquet (default: <run>/{mode}.parquet)")
    parser.add_argument("--json", action="store_true",
                        help="print the rows instead (no parquet)")
    parser.add_argument("--engine", default="duckdb", help="compute engine (default: duckdb)")
    return parser


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="orca", description="ORC results analysis")
    sub = parser.add_subparsers(dest="command", required=True)

    ing = sub.add_parser("ingest", help="archive -> per-table parquet + CSV")
    ing.add_argument("archive", help="ORC .7z/.p7b file or a dir of results")
    ing.add_argument("--out", default=None, help="output run dir (default: beside the archive)")
    ing.add_argument("--work-dir", default=None,
                     help="put runs and decrypted output here instead of beside the archive")
    ing.add_argument("--refresh", action="store_true",
                     help="re-decrypt and re-ingest even if products are already there")
    ing.add_argument("--jobs", type=int, default=None, help="parallel workers (default: CPUs)")
    ing.add_argument("--skip-errors", action="store_true",
                     help="skip a table orca cannot read instead of failing the whole ingest")
    ing.add_argument("--no-csv", action="store_true", help="skip CSV output")
    ing.add_argument("--no-trunc", action="store_true", help="skip truncated CSV")
    ing.add_argument("--exclude-cols", nargs="*", default=None, help="volatile columns to drop")
    ing.add_argument("--password", default=None, help="archive password")
    ing.add_argument("--private-key", default=None, help="PEM key to decrypt a .p7b")
    ing.add_argument("--cms-backend", default=None, choices=list(cms.BACKENDS),
                     help="pin the .p7b decryption backend")
    ing.add_argument("--engine", default="duckdb", help="compute engine (default: duckdb)")

    ex = sub.add_parser("extract", help="archive -> expanded tree (decrypts, unnests)")
    ex.add_argument("archive", help="ORC .7z/.p7b file or a dir with an Outcome json")
    ex.add_argument("--out", default=".", help="output directory (default: the current one)")
    ex.add_argument("--password", default=None, help="archive password")
    ex.add_argument("--private-key", default=None, help="PEM key to decrypt a .p7b")
    ex.add_argument("--cms-backend", default=None, choices=list(cms.BACKENDS),
                    help="pin the .p7b decryption backend")
    ex.add_argument("--work-dir", default=None,
                    help="keep the decrypted archive here instead of throwing it away, for "
                         "when the temp dir has no room for it or a later run will want it")
    ex.add_argument("--refresh", action="store_true",
                    help="re-decrypt and re-unseal even if the products are already there")
    ex.add_argument("--depth", type=int, default=None,
                    help="stop after this many layers of container: 1 decrypts the .p7b, "
                         "2 also expands the archive, 3 also expands its containers "
                         "(default: all of them)")

    ck = sub.add_parser("check", help="verify an archive's integrity without expanding it")
    ck.add_argument("archive", help="ORC .7z/.p7b file or a dir of results")
    ck.add_argument("--password", default=None, help="archive password")
    ck.add_argument("--private-key", default=None, help="PEM key to decrypt a .p7b")
    ck.add_argument("--cms-backend", default=None, choices=list(cms.BACKENDS),
                    help="pin the .p7b decryption backend")
    ck.add_argument("--work-dir", default=None,
                    help="keep the decrypted archive here instead of throwing it away, and "
                         "hold the one container at a time the walk needs on disk")
    ck.add_argument("--refresh", action="store_true",
                    help="re-decrypt and re-unseal even if the products are already there")
    ck.add_argument("--depth", type=int, default=None,
                    help="stop after this many layers of container: 1 opens the .p7b envelope, "
                         "2 also tests the archive, 3 also tests its containers "
                         "(default: all of them)")
    ck.add_argument("--no-outcome", action="store_true",
                    help="skip the size/sha1 check against the Outcome json")
    ck.add_argument("--one-line", action="store_true", help="one summary line")
    ck.add_argument("--json", action="store_true", help="summary JSON")

    mg = sub.add_parser("merge", help="run dir -> wide dfir-orc.parquet")
    mg.add_argument("--run", required=True, help="an ingested run dir")
    mg.add_argument("--out", default=None, help="output parquet (default: <run>/dfir-orc.parquet)")
    mg.add_argument("--lossless", action="store_true", help="keep every value as arrays (slow)")
    mg.add_argument("--ts-col", nargs="*", default=None, help="override tool timestamp: Tool=Column")
    mg.add_argument("--engine", default="duckdb", help="compute engine (default: duckdb)")

    rp = sub.add_parser("report", help="run dir(s) -> per-host views, or a row table",
                        description="Per-host views over ingested run dir(s). Two row tables "
                                    "are modes of their own: 'orca report outcome' (one row "
                                    "per command run) and 'orca report outline' (one row per "
                                    "ORC run).",
                        epilog="orca report outcome --root E:\\runs --out fleet.parquet")
    rp.add_argument("run_dirs", nargs="*", help="one or more run dirs")
    rp.add_argument("--root", default=None, help="discover run dirs under this parent")
    rp.add_argument("--view", action="append", default=None,
                    choices=["outcome", "empty", "stats", "errors"], help="view(s) to show")
    shape = rp.add_mutually_exclusive_group()
    shape.add_argument("--one-line", action="store_true", help="one summary line per host")
    shape.add_argument("--json", action="store_true", help="one JSON object per host")
    shape.add_argument("--list", action="store_true",
                       help="one line per command, every host, no headers")
    rp.add_argument("--failed", action="store_true",
                    help="with --list: only the commands that did not exit 0")
    rp.add_argument("--engine", default="duckdb", help="compute engine (default: duckdb)")

    gt = sub.add_parser("getthis", help="run dir -> getthis.parquet + collected/distinct counts")
    gt.add_argument("--run", required=True, help="an ingested run dir")
    gt.add_argument("--out", default=None, help="output parquet (default: <run>/getthis.parquet)")
    gt.add_argument("--json", action="store_true", help="summary as JSON")
    gt.add_argument("--engine", default="duckdb", help="compute engine (default: duckdb)")

    df = sub.add_parser("diff", help="compare two archives, run dirs, or parquet/CSV files")
    df.add_argument("paths", nargs="*",
                    help="two parquet/CSV for a file-pair diff, else two archives/run dirs")
    df.add_argument("--run", default=None, help="run A: archive (.7z/.p7b), result dir or run dir")
    df.add_argument("--reference", default=None, help="run B: same forms as --run")
    df.add_argument("--private-key", default=None, help="PEM key to decrypt a .p7b")
    df.add_argument("--password", default=None, help="archive password")
    df.add_argument("--cms-backend", default=None, choices=list(cms.BACKENDS),
                    help="pin the .p7b decryption backend")
    df.add_argument("--work-dir", default=None,
                    help="put runs and decrypted output here instead of beside each archive")
    df.add_argument("--refresh", action="store_true",
                    help="re-decrypt and re-ingest even if products are already there")
    df.add_argument("--skip-errors", action="store_true",
                    help="skip a table orca cannot read instead of failing the ingest")
    df.add_argument("--columns", nargs="*", default=None, help="restrict compare to these columns")
    df.add_argument("--exclude-cols", nargs="*", default=None, help="extra columns to exclude")
    df.add_argument("--exclude-files", nargs="*", default=None,
                    help="relpath glob(s) to ignore in the file-set diff, e.g. '*.log'")
    df.add_argument("--rows", action="store_true", help="also write differing rows to --out-dir")
    df.add_argument("--out-dir", default="orca-diff", help="row output dir (with --rows)")
    df.add_argument("--show", nargs="?", type=int, const=20, default=None,
                    help="preview up to N differing rows per changed table (default 20)")
    df.add_argument("--key", nargs="*", default=None,
                    help="pair NEW/REF rows on these columns (default: per-tool key)")
    df.add_argument("--patterns", type=int, default=10,
                    help="max change patterns shown per table (default 10)")
    df.add_argument("--one-line", action="store_true", help="one summary line")
    df.add_argument("--json", action="store_true", help="summary JSON")

    cl = sub.add_parser("collect", help="run ORC on a disk, a VHD or a VM -> archives")
    target = cl.add_mutually_exclusive_group(required=True)
    target.add_argument("--offline", default=None, help="disk image, mounted dir, or .vhd(x)")
    target.add_argument("--vm", default=None, help="Hyper-V virtual machine name")
    cl.add_argument("--orc", required=True, help="the configured ORC executable")
    cl.add_argument("--out", required=True, help="result directory")
    cl.add_argument("--temp", default=None, help="ORC's /TempDir= (default: a scratch dir)")
    cl.add_argument("--checkpoint", default=None, help="VM only: checkpoint to restore first")
    cl.add_argument("--credential", default=None,
                    help="VM only: .clixml/.xml or .json credential (default: prompt)")
    cl.add_argument("--no-pull", action="store_true",
                    help="VM only: leave the results in the guest or on the upload share")
    cl.add_argument("--public-key", default=None, help="PEM certificate: encrypt to .p7b")
    cl.add_argument("--upload", default=None, help="upload URI for the collected archives")
    cl.add_argument("--bits", action="store_true", help="upload with BITS instead of filecopy")
    cl.add_argument("--upload-operation", default=None, choices=list(localconfig.UPLOAD_OPERATIONS),
                    help="keep the uploaded archives locally, or delete them (default: copy)")
    cl.add_argument("--allow-invalid-uri", action="store_true",
                    help="emit the upload URI as one attribute, for %%VARIABLE%% values")
    cl.add_argument("--enable-key", nargs="*", default=None, dest="enable_keys",
                    help="ORC keyword(s) to enable on top of the embedded configuration")
    cl.add_argument("--disable-key", nargs="*", default=None, dest="disable_keys",
                    help="ORC keyword(s) to disable")
    cl.add_argument("--key", nargs="*", default=None, dest="keys",
                    help="run only these ORC keyword(s)")
    cl.add_argument("--local-config", default=None, help="a local.xml to merge over")
    cl.add_argument("--no-orc-copy", action="store_true",
                    help="run ORC from where it is instead of copying it into --out first")
    cl.add_argument("--force", action="store_true",
                    help="collect into a non-empty --out, and pass /overwrite to ORC")
    cl.add_argument("orc_args", nargs=argparse.REMAINDER,
                    help="arguments after '--' are passed to ORC verbatim")
    return parser


def main(argv=None) -> int:
    items = list(sys.argv[1:] if argv is None else argv)
    mode = report_table_mode(items)
    if mode:
        args = _build_mode_parser(mode).parse_args(items[2:])
        runner = outcome.run_outcome if mode == "outcome" else outline.run_outline
        runner(args.run_dirs, root=args.root, out=args.out, engine_name=args.engine,
               as_json=args.json)
        return 0

    args = _build_parser().parse_args(items)
    if args.command == "ingest":
        ingest.run_ingest(
            args.archive, args.out,
            jobs=args.jobs, make_csv=not args.no_csv, make_trunc=not args.no_trunc,
            exclude_cols=args.exclude_cols, password=args.password,
            private_key=args.private_key, cms_backend=args.cms_backend,
            engine_name=args.engine, work_dir=args.work_dir, refresh=args.refresh,
            skip_errors=args.skip_errors,
        )
    elif args.command == "extract":
        extract.run_extract(args.archive, args.out, password=args.password,
                            private_key=args.private_key, cms_backend=args.cms_backend,
                            work_dir=args.work_dir, depth=args.depth, refresh=args.refresh)
    elif args.command == "check":
        return check.run_check(args.archive, password=args.password,
                               private_key=args.private_key, cms_backend=args.cms_backend,
                               work_dir=args.work_dir, depth=args.depth, refresh=args.refresh,
                               outcome=not args.no_outcome, one_line=args.one_line,
                               as_json=args.json)
    elif args.command == "merge":
        overrides = {}
        for item in (args.ts_col or []):
            tool, _, column = item.partition("=")
            if tool and column:
                overrides[tool] = column
        merge.run_merge(args.run, out=args.out, lossless=args.lossless,
                        ts_overrides=overrides or None, engine_name=args.engine)
    elif args.command == "getthis":
        getthis.run_getthis(args.run, out=args.out, engine_name=args.engine,
                            as_json=args.json)
    elif args.command == "report":
        report.run_report(args.run_dirs, root=args.root, views=args.view,
                          one_line=args.one_line, as_json=args.json,
                          list_commands=args.list, failed_only=args.failed,
                          engine_name=args.engine)
    elif args.command == "diff":
        pa, pb = (args.paths + [None, None])[:2]
        diff.run_diff(path_a=pa, path_b=pb, run_a=args.run, run_b=args.reference,
                      columns=args.columns, exclude_cols=args.exclude_cols,
                      rows=args.rows, out_dir=args.out_dir,
                      one_line=args.one_line, as_json=args.json, show=args.show,
                      key=args.key, patterns=args.patterns,
                      work_dir=args.work_dir, refresh=args.refresh,
                      password=args.password, private_key=args.private_key,
                      cms_backend=args.cms_backend, exclude_files=args.exclude_files,
                      skip_errors=args.skip_errors)
    elif args.command == "collect":
        return collect.run_collect(
            orc=args.orc, out=args.out, offline=args.offline, vm=args.vm,
            checkpoint=args.checkpoint, temp=args.temp, public_key=args.public_key,
            upload=args.upload, bits=args.bits, allow_invalid_uri=args.allow_invalid_uri,
            enable_keys=args.enable_keys, disable_keys=args.disable_keys, keys=args.keys,
            local_config=args.local_config, credential=args.credential,
            pull=not args.no_pull, force=args.force, copy_orc=not args.no_orc_copy,
            upload_operation=args.upload_operation,
            orc_args=orc_passthrough(args.orc_args))
    return 0
