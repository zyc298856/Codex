# RK3588 NPU Experiment Command Drafts

Date: 2026-04-21

## Purpose

This note converts the first-round experiment matrix into executable command drafts.

It focuses on the current codebase reality:

- `rk_yolo_live_rtsp` is a **camera-device input** tool based on `cv::VideoCapture(..., cv::CAP_V4L2)`
- it does **not** currently support a fixed video file as the direct RTSP input source
- therefore:
  - live RTSP experiments can be written as executable commands now
  - fixed-input experiments need a temporary alternative path for this stage

## Current Input Constraint

Based on the current implementation in:

- `rk_yolo_live_rtsp/src/main.cpp`

the first argument is treated as a V4L2 camera device such as `/dev/video48`.

Relevant behavior:

- the usage string documents `[device=/dev/video48]`
- camera opening is implemented via `cap->open(device, cv::CAP_V4L2)`
- there is no current branch for local video-file replay input

Therefore, **do not document fixed-video RTSP commands as if they already work**.

## Build Command

Run this on the RK3588 board before the experiments:

```bash
cd /home/ubuntu/eclipse-workspace/eclipse-workspace/Thesis\ Project/rk_yolo_live_rtsp
mkdir -p build
cd build
cmake ..
make -j4
```

## Shared Runtime Assumptions

Unless a specific experiment changes them, the following assumptions are used:

- camera device: `/dev/video48`
- model: `../../yolov10n.rknn`
- mount path: `/yolo`
- resolution: `640 480`
- target FPS: `15`
- score threshold: `0.30`
- NMS threshold: `0.45`
- RTSP port: `8554`

Client URL:

```text
rtsp://<board-ip>:8554/yolo
```

## Live RTSP Experiment Commands

These correspond to:

- `EXP-01L`
- `EXP-02L`
- `EXP-03L`
- `EXP-04L`

### EXP-01L: Baseline

Goal:
- simplest live reference configuration

Recommended environment:

```bash
export RK_YOLO_INFER_WORKERS=1
export RK_YOLO_DYNAMIC_ROI=0
export RK_YOLO_TRACK_MODE=motion
export RK_YOLO_BOX_SMOOTH=0
export RK_YOLO_CAMERA_TUNE=1
```

Run:

```bash
cd /home/ubuntu/eclipse-workspace/eclipse-workspace/Thesis\ Project/rk_yolo_live_rtsp/build
./rk_yolo_live_rtsp /dev/video48 ../../yolov10n.rknn /yolo 640 480 15 0.30 0.45 8554 1
```

### EXP-02L: Pipeline

Goal:
- measure the benefit of capture/infer/publish decoupling under one inference worker

Recommended environment:

```bash
export RK_YOLO_INFER_WORKERS=1
export RK_YOLO_DYNAMIC_ROI=0
export RK_YOLO_TRACK_MODE=motion
export RK_YOLO_BOX_SMOOTH=0
export RK_YOLO_CAMERA_TUNE=1
```

Run:

```bash
cd /home/ubuntu/eclipse-workspace/eclipse-workspace/Thesis\ Project/rk_yolo_live_rtsp/build
./rk_yolo_live_rtsp /dev/video48 ../../yolov10n.rknn /yolo 640 480 15 0.30 0.45 8554 1
```

Note:
- `EXP-01L` and `EXP-02L` may use the same shell command if the pipeline path is already always active in the current implementation
- their distinction in the thesis should then be:
  - `EXP-01L`: conceptual baseline
  - `EXP-02L`: current pipeline implementation
- if a stricter non-pipeline baseline is needed later, it should be implemented as a dedicated simplified branch

### EXP-03L: Multi-Context

Goal:
- measure throughput gain and latency cost of dual RKNN contexts

Recommended environment:

```bash
export RK_YOLO_INFER_WORKERS=2
export RK_YOLO_DYNAMIC_ROI=0
export RK_YOLO_TRACK_MODE=motion
export RK_YOLO_BOX_SMOOTH=0
export RK_YOLO_CAMERA_TUNE=1
```

Run:

```bash
cd /home/ubuntu/eclipse-workspace/eclipse-workspace/Thesis\ Project/rk_yolo_live_rtsp/build
./rk_yolo_live_rtsp /dev/video48 ../../yolov10n.rknn /yolo 640 480 15 0.30 0.45 8554 1
```

Important note:
- multi-context mode is only activated when:
  - `RK_YOLO_INFER_WORKERS=2`
  - `detect_every_n=1`

### EXP-04L: Policy-Optimized

Goal:
- measure the practical throughput-latency trade-off using frame skipping, tracking, and ROI

Recommended environment:

