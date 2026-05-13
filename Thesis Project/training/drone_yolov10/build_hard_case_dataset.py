"""Build a reversible hard-case dataset for drone fine-tuning.

The script does not modify the original dataset. It creates a new dataset
folder whose train split references the original train images plus generated
hard-case samples:

* near_zoom: crop around labelled drones and resize back, simulating close-up
  or zoomed-in drones.
* motion_blur: apply directional blur while keeping original labels.
* edge: translate labelled drones near image borders and clip labels.

Generated labels are derived from the original YOLO labels, so this is safer
than blindly using pseudo labels from a weak model.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import random
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

import cv2
import numpy as np


IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}


@dataclass
class Box:
    cls: int
    x1: float
    y1: float
    x2: float
    y2: float

    def clipped(self, width: int, height: int) -> "Box | None":
        x1 = min(max(self.x1, 0.0), float(width))
        y1 = min(max(self.y1, 0.0), float(height))
        x2 = min(max(self.x2, 0.0), float(width))
        y2 = min(max(self.y2, 0.0), float(height))
        if x2 - x1 < 3.0 or y2 - y1 < 3.0:
            return None
        if (x2 - x1) * (y2 - y1) < 12.0:
            return None
        return Box(self.cls, x1, y1, x2, y2)

    def to_yolo(self, width: int, height: int) -> str:
        xc = ((self.x1 + self.x2) / 2.0) / width
        yc = ((self.y1 + self.y2) / 2.0) / height
        bw = (self.x2 - self.x1) / width
        bh = (self.y2 - self.y1) / height
        return f"{self.cls} {xc:.6f} {yc:.6f} {bw:.6f} {bh:.6f}"


def project_root() -> Path:
    return Path(__file__).resolve().parents[2]


def as_posix_path(path: Path) -> str:
    return str(path.resolve()).replace("\\", "/")


def list_images(path: Path) -> list[Path]:
    return sorted(p for p in path.rglob("*") if p.suffix.lower() in IMAGE_EXTS)


def label_path_for_image(dataset: Path, image_path: Path) -> Path:
    parts = list(image_path.parts)
    try:
        idx = parts.index("images")
        parts[idx] = "labels"
        return Path(*parts).with_suffix(".txt")
    except ValueError:
        rel = image_path.relative_to(dataset / "images")
        return dataset / "labels" / rel.with_suffix(".txt")


def read_yolo_labels(label_path: Path, width: int, height: int) -> list[Box]:
    boxes: list[Box] = []
    if not label_path.exists():
        return boxes
    for raw_line in label_path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) < 5:
            continue
        cls = int(float(parts[0]))
        xc, yc, bw, bh = map(float, parts[1:5])
        x1 = (xc - bw / 2.0) * width
        y1 = (yc - bh / 2.0) * height
        x2 = (xc + bw / 2.0) * width
        y2 = (yc + bh / 2.0) * height
        box = Box(cls, x1, y1, x2, y2).clipped(width, height)
        if box:
            boxes.append(box)
    return boxes


def write_yolo_labels(path: Path, boxes: Iterable[Box], width: int, height: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    text = "\n".join(box.to_yolo(width, height) for box in boxes)
    path.write_text(text + ("\n" if text else ""), encoding="utf-8")


def transform_crop_resize(
    image: np.ndarray,
    boxes: list[Box],
    rng: random.Random,
) -> tuple[np.ndarray, list[Box]] | None:
    h, w = image.shape[:2]
    target = rng.choice(boxes)
    bw = target.x2 - target.x1
    bh = target.y2 - target.y1
    cx = (target.x1 + target.x2) / 2.0
    cy = (target.y1 + target.y2) / 2.0

    context = rng.uniform(1.8, 3.4)
    crop_size = max(bw, bh) * context
    min_crop = min(w, h) * rng.uniform(0.28, 0.48)
    crop_w = min(float(w), max(crop_size * rng.uniform(1.0, 1.45), min_crop))
    crop_h = min(float(h), max(crop_size * rng.uniform(1.0, 1.45), min_crop))
    cx += rng.uniform(-0.12, 0.12) * crop_w
    cy += rng.uniform(-0.12, 0.12) * crop_h

    x1 = int(round(min(max(cx - crop_w / 2.0, 0.0), w - crop_w)))
    y1 = int(round(min(max(cy - crop_h / 2.0, 0.0), h - crop_h)))
    x2 = int(round(x1 + crop_w))
    y2 = int(round(y1 + crop_h))
    if x2 <= x1 + 10 or y2 <= y1 + 10:
        return None

    cropped = image[y1:y2, x1:x2]
    resized = cv2.resize(cropped, (w, h), interpolation=cv2.INTER_LINEAR)
    sx = w / float(x2 - x1)
    sy = h / float(y2 - y1)

    new_boxes: list[Box] = []
    for box in boxes:
        transformed = Box(
            box.cls,
            (box.x1 - x1) * sx,
            (box.y1 - y1) * sy,
            (box.x2 - x1) * sx,
            (box.y2 - y1) * sy,
        ).clipped(w, h)
        if transformed:
            new_boxes.append(transformed)
    return (resized, new_boxes) if new_boxes else None


def motion_blur_kernel(size: int, angle_deg: float) -> np.ndarray:
    kernel = np.zeros((size, size), dtype=np.float32)
    kernel[size // 2, :] = 1.0
    matrix = cv2.getRotationMatrix2D((size / 2 - 0.5, size / 2 - 0.5), angle_deg, 1.0)
    kernel = cv2.warpAffine(kernel, matrix, (size, size))
    kernel_sum = float(kernel.sum())
    if kernel_sum <= 0:
        kernel[size // 2, :] = 1.0
        kernel_sum = float(kernel.sum())
    return kernel / kernel_sum


def transform_motion_blur(
    image: np.ndarray,
    boxes: list[Box],
    rng: random.Random,
) -> tuple[np.ndarray, list[Box]] | None:
    kernel_size = rng.choice([7, 9, 11, 13, 15, 17])
    angle = rng.uniform(0.0, 180.0)
    kernel = motion_blur_kernel(kernel_size, angle)
    blurred = cv2.filter2D(image, -1, kernel)
    if rng.random() < 0.35:
        blurred = cv2.GaussianBlur(blurred, (3, 3), 0)
    return blurred, boxes


def transform_edge(
    image: np.ndarray,
    boxes: list[Box],
    rng: random.Random,
) -> tuple[np.ndarray, list[Box]] | None:
    h, w = image.shape[:2]
    target = rng.choice(boxes)
    cx = (target.x1 + target.x2) / 2.0
    cy = (target.y1 + target.y2) / 2.0
    edge = rng.choice(["left", "right", "top", "bottom"])
    crop_w = int(round(w * rng.uniform(0.58, 0.92)))
    crop_h = int(round(h * rng.uniform(0.58, 0.92)))
    crop_w = max(32, min(crop_w, w))
    crop_h = max(32, min(crop_h, h))
    margin = rng.uniform(0.03, 0.16)

    if edge == "left":
        x1 = cx - margin * crop_w
        y1 = cy - rng.uniform(0.25, 0.75) * crop_h
    elif edge == "right":
        x1 = cx - (1.0 - margin) * crop_w
        y1 = cy - rng.uniform(0.25, 0.75) * crop_h
    elif edge == "top":
        x1 = cx - rng.uniform(0.25, 0.75) * crop_w
        y1 = cy - margin * crop_h
    else:
        x1 = cx - rng.uniform(0.25, 0.75) * crop_w
        y1 = cy - (1.0 - margin) * crop_h

    x1 = int(round(min(max(x1, 0.0), float(w - crop_w))))
    y1 = int(round(min(max(y1, 0.0), float(h - crop_h))))
    x2 = x1 + crop_w
    y2 = y1 + crop_h
    cropped = image[y1:y2, x1:x2]
    if cropped.size == 0:
        return None
    sx = w / float(crop_w)
    sy = h / float(crop_h)
    resized = cv2.resize(cropped, (w, h), interpolation=cv2.INTER_LINEAR)
    new_boxes: list[Box] = []
    for box in boxes:
        transformed = Box(
            box.cls,
            (box.x1 - x1) * sx,
            (box.y1 - y1) * sy,
            (box.x2 - x1) * sx,
            (box.y2 - y1) * sy,
        ).clipped(w, h)
        if transformed:
            new_boxes.append(transformed)
    return (resized, new_boxes) if new_boxes else None


def draw_preview(image: np.ndarray, boxes: list[Box], tag: str) -> np.ndarray:
    out = image.copy()
    for box in boxes:
        cv2.rectangle(out, (int(box.x1), int(box.y1)), (int(box.x2), int(box.y2)), (0, 255, 0), 2)
    cv2.putText(out, tag, (10, 28), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 255), 2, cv2.LINE_AA)
    return out


def make_contact_sheet(previews: list[np.ndarray], out_path: Path, thumb_w: int = 320) -> None:
    if not previews:
        return
    thumbs: list[np.ndarray] = []
    for img in previews:
        h, w = img.shape[:2]
        scale = thumb_w / max(w, 1)
        thumb = cv2.resize(img, (thumb_w, max(1, int(h * scale))), interpolation=cv2.INTER_AREA)
        thumbs.append(thumb)
    cols = 3
    rows = math.ceil(len(thumbs) / cols)
    thumb_h = max(t.shape[0] for t in thumbs)
    canvas = np.full((rows * thumb_h, cols * thumb_w, 3), 245, dtype=np.uint8)
    for i, thumb in enumerate(thumbs):
        r = i // cols
        c = i % cols
        canvas[r * thumb_h : r * thumb_h + thumb.shape[0], c * thumb_w : c * thumb_w + thumb.shape[1]] = thumb
    out_path.parent.mkdir(parents=True, exist_ok=True)
    cv2.imwrite(str(out_path), canvas)


def clear_generated_dirs(output_dataset: Path) -> None:
    for subdir in [
        output_dataset / "images" / "train_hard",
        output_dataset / "labels" / "train_hard",
        output_dataset / "preview",
        output_dataset / "metadata",
    ]:
        if not subdir.exists():
            continue
        for file_path in subdir.rglob("*"):
            if file_path.is_file():
                file_path.unlink()


def write_split_list(path: Path, images: Iterable[Path]) -> None:
    path.write_text("\n".join(as_posix_path(p) for p in images) + "\n", encoding="utf-8")


def write_dataset_files(
    base_dataset: Path,
    output_dataset: Path,
    hard_images: list[Path],
    args: argparse.Namespace,
    summary: dict,
) -> None:
    train_images = list_images(base_dataset / "images" / "train")
    val_images = list_images(base_dataset / "images" / "val")
    test_images = list_images(base_dataset / "images" / "test")

    write_split_list(output_dataset / "train.txt", train_images + hard_images)
    write_split_list(output_dataset / "val.txt", val_images)
    write_split_list(output_dataset / "test.txt", test_images)

    dataset_yaml = f"""# Auto-generated by build_hard_case_dataset.py
