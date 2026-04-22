# Thesis Project

This directory contains the main workspace for my undergraduate thesis:

**Research on an Embedded-Platform Object Detection System**

The thesis focuses on RK3588-based deployment, real-time detection, and NPU multithreading experiments under a drone-detection application setting.

## What This Project Is

The workspace combines two layers:

- a legacy Jetson/NVIDIA-oriented codebase kept as migration reference
- a newer RK3588-oriented deployment, validation, training, and experiment workspace

The current technical mainline is:

- model conversion: `PyTorch -> ONNX -> RKNN`
- embedded deployment on **RK3588**
- a **C++ multithreaded** real-time detection pipeline
- throughput / latency / resource-use experiments for RK3588 NPU multithreading
- a drone-specific training and evaluation path

## Stable Repository Baseline

The GitHub repository should be read as the **stable, advisor-facing baseline**, not as a dump of every local experiment.

At the current stable baseline, the repository already demonstrates:

- RK3588 offline video validation through a standalone tool
- RK3588 live RTSP detection with USB camera input
- first-round NPU multithreading and policy-optimization results
- a drone-specific dataset and training workflow
- ONNX and RKNN export artifacts for the trained drone model

Items that are still being actively debugged or extended locally may not all be pushed to GitHub immediately. This is intentional so that the remote repository stays readable and stable.

## Current Progress Snapshot

### Confirmed working

- `rk_yolo_video` runs offline RK3588 validation on local videos
- `rk_yolo_live_rtsp` runs live RTSP detection on RK3588
- first-round live experiments already show a clear throughput-latency trade-off:
  - multi-context mode gives the highest throughput
  - policy-optimized mode gives the best practical real-time latency
- a drone-specific model has already been trained and exported as:
  - `best.pt`
  - `best.onnx`
  - `best.rk3588.fp.rknn`

### Current blocker

The most important unfinished technical issue is:

- the latest drone-specific RKNN model is not yet stable on the board-side runtime path used by `rk_yolo_video`
- the crash happens inside `librknnrt.so` during `rknn_run()`
- this is **not** a general video I/O failure and **not** a failure of the older generic runtime path

### Still in progress

- full replacement of the original Jetson encoder main pipeline
- GPIO / alarm / closed-loop action integration
- longer stability testing
- deeper optimization work such as INT8 quantization and RGA-first preprocessing

## Recommended Reading Order

If you are reading this repository to understand the thesis quickly, start with these files:

1. [`README.md`](../README.md)
2. [`rk_yolo_live_rtsp/README.md`](./rk_yolo_live_rtsp/README.md)
3. [`rk_yolo_video/README.md`](./rk_yolo_video/README.md)
4. [`datasets/drone_single_class/README.md`](./datasets/drone_single_class/README.md)
5. [`training/drone_yolov10/README.md`](./training/drone_yolov10/README.md)

These files together explain:

- the project goal
- the current system architecture
- what has already been validated
- where the current board-side blocker sits
- how the drone-specific training path fits into the thesis

## For Thesis Writing / AI Assistance

If this repository is used as input for writing the thesis, the most important points to preserve are:

- this is an **embedded deployment and systems optimization** thesis, not only a model-training project
- the strongest current contribution is the **RK3588 C++ real-time pipeline plus NPU multithreading experiments**
- the drone-specific model work is important, but it is currently one part of the overall system rather than the entire thesis
- the project already has usable experiment evidence for:
  - live RTSP detection
  - NPU throughput-latency trade-offs
  - public-video fixed-input evaluation

When drafting thesis text, avoid overstating originality. The current project emphasis is:

- engineering integration
- embedded deployment
- multithreaded heterogeneous pipeline design
- experimental comparison under RK3588 constraints

rather than claiming that a completely novel detection algorithm was invented here.

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
