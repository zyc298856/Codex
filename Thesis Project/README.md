# Thesis Project

This directory contains the main workspace for my undergraduate thesis:

**Research on an Embedded-Platform Object Detection System**

The project focuses on RK3588-based deployment, real-time detection, and NPU multithreading experiments under a drone-detection application setting.

## Overview

The workspace currently combines two layers:

- a legacy Jetson/NVIDIA-oriented codebase kept for migration reference
- a newer RK3588-oriented deployment, validation, training, and experiment workspace

The current technical mainline is:

- model conversion: `PyTorch -> ONNX -> RKNN`
- embedded deployment on **RK3588**
- **C++ multithreaded** real-time detection pipeline
- throughput / latency / resource-use experiments for RK3588 NPU multithreading

## Current Status

### Already working

- RK3588 offline video validation through a standalone tool
- RK3588 live RTSP detection with USB camera input
- first-round NPU multithreading and policy-optimization experiments
- public anti-UAV video bootstrap evaluation
- drone-specific dataset preparation, training, and RKNN conversion workflow

### Still in progress

- full replacement of the original Jetson encoder main pipeline
- stable board-side runtime support for the latest drone-specific RKNN model
- GPIO / alarm / closed-loop action integration
- longer stability testing
- deeper optimization work such as INT8 quantization and RGA-first preprocessing

## Repository Layout

- [`encoder/`](./encoder/)
  Legacy Jetson reference tree containing CUDA, TensorRT, Jetson multimedia, and related platform-specific code.

- [`rk_yolo_video/`](./rk_yolo_video/)
  Standalone RK3588 offline validation tool for local video input, RKNN inference, boxed output video, CSV output, and ROI sidecar export.

- [`rk_yolo_live_rtsp/`](./rk_yolo_live_rtsp/)
  Standalone RK3588 live demo tool for USB camera input, local file replay, RKNN inference, overlay drawing, and RTSP streaming.

- [`datasets/drone_single_class/`](./datasets/drone_single_class/)
  Isolated dataset scaffold for drone-specific training, public-video evaluation manifests, and data-cleaning scripts.

- [`training/`](./training/)
  Training entry points, experiment helpers, and model evaluation scripts.

- [`training_runs/`](./training_runs/)
  Model outputs such as `best.pt`, `best.onnx`, and converted RKNN artifacts used during validation and deployment work.

- [`eval_runs/`](./eval_runs/)
  Public-video evaluation outputs and board-side validation artifacts.

- [`docs/superpowers/specs/`](./docs/superpowers/specs/)
  Technical notes, experiment summaries, migration specs, and thesis-writing support documents created during the project.

## Fastest Working Paths

### 1. RK3588 Offline Validation

Use:

- [`rk_yolo_video/README.md`](./rk_yolo_video/README.md)

This path is intended for:

- local video file input
- board-side RKNN inference validation
- repeatable output generation for CSV / ROI / video comparison

### 2. RK3588 Live RTSP Detection

Use:

- [`rk_yolo_live_rtsp/README.md`](./rk_yolo_live_rtsp/README.md)

This path is intended for:

- USB UVC camera input
- real-time RKNN detection
- live overlay drawing
- RTSP streaming to a PC player such as VLC

## Model Files

The workspace keeps multiple model forms for regeneration, comparison, and deployment:

- `yolov10n.pt`
- `yolov10n.onnx`
- `yolov10n.rknn`
- `yolov10n.wsl.rk3588.fp.rknn`
- `yolov10n.512.onnx`
- `yolov10n.512.rk3588.fp.rknn`

The newer drone-specific training outputs are stored under:

- [`training_runs/drone_gpu_50e/weights/`](./training_runs/drone_gpu_50e/weights/)

## Drone-Specific Training Direction

The project is gradually moving from generic COCO-style detection toward a more task-specific drone detector.

Relevant materials:

- [`datasets/drone_single_class/README.md`](./datasets/drone_single_class/README.md)
- [`training/drone_yolov10/README.md`](./training/drone_yolov10/README.md)

## Notes

- This repository is still an active thesis engineering workspace.
- Legacy Jetson-specific code is intentionally retained as migration reference.
- Large raw dataset downloads, build outputs, and many experiment artifacts are ignored by Git to keep the repository manageable.
