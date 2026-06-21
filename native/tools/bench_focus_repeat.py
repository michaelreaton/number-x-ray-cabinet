#!/usr/bin/env python3
"""Run a focused native benchmark several times and summarize safe chunks."""

from __future__ import annotations

import argparse
import csv
import datetime as _dt
import re
import subprocess
import sys
from pathlib import Path


SUMMARY_FIELDS = [
    "run",
    "operation",
    "status",
    "speedRatio",
    "worstPairRatio",
    "stablePairs",
    "safeSizes",
    "safeSizeChunks",
    "longestSafeSizeChunk",
    "longestSafeSizeChunkCount",
    "blockerReason",
    "artifact",
    "progressArtifact",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Repeat --bench-focus runs and summarize benchmark_progress.tsv safe chunks."
    )
    parser.add_argument("--cli", required=True, help="Path to xray_cli or xray_cli.exe")
    parser.add_argument("--focus", required=True, help="Focus label, for example mul-combo-handoff-pocket")
    parser.add_argument("--runs", type=int, default=3, help="Number of focused runs to execute")
    parser.add_argument("--out", help="Output directory for raw artifacts and summary.tsv")
    parser.add_argument(
        "--progress-filter",
        help="Optional text filter passed to --bench-progress-tsv before summarizing",
    )
    parser.add_argument(
        "--keep-all-progress-rows",
        action="store_true",
        help="Summarize every progress row instead of only aggregate policy-gate rows",
    )
    return parser.parse_args()


def safe_label(text: str) -> str:
    label = re.sub(r"[^A-Za-z0-9_.-]+", "-", text.strip())
    return label.strip("-") or "focus"


def default_out_dir(focus: str) -> Path:
    stamp = _dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    return Path("native-test-runs") / f"{stamp}-{safe_label(focus)}-repeat"


def run_capture(args: list[str]) -> str:
    completed = subprocess.run(args, text=True, capture_output=True, check=False)
    if completed.returncode != 0:
        sys.stderr.write(completed.stdout)
        sys.stderr.write(completed.stderr)
        raise SystemExit(completed.returncode)
    return completed.stdout


def progress_rows(progress_text: str) -> list[dict[str, str]]:
    lines = progress_text.splitlines()
    for index, line in enumerate(lines):
        if line.startswith("category\t"):
            reader = csv.DictReader(lines[index:], delimiter="\t")
            return [dict(row) for row in reader]
    return []


def rows_to_summarize(rows: list[dict[str, str]], keep_all: bool) -> list[dict[str, str]]:
    if keep_all:
        return rows
    aggregate = [row for row in rows if row.get("category") == "policy-gate"]
    if aggregate:
        return aggregate
    return [row for row in rows if row.get("operation") and not row.get("operation", "").endswith("-pt")]


def summarize_row(run_index: int, row: dict[str, str], artifact: Path, progress_artifact: Path) -> dict[str, str]:
    stable = ""
    if row.get("stableSampleCount") or row.get("sampleCount"):
        stable = f"{row.get('stableSampleCount', '')}/{row.get('sampleCount', '')}"
    return {
        "run": str(run_index),
        "operation": row.get("operation", ""),
        "status": row.get("status", ""),
        "speedRatio": row.get("speedRatio", ""),
        "worstPairRatio": row.get("worstPairRatio", ""),
        "stablePairs": stable,
        "safeSizes": row.get("safeSizes", ""),
        "safeSizeChunks": row.get("safeSizeChunks", ""),
        "longestSafeSizeChunk": row.get("longestSafeSizeChunk", ""),
        "longestSafeSizeChunkCount": row.get("longestSafeSizeChunkCount", ""),
        "blockerReason": row.get("blockerReason", ""),
        "artifact": str(artifact),
        "progressArtifact": str(progress_artifact),
    }


def write_summary(path: Path, rows: list[dict[str, str]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=SUMMARY_FIELDS, delimiter="\t")
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def print_table(rows: list[dict[str, str]]) -> None:
    display_fields = [
        "run",
        "operation",
        "status",
        "speedRatio",
        "worstPairRatio",
        "safeSizes",
        "safeSizeChunks",
        "longestSafeSizeChunk",
        "blockerReason",
    ]
    widths = {field: len(field) for field in display_fields}
    for row in rows:
        for field in display_fields:
            widths[field] = min(32, max(widths[field], len(row.get(field, ""))))

    def cell(field: str, value: str) -> str:
        text = value if len(value) <= widths[field] else value[: widths[field] - 1] + "."
        return text.ljust(widths[field])

    print(" | ".join(cell(field, field) for field in display_fields))
    print("-+-".join("-" * widths[field] for field in display_fields))
    for row in rows:
        print(" | ".join(cell(field, row.get(field, "")) for field in display_fields))


def main() -> int:
    args = parse_args()
    if args.runs < 1:
        raise SystemExit("--runs must be at least 1")

    cli = Path(args.cli)
    out_dir = Path(args.out) if args.out else default_out_dir(args.focus)
    out_dir.mkdir(parents=True, exist_ok=True)

    summary: list[dict[str, str]] = []
    for run_index in range(1, args.runs + 1):
        artifact = out_dir / f"run{run_index:02d}.benchmark.tsv"
        progress_artifact = out_dir / f"run{run_index:02d}.progress.tsv"

        bench_text = run_capture([str(cli), "--bench-focus", args.focus, "--bench-tsv"])
        artifact.write_text(bench_text, encoding="utf-8", newline="")

        progress_cmd = [str(cli), "--bench-progress-tsv", str(artifact)]
        if args.progress_filter:
            progress_cmd.extend(["--bench-filter", args.progress_filter])
        progress_text = run_capture(progress_cmd)
        progress_artifact.write_text(progress_text, encoding="utf-8", newline="")

        rows = rows_to_summarize(progress_rows(progress_text), args.keep_all_progress_rows)
        for row in rows:
            summary.append(summarize_row(run_index, row, artifact, progress_artifact))

    summary_path = out_dir / "summary.tsv"
    write_summary(summary_path, summary)
    print(f"focus={args.focus}")
    print(f"runs={args.runs}")
    print(f"out={out_dir}")
    print(f"summary={summary_path}")
    if summary:
        print_table(summary)
    else:
        print("No progress rows matched the summary filter.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
