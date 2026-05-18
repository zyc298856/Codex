"""Add public-video hard cases to the reversible hard_v2 dataset.

This script deliberately separates two kinds of frames:

* train_public_neg: non-drone frames with empty YOLO labels. These can be
  added to the train split to reduce false positives on birds, helicopters,
  airplanes, trees, cars, and strong background edges.
* review_public_pos / review_detected_false_positive: frames that need manual
  review. They are not added to training automatically, because wrong boxes
  would teach the model the wrong target.

The original dataset, hard_v1 dataset, and stable model are not modified.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path

import cv2
import numpy as np


def project_root() -> Path:
    return Path(__file__).resolve().parents[2]


def as_posix(path: Path) -> str:
    return str(path.resolve()).replace("\\", "/")


def safe_stem(path: Path) -> str:
    text = path.stem.lower()
    out = []
    for ch in text:
        out.append(ch if ch.isalnum() else "_")
    return "".join(out).strip("_") or "video"


def existing(paths: list[Path]) -> list[Path]:
    return [p for p in paths if p.exists()]


def default_negative_videos(root: Path) -> list[Path]:
    raw = root / "datasets" / "drone_single_class" / "raw_sources"
    return existing(
        [
            raw / "public" / "Drone-detection-dataset" / "Data" / "Video_V" / "V_BIRD_001.mp4",
            raw / "public" / "Drone-detection-dataset" / "Data" / "Video_V" / "V_BIRD_002.mp4",
            raw / "public" / "Drone-detection-dataset" / "Data" / "Video_V" / "V_HELICOPTER_001.mp4",
            raw / "public" / "Drone-detection-dataset" / "Data" / "Video_V" / "V_HELICOPTER_002.mp4",
            raw / "public" / "Drone-detection-dataset" / "Data" / "Video_IR" / "IR_BIRD_001.mp4",
            raw / "public" / "Drone-detection-dataset" / "Data" / "Video_IR" / "IR_BIRD_002.mp4",
            raw / "public" / "Drone-detection-dataset" / "Data" / "Video_IR" / "IR_AIRPLANE_001.mp4",
            raw / "public" / "Drone-detection-dataset" / "Data" / "Video_IR" / "IR_AIRPLANE_002.mp4",
        ]
    )


def default_positive_review_videos(root: Path) -> list[Path]:
    raw = root / "datasets" / "drone_single_class" / "raw_sources"
    eval_runs = root / "rk_yolo_video" / "eval_runs"
    return existing(
        [
            raw / "public_videos" / "dut-anti-uav" / "videos" / "video01.mp4",
            raw / "public_videos" / "anti-uav-official-gifs" / "videos" / "anti_uav_fig1.mp4",
            raw / "public_videos" / "pexels_demo" / "pexels_18253602_drone_flying_18s_720p.mp4",
            raw / "public_videos" / "pexels_demo" / "pexels_4462852_drone_flying_sky_uhd.mp4",
            raw / "public_web_videos" / "2026-05-16" / "quadcopter_20200202_10s.webm",
            eval_runs
            / "public_web_video_stability_20260516_conf024"
            / "quadcopter_20200202_10s_det_conf024.mp4",
        ]
    )


def sample_indices(total: int, count: int, skip_start: int = 5, skip_end: int = 5) -> list[int]:
    if total <= 0 or count <= 0:
        return []
    start = min(skip_start, max(0, total - 1))
    end = max(start, total - skip_end - 1)
    if end <= start:
        return [start]
    n = min(count, end - start + 1)
    if n == 1:
        return [(start + end) // 2]
    return sorted({int(round(start + i * (end - start) / (n - 1))) for i in range(n)})


def extract_frames(
    video_path: Path,
    out_dir: Path,
    count: int,
    prefix: str,
    resize_width: int | None,
) -> list[Path]:
    cap = cv2.VideoCapture(str(video_path))
    if not cap.isOpened():
        raise RuntimeError(f"Cannot open video: {video_path}")

    total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT) or 0)
    indices = sample_indices(total, count)
    saved: list[Path] = []
    out_dir.mkdir(parents=True, exist_ok=True)

    for frame_idx in indices:
        cap.set(cv2.CAP_PROP_POS_FRAMES, frame_idx)
        ok, frame = cap.read()
        if not ok or frame is None:
            continue
        if resize_width and frame.shape[1] > resize_width:
            scale = resize_width / frame.shape[1]
            frame = cv2.resize(
                frame,
                (resize_width, max(1, int(round(frame.shape[0] * scale)))),
                interpolation=cv2.INTER_AREA,
            )
        out_path = out_dir / f"{prefix}_f{frame_idx:06d}.jpg"
        cv2.imwrite(str(out_path), frame, [int(cv2.IMWRITE_JPEG_QUALITY), 92])
        saved.append(out_path)

    cap.release()
    return saved


def make_contact_sheet(images: list[Path], out_path: Path, title: str, thumb_w: int = 300) -> None:
    if not images:
        return
    thumbs: list[np.ndarray] = []
    for path in images[:36]:
        img = cv2.imread(str(path))
        if img is None:
            continue
        h, w = img.shape[:2]
        scale = thumb_w / max(w, 1)
        thumb = cv2.resize(img, (thumb_w, max(1, int(h * scale))), interpolation=cv2.INTER_AREA)
        cv2.putText(
            thumb,
            path.stem[-18:],
            (8, 24),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.55,
            (0, 255, 255),
            2,
            cv2.LINE_AA,
        )
        thumbs.append(thumb)
    if not thumbs:
        return

    cols = 3
    rows = math.ceil(len(thumbs) / cols)
    title_h = 46
    thumb_h = max(t.shape[0] for t in thumbs)
    canvas = np.full((title_h + rows * thumb_h, cols * thumb_w, 3), 245, dtype=np.uint8)
    cv2.putText(canvas, title, (12, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.78, (30, 70, 120), 2, cv2.LINE_AA)
    for i, thumb in enumerate(thumbs):
        r = i // cols
        c = i % cols
        y = title_h + r * thumb_h
        x = c * thumb_w
        canvas[y : y + thumb.shape[0], x : x + thumb.shape[1]] = thumb
    out_path.parent.mkdir(parents=True, exist_ok=True)
    cv2.imwrite(str(out_path), canvas, [int(cv2.IMWRITE_JPEG_QUALITY), 92])


def append_unique_train_entries(dataset: Path, images: list[Path]) -> int:
    train_txt = dataset / "train.txt"
    existing_lines: list[str] = []
    existing_set: set[str] = set()
    if train_txt.exists():
        existing_lines = [line.strip() for line in train_txt.read_text(encoding="utf-8").splitlines() if line.strip()]
        existing_set = set(existing_lines)

    added = 0
    for image in images:
        line = as_posix(image)
        if line in existing_set:
            continue
        existing_lines.append(line)
        existing_set.add(line)
        added += 1
    train_txt.write_text("\n".join(existing_lines) + "\n", encoding="utf-8")
    return added


def write_empty_labels(dataset: Path, image_paths: list[Path]) -> None:
    for image in image_paths:
        rel = image.relative_to(dataset / "images")
        label = dataset / "labels" / rel.with_suffix(".txt")
        label.parent.mkdir(parents=True, exist_ok=True)
        label.write_text("", encoding="utf-8")


def write_csv(path: Path, rows: list[dict]) -> None:
    if not rows:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def build(args: argparse.Namespace) -> None:
    root = project_root()
    dataset = Path(args.dataset).resolve()
    if not (dataset / "dataset.yaml").exists():
        raise RuntimeError(f"Dataset YAML not found: {dataset / 'dataset.yaml'}")

    negative_videos = default_negative_videos(root) + [Path(p).resolve() for p in args.negative_video]
    review_videos = default_positive_review_videos(root) + [Path(p).resolve() for p in args.review_video]

    neg_dir = dataset / "images" / "train_public_neg"
    review_pos_dir = dataset / "review" / "public_positive_candidates" / "images"
    review_fp_dir = dataset / "review" / "detected_false_positive_candidates" / "images"
    metadata_dir = dataset / "metadata"
    preview_dir = dataset / "preview"

    negative_images: list[Path] = []
    negative_rows: list[dict] = []
    for video in negative_videos:
        if not video.exists():
            continue
        prefix = f"neg_{safe_stem(video)}"
        saved = extract_frames(video, neg_dir, args.neg_frames_per_video, prefix, args.resize_width)
        negative_images.extend(saved)
        for img in saved:
            negative_rows.append(
                {
                    "image_path": as_posix(img),
                    "source_video": as_posix(video),
                    "label_policy": "empty_label_hard_negative",
                    "notes": "non-drone public-video frame; safe to train with empty YOLO label",
                }
            )

    write_empty_labels(dataset, negative_images)
    added_to_train = append_unique_train_entries(dataset, negative_images)

    review_images: list[Path] = []
    review_rows: list[dict] = []
    for video in review_videos:
        if not video.exists():
            continue
        is_detected_output = "det_conf" in video.name or "boxed" in video.name
        out_dir = review_fp_dir if is_detected_output else review_pos_dir
        prefix = f"review_{safe_stem(video)}"
        saved = extract_frames(video, out_dir, args.review_frames_per_video, prefix, args.resize_width)
        review_images.extend(saved)
        for img in saved:
            review_rows.append(
                {
                    "image_path": as_posix(img),
                    "source_video": as_posix(video),
                    "label_policy": "manual_review_required",
                    "notes": (
                        "detected output only; do not train directly"
                        if is_detected_output
                        else "raw/public positive candidate; draw correct drone box before training"
                    ),
                }
            )

    make_contact_sheet(negative_images, preview_dir / "contact_sheet_public_negatives.jpg", "Hard negatives: empty labels")
    make_contact_sheet(review_images, preview_dir / "contact_sheet_public_review_candidates.jpg", "Manual-review candidates")
    write_csv(metadata_dir / "public_hard_negatives.csv", negative_rows)
    write_csv(metadata_dir / "public_positive_review_candidates.csv", review_rows)

    summary = {
        "dataset": as_posix(dataset),
        "negative_videos_found": len([v for v in negative_videos if v.exists()]),
        "review_videos_found": len([v for v in review_videos if v.exists()]),
        "hard_negative_images": len(negative_images),
        "hard_negative_images_added_to_train": added_to_train,
        "manual_review_candidate_images": len(review_images),
        "train_txt": as_posix(dataset / "train.txt"),
        "policy": {
            "hard_negatives": "empty labels are added to train.txt",
            "positive_candidates": "not used for training until manually labelled",
        },
    }
    metadata_dir.mkdir(parents=True, exist_ok=True)
    (metadata_dir / "public_hard_v2_summary.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )

    readme = f"""# Public-video hard cases for hard_v2

