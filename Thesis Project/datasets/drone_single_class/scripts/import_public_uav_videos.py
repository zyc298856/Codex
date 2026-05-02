#!/usr/bin/env python3
"""Download and register public anti-UAV videos for evaluation."""

from __future__ import annotations

import argparse
import json
import shutil
import tarfile
import urllib.request
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional

import cv2
import numpy as np
from PIL import Image, ImageSequence


VIDEO_EXTS = {".mp4", ".avi", ".mov", ".mkv", ".webm"}
IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}
GIF_EXTS = {".gif"}


@dataclass(frozen=True)
class DownloadItem:
    name: str
    kind: str
    url: str
    filename: str


@dataclass(frozen=True)
class SourceSpec:
    source_name: str
    target_type: str
    scene_type: str
    notes: str
    download_items: tuple[DownloadItem, ...]


SOURCES: Dict[str, SourceSpec] = {
    "dut-anti-uav-tracking": SourceSpec(
        source_name="dut-anti-uav-tracking",
        target_type="uav",
        scene_type="mixed_outdoor",
        notes="Official DUT-Anti-UAV tracking split. Videos are reconstructed from RGB image sequences.",
        download_items=(
            DownloadItem(
                name="tracking_img",
                kind="image_sequences",
                url="https://drive.google.com/open?id=1dlSPDggg6TRFMcC1jlYIJxxzUQS1mIh9",
                filename="dut_tracking_img.zip",
            ),
            DownloadItem(
                name="tracking_gt",
                kind="labels",
                url="https://drive.google.com/open?id=16PE3tBhT0lUGZLA8-zIRYvNUvxfhFZJq",
                filename="dut_tracking_gt.zip",
            ),
        ),
    ),
    "anti-uav300": SourceSpec(
        source_name="anti-uav300",
        target_type="uav",
        scene_type="mixed_outdoor",
        notes="Official Anti-UAV300 release. Contains RGB and IR videos; evaluation should prefer RGB clips.",
        download_items=(
            DownloadItem(
                name="anti_uav300_bundle",
                kind="videos",
                url="https://drive.google.com/file/d/1NPYaop35ocVTYWHOYQQHn8YHsM9jmLGr/view",
                filename="anti_uav300.zip",
            ),
        ),
    ),
    "anti-uav-official-gifs": SourceSpec(
        source_name="anti-uav-official-gifs",
        target_type="uav",
        scene_type="mixed_outdoor",
        notes="Small official Anti-UAV demo GIFs converted to mp4 for immediate fixed-input evaluation.",
        download_items=(
            DownloadItem(
                name="anti_uav_fig1",
                kind="gif_demo",
                url="https://raw.githubusercontent.com/ZhaoJ9014/Anti-UAV/master/Fig/1.gif",
                filename="anti_uav_fig1.gif",
            ),
            DownloadItem(
                name="anti_uav_fig2",
                kind="gif_demo",
                url="https://raw.githubusercontent.com/ZhaoJ9014/Anti-UAV/master/Fig/2.gif",
                filename="anti_uav_fig2.gif",
            ),
            DownloadItem(
                name="anti_uav_fig3",
                kind="gif_demo",
                url="https://raw.githubusercontent.com/ZhaoJ9014/Anti-UAV/master/Fig/3.gif",
                filename="anti_uav_fig3.gif",
            ),
        ),
    ),
}


def dataset_root_from_script() -> Path:
    return Path(__file__).resolve().parents[1]


def build_parser() -> argparse.ArgumentParser:
    dataset_root = dataset_root_from_script()
    parser = argparse.ArgumentParser(
        description="Download and register public anti-UAV videos for evaluation."
    )
    parser.add_argument(
        "--source",
        choices=sorted(SOURCES.keys()),
        default="dut-anti-uav-tracking",
        help="Public source to download and register.",
    )
    parser.add_argument("--dataset-root", type=Path, default=dataset_root)
    parser.add_argument(
        "--download",
        action="store_true",
        help="Actually download missing source archives. If omitted, only extraction/manifest steps run.",
    )
    parser.add_argument(
        "--skip-extract",
        action="store_true",
        help="Skip archive extraction if you only want to refresh the manifest.",
    )
    parser.add_argument(
        "--max-sequences",
        type=int,
        default=0,
        help="Limit the number of reconstructed/registered videos. 0 means no limit.",
    )
    parser.add_argument(
        "--rebuild-videos",
        action="store_true",
        help="Rebuild reconstructed mp4 files even if they already exist.",
    )
    parser.add_argument(
        "--frame-rate",
        type=float,
        default=20.0,
        help="Frame rate used when reconstructing mp4 files from image sequences.",
    )
    return parser


def ensure_gdown():
    try:
        import gdown  # type: ignore
    except ImportError as exc:  # pragma: no cover - dependency guidance
        raise SystemExit(
            "Missing dependency: gdown. Install it in the current environment first, "
            "for example `python -m pip install gdown`."
        ) from exc
    return gdown


