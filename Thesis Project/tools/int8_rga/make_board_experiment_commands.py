#!/usr/bin/env python3
"""Generate RK3588 board-side commands for FP/INT8/RGA comparison runs."""

from __future__ import annotations

import argparse
import shlex
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List


@dataclass(frozen=True)
class ExperimentCase:
    name: str
    model_kind: str
    description: str
    env: Dict[str, str]


def quote(value: str) -> str:
    return shlex.quote(value)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Generate rk_yolo_video experiment commands.")
    parser.add_argument("--video", required=True, help="Board-side input video path.")
    parser.add_argument("--fp-model", required=True, help="Board-side FP RKNN model path.")
    parser.add_argument("--int8-model", help="Board-side INT8 RKNN model path.")
    parser.add_argument("--binary", default="./rk_yolo_video", help="Board-side binary path.")
    parser.add_argument("--out-dir", required=True, help="Board-side output directory.")
    parser.add_argument("--score", default="0.35", help="Score threshold.")
    parser.add_argument("--nms", default="0.45", help="NMS threshold.")
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="Markdown file to write the generated command matrix.",
    )
    return parser


def experiment_cases(include_int8: bool) -> List[ExperimentCase]:
    cases = [
        ExperimentCase(
            name="fp_opencv_baseline",
            model_kind="fp",
            description="FP RKNN baseline with OpenCV preprocessing.",
            env={"RK_YOLO_PROFILE": "1", "RK_YOLO_PREPROCESS": "opencv"},
        ),
        ExperimentCase(
            name="fp_rga_resize",
            model_kind="fp",
            description="FP RKNN with RGA RGB resize preprocessing.",
            env={"RK_YOLO_PROFILE": "1", "RK_YOLO_PREPROCESS": "rga"},
        ),
        ExperimentCase(
            name="fp_rga_cvt_resize",
            model_kind="fp",
            description="FP RKNN with fused RGA BGR-to-RGB conversion and resize.",
            env={"RK_YOLO_PROFILE": "1", "RK_YOLO_PREPROCESS": "rga_cvt_resize"},
        ),
        ExperimentCase(
            name="fp_rga_letterbox",
            model_kind="fp",
            description="FP RKNN with RGA letterbox preprocessing.",
            env={
                "RK_YOLO_PROFILE": "1",
                "RK_YOLO_PREPROCESS": "rga_cvt_resize",
                "RK_YOLO_RGA_LETTERBOX": "1",
            },
        ),
        ExperimentCase(
            name="fp_zero_copy",
            model_kind="fp",
            description="FP RKNN with zero-copy input memory path.",
            env={"RK_YOLO_PROFILE": "1", "RK_YOLO_ZERO_COPY_INPUT": "1"},
        ),
    ]

    if include_int8:
        cases.extend(
            [
                ExperimentCase(
                    name="int8_opencv_baseline",
                    model_kind="int8",
                    description="INT8 RKNN baseline with OpenCV preprocessing.",
                    env={"RK_YOLO_PROFILE": "1", "RK_YOLO_PREPROCESS": "opencv"},
                ),
                ExperimentCase(
                    name="int8_rga_cvt_resize",
                    model_kind="int8",
                    description="INT8 RKNN with fused RGA BGR-to-RGB conversion and resize.",
                    env={"RK_YOLO_PROFILE": "1", "RK_YOLO_PREPROCESS": "rga_cvt_resize"},
                ),
                ExperimentCase(
                    name="int8_rga_letterbox",
                    model_kind="int8",
                    description="INT8 RKNN with RGA letterbox preprocessing.",
                    env={
                        "RK_YOLO_PROFILE": "1",
                        "RK_YOLO_PREPROCESS": "rga_cvt_resize",
                        "RK_YOLO_RGA_LETTERBOX": "1",
                    },
                ),
                ExperimentCase(
                    name="int8_zero_copy",
                    model_kind="int8",
                    description="INT8 RKNN with zero-copy input memory path.",
                    env={"RK_YOLO_PROFILE": "1", "RK_YOLO_ZERO_COPY_INPUT": "1"},
                ),
            ]
        )
    return cases


def command_for_case(args: argparse.Namespace, case: ExperimentCase) -> str:
    model = args.fp_model if case.model_kind == "fp" else args.int8_model
    out_dir = args.out_dir.rstrip("/")
    output_video = f"{out_dir}/{case.name}.mp4"
    detections_csv = f"{out_dir}/{case.name}.detections.csv"
    roi_jsonl = f"{out_dir}/{case.name}.roi.jsonl"
    alarm_csv = f"{out_dir}/{case.name}.alarm_events.csv"
    log_path = f"{out_dir}/{case.name}.log"

    env_parts = [f"{key}={quote(value)}" for key, value in sorted(case.env.items())]
    argv = [
        quote(args.binary),
        quote(args.video),
        quote(output_video),
        quote(model),
        quote(args.score),
        quote(args.nms),
        quote(detections_csv),
        quote(roi_jsonl),
        quote(alarm_csv),
    ]
    return f"{' '.join(env_parts)} {' '.join(argv)} 2>&1 | tee {quote(log_path)}"


def render_markdown(args: argparse.Namespace, cases: Iterable[ExperimentCase]) -> str:
    lines = [
        "# INT8/RGA Board Experiment Command Matrix",
        "",
        "Generated commands for repeatable RK3588 fixed-video comparison.",
        "",
        "## Inputs",
        "",
        f"- video: `{args.video}`",
        f"- fp_model: `{args.fp_model}`",
        f"- int8_model: `{args.int8_model or ''}`",
        f"- output_dir: `{args.out_dir}`",
        f"- score_threshold: `{args.score}`",
        f"- nms_threshold: `{args.nms}`",
        "",
        "## Prepare Output Directory",
        "",
        "```bash",
        f"mkdir -p {quote(args.out_dir)}",
        "```",
        "",
        "## Cases",
        "",
        "| Case | Model | Purpose |",
        "| --- | --- | --- |",
    ]

    case_list = list(cases)
    for case in case_list:
        lines.append(f"| `{case.name}` | `{case.model_kind}` | {case.description} |")

    lines.extend(["", "## Commands", ""])
    for case in case_list:
        lines.extend(
            [
                f"### {case.name}",
                "",
                "```bash",
                command_for_case(args, case),
                "```",
                "",
            ]
        )

    lines.extend(
        [
            "## Suggested Comparison Fields",
            "",
            "- Stream FPS and output video continuity.",
            "- `prepare_ms`, `input_set_or_update_ms`, `rknn_run_ms`, `outputs_get_ms`, `decode_ms`, `render_ms`, and `total_work_ms` from `profile_csv` logs.",
            "- Obvious false positives and false negatives in the output video.",
            "- Whether logs contain RGA fallback, zero-copy fallback, or model runtime errors.",
        ]
    )
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    cases = experiment_cases(include_int8=bool(args.int8_model))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(render_markdown(args, cases), encoding="utf-8")
    print(f"wrote={args.output}")
    print(f"cases={len(cases)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
