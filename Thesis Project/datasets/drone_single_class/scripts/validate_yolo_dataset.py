#!/usr/bin/env python3
"""Validate and optionally clean the derived YOLO dataset tree."""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List

from PIL import Image, UnidentifiedImageError


IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}
SPLITS = ("train", "val", "test")


@dataclass
class Issue:
    split: str
    image: str
    label: str
    reason: str


def dataset_root_from_script() -> Path:
    return Path(__file__).resolve().parents[1]


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Validate a YOLO dataset and optionally remove invalid pairs.")
    parser.add_argument("--dataset-root", type=Path, default=dataset_root_from_script())
    parser.add_argument("--remove-invalid", action="store_true")
    parser.add_argument("--report-name", default="dataset_validation_report.json")
    return parser


def iter_images(split_dir: Path) -> Iterable[Path]:
    for path in sorted(split_dir.iterdir()):
        if path.is_file() and path.suffix.lower() in IMAGE_EXTS:
            yield path


def validate_image(image_path: Path) -> str | None:
    try:
        with Image.open(image_path) as image:
            image.verify()
        with Image.open(image_path) as image:
            image.load()
    except (UnidentifiedImageError, OSError, ValueError) as exc:
        return f"invalid_image:{type(exc).__name__}:{exc}"
    return None


def validate_label(label_path: Path) -> str | None:
    if not label_path.exists():
        return "missing_label"

    lines = label_path.read_text(encoding="utf-8").splitlines()
    for index, raw in enumerate(lines, start=1):
        line = raw.strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) != 5:
            return f"invalid_label_format:line_{index}"
        try:
            cls = int(float(parts[0]))
            coords = [float(value) for value in parts[1:]]
        except ValueError:
            return f"invalid_label_value:line_{index}"
        if cls != 0:
            return f"unexpected_class_{cls}:line_{index}"
        if any(value < 0.0 or value > 1.0 for value in coords):
            return f"label_out_of_range:line_{index}"
    return None


def remove_pair(image_path: Path, label_path: Path) -> None:
    if image_path.exists():
        image_path.unlink()
    if label_path.exists():
        label_path.unlink()


def remove_split_cache(labels_root: Path) -> None:
    for cache_name in ("train.cache", "val.cache", "test.cache"):
        cache_path = labels_root / cache_name
        if cache_path.exists():
            cache_path.unlink()


def main() -> int:
    args = build_parser().parse_args()
    dataset_root = args.dataset_root.resolve()
    images_root = dataset_root / "images"
    labels_root = dataset_root / "labels"
    manifests_root = dataset_root / "manifests"
    manifests_root.mkdir(parents=True, exist_ok=True)

    summary: Dict[str, Dict[str, int]] = {}
    issues: List[Issue] = []

    for split in SPLITS:
        image_dir = images_root / split
        label_dir = labels_root / split
        split_summary = {
            "images_scanned": 0,
            "valid_pairs": 0,
            "invalid_pairs": 0,
            "negative_images": 0,
        }
        for image_path in iter_images(image_dir):
            split_summary["images_scanned"] += 1
            label_path = label_dir / f"{image_path.stem}.txt"

            image_issue = validate_image(image_path)
            label_issue = validate_label(label_path)
            issue_reason = image_issue or label_issue

            if issue_reason:
                split_summary["invalid_pairs"] += 1
                issues.append(
                    Issue(
                        split=split,
                        image=str(image_path.relative_to(dataset_root)),
                        label=str(label_path.relative_to(dataset_root)),
                        reason=issue_reason,
                    )
                )
                if args.remove_invalid:
                    remove_pair(image_path, label_path)
                continue

            lines = [line.strip() for line in label_path.read_text(encoding="utf-8").splitlines() if line.strip()]
            if not lines:
                split_summary["negative_images"] += 1
            split_summary["valid_pairs"] += 1

        summary[split] = split_summary

    if args.remove_invalid:
        remove_split_cache(labels_root)

    report = {
        "dataset_root": str(dataset_root),
        "remove_invalid": args.remove_invalid,
        "summary": summary,
        "issues": [issue.__dict__ for issue in issues],
    }
    report_path = manifests_root / args.report_name
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")

    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
