# Codex

Jetson/NVIDIA old workspace plus the current RK3588 migration work for YOLOv10-based detection.

This repository is not a polished product yet. It is a working engineering workspace that now contains:

- the original Jetson-oriented code path under `encoder/`
- a standalone RK3588 video validation tool under `rk_yolo_video/`
- a standalone RK3588 live RTSP demo under `rk_yolo_live_rtsp/`
- an isolated drone dataset scaffold under `datasets/drone_single_class/`

## Current Status

What is already working on RK3588:

- `YOLOv10 RKNN + local video file + boxed output video`
- `USB UVC camera + RKNN YOLOv10 + RTSP live stream`
- startup tuning for the tested `HBS Camera` UVC module
- a first-pass migration bridge from RKNN detections back toward the legacy encoder-style ROI output

What is not finished yet:

- full replacement of the original Jetson encoder pipeline
- UNet migration to RK3588
- a dedicated drone-trained detection model
- end-to-end cleanup of all Jetson-specific dependencies in the legacy tree

## Repository Layout

- `encoder/`
  Original Jetson-oriented pipeline. This is the legacy reference tree and still contains CUDA, TensorRT, Jetson multimedia, and Live555-related code.
- `rk_yolo_video/`
  Minimal RK3588 validation path for offline video input and boxed output video.
- `rk_yolo_live_rtsp/`
  Minimal RK3588 real-time demo for USB camera input, RKNN inference, on-frame boxes, and RTSP output.
- `datasets/drone_single_class/`
  Isolated dataset scaffold for training a dedicated `drone` detector without disturbing the current runtime path.
- `docs/superpowers/specs/`
  Working design notes written during migration and dataset planning.

## Fastest Working Paths

### 1. Offline RK3588 validation

See:

- [rk_yolo_video/README.md](rk_yolo_video/README.md)

This path is meant for:

- local video file input
- RKNN inference verification
- output video generation with detection boxes

### 2. Live RK3588 RTSP demo

See:

- [rk_yolo_live_rtsp/README.md](rk_yolo_live_rtsp/README.md)

This path is meant for:

- USB UVC camera input
- real-time RKNN detection
- box overlay
- RTSP streaming to a PC player such as VLC

## Model Files In This Workspace

The workspace currently keeps several model forms for bring-up and comparison:

- `yolov10n.pt`
- `yolov10n.onnx`
- `yolov10n.rknn`
- `yolov10n.wsl.rk3588.fp.rknn`
- `yolov10n.512.onnx`
- `yolov10n.512.rk3588.fp.rknn`

In practice, the RK3588 demos use the RKNN models, while the `.pt` and `.onnx` files are kept as regeneration sources.

## Dataset Direction

The current long-term accuracy direction is to move away from generic COCO-only detection and train a dedicated drone detector.

See:

- [datasets/drone_single_class/README.md](datasets/drone_single_class/README.md)
- [docs/superpowers/specs/2026-04-14-drone-dataset-design.md](docs/superpowers/specs/2026-04-14-drone-dataset-design.md)

The dataset tree is intentionally separated from the current RK3588 runtime tools so data work does not break the known-good demo paths.

## Notes

- This repository still includes a lot of legacy Jetson-specific code for reference and staged migration.
- Generated build outputs, large raw dataset downloads, and RTSP/video artifacts are intentionally ignored by Git.
- The repository currently reflects practical migration work rather than a final cleaned release branch.
