"""orca CLI. Terse help: one line per option."""
import argparse
import sys

from . import (check, cms, collect, extract, getthis, ingest, localconfig, logs, merge,
               outcome, outline, report, diff)


def orc_passthrough(remainder: list[str]) -> list[str]:
    """argparse keeps the '--' separator in REMAINDER; ORC must not see it."""
    items = list(remainder or [])
    if items and items[0] == "--":
        return items[1:]

    return items


def view_names(value: str) -> list[str]:
    """One '--view' value: a name, or a comma list of them.

    Validated here rather than by argparse 'choices', which cannot see inside a comma list.
    An unknown name is refused rather than ignored: a view that silently matched nothing would
    print a host header and read as a clean run.
    """
    names = [name.strip() for name in value.split(",") if name.strip()]
    unknown = [name for name in names if name not in report.VIEW_NAMES]
    if unknown:
        raise argparse.ArgumentTypeError(
            f'unknown view {", ".join(unknown)} (pick from {", ".join(report.VIEW_NAMES)})')

    return names


def context_count(value: str) -> int:
    """The optional count of '--backtrace N': how many log lines either side to print."""
    try:
        count = int(value)
    except ValueError:
        raise argparse.ArgumentTypeError(f"'{value}' is not a number of lines")

    if count < 1:
        raise argparse.ArgumentTypeError(
            f"{count} lines either side is no context; leave the count out for "
            f"{logs.DEFAULT_CONTEXT}")

    return count


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
    parser.add_argument("paths", nargs="+",
                        help="collection(s), ingested dataset(s), or a directory holding either")
    parser.add_argument("--out", default=None,
                        help=f"output parquet (default: <dataset>/{mode}.parquet)")
    parser.add_argument("--json", action="store_true",
                        help="print the rows instead (no parquet)")
    parser.add_argument("--recurse", action="store_true",
                        help="look in every directory under the ones given")
    parser.add_argument("--private-key", default=None,
                        help="PEM key to unseal a sealed Outcome or Outline")
    parser.add_argument("--cms-backend", default=None, choices=list(cms.BACKENDS),
                        help="pin the .p7b decryption backend")
    parser.add_argument("--work-dir", default=None,
                        help="keep the unsealed sidecars here instead of throwing them away")
    parser.add_argument("--refresh", action="store_true",
                        help="unseal again even if the sidecars are already there")
    parser.add_argument("--engine", default="duckdb", help="compute engine (default: duckdb)")
    return parser


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="orca",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description="Analyse DFIR-ORC results. orca turns a collection - the archives ORC "
                    "wrote, with its Outcome, Outlines and log - into per-table parquet you "
                    "can query, summarise and diff across versions, without ever expanding "
                    "the collected tree. Standard library only: it shells out to duckdb.exe "
                    "and 7z.exe.",
        epilog="the usual way through:\n"
               "  orca collect --offline w10.dd --orc build\\orc.exe --out E:\\dumps\\w10\n"
               "  orca check   E:\\dumps\\w10           is the collection intact\n"
               "  orca report  E:\\dumps\\w10           did every command succeed\n"
               "  orca ingest  E:\\dumps\\w10           collection -> dataset\n"
               "  orca merge   E:\\dumps\\w10\\ORC_w10   one wide row per file\n"
               "  orca diff --run NEW --reference REF what changed between two of them")
    sub = parser.add_subparsers(dest="command", required=True)

    cl = sub.add_parser(
        "collect", help="run ORC on a disk image, a VHD or a VM -> a collection",
        description="Run ORC and leave a collection the other commands read. The target is a "
                    "disk image, a mounted directory, a VHD(X) attached read-only for the "
                    "run, or a Hyper-V VM - restored to a checkpoint, run through PowerShell "
                    "Direct, and its results pulled back along with whatever ORC printed. The "
                    "result directory holds the archives, the local.xml ORC ran with, "
                    "collect.json (the target, ORC's sha256, the argv, the timings and the "
                    "exit code) and the ORC binary itself, so what produced the results sits "
                    "beside them. ORC's own non-zero exit is returned rather than treated as "
                    "a failed collect: a partial collection is still worth diffing.")
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
    ing = sub.add_parser(
        "ingest", help="collection -> a dataset of per-table parquet + CSV",
        description="Turn a collection into a dataset: one parquet per collected table, the "
                    "same tables as CSV beside them, the Outcome, Outlines and logs copied "
                    "in, and the '.orca' bookkeeping a rebuild, a report and a diff go by. "
                    "The collected tree is never expanded - each table is read out of its "
                    "container and converted - so a 2 GB collection costs tables, not a tree. "
                    "A sealed '.p7b' is decrypted first and the plaintext kept beside it for "
                    "the next command. Every column is text: ORC's forensic CSVs carry cells "
                    "that would otherwise abort type detection, and text keeps schemas stable "
                    "across versions for diffing.")
    ing.add_argument("source", help="an ORC archive, or a directory holding a collection")
    ing.add_argument("--out", default=None, help="output run dir (default: beside the archive)")
    ing.add_argument("--work-dir", default=None,
                     help="put runs and decrypted output here instead of beside the archive")
    ing.add_argument("--collection", default=None,
                     help="when the directory holds several runs, the one to ingest "
                          "(its computer name, its run name, or its Outcome filename)")
    ing.add_argument("--refresh", action="store_true",
                     help="re-decrypt and re-ingest even if products are already there")
    ing.add_argument("--jobs", type=int, default=None, help="parallel workers (default: CPUs)")
    ing.add_argument("--strict", action="store_true",
        help="stop at the first collected file orca cannot "
             "read, instead of skipping it and saying so")
    ing.add_argument("--no-csv", action="store_true", help="skip CSV output")
    ing.add_argument("--no-trunc", action="store_true", help="skip truncated CSV")
    ing.add_argument("--exclude-cols", nargs="*", default=None, help="volatile columns to drop")
    ing.add_argument("--password", default=None, help="archive password")
    ing.add_argument("--private-key", default=None, help="PEM key to decrypt a .p7b")
    ing.add_argument("--cms-backend", default=None, choices=list(cms.BACKENDS),
                     help="pin the .p7b decryption backend")
    ing.add_argument("--engine", default="duckdb", help="compute engine (default: duckdb)")

    ex = sub.add_parser(
        "extract", help="collection -> the expanded tree (decrypts, unnests)",
        description="Expand a collection into a directory tree, decrypting and unnesting as "
                    "it goes - the successor to Expand-OrcArchive. For when you genuinely "
                    "want the collected files on disk; prefer 'ingest' when you want to query "
                    "the data. '--depth' says how many layers of container to unwrap, "
                    "counting the '.p7b' envelope as the first. Nothing is written back into "
                    "the collection: it is evidence, and reading it must not change it.")
    ex.add_argument("collection", help="an ORC archive, or a directory holding a collection")
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
    ex.add_argument("--strict", action="store_true",
                    help="stop at the first archive or container that cannot be opened, "
                         "instead of skipping it and saying so")
    ex.add_argument("--depth", type=int, default=None,
                    help="stop after this many layers of container: 1 decrypts the .p7b, "
                         "2 also expands the archive, 3 also expands its containers "
                         "(default: all of them)")

    ck = sub.add_parser(
        "check", help="verify a collection's integrity without expanding it",
        description="Verify a collection is intact without expanding it. Three layers, "
                    "cheapest first: the size and sha1 ORC recorded in its Outcome, then the "
                    "CMS envelope and the journal inside it, then '7z t' over every "
                    "container, which decompresses each entry and recomputes its CRC rather "
                    "than trusting the one the archive claims. Containers are carried out one "
                    "at a time, so peak disk is one container per level of nesting rather "
                    "than the whole tree. Exit code 1 if anything failed, which is the point "
                    "of it in a pipeline.")
    ck.add_argument("collection", help="an ORC archive, or a directory holding a collection")
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

    mg = sub.add_parser(
        "merge", help="dataset -> wide dfir-orc.parquet (ingests if needed)",
        description="One wide row per file: every file-keyed table of a dataset joined on "
                    "(VolumeID, SnapshotID, FRN) into dfir-orc.parquet, so a cross-tool "
                    "question needs no joins of yours. Scalar by default - the most recent "
                    "value per column, by each tool's own timestamp - while '--lossless' "
                    "keeps every distinct value as a recency-ordered array and costs "
                    "noticeably more. Hand it a collection or an archive instead of a dataset "
                    "and it is ingested first, then merged.")
    mg.add_argument("dataset", help="a dataset, or a collection to ingest first")
    mg.add_argument("--out", default=None,
                    help="output parquet (default: <dataset>/dfir-orc.parquet)")
    mg.add_argument("--lossless", action="store_true", help="keep every value as arrays (slow)")
    mg.add_argument("--ts-col", nargs="*", default=None, help="override tool timestamp: Tool=Column")
    mg.add_argument("--password", default=None, help="archive password")
    mg.add_argument("--private-key", default=None, help="PEM key to decrypt a .p7b")
    mg.add_argument("--cms-backend", default=None, choices=list(cms.BACKENDS),
                    help="pin the .p7b decryption backend")
    mg.add_argument("--work-dir", default=None,
                    help="put the dataset and decrypted output here instead of beside the "
                         "archive")
    mg.add_argument("--refresh", action="store_true",
                    help="re-ingest even if a dataset is already there")
    mg.add_argument("--strict", action="store_true",
       help="stop at the first collected file orca cannot "
            "read, instead of skipping it and saying so")
    mg.add_argument("--engine", default="duckdb", help="compute engine (default: duckdb)")

    rp = sub.add_parser(
        "report", help="collection(s) or dataset(s) -> per-host views, or a row table",
        description="Is this collection healthy. Per host: whether every command succeeded "
                    "and which did not, the tables an ingest produced and which came out "
                    "empty, the logs carrying errors and what the critical ones said. It "
                    "reads ORC's own Outcome, Outlines and log - unsealing them when they "
                    "are encrypted, and pulling each command's log out of the archive it is "
                    "in - so most of it works on a collection as it arrives, before anything "
                    "is ingested; "
                    "only the two views built from '.orca' need a dataset, and they name the "
                    "command that would fill them rather than running it. Every Outcome in a "
                    "directory is a run of its own, so a merged dump of a fleet reports every "
                    "machine in it. Two row tables are modes: 'orca report outcome' (one row "
                    "per command run) and 'orca report outline' (one row per ORC run).",
        epilog="orca report outcome E:\\datasets --recurse --out fleet.parquet")
    rp.add_argument("paths", nargs="+",
                    help="collection(s), ingested dataset(s), or a directory holding either")
    rp.add_argument("--view", action="extend", type=view_names, default=None,
                    metavar="NAME[,NAME...]",
                    help=f'view(s) to show: {" | ".join(report.VIEW_NAMES)}')
    rp.add_argument("--backtrace", nargs="?", type=context_count,
                    const=logs.DEFAULT_CONTEXT, default=None, metavar="LINES",
                    help="with a message view: print the LINES log lines either side of each "
                         f"message (default: {logs.DEFAULT_CONTEXT})")
    rp.add_argument("--no-archive-logs", dest="archive_logs", action="store_false",
                    help="do not pull the per-command logs out of the archives")
    shape = rp.add_mutually_exclusive_group()
    shape.add_argument("--one-line", action="store_true", help="one summary line per host")
    shape.add_argument("--json", action="store_true", help="one JSON object per host")
    shape.add_argument("--list", action="store_true",
                       help="one line per command, every host, no headers")
    rp.add_argument("--failed", action="store_true",
                    help="with --list: only the commands that did not exit 0")
    rp.add_argument("--recurse", action="store_true",
                    help="look in every directory under the ones given")
    rp.add_argument("--ignore", default=None,
                    help="JSON list of names (fnmatch) this report should pass over")
    rp.add_argument("--private-key", default=None,
                    help="PEM key to unseal a sealed Outcome, Outline or log")
    rp.add_argument("--cms-backend", default=None, choices=list(cms.BACKENDS),
                    help="pin the .p7b decryption backend")
    rp.add_argument("--work-dir", default=None,
                    help="keep the unsealed sidecars here instead of throwing them away")
    rp.add_argument("--refresh", action="store_true",
                    help="unseal again even if the sidecars are already there")
    rp.add_argument("--engine", default="duckdb", help="compute engine (default: duckdb)")

    gt = sub.add_parser(
        "getthis", help="dataset -> getthis.parquet + what each container collected",
        description="What GetThis actually collected: how many files each container archive "
                    "brought back, and getthis.parquet - one row per collected sample, with "
                    "its hashes, sizes, the eight MFT and filename timestamps and which rule "
                    "matched. Not the same thing as the merged table, which is one row per "
                    "file: a file collected twice, as two ADS or by two command sets, is two "
                    "samples here and one row there. Hand it a collection or an archive and "
                    "it is ingested first.")
    gt.add_argument("dataset", help="a dataset, or a collection to ingest first")
    gt.add_argument("--out", default=None,
                    help="output parquet (default: <dataset>/getthis.parquet)")
    gt.add_argument("--json", action="store_true", help="summary as JSON")
    gt.add_argument("--password", default=None, help="archive password")
    gt.add_argument("--private-key", default=None, help="PEM key to decrypt a .p7b")
    gt.add_argument("--cms-backend", default=None, choices=list(cms.BACKENDS),
                    help="pin the .p7b decryption backend")
    gt.add_argument("--work-dir", default=None,
                    help="put the dataset and decrypted output here instead of beside the "
                         "archive")
    gt.add_argument("--refresh", action="store_true",
                    help="re-ingest even if a dataset is already there")
    gt.add_argument("--strict", action="store_true",
       help="stop at the first collected file orca cannot "
            "read, instead of skipping it and saying so")
    gt.add_argument("--engine", default="duckdb", help="compute engine (default: duckdb)")

    df = sub.add_parser(
        "diff", help="compare two collections, two datasets, or two parquet/CSV files",
        description="Compare two collections, two datasets, or two table files. Whole runs "
                    "are compared in two layers: the file set and the binary CRC of every "
                    "leaf, then the rows of each table with volatile columns dropped, paired "
                    "on per-tool identity columns so a modification names the columns that "
                    "changed instead of hiding them past the right edge. Either side may be "
                    "an archive, a collection or a dataset; whatever needs ingesting is "
                    "ingested and cached, so comparing the same pair again is cheap.")
    df.add_argument("paths", nargs="*",
                    help="two parquet/CSV for a file-pair diff, else two archives/run dirs")
    df.add_argument("--run", default=None,
                    help="run A: archive (.7z/.p7b), collection dir or ingested dataset")
    df.add_argument("--reference", default=None, help="run B: same forms as --run")
    df.add_argument("--private-key", default=None, help="PEM key to decrypt a .p7b")
    df.add_argument("--password", default=None, help="archive password")
    df.add_argument("--cms-backend", default=None, choices=list(cms.BACKENDS),
                    help="pin the .p7b decryption backend")
    df.add_argument("--work-dir", default=None,
                    help="put runs and decrypted output here instead of beside each archive")
    df.add_argument("--refresh", action="store_true",
                    help="re-decrypt and re-ingest even if products are already there")
    df.add_argument("--strict", action="store_true",
       help="stop at the first collected file orca cannot "
            "read, instead of skipping it and saying so")
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

    return parser


