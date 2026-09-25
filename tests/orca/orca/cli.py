"""orca CLI. Terse help: one line per option."""
import argparse
import sys

from . import cms, extract, ingest, merge, outcome, outline, report, diff, getthis


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
    ex.add_argument("--out", required=True, help="output directory")
    ex.add_argument("--password", default=None, help="archive password")
    ex.add_argument("--private-key", default=None, help="PEM key to decrypt a .p7b")
    ex.add_argument("--cms-backend", default=None, choices=list(cms.BACKENDS),
                    help="pin the .p7b decryption backend")

    mg = sub.add_parser("merge", help="run dir -> wide dfir-orc.parquet")
    mg.add_argument("--run", required=True, help="an ingested run dir")
    mg.add_argument("--out", default=None, help="output parquet (default: <run>/dfir-orc.parquet)")
    mg.add_argument("--lossless", action="store_true", help="keep every value as arrays (slow)")
    mg.add_argument("--ts-col", nargs="*", default=None, help="override tool timestamp: Tool=Column")
    mg.add_argument("--engine", default="duckdb", help="compute engine (default: duckdb)")

    rp = sub.add_parser("report", help="run dir(s) -> per-host analyze views")
    rp.add_argument("run_dirs", nargs="*", help="one or more run dirs")
    rp.add_argument("--root", default=None, help="discover run dirs under this parent")
    rp.add_argument("--view", action="append", default=None,
                    choices=["outcome", "empty", "stats", "errors"], help="view(s) to show")
    rp.add_argument("--one-line", action="store_true", help="one summary line per host")
    rp.add_argument("--json", action="store_true", help="summary JSON per host")
    rp.add_argument("--engine", default="duckdb", help="compute engine (default: duckdb)")

    oc = sub.add_parser("outcome", help="run dir(s) -> one queryable row per command run")
    oc.add_argument("run_dirs", nargs="*", help="one or more run dirs")
    oc.add_argument("--root", default=None, help="discover run dirs under this parent")
    oc.add_argument("--out", default=None, help="output parquet (default: <run>/outcome.parquet)")
    oc.add_argument("--json", action="store_true", help="print the rows instead (no parquet)")
    oc.add_argument("--engine", default="duckdb", help="compute engine (default: duckdb)")

    ol = sub.add_parser("outline", help="run dir(s) -> one queryable row per ORC run")
    ol.add_argument("run_dirs", nargs="*", help="one or more run dirs")
    ol.add_argument("--root", default=None, help="discover run dirs under this parent")
    ol.add_argument("--out", default=None, help="output parquet (default: <run>/outline.parquet)")
    ol.add_argument("--json", action="store_true", help="print the rows instead (no parquet)")
    ol.add_argument("--engine", default="duckdb", help="compute engine (default: duckdb)")

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
    args = _build_parser().parse_args(argv)
    if args.command == "ingest":
        ingest.run_ingest(
            args.archive, args.out,
            jobs=args.jobs, make_csv=not args.no_csv, make_trunc=not args.no_trunc,
            exclude_cols=args.exclude_cols, password=args.password,
            private_key=args.private_key, cms_backend=args.cms_backend,
            engine_name=args.engine, work_dir=args.work_dir, refresh=args.refresh,
        )
    elif args.command == "extract":
        extract.run_extract(args.archive, args.out, password=args.password,
                            private_key=args.private_key, cms_backend=args.cms_backend)
    elif args.command == "merge":
        overrides = {}
        for item in (args.ts_col or []):
            tool, _, column = item.partition("=")
            if tool and column:
                overrides[tool] = column
        merge.run_merge(args.run, out=args.out, lossless=args.lossless,
                        ts_overrides=overrides or None, engine_name=args.engine)
    elif args.command == "outcome":
        outcome.run_outcome(args.run_dirs, root=args.root, out=args.out,
                            engine_name=args.engine, as_json=args.json)
    elif args.command == "outline":
        outline.run_outline(args.run_dirs, root=args.root, out=args.out,
                            engine_name=args.engine, as_json=args.json)
    elif args.command == "getthis":
        getthis.run_getthis(args.run, out=args.out, engine_name=args.engine,
                            as_json=args.json)
    elif args.command == "report":
        report.run_report(args.run_dirs, root=args.root, views=args.view,
                          one_line=args.one_line, as_json=args.json,
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
                      cms_backend=args.cms_backend, exclude_files=args.exclude_files)
    return 0
