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
    "repeatStableLongestChunk",
    "repeatStableLongestChunkSpan",
    "repeatStableTotalChunkSpan",
    "maxWorstPairRatio",
    "statuses",
    "focusOut",
    "summary",
    "repeatStableSummary",
]

FOCUS_PRESETS = {
    "post-pocket-novelty": [
        "mul-full-audit-pocket",
        "mul-backend-gap",
        "mul-toom4-top",
        "mul-toom5-smoke",
        "mul-toom-div-transition",
        "mul-combo-handoff-boundary",
        "mul-sparse",
    ],
    "sparse-paper": [
        "mul-sparse",
    ],
}

PRESET_TIMEOUT_SECONDS = {
    "post-pocket-novelty": 60.0,
    "sparse-paper": 45.0,
}


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
    parser.add_argument(
        "--preset",
        action="append",
        choices=sorted(FOCUS_PRESETS),
        default=[],
        help="Named focus bundle. Repeatable; explicit --focus labels are appended after presets.",
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
        "--timeout-seconds",
        type=float,
        help="Optional per xray_cli invocation timeout forwarded to bench_focus_repeat.py",
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


def expand_focuses(presets: list[str], focuses: list[str]) -> list[str]:
    expanded: list[str] = []
    seen: set[str] = set()
    for preset in presets:
        for focus in FOCUS_PRESETS[preset]:
            if focus not in seen:
                expanded.append(focus)
                seen.add(focus)
    for focus in focuses:
        if focus not in seen:
            expanded.append(focus)
            seen.add(focus)
    return expanded


def effective_timeout_seconds(presets: list[str], requested: float | None) -> float | None:
    if requested is not None:
        return requested
    defaults = [PRESET_TIMEOUT_SECONDS[preset] for preset in presets if preset in PRESET_TIMEOUT_SECONDS]
    return min(defaults) if defaults else None


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
                "repeatStableLongestChunk": row.get("repeatStableLongestChunk", ""),
                "repeatStableLongestChunkSpan": row.get("repeatStableLongestChunkSpan", ""),
                "repeatStableTotalChunkSpan": row.get("repeatStableTotalChunkSpan", ""),
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


def parse_int(text: str) -> int:
    try:
        return int(text)
    except (TypeError, ValueError):
        return 0


def parse_float(text: str) -> float:
    try:
        return float(text)
    except (TypeError, ValueError):
        return float("inf")


def has_repeat_stable_chunk(row: dict[str, str]) -> bool:
    chunks = row.get("repeatStableSafeChunks", "")
    return bool(chunks and chunks != "none")


BLOCKED_STATUS_TOKENS = (
    "baseline-faster",
    "blocked",
    "incomplete",
    "lower-bound",
    "mismatch",
    "no-complete",
    "no-margin",
    "regression",
    "timeout",
)


def has_blocked_status(row: dict[str, str]) -> bool:
    statuses = row.get("statuses", "").lower()
    return any(token in statuses for token in BLOCKED_STATUS_TOKENS)


def is_audit_candidate(row: dict[str, str]) -> bool:
    if not has_repeat_stable_chunk(row):
        return False
    if has_blocked_status(row):
        return False
    return parse_float(row.get("maxWorstPairRatio", "")) <= 1.0


def ranked_matrix_rows(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    candidates = [row for row in rows if has_repeat_stable_chunk(row)]
    return sorted(
        candidates,
        key=lambda row: (
            -parse_int(row.get("repeatStableLongestChunkSpan", "")),
            -parse_int(row.get("repeatStableTotalChunkSpan", "")),
            -parse_int(row.get("runsWithSafeChunks", "")),
            parse_float(row.get("maxWorstPairRatio", "")),
            row.get("focus", ""),
            row.get("operation", ""),
        ),
    )


def audit_candidate_rows(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    return [row for row in ranked_matrix_rows(rows) if is_audit_candidate(row)]


def print_matrix(rows: list[dict[str, str]]) -> None:
    display_fields = [
        "focus",
        "operation",
        "runsSeen",
        "runsWithSafeChunks",
        "repeatStableSafeChunks",
        "repeatStableLongestChunk",
        "repeatStableLongestChunkSpan",
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
    timeout_seconds: float | None,
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
    if timeout_seconds is not None:
        command.extend(["--timeout-seconds", f"{timeout_seconds:g}"])
    run_checked(command)
    return focus_out


def run_self_test() -> int:
    assert expand_focuses(["sparse-paper"], []) == ["mul-sparse"]
    assert expand_focuses(["sparse-paper"], ["mul-sparse", "mul-backend-gap"]) == [
        "mul-sparse",
        "mul-backend-gap",
    ]
    assert expand_focuses(["post-pocket-novelty"], ["mul-sparse"]) == FOCUS_PRESETS["post-pocket-novelty"]
    assert effective_timeout_seconds(["sparse-paper"], None) == 45.0
    assert effective_timeout_seconds(["post-pocket-novelty"], None) == 60.0
    assert effective_timeout_seconds(["post-pocket-novelty"], 12.0) == 12.0

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
                    "repeatStableLongestChunk",
                    "repeatStableLongestChunkSpan",
                    "repeatStableTotalChunkSpan",
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
                    "repeatStableLongestChunk": "11717-16384",
                    "repeatStableLongestChunkSpan": "4668",
                    "repeatStableTotalChunkSpan": "4668",
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
        assert rows[0]["repeatStableLongestChunkSpan"] == "4668"
        ranked = ranked_matrix_rows(
            rows
            + [
                {
                    "focus": "focus-b",
                    "operation": "op-b",
                    "runsSeen": "3",
                    "runsWithSafeChunks": "3",
                    "repeatStableSafeChunks": "11717",
                    "repeatStableChunkCount": "1",
                    "repeatStableLongestChunk": "11717",
                    "repeatStableLongestChunkSpan": "1",
                    "repeatStableTotalChunkSpan": "1",
                    "maxWorstPairRatio": "1.100000",
                    "statuses": "worst-pair-regression",
                },
                {
                    "focus": "focus-c",
                    "operation": "op-c",
                    "runsSeen": "3",
                    "runsWithSafeChunks": "0",
                    "repeatStableSafeChunks": "none",
                    "repeatStableChunkCount": "0",
                    "repeatStableLongestChunk": "none",
                    "repeatStableLongestChunkSpan": "0",
                    "repeatStableTotalChunkSpan": "0",
                    "maxWorstPairRatio": "0.900000",
                    "statuses": "clean",
                },
                {
                    "focus": "focus-d",
                    "operation": "op-d",
                    "runsSeen": "3",
                    "runsWithSafeChunks": "3",
                    "repeatStableSafeChunks": "20000-22000",
                    "repeatStableChunkCount": "1",
                    "repeatStableLongestChunk": "20000-22000",
                    "repeatStableLongestChunkSpan": "2001",
                    "repeatStableTotalChunkSpan": "2001",
                    "maxWorstPairRatio": "0.990000",
                    "statuses": "candidate-faster",
                },
                {
                    "focus": "focus-e",
                    "operation": "op-e",
                    "runsSeen": "3",
                    "runsWithSafeChunks": "3",
                    "repeatStableSafeChunks": "24000-26000",
                    "repeatStableChunkCount": "1",
                    "repeatStableLongestChunk": "24000-26000",
                    "repeatStableLongestChunkSpan": "2001",
                    "repeatStableTotalChunkSpan": "2001",
                    "maxWorstPairRatio": "0.980000",
                    "statuses": "backend-regression",
                },
            ]
        )
        assert [row["operation"] for row in ranked] == ["op-a", "op-e", "op-d", "op-b"]
        audit_ready = audit_candidate_rows(ranked)
        assert [row["operation"] for row in audit_ready] == ["op-d"]
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
    focuses = expand_focuses(args.preset, args.focus)
    timeout_seconds = effective_timeout_seconds(args.preset, args.timeout_seconds)
    if not args.cli or not focuses:
        raise SystemExit("--cli and at least one --focus or --preset are required unless --self-test is set")
    if args.runs < 1:
        raise SystemExit("--runs must be at least 1")
    if timeout_seconds is not None and timeout_seconds <= 0:
        raise SystemExit("--timeout-seconds must be greater than 0")

    script_dir = Path(__file__).resolve().parent
    repeat_script = script_dir / "bench_focus_repeat.py"
    cli = Path(args.cli)
    out_dir = Path(args.out) if args.out else default_out_dir()
    out_dir.mkdir(parents=True, exist_ok=True)

    matrix: list[dict[str, str]] = []
    for focus in focuses:
        focus_out = run_focus_repeat(
            repeat_script,
            cli,
            focus,
            args.runs,
            out_dir,
            args.progress_filter,
            args.keep_all_progress_rows,
            timeout_seconds,
        )
        matrix.extend(matrix_rows_for_focus(focus, focus_out))

    matrix_path = out_dir / "matrix.tsv"
    write_matrix(matrix_path, matrix)
    ranked = ranked_matrix_rows(matrix)
    ranked_path = out_dir / "matrix_ranked.tsv"
    write_matrix(ranked_path, ranked)
    audit_candidates = audit_candidate_rows(matrix)
    audit_candidates_path = out_dir / "matrix_audit_candidates.tsv"
    write_matrix(audit_candidates_path, audit_candidates)
    print(f"runs={args.runs}")
    if args.preset:
        print(f"presets={','.join(args.preset)}")
    if timeout_seconds is not None:
        print(f"timeoutSeconds={timeout_seconds:g}")
    print(f"out={out_dir}")
    print(f"matrix={matrix_path}")
    print(f"rankedMatrix={ranked_path}")
    print(f"auditCandidates={audit_candidates_path}")
    if matrix:
        print_matrix(matrix)
    else:
        print("No repeat-stable rows were produced.")
    print()
    if ranked:
        print("Ranked repeat-stable candidates")
        print_matrix(ranked)
    else:
        print("No repeat-stable candidate rows.")
    print()
    if audit_candidates:
        print("Audit-ready repeat-stable candidates")
        print_matrix(audit_candidates)
    else:
        print("No audit-ready repeat-stable candidates.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
