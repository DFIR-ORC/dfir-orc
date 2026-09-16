"""Per-output timing records and their CSV report."""
import csv
from dataclasses import dataclass, asdict, fields


@dataclass
class TimingRecord:
    name: str
    kind: str       # "parquet" | "csv" | "csv.trunc"
    rows: int
    bytes: int
    seconds: float


def write_timing_csv(records: list[TimingRecord], path: str) -> None:
    headers = [f.name for f in fields(TimingRecord)]
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=headers)
        writer.writeheader()
        for rec in records:
            writer.writerow(asdict(rec))


def summarize(records: list[TimingRecord]) -> str:
    total = sum(r.seconds for r in records)
    slowest = sorted(records, key=lambda r: r.seconds, reverse=True)[:5]
    lines = [f"{len(records)} files in {round(total, 1)}s; slowest:"]
    for r in slowest:
        lines.append(f"  {r.name} [{r.kind}] {round(r.seconds, 2)}s")
    return "\n".join(lines)
