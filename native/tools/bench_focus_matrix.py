#!/usr/bin/env python3
"""Run several focused benchmarks and summarize repeat-stable safe chunks."""

from __future__ import annotations

import argparse
import csv
import datetime as _dt
import re
import subprocess
import sys
import tempfile
from pathlib import Path


MATRIX_FIELDS = [
    "focus",
    "operation",
    "runsSeen",
    "runsWithSafeChunks",
    "repeatStableSafeChunks",
    "repeatStableChunkCount",
    "maxWorstPairRatio",
    "statuses",
    "focusOut",
    "summary",
    "repeatStableSummary",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run multiple --bench-focus repeat sweeps and summarize repeat-stable chunks."
    )
    parser.add_argument("--cli", help="Path to xray_cli or xray_cli.exe")
    parser.add_argument(
        "--focus",
        action="append",
        default=[],
        help="Focus label to run. Repeat this flag for each focus.",
    )
    parser.add_argument("--runs", type=int, default=3, help="Repeats per focus")
    parser.add_argument("--out", help="Output directory for per-focus artifacts and matrix.tsv")
    parser.add_argument(
        "--progress-filter",
        help="Optional text filter forwarded to bench_focus_repeat.py",
    )
    parser.add_argument(
        "--keep-all-progress-rows",
        action="store_true",
        help="Forwarded to bench_focus_repeat.py for detailed row summaries",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="Run the matrix parser self-test without invoking xray_cli",
    )
    return parser.parse_args()


def safe_label(text: str) -> str:
    label = re.sub(r"[^A-Za-z0-9_.-]+", "-", text.strip())
    return label.strip("-") or "focus"


def default_out_dir() -> Path:
    stamp = _dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    return Path("native-test-runs") / f"{stamp}-focus-matrix"


def run_checked(args: list[str]) -> None:
    completed = subprocess.run(args, text=True, capture_output=True, check=False)
    if completed.returncode != 0:
        sys.stderr.write(completed.stdout)
        sys.stderr.write(completed.stderr)
        raise SystemExit(completed.returncode)


def read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return [dict(row) for row in csv.DictReader(handle, delimiter="\t")]


def matrix_rows_for_focus(focus: str, focus_out: Path) -> list[dict[str, str]]:
    repeat_summary = focus_out / "repeat_stable_chunks.tsv"
    summary_path = focus_out / "summary.tsv"
    rows = []
    for row in read_tsv(repeat_summary):
        rows.append(
            {
                "focus": focus,
                "operation": row.get("operation", ""),
                "runsSeen": row.get("runsSeen", ""),
                "runsWithSafeChunks": row.get("runsWithSafeChunks", ""),
                "repeatStableSafeChunks": row.get("repeatStableSafeChunks", ""),
                "repeatStableChunkCount": row.get("repeatStableChunkCount", ""),
                "maxWorstPairRatio": row.get("maxWorstPairRatio", ""),
                "statuses": row.get("statuses", ""),
                "focusOut": str(focus_out),
                "summary": str(summary_path),
                "repeatStableSummary": str(repeat_summary),
            }
        )
    return rows


def write_matrix(path: Path, rows: list[dict[str, str]]) -> None:
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=MATRIX_FIELDS, delimiter="\t")
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def print_matrix(rows: list[dict[str, str]]) -> None:
    display_fields = [
        "focus",
        "operation",
        "runsSeen",
        "runsWithSafeChunks",
        "repeatStableSafeChunks",
        "maxWorstPairRatio",
        "statuses",
    ]
    widths = {field: len(field) for field in display_fields}
    for row in rows:
        for field in display_fields:
            widths[field] = min(34, max(widths[field], len(row.get(field, ""))))

    def cell(field: str, value: str) -> str:
        width = widths[field]
        text = value if len(value) <= width else value[: width - 1] + "."
        return text.ljust(width)

    print(" | ".join(cell(field, field) for field in display_fields))
    print("-+-".join("-" * widths[field] for field in display_fields))
    for row in rows:
        print(" | ".join(cell(field, row.get(field, "")) for field in display_fields))


def run_focus_repeat(
    repeat_script: Path,
    cli: Path,
    focus: str,
    runs: int,
    out_dir: Path,
    progress_filter: str | None,
    keep_all_progress_rows: bool,
) -> Path:
    focus_out = out_dir / safe_label(focus)
    command = [
        sys.executable,
        str(repeat_script),
        "--cli",
        str(cli),
        "--focus",
        focus,
        "--runs",
        str(runs),
        "--out",
        str(focus_out),
    ]
    if progress_filter:
        command.extend(["--progress-filter", progress_filter])
    if keep_all_progress_rows:
        command.append("--keep-all-progress-rows")
    run_checked(command)
    return focus_out


def run_self_test() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        focus_out = Path(tmp) / "focus-a"
        focus_out.mkdir()
        (focus_out / "summary.tsv").write_text("run\toperation\n1\top-a\n", encoding="utf-8")
        with (focus_out / "repeat_stable_chunks.tsv").open("w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(
                handle,
                fieldnames=[
                    "operation",
                    "runsSeen",
                    "runsWithSafeChunks",
                    "repeatStableSafeChunks",
                    "repeatStableChunkCount",
                    "statuses",
                    "minSpeedRatio",
                    "maxSpeedRatio",
                    "maxWorstPairRatio",
                ],
                delimiter="\t",
            )
            writer.writeheader()
            writer.writerow(
                {
                    "operation": "op-a",
                    "runsSeen": "3",
                    "runsWithSafeChunks": "3",
                    "repeatStableSafeChunks": "11717-16384",
                    "repeatStableChunkCount": "1",
                    "statuses": "clean",
                    "minSpeedRatio": "0.700000",
                    "maxSpeedRatio": "0.900000",
                    "maxWorstPairRatio": "1.030000",
                }
            )
        rows = matrix_rows_for_focus("focus-a", focus_out)
        assert rows[0]["focus"] == "focus-a"
        assert rows[0]["operation"] == "op-a"
        assert rows[0]["repeatStableSafeChunks"] == "11717-16384"
        matrix_path = Path(tmp) / "matrix.tsv"
        write_matrix(matrix_path, rows)
        reread = read_tsv(matrix_path)
        assert reread[0]["maxWorstPairRatio"] == "1.030000"
    print("bench_focus_matrix self-test passed")
    return 0


def main() -> int:
    args = parse_args()
    if args.self_test:
        return run_self_test()
    if not args.cli or not args.focus:
        raise SystemExit("--cli and at least one --focus are required unless --self-test is set")
    if args.runs < 1:
        raise SystemExit("--runs must be at least 1")

    script_dir = Path(__file__).resolve().parent
    repeat_script = script_dir / "bench_focus_repeat.py"
    cli = Path(args.cli)
    out_dir = Path(args.out) if args.out else default_out_dir()
    out_dir.mkdir(parents=True, exist_ok=True)

    matrix: list[dict[str, str]] = []
    for focus in args.focus:
        focus_out = run_focus_repeat(
            repeat_script,
            cli,
            focus,
            args.runs,
            out_dir,
            args.progress_filter,
            args.keep_all_progress_rows,
        )
        matrix.extend(matrix_rows_for_focus(focus, focus_out))

    matrix_path = out_dir / "matrix.tsv"
    write_matrix(matrix_path, matrix)
    print(f"runs={args.runs}")
    print(f"out={out_dir}")
    print(f"matrix={matrix_path}")
    if matrix:
        print_matrix(matrix)
    else:
        print("No repeat-stable rows were produced.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
