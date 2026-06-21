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

REPEAT_STABLE_FIELDS = [
    "operation",
    "runsSeen",
    "runsWithSafeChunks",
    "repeatStableSafeChunks",
    "repeatStableChunkCount",
    "statuses",
    "minSpeedRatio",
    "maxSpeedRatio",
    "maxWorstPairRatio",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Repeat --bench-focus runs and summarize benchmark_progress.tsv safe chunks."
    )
    parser.add_argument("--cli", help="Path to xray_cli or xray_cli.exe")
    parser.add_argument("--focus", help="Focus label, for example mul-combo-handoff-pocket")
    parser.add_argument("--runs", type=int, default=3, help="Number of focused runs to execute")
    parser.add_argument("--out", help="Output directory for raw artifacts and summary.tsv")
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="Run the helper's safe-chunk intersection self-test without invoking xray_cli",
    )
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


def parse_float(text: str) -> float | None:
    try:
        return float(text)
    except (TypeError, ValueError):
        return None


def parse_safe_chunk_ranges(text: str) -> list[tuple[int, int]]:
    if not text or text == "none":
        return []
    ranges: list[tuple[int, int]] = []
    for token in text.split(","):
        token = token.strip()
        if not token:
            continue
        if "-" in token:
            left, right = token.split("-", 1)
        else:
            left = right = token
        try:
            start = int(left)
            end = int(right)
        except ValueError:
            continue
        if start > end:
            start, end = end, start
        ranges.append((start, end))
    return merge_ranges(ranges)


def merge_ranges(ranges: list[tuple[int, int]]) -> list[tuple[int, int]]:
    if not ranges:
        return []
    merged: list[tuple[int, int]] = []
    for start, end in sorted(ranges):
        if not merged or start > merged[-1][1]:
            merged.append((start, end))
        else:
            merged[-1] = (merged[-1][0], max(merged[-1][1], end))
    return merged


def intersect_ranges(left: list[tuple[int, int]], right: list[tuple[int, int]]) -> list[tuple[int, int]]:
    intersection: list[tuple[int, int]] = []
    left_index = 0
    right_index = 0
    while left_index < len(left) and right_index < len(right):
        left_start, left_end = left[left_index]
        right_start, right_end = right[right_index]
        start = max(left_start, right_start)
        end = min(left_end, right_end)
        if start <= end:
            intersection.append((start, end))
        if left_end < right_end:
            left_index += 1
        else:
            right_index += 1
    return merge_ranges(intersection)


def format_ranges(ranges: list[tuple[int, int]]) -> str:
    if not ranges:
        return "none"
    return ",".join(str(start) if start == end else f"{start}-{end}" for start, end in ranges)


def repeat_stable_rows(rows: list[dict[str, str]], total_runs: int) -> list[dict[str, str]]:
    by_operation: dict[str, list[dict[str, str]]] = {}
    for row in rows:
        operation = row.get("operation", "")
        if not operation:
            continue
        by_operation.setdefault(operation, []).append(row)

    stable_rows: list[dict[str, str]] = []
    for operation in sorted(by_operation):
        op_rows = by_operation[operation]
        runs_seen = {row.get("run", "") for row in op_rows if row.get("run", "")}
        chunk_sets = [parse_safe_chunk_ranges(row.get("safeSizeChunks", "")) for row in op_rows]
        runs_with_safe_chunks = {
            row.get("run", "") for row, chunks in zip(op_rows, chunk_sets) if row.get("run", "") and chunks
        }

        repeat_chunks: list[tuple[int, int]] = []
        if len(runs_seen) == total_runs and chunk_sets and all(chunk_sets):
            repeat_chunks = chunk_sets[0]
            for chunks in chunk_sets[1:]:
                repeat_chunks = intersect_ranges(repeat_chunks, chunks)
                if not repeat_chunks:
                    break

        speed_values = [value for value in (parse_float(row.get("speedRatio", "")) for row in op_rows) if value is not None]
        worst_values = [value for value in (parse_float(row.get("worstPairRatio", "")) for row in op_rows) if value is not None]
        statuses = sorted({row.get("status", "") for row in op_rows if row.get("status", "")})
        stable_rows.append(
            {
                "operation": operation,
                "runsSeen": str(len(runs_seen)),
                "runsWithSafeChunks": str(len(runs_with_safe_chunks)),
                "repeatStableSafeChunks": format_ranges(repeat_chunks),
                "repeatStableChunkCount": str(len(repeat_chunks)),
                "statuses": ",".join(statuses),
                "minSpeedRatio": f"{min(speed_values):.6f}" if speed_values else "",
                "maxSpeedRatio": f"{max(speed_values):.6f}" if speed_values else "",
                "maxWorstPairRatio": f"{max(worst_values):.6f}" if worst_values else "",
            }
        )
    return stable_rows


