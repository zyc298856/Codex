#!/usr/bin/env python3
"""Evaluate the current drone model on public anti-UAV videos."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
from typing import Dict, Iterable, List

import cv2


def project_root() -> Path:
    return Path(__file__).resolve().parents[2]


def build_parser() -> argparse.ArgumentParser:
    root = project_root()
    parser = argparse.ArgumentParser(description="Run public-video evaluation for the current drone detector.")
    parser.add_argument(
        "--manifest",
        type=Path,
        default=root / "datasets" / "drone_single_class" / "manifests" / "public_video_eval_manifest.json",
    )
    parser.add_argument(
        "--model",
        type=Path,
        default=root / "training_runs" / "drone_gpu_50e" / "weights" / "best.pt",
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        default=root / "eval_runs" / "public_videos",
    )
    parser.add_argument("--source-name", default="")
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--imgsz", type=int, default=640)
    parser.add_argument("--conf", type=float, default=0.35)
    parser.add_argument("--device", default="0")
    parser.add_argument("--frame-limit", type=int, default=0)
    return parser


def load_manifest(path: Path) -> List[Dict[str, object]]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    return payload.get("videos", [])


def iter_selected_videos(entries: List[Dict[str, object]], source_name: str, limit: int) -> Iterable[Dict[str, object]]:
    selected = []
    for entry in entries:
        if source_name and entry.get("source_name") != source_name:
            continue
        selected.append(entry)
    if limit > 0:
        selected = selected[:limit]
    return selected


def safe_stem(path: Path) -> str:
    return path.stem.replace(" ", "_")


def save_image(path: Path, image) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    cv2.imwrite(str(path), image)


def main() -> int:
    args = build_parser().parse_args()

    try:
        from ultralytics import YOLO
    except ImportError as exc:
        raise SystemExit("Missing dependency: ultralytics") from exc

    manifest_path = args.manifest.resolve()
    if not manifest_path.exists():
        raise SystemExit(f"Manifest not found: {manifest_path}")

    model_path = args.model.resolve()
    if not model_path.exists():
        raise SystemExit(f"Model not found: {model_path}")

    output_root = args.output_root.resolve()
    output_root.mkdir(parents=True, exist_ok=True)

    entries = list(iter_selected_videos(load_manifest(manifest_path), args.source_name, args.limit))
    if not entries:
        raise SystemExit("No videos selected from manifest.")

    model = YOLO(str(model_path))
    aggregate: List[Dict[str, object]] = []

    for entry in entries:
        video_path = Path(str(entry["video_path"])).resolve()
        if not video_path.exists():
            print(f"[skip] missing video: {video_path}")
            continue

        run_name = f"{entry['source_name']}__{safe_stem(video_path)}"
        run_dir = output_root / run_name
        run_dir.mkdir(parents=True, exist_ok=True)

        cap = cv2.VideoCapture(str(video_path))
        if not cap.isOpened():
            print(f"[skip] failed to open video: {video_path}")
            continue

        fps = cap.get(cv2.CAP_PROP_FPS)
        if fps <= 1.0:
            fps = 20.0
        width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        writer = cv2.VideoWriter(
            str(run_dir / "annotated.mp4"),
            cv2.VideoWriter_fourcc(*"mp4v"),
            fps,
            (width, height),
        )
        if not writer.isOpened():
            raise SystemExit(f"Failed to open output video writer for {run_dir}")

        csv_path = run_dir / "detections.csv"
        summary_path = run_dir / "summary.json"
        first_detection_frame = run_dir / "first_detection.jpg"
        max_score_frame = run_dir / "max_score_detection.jpg"

        frame_index = 0
        frames_with_detections = 0
        total_detections = 0
        max_score = -1.0
        saved_first_detection = False
        max_score_image = None

        with csv_path.open("w", encoding="utf-8", newline="") as csv_file:
            writer_csv = csv.writer(csv_file)
            writer_csv.writerow(
                [
                    "video_name",
                    "frame_index",
                    "class_id",
                    "score",
                    "left",
                    "top",
                    "width",
                    "height",
                ]
            )

            while True:
                ok, frame = cap.read()
                if not ok or frame is None:
                    break
                frame_index += 1
                if args.frame_limit > 0 and frame_index > args.frame_limit:
                    break

                results = model.predict(frame, conf=args.conf, imgsz=args.imgsz, device=args.device, verbose=False)
                result = results[0]
                annotated = result.plot()

                frame_has_detection = False
                if result.boxes is not None:
                    boxes = result.boxes.xyxy.cpu().numpy()
                    scores = result.boxes.conf.cpu().numpy()
                    classes = result.boxes.cls.cpu().numpy()
                    for box, score, cls in zip(boxes, scores, classes):
                        x1, y1, x2, y2 = [int(round(value)) for value in box.tolist()]
                        width_box = max(0, x2 - x1)
                        height_box = max(0, y2 - y1)
                        writer_csv.writerow(
                            [video_path.name, frame_index, int(cls), float(score), x1, y1, width_box, height_box]
                        )
                        frame_has_detection = True
                        total_detections += 1
                        if float(score) > max_score:
                            max_score = float(score)
                            max_score_image = annotated.copy()

                if frame_has_detection:
                    frames_with_detections += 1
                    if not saved_first_detection:
                        save_image(first_detection_frame, annotated)
                        saved_first_detection = True

                writer.write(annotated)

        cap.release()
        writer.release()

        if max_score_image is not None:
            save_image(max_score_frame, max_score_image)

        summary = {
            "source_name": entry["source_name"],
            "video_path": str(video_path),
            "has_labels": bool(entry.get("has_labels", False)),
            "frame_count": frame_index,
            "frames_with_detections": frames_with_detections,
            "total_detections": total_detections,
            "max_score": max_score if max_score >= 0.0 else None,
            "model_path": str(model_path),
            "conf": args.conf,
            "imgsz": args.imgsz,
            "device": args.device,
        }
        summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
        aggregate.append(summary)
        print(f"[done] {video_path.name} -> {run_dir}")

    aggregate_path = output_root / "aggregate_summary.json"
    aggregate_path.write_text(json.dumps(aggregate, indent=2), encoding="utf-8")
    print(f"aggregate summary: {aggregate_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