def download_item(item: DownloadItem, downloads_dir: Path) -> Path:
    downloads_dir.mkdir(parents=True, exist_ok=True)
    target_path = downloads_dir / item.filename
    if target_path.exists():
        print(f"[skip] download exists: {target_path}")
        return target_path

    print(f"[download] {item.name} -> {target_path}")
    if "drive.google.com" in item.url:
        gdown = ensure_gdown()
        result = gdown.download(url=item.url, output=str(target_path), quiet=False, resume=True)
        if result is None or not target_path.exists():
            raise SystemExit(f"Failed to download: {item.url}")
    else:
        urllib.request.urlretrieve(item.url, target_path)
    return target_path


def unpack_archive(archive_path: Path, extract_dir: Path) -> None:
    extract_dir.mkdir(parents=True, exist_ok=True)
    marker = extract_dir / ".extract_complete"
    if marker.exists():
        print(f"[skip] extract exists: {extract_dir}")
        return

    print(f"[extract] {archive_path} -> {extract_dir}")
    suffixes = {suffix.lower() for suffix in archive_path.suffixes}
    if ".zip" in suffixes:
        with zipfile.ZipFile(archive_path) as zip_file:
            zip_file.extractall(extract_dir)
    elif ".tar" in suffixes or ".gz" in suffixes or ".tgz" in suffixes:
        with tarfile.open(archive_path) as tar_file:
            tar_file.extractall(extract_dir)
    else:
        try:
            shutil.unpack_archive(str(archive_path), str(extract_dir))
        except shutil.ReadError as exc:
            raise SystemExit(
                f"Unsupported archive format for {archive_path}. Please rename it to a known extension "
                "or extract it manually."
            ) from exc
    marker.write_text("ok\n", encoding="utf-8")


def iter_videos(root: Path) -> Iterable[Path]:
    for path in sorted(root.rglob("*")):
        if path.is_file() and path.suffix.lower() in VIDEO_EXTS:
            yield path


def iter_sequence_dirs(root: Path) -> Iterable[Path]:
    seen: set[Path] = set()
    for path in sorted(root.rglob("*")):
        if not path.is_dir():
            continue
        image_count = sum(1 for item in path.iterdir() if item.is_file() and item.suffix.lower() in IMAGE_EXTS)
        if image_count >= 8 and path not in seen:
            seen.add(path)
            yield path


def sequence_images(sequence_dir: Path) -> List[Path]:
    return sorted(
        [
            path
            for path in sequence_dir.iterdir()
            if path.is_file() and path.suffix.lower() in IMAGE_EXTS
        ]
    )


def build_video_from_sequence(sequence_dir: Path, output_path: Path, fps: float) -> Optional[Path]:
    images = sequence_images(sequence_dir)
    if not images:
        return None

    output_path.parent.mkdir(parents=True, exist_ok=True)
    first_frame = cv2.imread(str(images[0]))
    if first_frame is None:
        return None

    height, width = first_frame.shape[:2]
    writer = cv2.VideoWriter(
        str(output_path),
        cv2.VideoWriter_fourcc(*"mp4v"),
        fps,
        (width, height),
    )
    if not writer.isOpened():
        raise SystemExit(f"Failed to open output video writer: {output_path}")

    writer.write(first_frame)
    for image_path in images[1:]:
        frame = cv2.imread(str(image_path))
        if frame is None:
            continue
        if frame.shape[1] != width or frame.shape[0] != height:
            frame = cv2.resize(frame, (width, height))
        writer.write(frame)
    writer.release()
    return output_path


def build_video_from_gif(gif_path: Path, output_path: Path, fps: float) -> Optional[Path]:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with Image.open(gif_path) as image:
        frames = [frame.convert("RGB") for frame in ImageSequence.Iterator(image)]
    if not frames:
        return None

    first_frame = cv2.cvtColor(np.array(frames[0]), cv2.COLOR_RGB2BGR)
    height, width = first_frame.shape[:2]
    writer = cv2.VideoWriter(
        str(output_path),
        cv2.VideoWriter_fourcc(*"mp4v"),
        fps,
        (width, height),
    )
    if not writer.isOpened():
        raise SystemExit(f"Failed to open output video writer: {output_path}")

    writer.write(first_frame)
    for frame in frames[1:]:
        bgr = cv2.cvtColor(np.array(frame), cv2.COLOR_RGB2BGR)
        if bgr.shape[1] != width or bgr.shape[0] != height:
            bgr = cv2.resize(bgr, (width, height))
        writer.write(bgr)
    writer.release()
    return output_path


def relative_sequence_name(sequence_dir: Path, root: Path) -> str:
    return "__".join(sequence_dir.relative_to(root).parts)


def find_candidate_root(extracted_dir: Path, name_hint: str) -> Path:
    direct = extracted_dir / name_hint
    if direct.exists():
        return direct
    nested_matches = list(extracted_dir.rglob(name_hint))
    if nested_matches:
        return nested_matches[0]
    return extracted_dir