def write_rows(path: Path, rows: list[dict[str, str]], fields: list[str]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, delimiter="\t")
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def print_table(rows: list[dict[str, str]], display_fields: list[str]) -> None:
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


def run_self_test() -> int:
    rows = [
        {
            "run": "1",
            "operation": "op-a",
            "safeSizeChunks": "11717-16384,24103",
            "status": "clean",
            "speedRatio": "0.900000",
            "worstPairRatio": "1.010000",
        },
        {
            "run": "2",
            "operation": "op-a",
            "safeSizeChunks": "11717-14831",
            "status": "clean",
            "speedRatio": "0.800000",
            "worstPairRatio": "1.030000",
        },
        {
            "run": "3",
            "operation": "op-a",
            "safeSizeChunks": "11717,16384",
            "status": "clean",
            "speedRatio": "0.700000",
            "worstPairRatio": "1.020000",
        },
        {
            "run": "1",
            "operation": "op-b",
            "safeSizeChunks": "14831",
            "status": "noisy",
            "speedRatio": "1.100000",
            "worstPairRatio": "1.200000",
        },
        {
            "run": "2",
            "operation": "op-b",
            "safeSizeChunks": "none",
            "status": "noisy",
            "speedRatio": "1.200000",
            "worstPairRatio": "1.300000",
        },
        {
            "run": "3",
            "operation": "op-b",
            "safeSizeChunks": "14831",
            "status": "duplicate",
            "speedRatio": "1.000000",
            "worstPairRatio": "1.100000",
        },
    ]
    stable = repeat_stable_rows(rows, 3)
    by_operation = {row["operation"]: row for row in stable}
    assert by_operation["op-a"]["repeatStableSafeChunks"] == "11717"
    assert by_operation["op-a"]["runsWithSafeChunks"] == "3"
    assert by_operation["op-a"]["maxWorstPairRatio"] == "1.030000"
    assert by_operation["op-b"]["repeatStableSafeChunks"] == "none"
    assert by_operation["op-b"]["runsWithSafeChunks"] == "2"
    print("bench_focus_repeat self-test passed")
    return 0


def main() -> int:
    args = parse_args()
    if args.self_test:
        return run_self_test()
    if not args.cli or not args.focus:
        raise SystemExit("--cli and --focus are required unless --self-test is set")
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
    write_rows(summary_path, summary, SUMMARY_FIELDS)
    repeat_stable = repeat_stable_rows(summary, args.runs)
    repeat_stable_path = out_dir / "repeat_stable_chunks.tsv"
    write_rows(repeat_stable_path, repeat_stable, REPEAT_STABLE_FIELDS)
    print(f"focus={args.focus}")
    print(f"runs={args.runs}")
    print(f"out={out_dir}")
    print(f"summary={summary_path}")
    print(f"repeatStableSummary={repeat_stable_path}")
    if summary:
        print_table(
            summary,
            [
                "run",
                "operation",
                "status",
                "speedRatio",
                "worstPairRatio",
                "safeSizes",
                "safeSizeChunks",
                "longestSafeSizeChunk",
                "blockerReason",
            ],
        )
        print()
        print("Repeat-stable safe chunks")
        print_table(
            repeat_stable,
            [
                "operation",
                "runsSeen",
                "runsWithSafeChunks",
                "repeatStableSafeChunks",
                "maxWorstPairRatio",
                "statuses",
            ],
        )
    else:
        print("No progress rows matched the summary filter.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
