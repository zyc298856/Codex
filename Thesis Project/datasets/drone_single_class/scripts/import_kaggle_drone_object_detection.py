#!/usr/bin/env python3
"""Import a YOLO-style Kaggle drone dataset into the local single-class scaffold."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional


IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}
SPLIT_ALIASES = {
    "train": ("train", "training"),
    "val": ("val", "valid", "validation"),
    "test": ("test", "testing"),
}


@dataclass
class SplitPaths:
    image_dir: Path
    label_dir: Optional[Path]


@dataclass
class PairRecord:
    image_path: Path
    label_path: Optional[Path]


def dataset_root_from_script() -> Path:
    return Path(__file__).resolve().parents[1]


def build_parser() -> argparse.ArgumentParser:
    default_dataset_root = dataset_root_from_script()
    default_source_root = default_dataset_root / "raw_sources" / "public" / "kaggle_drone_object_detection"
    parser = argparse.ArgumentParser(
        description="Import a YOLO-style Kaggle drone dataset into the standard scaffold."
    )
    parser.add_argument("--source-root", type=Path, default=default_source_root)
    parser.add_argument("--dataset-root", type=Path, default=default_dataset_root)
    parser.add_argument("--source-tag", default="kaggle_drone_object_detection")
    parser.add_argument("--clean-dest", action="store_true")
    return parser


def find_named_dir(parent: Path, aliases: Iterable[str]) -> Optional[Path]:
    for alias in aliases:
        candidate = parent / alias
        if candidate.is_dir():
            return candidate
    return None


def detect_layout(source_root: Path) -> Dict[str, SplitPaths]:
    layouts: List[Dict[str, SplitPaths]] = []

    images_root = source_root / "images"
    labels_root = source_root / "labels"
    if images_root.is_dir():
        mapping: Dict[str, SplitPaths] = {}
        for split, aliases in SPLIT_ALIASES.items():
            image_dir = find_named_dir(images_root, aliases)
            if image_dir:
                label_dir = find_named_dir(labels_root, aliases) if labels_root.is_dir() else None
                mapping[split] = SplitPaths(image_dir=image_dir, label_dir=label_dir)
        if mapping:
            layouts.append(mapping)

    mapping = {}
    for split, aliases in SPLIT_ALIASES.items():
        split_dir = find_named_dir(source_root, aliases)
        if not split_dir:
            continue
        image_dir = split_dir / "images" if (split_dir / "images").is_dir() else split_dir
        label_dir = split_dir / "labels" if (split_dir / "labels").is_dir() else None
        mapping[split] = SplitPaths(image_dir=image_dir, label_dir=label_dir)
    if mapping:
        layouts.append(mapping)

    if not layouts:
        raise FileNotFoundError(
            f"Could not detect a supported YOLO-style layout under: {source_root}"
        )

    best = max(layouts, key=lambda item: sum(1 for split in item if any_images(item[split].image_dir)))
    return best


def any_images(path: Path) -> bool:
    return any(p.suffix.lower() in IMAGE_EXTS for p in path.iterdir()) if path.is_dir() else False


def iter_images(path: Path) -> Iterable[Path]:
    for entry in sorted(path.iterdir()):
        if entry.is_file() and entry.suffix.lower() in IMAGE_EXTS:
            yield entry


def find_flat_pair_dir(source_root: Path) -> Optional[Path]:
    candidates = [source_root]
    candidates.extend(path for path in source_root.rglob("*") if path.is_dir())
    best_dir: Optional[Path] = None
    best_count = 0
    for directory in candidates:
        images = list(iter_images(directory))
        txt_count = sum(1 for item in directory.iterdir() if item.is_file() and item.suffix.lower() == ".txt")
        score = min(len(images), txt_count)
        if score > best_count:
            best_count = score
            best_dir = directory
    return best_dir if best_count > 0 else None


def list_flat_pairs(directory: Path) -> List[PairRecord]:
    pairs: List[PairRecord] = []
    for image_path in iter_images(directory):
        label_path = directory / f"{image_path.stem}.txt"
        pairs.append(PairRecord(image_path=image_path, label_path=label_path if label_path.is_file() else None))
    return pairs


def split_for_stem(stem: str) -> str:
    bucket = int(hashlib.md5(stem.encode("utf-8")).hexdigest()[:8], 16) % 100
    if bucket < 70:
        return "train"
    if bucket < 90:
        return "val"
    return "test"


def normalize_label_lines(lines: Iterable[str]) -> List[str]:
    normalized: List[str] = []
    for raw in lines:
        line = raw.strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) < 5:
            continue
        parts[0] = "0"
        normalized.append(" ".join(parts[:5]))
    return normalized


def import_split(
    split: str,
    split_paths: SplitPaths,
    dataset_root: Path,
    source_tag: str,
) -> Dict[str, int]:
    image_dest = dataset_root / "images" / split
    label_dest = dataset_root / "labels" / split
    image_dest.mkdir(parents=True, exist_ok=True)
    label_dest.mkdir(parents=True, exist_ok=True)

    imported = 0
    labeled = 0
    negatives = 0

    for image_path in iter_images(split_paths.image_dir):
        stem = f"{source_tag}_{split}_{image_path.stem}"
        out_image = image_dest / f"{stem}{image_path.suffix.lower()}"
        out_label = label_dest / f"{stem}.txt"

        shutil.copy2(image_path, out_image)

        label_lines: List[str] = []
        if split_paths.label_dir:
            candidate = split_paths.label_dir / f"{image_path.stem}.txt"
            if candidate.is_file():
                label_lines = normalize_label_lines(candidate.read_text(encoding="utf-8").splitlines())

        out_label.write_text("\n".join(label_lines) + ("\n" if label_lines else ""), encoding="utf-8")

        imported += 1
        if label_lines:
            labeled += 1
        else:
            negatives += 1

    return {
        "images": imported,
        "labeled_images": labeled,
        "negative_images": negatives,
    }


def import_flat_pairs(
    pairs: List[PairRecord],
    dataset_root: Path,
    source_tag: str,
) -> Dict[str, Dict[str, int]]:
    summary: Dict[str, Dict[str, int]] = {
        "train": {"images": 0, "labeled_images": 0, "negative_images": 0},
        "val": {"images": 0, "labeled_images": 0, "negative_images": 0},
        "test": {"images": 0, "labeled_images": 0, "negative_images": 0},
    }

    for pair in pairs:
        split = split_for_stem(pair.image_path.stem)
        image_dest = dataset_root / "images" / split
        label_dest = dataset_root / "labels" / split
        image_dest.mkdir(parents=True, exist_ok=True)
        label_dest.mkdir(parents=True, exist_ok=True)

        stem = f"{source_tag}_{split}_{pair.image_path.stem}"
        out_image = image_dest / f"{stem}{pair.image_path.suffix.lower()}"
        out_label = label_dest / f"{stem}.txt"

        shutil.copy2(pair.image_path, out_image)

        label_lines: List[str] = []
        if pair.label_path:
            label_lines = normalize_label_lines(pair.label_path.read_text(encoding="utf-8").splitlines())
        out_label.write_text("\n".join(label_lines) + ("\n" if label_lines else ""), encoding="utf-8")

        summary[split]["images"] += 1
        if label_lines:
            summary[split]["labeled_images"] += 1
        else:
            summary[split]["negative_images"] += 1

    return summary


def maybe_clean_dest(dataset_root: Path) -> None:
    for split in SPLIT_ALIASES:
        for kind in ("images", "labels"):
            split_dir = dataset_root / kind / split
            if not split_dir.exists():
                continue
            for entry in split_dir.iterdir():
                if entry.is_file():
                    entry.unlink()


def write_manifest(dataset_root: Path, source_root: Path, source_tag: str, summary: Dict[str, Dict[str, int]]) -> Path:
    manifests_dir = dataset_root / "manifests"
    manifests_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = manifests_dir / f"{source_tag}_import_summary.json"
    payload = {
        "source_root": str(source_root),
        "dataset_root": str(dataset_root),
        "source_tag": source_tag,
        "splits": summary,
    }
    manifest_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    return manifest_path


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    source_root = args.source_root.resolve()
    dataset_root = args.dataset_root.resolve()

    if not source_root.exists():
        raise FileNotFoundError(f"Source root does not exist: {source_root}")

    if args.clean_dest:
        maybe_clean_dest(dataset_root)

    summary: Dict[str, Dict[str, int]]
    try:
        layout = detect_layout(source_root)
        summary = {}
        for split, split_paths in layout.items():
            summary[split] = import_split(split, split_paths, dataset_root, args.source_tag)
    except FileNotFoundError:
        flat_dir = find_flat_pair_dir(source_root)
        if not flat_dir:
            raise
        pairs = list_flat_pairs(flat_dir)
        if not pairs:
            raise FileNotFoundError(f"No image/label pairs found under: {source_root}")
        summary = import_flat_pairs(pairs, dataset_root, args.source_tag)

    manifest_path = write_manifest(dataset_root, source_root, args.source_tag, summary)

    print(json.dumps({"status": "ok", "manifest": str(manifest_path), "splits": summary}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