# This dataset is a reversible hard-case extension of drone_single_class.
path: {as_posix_path(output_dataset)}
train: train.txt
val: val.txt
test: test.txt

names:
  0: drone
"""
    (output_dataset / "dataset.yaml").write_text(dataset_yaml, encoding="utf-8")

    readme = f"""# drone_single_class_hard_v1

This dataset is generated from `drone_single_class` without modifying the
original dataset. It is intended for a controlled fine-tuning experiment that
targets three weak cases observed during the demo:

- near_zoom: close-up or digitally zoomed drone appearance.
- motion_blur: fast camera/object motion causing blurred UAV contours.
- edge: drone near or partially clipped by the image border.

Generation settings:

- seed: `{args.seed}`
- near_zoom: `{args.near_count}`
- motion_blur: `{args.blur_count}`
- edge: `{args.edge_count}`
- base dataset: `{as_posix_path(base_dataset)}`

Training entry example:

```powershell
& "{as_posix_path(Path(args.python_hint))}" "{as_posix_path(project_root() / 'training' / 'drone_yolov10' / 'train_drone_yolov10.py')}" `
  --model "{as_posix_path(project_root() / 'training_runs' / 'drone_gpu_50e' / 'weights' / 'best.pt')}" `
  --data "{as_posix_path(output_dataset / 'dataset.yaml')}" `
  --name drone_hard_v1_ft `
  --epochs 30 --imgsz 640 --batch 8 --device cpu
```

