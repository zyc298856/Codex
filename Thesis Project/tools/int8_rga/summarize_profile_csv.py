#!/usr/bin/env python3
"""Summarize rk_yolo_video profile_csv logs."""

from __future__ import annotations

import argparse
import csv
import glob
from pathlib import Path
from statistics import mean
from typing import Dict, Iterable, List


NUMERIC_FIELDS = [
    "prepare_ms",
    "input_set_or_update_ms",
    "rknn_run_ms",
    "outputs_get_ms",
    "decode_ms",
    "outputs_release_ms",
    "render_ms",
    "total_work_ms",
]


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Summarize profile_csv rows from log files.")
    parser.add_argument(
        "--logs",
        nargs="+",
        required=True,
        help="Log files or glob patterns containing profile_csv rows.",
    )
    parser.add_argument("--output", type=Path, help="Output summary CSV. Default: stdout.")
    return parser


def expand_patterns(patterns: Iterable[str]) -> List[Path]:
    files: List[Path] = []
    for pattern in patterns:
        matches = glob.glob(pattern)
        if matches:
            files.extend(Path(match) for match in matches)
        else:
            files.append(Path(pattern))
    return sorted({path for path in files if path.exists()})


def parse_log(path: Path) -> List[Dict[str, str]]:
    header: List[str] = []
    rows: List[Dict[str, str]] = []
    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw_line.strip().lstrip("\ufeff")
        if line.startswith("profile_csv_header,"):
            header = line.split(",")[1:]
            continue
        if line.startswith("profile_csv,") and header:
            values = line.split(",")[1:]
            if len(values) == len(header):
                rows.append(dict(zip(header, values)))
    return rows


def summarize_file(path: Path) -> Dict[str, str]:
    rows = parse_log(path)
    result: Dict[str, str] = {
        "log": path.as_posix(),
        "frames": str(len(rows)),
        "input_modes": "",
        "detections_total": "0",
    }
    if not rows:
        for field in NUMERIC_FIELDS:
            result[f"mean_{field}"] = ""
        return result

    result["input_modes"] = "|".join(sorted({row.get("input_mode", "") for row in rows}))
    detections_total = 0
    for row in rows:
        try:
            detections_total += int(float(row.get("detections", "0")))
        except ValueError:
            pass
    result["detections_total"] = str(detections_total)

    for field in NUMERIC_FIELDS:
        values: List[float] = []
        for row in rows:
            try:
                values.append(float(row.get(field, "")))
            except ValueError:
                pass
        result[f"mean_{field}"] = f"{mean(values):.3f}" if values else ""
    return result


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    log_files = expand_patterns(args.logs)
    if not log_files:
        raise FileNotFoundError(f"no log files matched: {args.logs}")

    rows = [summarize_file(path) for path in log_files]
    fieldnames = ["log", "frames", "input_modes", "detections_total"] + [
        f"mean_{field}" for field in NUMERIC_FIELDS
    ]

    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(rows)
        print(f"wrote={args.output}")
    else:
        writer = csv.DictWriter(__import__("sys").stdout, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
