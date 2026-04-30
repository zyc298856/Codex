#!/usr/bin/env python3
"""Convert YOLO ONNX models to RKNN FP or INT8 models for RK3588."""

from __future__ import annotations

import argparse
import ast
from pathlib import Path
from typing import List


def parse_triplet(text: str) -> List[float]:
    parts = [item.strip() for item in text.split(",") if item.strip()]
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("expected three comma-separated values, e.g. 0,0,0")
    try:
        return [float(item) for item in parts]
    except ValueError as exc:
        raise argparse.ArgumentTypeError(str(exc)) from exc


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Convert an ONNX model to RKNN. INT8 mode requires a calibration list."
    )
    parser.add_argument("--onnx", required=True, type=Path, help="Input ONNX model path.")
    parser.add_argument("--output", required=True, type=Path, help="Output RKNN model path.")
    parser.add_argument("--dtype", choices=["fp", "int8"], default="fp", help="RKNN build type.")
    parser.add_argument("--target", default="rk3588", help="RKNN target platform.")
    parser.add_argument(
        "--auto-hybrid",
        action="store_true",
        help=(
            "Enable RKNN Toolkit2 automatic hybrid quantization in INT8 mode. "
            "Default is off to preserve the fully quantized baseline."
        ),
    )
    parser.add_argument(
        "--custom-hybrid",
        help=(
            "Manual RKNN hybrid quantization ranges, e.g. "
            "\"[['start_node','end_node']]\". Only valid with --dtype int8."
        ),
    )
    parser.add_argument(
        "--custom-hybrid-pair",
        action="append",
        default=[],
        metavar="START,END",
        help=(
            "Manual RKNN hybrid range as one START,END pair. Can be repeated. "
            "This is easier to pass through shells than --custom-hybrid."
        ),
    )
    parser.add_argument(
        "--dataset",
        type=Path,
        help="Calibration dataset txt for INT8. Required when --dtype int8.",
    )
    parser.add_argument(
        "--mean-values",
        type=parse_triplet,
        default=[0.0, 0.0, 0.0],
        help="Input mean values, comma-separated. Default: 0,0,0.",
    )
    parser.add_argument(
        "--std-values",
        type=parse_triplet,
        default=[255.0, 255.0, 255.0],
        help="Input std values, comma-separated. Default: 255,255,255.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Validate paths and print the planned conversion without importing RKNN Toolkit2.",
    )
    return parser


def validate_args(args: argparse.Namespace) -> None:
    if not args.onnx.exists():
        raise FileNotFoundError(f"ONNX model does not exist: {args.onnx}")
    if args.dtype == "int8":
        if args.dataset is None:
            raise ValueError("--dataset is required when --dtype int8")
        if not args.dataset.exists():
            raise FileNotFoundError(f"Calibration dataset does not exist: {args.dataset}")
    elif args.auto_hybrid or args.custom_hybrid or args.custom_hybrid_pair:
        raise ValueError("--auto-hybrid and --custom-hybrid are only valid when --dtype int8")
    if args.auto_hybrid and (args.custom_hybrid or args.custom_hybrid_pair):
        raise ValueError("--auto-hybrid and custom hybrid options cannot be used together")
    if args.custom_hybrid and args.custom_hybrid_pair:
        raise ValueError("--custom-hybrid and --custom-hybrid-pair cannot be used together")


def parse_custom_hybrid(text: str) -> List[List[str]]:
    try:
        value = ast.literal_eval(text)
    except (SyntaxError, ValueError) as exc:
        raise argparse.ArgumentTypeError(f"invalid --custom-hybrid: {exc}") from exc
    if not isinstance(value, list):
        raise argparse.ArgumentTypeError("--custom-hybrid must be a list")
    for pair in value:
        if (
            not isinstance(pair, list)
            or len(pair) != 2
            or not all(isinstance(item, str) for item in pair)
        ):
            raise argparse.ArgumentTypeError(
                "--custom-hybrid must look like [['start_node','end_node']]"
            )
    return value


def parse_custom_hybrid_pairs(pairs: List[str]) -> List[List[str]]:
    parsed = []
    for pair in pairs:
        start, sep, end = pair.partition(",")
        if sep != "," or not start or not end:
            raise argparse.ArgumentTypeError(
                "--custom-hybrid-pair must use START,END format"
            )
        parsed.append([start, end])
    return parsed


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    validate_args(args)
    if args.custom_hybrid:
        custom_hybrid = parse_custom_hybrid(args.custom_hybrid)
    else:
        custom_hybrid = parse_custom_hybrid_pairs(args.custom_hybrid_pair)

    do_quantization = args.dtype == "int8"
    print("RKNN conversion plan")
    print(f"  onnx={args.onnx}")
    print(f"  output={args.output}")
    print(f"  target={args.target}")
    print(f"  dtype={args.dtype}")
    print(f"  do_quantization={do_quantization}")
    print(f"  auto_hybrid={args.auto_hybrid}")
    print(f"  custom_hybrid={custom_hybrid if custom_hybrid else ''}")
    print(f"  dataset={args.dataset if args.dataset else ''}")
    print(f"  mean_values={args.mean_values}")
    print(f"  std_values={args.std_values}")

    if args.dry_run:
        print("dry_run=on, conversion skipped")
        return 0

    try:
        from rknn.api import RKNN  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "RKNN Toolkit2 is not available in this Python environment. "
            "Activate the RKNN Toolkit2 environment or rerun with --dry-run."
        ) from exc

    args.output.parent.mkdir(parents=True, exist_ok=True)

    rknn = RKNN(verbose=True)
    try:
        ret = rknn.config(
            mean_values=[args.mean_values],
            std_values=[args.std_values],
            target_platform=args.target,
        )
        if ret != 0:
            raise RuntimeError(f"rknn.config failed: {ret}")

        ret = rknn.load_onnx(model=str(args.onnx))
        if ret != 0:
            raise RuntimeError(f"rknn.load_onnx failed: {ret}")

        if do_quantization:
            if custom_hybrid:
                ret = rknn.hybrid_quantization_step1(
                    dataset=str(args.dataset),
                    custom_hybrid=custom_hybrid,
                )
                if ret != 0:
                    raise RuntimeError(f"rknn.hybrid_quantization_step1 failed: {ret}")

                model_stem = args.onnx.name.rsplit(".", 1)[0]
                ret = rknn.hybrid_quantization_step2(
                    model_input=f"{model_stem}.model",
                    data_input=f"{model_stem}.data",
                    model_quantization_cfg=f"{model_stem}.quantization.cfg",
                )
            else:
                ret = rknn.build(
                    do_quantization=True,
                    dataset=str(args.dataset),
                    auto_hybrid=args.auto_hybrid,
                )
        else:
            ret = rknn.build(do_quantization=False)
        if ret != 0:
            raise RuntimeError(f"rknn.build failed: {ret}")

        ret = rknn.export_rknn(str(args.output))
        if ret != 0:
            raise RuntimeError(f"rknn.export_rknn failed: {ret}")
    finally:
        rknn.release()

    print(f"exported={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
