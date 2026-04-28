# RK3588 RGA Preprocess Validation

Date: 2026-04-28

## Purpose

The advisor suggested that RGA hardware preprocessing is important for the graduation project. To keep the existing stable pipeline intact, RGA was implemented as an optional experimental path in `rk_yolo_video`.

The default path remains OpenCV. RGA is enabled only when:

```bash
RK_YOLO_PREPROCESS=rga
```

## Implementation Scope

Changed files:

- `Thesis Project/rk_yolo_video/CMakeLists.txt`
- `Thesis Project/rk_yolo_video/include/yolo_rknn.h`
- `Thesis Project/rk_yolo_video/src/yolo_rknn.cpp`
- `Thesis Project/rk_yolo_video/README.md`

The first RGA implementation only accelerates the resize stage:

```text
BGR frame -> OpenCV BGR2RGB -> RGA resize -> OpenCV letterbox padding -> RKNN input
```

This conservative design avoids changing RKNN input format, detection decoding, rendering, CSV output, ROI JSONL output, and the existing stable OpenCV path.

If `librga` is unavailable at build time, or if `imresize()` fails at runtime, the program falls back to OpenCV resize.

## Board Environment

Target board:

```text
RK3588 Ubuntu board, SSH ubuntu@192.168.10.186
```

RGA availability:

```text
/dev/rga exists
librga-dev installed
librga pkg-config entry available
rga_api version 1.10.1_[10]
```

Board build result:

```text
-- librga found: optional RK_YOLO_PREPROCESS=rga path enabled
[100%] Built target rk_yolo_video
```

## Validation Model And Videos

Model:

```text
/home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn
```

Videos:

```text
/home/ubuntu/public_videos/anti_uav_fig2.mp4
/home/ubuntu/public_videos/anti_uav_fig1.mp4
```

## Commands

OpenCV baseline:

```bash
RK_YOLO_PROFILE=1 RK_YOLO_PREPROCESS=opencv RK_YOLO_ZERO_COPY_INPUT=0 \
  ./build/rk_yolo_video /home/ubuntu/public_videos/anti_uav_fig2.mp4 \
  artifacts/rga_validation/opencv.mp4 \
  /home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn \
  0.25 0.45 artifacts/rga_validation/opencv.csv artifacts/rga_validation/opencv.roi.jsonl
```

RGA resize path:

```bash
RK_YOLO_PROFILE=1 RK_YOLO_PREPROCESS=rga RK_YOLO_ZERO_COPY_INPUT=0 \
  ./build/rk_yolo_video /home/ubuntu/public_videos/anti_uav_fig2.mp4 \
  artifacts/rga_validation/rga_resize.mp4 \
  /home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn \
  0.25 0.45 artifacts/rga_validation/rga_resize.csv artifacts/rga_validation/rga_resize.roi.jsonl
```

## Results

All averages below exclude the first five warm-up frames.

### `anti_uav_fig2.mp4`

| Path | Frames | Total detections | `prepare_ms` | `input_set_or_update_ms` | `rknn_run_ms` | `decode_nms_ms` | `render_ms` | `total_work_ms` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| OpenCV | 160 | 0 | 4.43 | 31.74 | 48.60 | 3.39 | 53.40 | 142.23 |
| RGA resize | 160 | 0 | 6.56 | 46.25 | 49.19 | 4.19 | 53.18 | 160.21 |

Detection CSV comparison:

```text
fig2 csv identical
```

### `anti_uav_fig1.mp4`

| Path | Frames | Total detections | Detection frames | `prepare_ms` | `input_set_or_update_ms` | `rknn_run_ms` | `decode_nms_ms` | `render_ms` | `total_work_ms` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| OpenCV | 130 | 47 | 43 | 4.72 | 49.72 | 50.32 | 4.29 | 39.25 | 149.14 |
| RGA resize | 130 | 47 | 43 | 5.81 | 47.97 | 50.20 | 4.27 | 39.00 | 148.11 |

Detection CSV comparison:

```text
fig1 csv identical
```

## Interpretation

The RGA path is functionally correct and reproducible:

- It builds on the RK3588 board with `librga`.
- It runs through the same RKNN model and post-processing path.
- It produces identical detection CSV files compared with the OpenCV baseline on both validation videos.

The first-stage virtual-address RGA resize path does not show a stable preprocessing speed advantage. On `anti_uav_fig2.mp4`, it is slower than OpenCV. On `anti_uav_fig1.mp4`, total work time is slightly lower, but `prepare_ms` itself is still higher.

This suggests that simply calling RGA through virtual-address buffers for resize is not enough to guarantee acceleration. Further optimization should focus on reducing memory copies and format conversion overhead.

## Current Recommendation

Keep OpenCV as the default demonstration and stable experiment path:

```bash
RK_YOLO_PREPROCESS=opencv
```

Keep RGA as an implemented optional hardware-preprocessing experiment:

```bash
RK_YOLO_PREPROCESS=rga
```

For thesis writing, the defensible conclusion is:

> This project implemented and validated an optional RGA-assisted preprocessing path on RK3588. The experiment confirms functional correctness and output consistency, but the first-stage virtual-address resize implementation does not yet provide a stable speedup over the OpenCV baseline. Therefore, the final stable system keeps OpenCV preprocessing by default, while RGA remains a verified optimization direction for future zero-copy or DMA-buffer-based preprocessing.

## Next Optimization Direction

If more time is available, the next RGA phase should explore:

- combining color conversion and resize in one RGA operation;
- reusing preallocated RGA buffers across frames;
- using DMA/RGA-friendly memory instead of virtual-address wrapped `cv::Mat`;
- integrating RGA closer to camera/video decode buffers to avoid unnecessary CPU memory copies.
