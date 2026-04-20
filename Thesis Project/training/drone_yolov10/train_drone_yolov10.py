#!/usr/bin/env python3
"""Bootstrap trainer for the first single-class drone detector."""

from __future__ import annotations

import argparse
from pathlib import Path
import tempfile

import yaml


def project_root() -> Path:
    return Path(__file__).resolve().parents[2]


def build_parser() -> argparse.ArgumentParser:
    root = project_root()
    parser = argparse.ArgumentParser(description="Train the first single-class drone detector.")
    parser.add_argument("--model", type=Path, default=root / "yolov10n.pt")
    parser.add_argument("--data", type=Path, default=root / "datasets" / "drone_single_class" / "dataset.yaml")
    parser.add_argument("--project", type=Path, default=root / "training_runs")
    parser.add_argument("--name", default="drone_yolov10_bootstrap")
    parser.add_argument("--epochs", type=int, default=100)
    parser.add_argument("--imgsz", type=int, default=640)
    parser.add_argument("--batch", type=int, default=16)
    parser.add_argument("--device", default="0")
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--patience", type=int, default=20)
    parser.add_argument("--export-onnx", action="store_true")
    return parser


def resolve_dataset_yaml(data_path: Path) -> Path:
    payload = yaml.safe_load(data_path.read_text(encoding="utf-8"))
    dataset_dir = data_path.parent.resolve()
    configured_path = payload.get("path", ".")

    if configured_path in (None, "", "."):
        payload["path"] = str(dataset_dir)
    else:
        configured = Path(str(configured_path))
        if not configured.is_absolute():
            payload["path"] = str((dataset_dir / configured).resolve())

    temp_dir = Path(tempfile.mkdtemp(prefix="drone_dataset_yaml_"))
    resolved_yaml = temp_dir / data_path.name
    resolved_yaml.write_text(yaml.safe_dump(payload, sort_keys=False), encoding="utf-8")
    return resolved_yaml


def main() -> int:
    args = build_parser().parse_args()

    try:
        from ultralytics import YOLO
    except ImportError as exc:
        raise SystemExit(
            "Missing dependency: ultralytics. Install torch/ultralytics/onnx first, "
            "preferably in a Python 3.10-3.12 environment."
        ) from exc

    model_path = args.model.resolve()
    data_path = args.data.resolve()
    project_path = args.project.resolve()

    if not model_path.exists():
        raise SystemExit(f"Model file not found: {model_path}")
    if not data_path.exists():
        raise SystemExit(f"Dataset yaml not found: {data_path}")

    resolved_data_path = resolve_dataset_yaml(data_path)

    model = YOLO(str(model_path))
    results = model.train(
        data=str(resolved_data_path),
        epochs=args.epochs,
        imgsz=args.imgsz,
        batch=args.batch,
        device=args.device,
        workers=args.workers,
        patience=args.patience,
        project=str(project_path),
        name=args.name,
    )

    print(f"Training finished: {results.save_dir}")

    if args.export_onnx:
        exported = model.export(format="onnx")
        print(f"ONNX export finished: {exported}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