def main(argv=None) -> int:
    items = list(sys.argv[1:] if argv is None else argv)
    mode = report_table_mode(items)
    if mode:
        args = _build_mode_parser(mode).parse_args(items[2:])
        runner = outcome.run_outcome if mode == "outcome" else outline.run_outline
        runner(args.paths, out=args.out, engine_name=args.engine,
               as_json=args.json, private_key=args.private_key,
               cms_backend=args.cms_backend, work_dir=args.work_dir, refresh=args.refresh,
               recurse=args.recurse)
        return 0

    args = _build_parser().parse_args(items)
    if args.command == "ingest":
        ingest.run_ingest(
            args.source, args.out, collection_name=args.collection,
            jobs=args.jobs, make_csv=not args.no_csv, make_trunc=not args.no_trunc,
            exclude_cols=args.exclude_cols, password=args.password,
            private_key=args.private_key, cms_backend=args.cms_backend,
            engine_name=args.engine, work_dir=args.work_dir, refresh=args.refresh,
            strict=args.strict,
        )
    elif args.command == "extract":
        extract.run_extract(args.collection, args.out, password=args.password,
                            private_key=args.private_key, cms_backend=args.cms_backend,
                            work_dir=args.work_dir, depth=args.depth, refresh=args.refresh,
                            strict=args.strict)
    elif args.command == "check":
        return check.run_check(args.collection, password=args.password,
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
        merge.run_merge(args.dataset, out=args.out, lossless=args.lossless,
                        ts_overrides=overrides or None, engine_name=args.engine,
                        password=args.password, private_key=args.private_key,
                        cms_backend=args.cms_backend, work_dir=args.work_dir,
                        refresh=args.refresh, strict=args.strict)
    elif args.command == "getthis":
        getthis.run_getthis(args.dataset, out=args.out, engine_name=args.engine,
                            as_json=args.json, password=args.password,
                            private_key=args.private_key, cms_backend=args.cms_backend,
                            work_dir=args.work_dir, refresh=args.refresh,
                            strict=args.strict)
    elif args.command == "report":
        report.run_report(args.paths, views=args.view,
                          one_line=args.one_line, as_json=args.json,
                          list_commands=args.list, failed_only=args.failed,
                          private_key=args.private_key, cms_backend=args.cms_backend,
                          work_dir=args.work_dir, refresh=args.refresh,
                          ignore_file=args.ignore, recurse=args.recurse,
                          context=args.backtrace, archive_logs=args.archive_logs,
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
                      strict=args.strict)
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