Review `preview/contact_sheet.jpg` before formal training. The original stable
model and original dataset remain unchanged.
"""
    (output_dataset / "README.md").write_text(readme, encoding="utf-8")

    summary_path = output_dataset / "metadata" / "build_summary.json"
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    summary_path.write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")


def build(args: argparse.Namespace) -> None:
    rng = random.Random(args.seed)
    base_dataset = Path(args.base_dataset).resolve()
    output_dataset = Path(args.output_dataset).resolve()
    output_dataset.mkdir(parents=True, exist_ok=True)

    if args.overwrite_generated:
        clear_generated_dirs(output_dataset)

    hard_img_dir = output_dataset / "images" / "train_hard"
    hard_label_dir = output_dataset / "labels" / "train_hard"
    hard_img_dir.mkdir(parents=True, exist_ok=True)
    hard_label_dir.mkdir(parents=True, exist_ok=True)

    source_images = list_images(base_dataset / "images" / "train")
    labelled_sources: list[tuple[Path, Path]] = []
    for img_path in source_images:
        label_path = label_path_for_image(base_dataset, img_path)
        if label_path.exists() and label_path.read_text(encoding="utf-8").strip():
            labelled_sources.append((img_path, label_path))
    if not labelled_sources:
        raise RuntimeError(f"No labelled training images found in {base_dataset}")

    transforms = [
        ("near_zoom", args.near_count, transform_crop_resize),
        ("motion_blur", args.blur_count, transform_motion_blur),
        ("edge", args.edge_count, transform_edge),
    ]
    hard_images: list[Path] = []
    preview_images: list[np.ndarray] = []
    previews_by_tag: dict[str, list[np.ndarray]] = {tag: [] for tag, _, _ in transforms}
    records: list[dict[str, str | int | float]] = []

    for tag, count, transform in transforms:
        generated = 0
        attempts = 0
        while generated < count and attempts < count * 30:
            attempts += 1
            src_img, src_label = rng.choice(labelled_sources)
            image = cv2.imread(str(src_img))
            if image is None:
                continue
            h, w = image.shape[:2]
            boxes = read_yolo_labels(src_label, w, h)
            if not boxes:
                continue
            result = transform(image, boxes, rng)
            if result is None:
                continue
            aug_image, aug_boxes = result
            if not aug_boxes:
                continue

            stem = f"{tag}_{generated:04d}_{src_img.stem}"
            out_img = hard_img_dir / f"{stem}.jpg"
            out_label = hard_label_dir / f"{stem}.txt"
            cv2.imwrite(str(out_img), aug_image, [int(cv2.IMWRITE_JPEG_QUALITY), 92])
            write_yolo_labels(out_label, aug_boxes, aug_image.shape[1], aug_image.shape[0])
            hard_images.append(out_img)
            records.append(
                {
                    "case_type": tag,
                    "image": as_posix_path(out_img),
                    "label": as_posix_path(out_label),
                    "source_image": as_posix_path(src_img),
                    "source_label": as_posix_path(src_label),
                    "box_count": len(aug_boxes),
                }
            )
            preview = draw_preview(aug_image, aug_boxes, tag)
            if len(preview_images) < args.preview_count:
                preview_images.append(preview)
            if len(previews_by_tag[tag]) < args.preview_count:
                previews_by_tag[tag].append(preview)
            generated += 1

        if generated < count:
            print(f"[WARN] generated {generated}/{count} for {tag}; source labels may be limited.")

    metadata_dir = output_dataset / "metadata"
    metadata_dir.mkdir(parents=True, exist_ok=True)
    with (metadata_dir / "generated_hard_cases.csv").open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=["case_type", "image", "label", "source_image", "source_label", "box_count"],
        )
        writer.writeheader()
        writer.writerows(records)

    make_contact_sheet(preview_images, output_dataset / "preview" / "contact_sheet.jpg")
    for tag, tag_previews in previews_by_tag.items():
        make_contact_sheet(tag_previews, output_dataset / "preview" / f"contact_sheet_{tag}.jpg")

    counts = {tag: sum(1 for r in records if r["case_type"] == tag) for tag, _, _ in transforms}
    summary = {
        "base_dataset": as_posix_path(base_dataset),
        "output_dataset": as_posix_path(output_dataset),
        "seed": args.seed,
        "original_train_images": len(source_images),
        "labelled_source_images": len(labelled_sources),
        "generated_total": len(hard_images),
        "generated_counts": counts,
        "train_total_with_hard_cases": len(source_images) + len(hard_images),
    }
    write_dataset_files(base_dataset, output_dataset, hard_images, args, summary)
    print(json.dumps(summary, indent=2, ensure_ascii=False))
    print(f"[OK] Preview: {output_dataset / 'preview' / 'contact_sheet.jpg'}")
    print(f"[OK] Dataset YAML: {output_dataset / 'dataset.yaml'}")


def parse_args() -> argparse.Namespace:
    root = project_root()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--base-dataset",
        default=str(root / "datasets" / "drone_single_class"),
        help="Original YOLO dataset folder. It will not be modified.",
    )
    parser.add_argument(
        "--output-dataset",
        default=str(root / "datasets" / "drone_single_class_hard_v1"),
        help="New hard-case dataset folder.",
    )
    parser.add_argument("--near-count", type=int, default=240)
    parser.add_argument("--blur-count", type=int, default=240)
    parser.add_argument("--edge-count", type=int, default=240)
    parser.add_argument("--preview-count", type=int, default=24)
    parser.add_argument("--seed", type=int, default=20260512)
    parser.add_argument(
        "--overwrite-generated",
        action="store_true",
        help="Delete generated hard-case files in the output dataset before rebuilding.",
    )
    parser.add_argument(
        "--python-hint",
        default=str(Path("C:/Users/Tony/Desktop/eclipse-workspace-codex/.venv_pc/Scripts/python.exe")),
        help="Only used in the generated README training example.",
    )
    return parser.parse_args()


if __name__ == "__main__":
    build(parse_args())
