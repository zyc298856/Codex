#!/usr/bin/env python3
"""Analyze single-class drone detector predictions on a dataset split."""

from __future__ import annotations

import argparse
import csv
import json
import math
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
import tempfile
from typing import Iterable

import yaml


IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}


@dataclass
class Box:
    x1: float
    y1: float
    x2: float
    y2: float
    conf: float | None = None

    @property
    def area(self) -> float:
        return max(0.0, self.x2 - self.x1) * max(0.0, self.y2 - self.y1)


def project_root() -> Path:
    return Path(__file__).resolve().parents[2]


def build_parser() -> argparse.ArgumentParser:
    root = project_root()
    parser = argparse.ArgumentParser(description="Analyze YOLO drone predictions on a dataset split.")
    parser.add_argument("--model", type=Path, default=root / "training_runs" / "drone_gpu_50e" / "weights" / "best.pt")
    parser.add_argument("--data", type=Path, default=root / "datasets" / "drone_single_class" / "dataset.yaml")
    parser.add_argument("--split", choices=("train", "val", "test"), default="test")
    parser.add_argument("--imgsz", type=int, default=640)
    parser.add_argument("--conf", type=float, default=0.25)
    parser.add_argument("--iou-thres", type=float, default=0.5)
    parser.add_argument("--batch", type=int, default=16)
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--top-k", type=int, default=20)
    parser.add_argument("--name", default="drone_gpu_50e_test_analysis")
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=root / "training_runs" / "analysis",
        help="Directory to store generated reports.",
    )
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


def load_dataset_paths(data_path: Path, split: str) -> tuple[Path, Path]:
    payload = yaml.safe_load(data_path.read_text(encoding="utf-8"))
    dataset_dir = data_path.parent.resolve()
    base_path = payload.get("path", ".")
    if base_path in (None, "", "."):
        root = dataset_dir
    else:
        base = Path(str(base_path))
        root = base if base.is_absolute() else (dataset_dir / base).resolve()

    image_dir = (root / payload[split]).resolve()
    label_rel = Path(payload[split])
    label_dir = (root / "labels" / label_rel.name).resolve()
    return image_dir, label_dir


def iter_images(image_dir: Path) -> list[Path]:
    return sorted(path for path in image_dir.iterdir() if path.is_file() and path.suffix.lower() in IMAGE_SUFFIXES)


def load_ground_truth(label_path: Path, image_width: int, image_height: int) -> list[Box]:
    if not label_path.exists():
        return []

    boxes: list[Box] = []
    for line in label_path.read_text(encoding="utf-8").splitlines():
        parts = line.strip().split()
        if len(parts) != 5:
            continue
        _, cx, cy, w, h = map(float, parts)
        box_w = w * image_width
        box_h = h * image_height
        x_center = cx * image_width
        y_center = cy * image_height
        boxes.append(
            Box(
                x1=x_center - box_w / 2.0,
                y1=y_center - box_h / 2.0,
                x2=x_center + box_w / 2.0,
                y2=y_center + box_h / 2.0,
            )
        )
    return boxes


def iou(a: Box, b: Box) -> float:
    inter_x1 = max(a.x1, b.x1)
    inter_y1 = max(a.y1, b.y1)
    inter_x2 = min(a.x2, b.x2)
    inter_y2 = min(a.y2, b.y2)
    inter_w = max(0.0, inter_x2 - inter_x1)
    inter_h = max(0.0, inter_y2 - inter_y1)
    inter_area = inter_w * inter_h
    if inter_area <= 0:
        return 0.0
    union = a.area + b.area - inter_area
    if union <= 0:
        return 0.0
    return inter_area / union


def greedy_match(preds: list[Box], gts: list[Box], iou_threshold: float) -> tuple[int, int, int, list[float]]:
    matches: list[tuple[float, int, int]] = []
    for pred_index, pred in enumerate(preds):
        for gt_index, gt in enumerate(gts):
            overlap = iou(pred, gt)
            if overlap >= iou_threshold:
                matches.append((overlap, pred_index, gt_index))

    matches.sort(reverse=True)
    used_preds: set[int] = set()
    used_gts: set[int] = set()
    matched_ious: list[float] = []

    for overlap, pred_index, gt_index in matches:
        if pred_index in used_preds or gt_index in used_gts:
            continue
        used_preds.add(pred_index)
        used_gts.add(gt_index)
        matched_ious.append(overlap)

    tp = len(used_preds)
    fp = len(preds) - tp
    fn = len(gts) - len(used_gts)
    return tp, fp, fn, matched_ious


def serializable_box(box: Box) -> dict[str, float]:
    payload = {
        "x1": round(box.x1, 2),
        "y1": round(box.y1, 2),
        "x2": round(box.x2, 2),
        "y2": round(box.y2, 2),
    }
    if box.conf is not None:
        payload["conf"] = round(box.conf, 4)
    return payload


def difficulty_score(record: dict[str, object]) -> tuple[int, int, float]:
    return (
        int(record["fn"]) + int(record["fp"]),
        int(record["gt_count"]),
        float(record["best_pred_conf"] or 0.0),
    )


def mean(values: Iterable[float]) -> float | None:
    items = list(values)
    if not items:
        return None
    return sum(items) / len(items)


