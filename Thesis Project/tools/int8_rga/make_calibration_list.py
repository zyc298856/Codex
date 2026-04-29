#!/usr/bin/env python3
"""Create an RKNN calibration image list from a YOLO dataset."""

from __future__ import annotations

import argparse
import random
from pathlib import Path
from typing import Iterable, List, Optional


IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".bmp"}


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Generate a deterministic calibration image list.")
    parser.add_argument("--dataset-yaml", type=Path, help="YOLO dataset.yaml path.")
    parser.add_argument(
        "--split",
        action="append",
        default=[],
        choices=["train", "val", "test"],
        help="Dataset split to use. Can be repeated. Default: train.",
    )
    parser.add_argument(
        "--image-dir",
        action="append",
        type=Path,
        default=[],
        help="Additional image directory or image-list txt. Can be repeated.",
    )
    parser.add_argument("--output", required=True, type=Path, help="Output calibration txt path.")
    parser.add_argument("--limit", type=int, default=200, help="Maximum number of images to write.")
    parser.add_argument("--seed", type=int, default=20260429, help="Shuffle seed.")
    parser.add_argument(
        "--relative-to",
        type=Path,
        help="Write paths relative to this directory. Default writes absolute paths.",
    )
    return parser


def simple_yaml_value(lines: Iterable[str], key: str) -> Optional[str]:
    prefix = f"{key}:"
    for line in lines:
        stripped = line.strip()
        if stripped.startswith(prefix):
            value = stripped[len(prefix) :].strip()
            return value.strip("'\"") if value else None
    return None


def collect_from_path(path: Path) -> List[Path]:
    if path.is_file() and path.suffix.lower() == ".txt":
        base = path.parent
        images: List[Path] = []
        for line in path.read_text(encoding="utf-8").splitlines():
            item = line.strip()
            if not item or item.startswith("#"):
                continue
            image_path = Path(item)
            if not image_path.is_absolute():
                image_path = base / image_path
            if image_path.suffix.lower() in IMAGE_EXTENSIONS and image_path.exists():
                images.append(image_path)
        return images

    if path.is_file() and path.suffix.lower() in IMAGE_EXTENSIONS:
        return [path]

    if path.is_dir():
        return [
            item
            for item in path.rglob("*")
            if item.is_file() and item.suffix.lower() in IMAGE_EXTENSIONS
        ]

    return []


def dataset_paths(dataset_yaml: Path, splits: List[str]) -> List[Path]:
    lines = dataset_yaml.read_text(encoding="utf-8").splitlines()
    dataset_root = simple_yaml_value(lines, "path")
    base = dataset_yaml.parent
    if dataset_root:
        root_path = Path(dataset_root)
        base = root_path if root_path.is_absolute() else dataset_yaml.parent / root_path

    paths: List[Path] = []
    for split in splits:
        value = simple_yaml_value(lines, split)
        if not value:
            continue
        split_path = Path(value)
        if not split_path.is_absolute():
            split_path = base / split_path
        paths.append(split_path)
    return paths


def normalize_output_path(path: Path, relative_to: Optional[Path]) -> str:
    resolved = path.resolve()
    if relative_to is not None:
        try:
            return resolved.relative_to(relative_to.resolve()).as_posix()
        except ValueError:
            return resolved.as_posix()
    return resolved.as_posix()


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    if args.limit <= 0:
        raise ValueError("--limit must be greater than zero")

    splits = args.split or ["train"]
    sources: List[Path] = []
    if args.dataset_yaml:
        if not args.dataset_yaml.exists():
            raise FileNotFoundError(f"dataset yaml does not exist: {args.dataset_yaml}")
        sources.extend(dataset_paths(args.dataset_yaml, splits))
    sources.extend(args.image_dir)

    if not sources:
        raise ValueError("no sources provided; use --dataset-yaml or --image-dir")

    images: List[Path] = []
    for source in sources:
        images.extend(collect_from_path(source))

    unique_images = sorted({image.resolve() for image in images})
    if not unique_images:
        raise RuntimeError(f"no images found from sources: {sources}")

    rng = random.Random(args.seed)
    rng.shuffle(unique_images)
    selected = unique_images[: args.limit]

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        "\n".join(normalize_output_path(image, args.relative_to) for image in selected) + "\n",
        encoding="utf-8",
    )
    print(f"sources={len(sources)}")
    print(f"images_found={len(unique_images)}")
    print(f"images_written={len(selected)}")
    print(f"output={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