This folder extends `drone_single_class_hard_v2` without modifying the original
stable dataset or model.

## What was added

- `images/train_public_neg/`: public-video hard negatives with empty YOLO labels.
  These are added to `train.txt`.
- `review/public_positive_candidates/`: candidate positive frames that must be
  manually labelled before training.
- `review/detected_false_positive_candidates/`: frames from detected/boxed videos.
  These are for visual diagnosis only and must not be used directly as labels.

## Safety rule

Do not train on model-predicted boxes from false-positive videos. If a frame
contains a real drone, draw the correct box manually first. If it is a background
false positive, keep the label empty and treat it as a hard negative.

## Current summary

```json
{json.dumps(summary, indent=2, ensure_ascii=False)}
```
"""
    (dataset / "README_public_hard_cases.md").write_text(readme, encoding="utf-8")
    print(json.dumps(summary, indent=2, ensure_ascii=False))


def parse_args() -> argparse.Namespace:
    root = project_root()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--dataset",
        default=str(root / "datasets" / "drone_single_class_hard_v2"),
        help="Existing hard_v2 dataset generated by build_hard_case_dataset.py.",
    )
    parser.add_argument("--neg-frames-per-video", type=int, default=18)
    parser.add_argument("--review-frames-per-video", type=int, default=10)
    parser.add_argument("--resize-width", type=int, default=1280)
    parser.add_argument("--negative-video", action="append", default=[])
    parser.add_argument("--review-video", action="append", default=[])
    return parser.parse_args()


if __name__ == "__main__":
    build(parse_args())