```bash
export RK_YOLO_INFER_WORKERS=1
export RK_YOLO_DYNAMIC_ROI=1
export RK_YOLO_ROI_MARGIN=0.35
export RK_YOLO_ROI_MIN_COVERAGE=0.55
export RK_YOLO_ROI_REFRESH=5
export RK_YOLO_TRACK_MODE=motion
export RK_YOLO_BOX_SMOOTH=0
export RK_YOLO_CAMERA_TUNE=1
```

Run:

```bash
cd /home/ubuntu/eclipse-workspace/eclipse-workspace/Thesis\ Project/rk_yolo_live_rtsp/build
./rk_yolo_live_rtsp /dev/video48 ../../yolov10n.rknn /yolo 640 480 15 0.30 0.45 8554 3
```

## Drone-Specific Model Command Draft

Once the drone-specific model is ready for live RTSP validation, use the offline-validated threshold pair first.

Recommended environment:

```bash
export RK_YOLO_INFER_WORKERS=1
export RK_YOLO_DYNAMIC_ROI=1
export RK_YOLO_TRACK_MODE=motion
export RK_YOLO_BOX_SMOOTH=0
export RK_YOLO_CAMERA_TUNE=1
```

Run:

```bash
cd /home/ubuntu/eclipse-workspace/eclipse-workspace/Thesis\ Project/rk_yolo_live_rtsp/build
./rk_yolo_live_rtsp /dev/video48 ../../training_runs/drone_gpu_50e/weights/best.rk3588.fp.rknn /yolo 640 480 15 0.35 0.45 8554 3
```

## Fixed-Input Experiment: Current Practical Alternative

Because `rk_yolo_live_rtsp` currently does not accept a local video file as input, use the following temporary strategy for the **repeatable fixed-input layer**:

1. Use `rk_yolo_video` for fixed recorded input evaluation.
2. Use `rk_yolo_live_rtsp` for live-camera validation.

This means the first-round thesis wording can be:

- **repeatable fixed-input comparison**: offline video detector path
- **real-time deployment validation**: live RTSP path

### Fixed-Input Command Draft with `rk_yolo_video`

Build:

```bash
cd /home/ubuntu/eclipse-workspace/eclipse-workspace/Thesis\ Project/rk_yolo_video
mkdir -p build
cd build
cmake ..
make -j4
```

Run:

```bash
cd /home/ubuntu/eclipse-workspace/eclipse-workspace/Thesis\ Project/rk_yolo_video/build
./rk_yolo_video /home/ubuntu/test.mp4 /home/ubuntu/test_rk_yolo.mp4 ../../yolov10n.rknn 0.30 0.45 /home/ubuntu/test_rk_yolo.csv /home/ubuntu/test_rk_yolo.roi.jsonl
```

Drone-specific model variant:

```bash
cd /home/ubuntu/eclipse-workspace/eclipse-workspace/Thesis\ Project/rk_yolo_video/build
./rk_yolo_video /home/ubuntu/test.mp4 /home/ubuntu/test_drone_model.mp4 ../../training_runs/drone_gpu_50e/weights/best.rk3588.fp.rknn 0.35 0.45 /home/ubuntu/test_drone_model.csv /home/ubuntu/test_drone_model.roi.jsonl
```

## Recommended Logging Practice

For every RTSP experiment run:

1. redirect stdout/stderr to a log file
2. record the exact environment variables used
3. save a short run duration such as 60 s or 120 s

Example:

```bash
export RK_YOLO_INFER_WORKERS=2
export RK_YOLO_DYNAMIC_ROI=0
export RK_YOLO_TRACK_MODE=motion
export RK_YOLO_BOX_SMOOTH=0
export RK_YOLO_CAMERA_TUNE=1

cd /home/ubuntu/eclipse-workspace/eclipse-workspace/Thesis\ Project/rk_yolo_live_rtsp/build
./rk_yolo_live_rtsp /dev/video48 ../../yolov10n.rknn /yolo 640 480 15 0.30 0.45 8554 1 \
  > /home/ubuntu/exp03_multictx.log 2>&1
```

## Recommended Naming

- `exp01l_baseline.log`
- `exp02l_pipeline.log`
- `exp03l_multictx.log`
- `exp04l_policy.log`

For fixed-input artifacts:

- `exp01_fixed_output.mp4`
- `exp01_fixed_output.csv`
- `exp01_fixed_output.roi.jsonl`

## Next Improvement

If stricter fixed-input RTSP comparison is needed later, the clean long-term path is:

- add a replay-input mode to `rk_yolo_live_rtsp`
- accept either `/dev/video*` or a local video file path
- then unify the fixed-input and live-input experiments under one executable

That is a future enhancement, not a current assumption.