def collect_dut_tracking_videos(
    extracted_dir: Path, videos_dir: Path, max_sequences: int, rebuild_videos: bool, fps: float
) -> List[Dict[str, object]]:
    img_root = find_candidate_root(extracted_dir, "img")
    gt_root = find_candidate_root(extracted_dir, "gt")
    entries: List[Dict[str, object]] = []
    sequence_dirs = list(iter_sequence_dirs(img_root))
    if max_sequences > 0:
        sequence_dirs = sequence_dirs[:max_sequences]

    for sequence_dir in sequence_dirs:
        seq_name = relative_sequence_name(sequence_dir, img_root)
        output_video = videos_dir / f"{seq_name}.mp4"
        if rebuild_videos or not output_video.exists():
            built = build_video_from_sequence(sequence_dir, output_video, fps)
            if built is None:
                continue

        gt_candidate = gt_root / sequence_dir.relative_to(img_root)
        has_labels = gt_candidate.exists()
        entries.append(
            {
                "source_name": "dut-anti-uav-tracking",
                "video_path": str(output_video),
                "has_labels": has_labels,
                "scene_type": "mixed_outdoor",
                "target_type": "uav",
                "notes": f"Reconstructed from official DUT tracking RGB image sequence: {seq_name}",
                "source_sequence_dir": str(sequence_dir),
                "ground_truth_dir": str(gt_candidate) if has_labels else "",
            }
        )
    return entries


def collect_video_entries(
    source_key: str,
    extracted_dir: Path,
    videos_dir: Path,
    max_sequences: int,
    rebuild_videos: bool,
    fps: float,
) -> List[Dict[str, object]]:
    if source_key == "dut-anti-uav-tracking":
        return collect_dut_tracking_videos(extracted_dir, videos_dir, max_sequences, rebuild_videos, fps)
    if source_key == "anti-uav-official-gifs":
        entries: List[Dict[str, object]] = []
        gif_paths = sorted(path for path in extracted_dir.rglob("*") if path.is_file() and path.suffix.lower() in GIF_EXTS)
        if max_sequences > 0:
            gif_paths = gif_paths[:max_sequences]
        for gif_path in gif_paths:
            output_video = videos_dir / f"{gif_path.stem}.mp4"
            if rebuild_videos or not output_video.exists():
                built = build_video_from_gif(gif_path, output_video, fps)
                if built is None:
                    continue
            entries.append(
                {
                    "source_name": source_key,
                    "video_path": str(output_video),
                    "has_labels": False,
                    "scene_type": "mixed_outdoor",
                    "target_type": "uav",
                    "notes": f"Converted from official Anti-UAV demo GIF: {gif_path.name}",
                    "source_animation": str(gif_path),
                }
            )
        return entries

    spec = SOURCES[source_key]
    entries: List[Dict[str, object]] = []
    video_paths = list(iter_videos(extracted_dir))
    if max_sequences > 0:
        video_paths = video_paths[:max_sequences]

    for video_path in video_paths:
        entries.append(
            {
                "source_name": source_key,
                "video_path": str(video_path),
                "has_labels": False,
                "scene_type": spec.scene_type,
                "target_type": spec.target_type,
                "notes": spec.notes,
            }
        )
    return entries


def write_manifest(dataset_root: Path, source_key: str, entries: List[Dict[str, object]]) -> Path:
    manifests_dir = dataset_root / "manifests"
    manifests_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = manifests_dir / "public_video_eval_manifest.json"

    payload = {"videos": []}
    if manifest_path.exists():
        payload = json.loads(manifest_path.read_text(encoding="utf-8"))
        payload.setdefault("videos", [])

    existing = [item for item in payload["videos"] if item.get("source_name") != source_key]
    existing.extend(entries)
    payload["videos"] = existing
    payload["updated_source"] = source_key

    manifest_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    return manifest_path


def main() -> int:
    args = build_parser().parse_args()
    dataset_root = args.dataset_root.resolve()
    spec = SOURCES[args.source]

    source_root = dataset_root / "raw_sources" / "public_videos" / spec.source_name
    downloads_dir = source_root / "downloads"
    extracted_dir = source_root / "extracted"
    videos_dir = source_root / "videos"
    source_root.mkdir(parents=True, exist_ok=True)

    if args.download:
        for item in spec.download_items:
            archive_path = download_item(item, downloads_dir)
            if item.kind == "gif_demo":
                target_path = extracted_dir / item.name / archive_path.name
                target_path.parent.mkdir(parents=True, exist_ok=True)
                if not target_path.exists():
                    shutil.copy2(archive_path, target_path)
            elif not args.skip_extract:
                unpack_archive(archive_path, extracted_dir / item.name)

    entries = collect_video_entries(
        args.source,
        extracted_dir,
        videos_dir,
        max_sequences=args.max_sequences,
        rebuild_videos=args.rebuild_videos,
        fps=args.frame_rate,
    )
    if not entries:
        raise SystemExit(
            f"No video entries found for {args.source}. Download/extract the source first or check the extracted layout."
        )

    manifest_path = write_manifest(dataset_root, args.source, entries)
    print(json.dumps({"source": args.source, "manifest": str(manifest_path), "videos": entries[:10], "count": len(entries)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
