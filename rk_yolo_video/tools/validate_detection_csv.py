#!/usr/bin/env python3

import argparse
import csv
import json
import sys
from pathlib import Path
from typing import Tuple


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate an rk_yolo_video detections CSV against a known-good baseline."
    )
    parser.add_argument("csv_path", help="Path to the detections CSV file.")
    parser.add_argument(
        "--baseline",
        default="",
        help="Optional baseline JSON file with expected frame and detection counts.",
    )
    parser.add_argument("--expected-frames", type=int, default=-1)
    parser.add_argument("--expected-frames-with-detections", type=int, default=-1)
    parser.add_argument("--expected-total-detections", type=int, default=-1)
    return parser.parse_args()


def load_baseline(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def summarize_csv(path: Path) -> Tuple[int, int, int]:
    total_detections = 0
    frames_with_detections = set()
    max_frame_index = 0

    with path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            total_detections += 1
            frame_index = int(row["frame_index"])
            frames_with_detections.add(frame_index)
            if frame_index > max_frame_index:
                max_frame_index = frame_index

    return max_frame_index, len(frames_with_detections), total_detections


def main() -> int:
    args = parse_args()
    csv_path = Path(args.csv_path).expanduser().resolve()
    if not csv_path.is_file():
        print(f"CSV not found: {csv_path}", file=sys.stderr)
        return 1

    expected_frames = args.expected_frames
    expected_frames_with_detections = args.expected_frames_with_detections
    expected_total_detections = args.expected_total_detections

    if args.baseline:
        baseline = load_baseline(Path(args.baseline).expanduser().resolve())
        expected_frames = baseline.get("expected_frames", expected_frames)
        expected_frames_with_detections = baseline.get(
            "expected_frames_with_detections", expected_frames_with_detections
        )
        expected_total_detections = baseline.get(
            "expected_total_detections", expected_total_detections
        )

    actual_frames, actual_frames_with_detections, actual_total_detections = summarize_csv(csv_path)

    print(f"csv={csv_path}")
    print(f"frames={actual_frames}")
    print(f"frames_with_detections={actual_frames_with_detections}")
    print(f"total_detections={actual_total_detections}")

    failures = []
    if expected_frames >= 0 and actual_frames != expected_frames:
        failures.append(f"expected frames={expected_frames}, got {actual_frames}")
    if (
        expected_frames_with_detections >= 0
        and actual_frames_with_detections != expected_frames_with_detections
    ):
        failures.append(
            "expected frames_with_detections="
            f"{expected_frames_with_detections}, got {actual_frames_with_detections}"
        )
    if expected_total_detections >= 0 and actual_total_detections != expected_total_detections:
        failures.append(
            f"expected total_detections={expected_total_detections}, got {actual_total_detections}"
        )

    if failures:
        print("validation failed:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 2

    print("validation passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
