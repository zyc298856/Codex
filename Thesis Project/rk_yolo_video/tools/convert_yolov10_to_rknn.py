#!/usr/bin/env python3

import argparse
from pathlib import Path
import sys

from rknn.api import RKNN


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert a YOLOv10 ONNX model into an RKNN model for Rockchip NPUs."
    )
    parser.add_argument("onnx_model", help="Path to the YOLOv10 ONNX model.")
    parser.add_argument(
        "--target",
        default="rk3588",
        help="RKNN target platform, for example rk3588, rk3576, rk3568.",
    )
    parser.add_argument(
        "--dtype",
        choices=["fp", "i8", "u8"],
        default="fp",
        help="Export type. Use fp for the simplest first pass. Use i8/u8 when a calibration dataset is available.",
    )
    parser.add_argument(
        "--dataset",
        default="",
        help="Calibration dataset txt file. Required for i8/u8 quantization.",
    )
    parser.add_argument(
        "--output",
        default="",
        help="Output RKNN file path. Defaults to <onnx_model_stem>.<target>.<dtype>.rknn",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Enable verbose RKNN logging.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    onnx_path = Path(args.onnx_model).expanduser().resolve()
    if not onnx_path.is_file():
      print(f"ONNX model not found: {onnx_path}", file=sys.stderr)
      return 1

    do_quant = args.dtype in {"i8", "u8"}
    dataset = args.dataset.strip()
    if do_quant and not dataset:
      print("Quantized export requires --dataset.", file=sys.stderr)
      return 2

    if args.output:
      output_path = Path(args.output).expanduser().resolve()
    else:
      output_path = onnx_path.with_name(f"{onnx_path.stem}.{args.target}.{args.dtype}.rknn")
    output_path.parent.mkdir(parents=True, exist_ok=True)

    print(f"ONNX model : {onnx_path}")
    print(f"Target     : {args.target}")
    print(f"DType      : {args.dtype}")
    print(f"Output     : {output_path}")
    if dataset:
      print(f"Dataset    : {Path(dataset).expanduser().resolve()}")

    rknn = RKNN(verbose=args.verbose)

    print("--> Config model")
    rknn.config(
        mean_values=[[0, 0, 0]],
        std_values=[[255, 255, 255]],
        target_platform=args.target,
    )
    print("done")

    print("--> Loading ONNX model")
    ret = rknn.load_onnx(model=str(onnx_path))
    if ret != 0:
      print(f"Load model failed: {ret}", file=sys.stderr)
      return ret
    print("done")

    print("--> Building RKNN model")
    ret = rknn.build(do_quantization=do_quant, dataset=dataset if do_quant else None)
    if ret != 0:
      print(f"Build model failed: {ret}", file=sys.stderr)
      return ret
    print("done")

    print("--> Export RKNN model")
    ret = rknn.export_rknn(str(output_path))
    if ret != 0:
      print(f"Export RKNN failed: {ret}", file=sys.stderr)
      return ret
    print("done")

    rknn.release()
    return 0


if __name__ == "__main__":
    sys.exit(main())