def main() -> int:
    args = build_parser().parse_args()

    try:
        from PIL import Image
        from ultralytics import YOLO
    except ImportError as exc:
        raise SystemExit("Missing dependencies: install ultralytics and pillow in the training environment.") from exc

    model_path = args.model.resolve()
    data_path = args.data.resolve()
    output_root = args.output_dir.resolve() / args.name
    output_root.mkdir(parents=True, exist_ok=True)

    if not model_path.exists():
        raise SystemExit(f"Model file not found: {model_path}")
    if not data_path.exists():
        raise SystemExit(f"Dataset yaml not found: {data_path}")

    resolved_data_path = resolve_dataset_yaml(data_path)
    image_dir, label_dir = load_dataset_paths(data_path, args.split)
    image_paths = iter_images(image_dir)
    if not image_paths:
        raise SystemExit(f"No images found in split '{args.split}' at {image_dir}")

    model = YOLO(str(model_path))
    results = model.predict(
        source=[str(path) for path in image_paths],
        imgsz=args.imgsz,
        conf=args.conf,
        batch=args.batch,
        device=args.device,
        verbose=False,
        stream=False,
    )

    records: list[dict[str, object]] = []
    fp_only = 0
    fn_only = 0
    both_fp_fn = 0
    perfect = 0
    total_tp = 0
    total_fp = 0
    total_fn = 0
    total_gt = 0
    total_pred = 0
    matched_iou_values: list[float] = []
    pred_conf_values: list[float] = []
    image_size_counter: Counter[str] = Counter()

    for image_path, result in zip(image_paths, results, strict=True):
        with Image.open(image_path) as image:
            width, height = image.size

        image_size_counter[f"{width}x{height}"] += 1
        label_path = label_dir / f"{image_path.stem}.txt"
        gt_boxes = load_ground_truth(label_path, width, height)

        pred_boxes: list[Box] = []
        if result.boxes is not None:
            xyxy = result.boxes.xyxy.cpu().tolist()
            confs = result.boxes.conf.cpu().tolist()
            for coords, conf in zip(xyxy, confs, strict=True):
                pred_boxes.append(Box(coords[0], coords[1], coords[2], coords[3], conf=float(conf)))
                pred_conf_values.append(float(conf))

        tp, fp, fn, matched_ious = greedy_match(pred_boxes, gt_boxes, args.iou_thres)
        matched_iou_values.extend(matched_ious)

        total_tp += tp
        total_fp += fp
        total_fn += fn
        total_gt += len(gt_boxes)
        total_pred += len(pred_boxes)

        if fp and fn:
            both_fp_fn += 1
        elif fp:
            fp_only += 1
        elif fn:
            fn_only += 1
        else:
            perfect += 1

        best_pred_conf = max((box.conf or 0.0) for box in pred_boxes) if pred_boxes else None
        record = {
            "image": str(image_path),
            "label": str(label_path),
            "width": width,
            "height": height,
            "gt_count": len(gt_boxes),
            "pred_count": len(pred_boxes),
            "tp": tp,
            "fp": fp,
            "fn": fn,
            "best_pred_conf": round(best_pred_conf, 4) if best_pred_conf is not None else None,
            "avg_matched_iou": round(mean(matched_ious), 4) if matched_ious else None,
            "gt_boxes": [serializable_box(box) for box in gt_boxes[:10]],
            "pred_boxes": [serializable_box(box) for box in pred_boxes[:10]],
        }
        records.append(record)

    hard_examples = sorted(records, key=difficulty_score, reverse=True)
    top_failures = [record for record in hard_examples if int(record["fp"]) or int(record["fn"])][: args.top_k]
    top_false_negatives = [record for record in hard_examples if int(record["fn"])][: args.top_k]
    top_false_positives = [record for record in hard_examples if int(record["fp"])][: args.top_k]

    precision = total_tp / (total_tp + total_fp) if (total_tp + total_fp) else 0.0
    recall = total_tp / (total_tp + total_fn) if (total_tp + total_fn) else 0.0
    f1 = 2 * precision * recall / (precision + recall) if (precision + recall) else 0.0

    summary = {
        "model": str(model_path),
        "data": str(data_path),
        "split": args.split,
        "imgsz": args.imgsz,
        "conf": args.conf,
        "iou_threshold": args.iou_thres,
        "batch": args.batch,
        "device": args.device,
        "images_analyzed": len(records),
        "total_gt_boxes": total_gt,
        "total_pred_boxes": total_pred,
        "tp": total_tp,
        "fp": total_fp,
        "fn": total_fn,
        "precision": round(precision, 6),
        "recall": round(recall, 6),
        "f1": round(f1, 6),
        "perfect_images": perfect,
        "fp_only_images": fp_only,
        "fn_only_images": fn_only,
        "mixed_error_images": both_fp_fn,
        "mean_pred_conf": round(mean(pred_conf_values) or 0.0, 6),
        "mean_matched_iou": round(mean(matched_iou_values) or 0.0, 6),
        "image_sizes": dict(image_size_counter),
        "top_failures": top_failures,
        "top_false_negatives": top_false_negatives,
        "top_false_positives": top_false_positives,
    }

    json_path = output_root / "summary.json"
    json_path.write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")

    csv_path = output_root / "per_image.csv"
    with csv_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=[
                "image",
                "label",
                "width",
                "height",
                "gt_count",
                "pred_count",
                "tp",
                "fp",
                "fn",
                "best_pred_conf",
                "avg_matched_iou",
            ],
        )
        writer.writeheader()
        for record in records:
            writer.writerow({key: record[key] for key in writer.fieldnames})

    print(f"Analysis finished: {output_root}")
    print(json.dumps({k: summary[k] for k in ('images_analyzed', 'tp', 'fp', 'fn', 'precision', 'recall', 'f1')}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
