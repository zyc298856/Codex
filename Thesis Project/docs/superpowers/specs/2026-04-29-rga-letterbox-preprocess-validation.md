# 2026-04-29 RGA Letterbox Preprocess Validation

## Purpose

This experiment checks whether the model-input letterbox step can be partially offloaded to RK3588
RGA without changing the stable RKNN inference and post-processing path.

The experimental switch is:

```bash
RK_YOLO_RGA_LETTERBOX=1
```

Default behavior remains unchanged:

```text
OpenCV/RGA resize -> OpenCV copyMakeBorder -> RKNN input upload
```

Experimental behavior:

```text
RGA BGR-to-RGB resize -> write into RGB letterbox canvas -> RKNN input upload
```

## Implementation Scope

Shared detector:

```text
rk_yolo_video/src/yolo_rknn.cpp
```

Affected programs:

```text
rk_yolo_video
rk_yolo_live_rtsp
```

Runtime behavior:

- When `RK_YOLO_RGA_LETTERBOX=0`, preprocessing follows the existing stable path.
- When `RK_YOLO_RGA_LETTERBOX=1`, the detector first tries the RGA letterbox path.
- If RGA rejects the operation, the detector falls back to the existing preprocessing path.
- RKNN input upload, output decoding, NMS, drawing, alarm overlay, and RTSP publishing are unchanged.

## Validation Plan

Build on the RK3588 board:

```bash
cd /home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_video/build
cmake ..
cmake --build . -j2
```

Run a fixed-video comparison:

```bash
RK_YOLO_PROFILE=1 RK_YOLO_PREPROCESS=rga_cvt_resize RK_YOLO_RGA_LETTERBOX=0 ./rk_yolo_video ...
RK_YOLO_PROFILE=1 RK_YOLO_PREPROCESS=rga_cvt_resize RK_YOLO_RGA_LETTERBOX=1 ./rk_yolo_video ...
```

Primary checks:

- Program builds successfully with `librga`.
- The experimental path does not crash or corrupt detection output.
- Profile rows still include `prepare_ms`, `input_set_or_update_ms`, `rknn_run_ms`,
  `outputs_get_ms`, `decode_nms_ms`, `render_ms`, and `total_work_ms`.
- Output video and CSV remain usable for thesis screenshots and fixed-video comparison.

## Result

Board-side build and fixed-video validation passed.

Artifact directory:

```text
/home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_video/artifacts/rga_letterbox_20260429_003035
```

Build result:

- `rk_yolo_video` built successfully with `librga`.
- `rk_yolo_live_rtsp` built successfully with the shared detector source.

Test input:

```text
/home/ubuntu/public_videos/anti_uav_fig1.mp4
```

Model:

```text
/home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn
```

Runtime settings:

```bash
RK_YOLO_PROFILE=1
RK_YOLO_PREPROCESS=rga_cvt_resize
score=0.20
nms=0.45
```

Comparison:

| Mode | `RK_YOLO_RGA_LETTERBOX` | profile rows | detection CSV rows | avg prepare ms | avg input ms | avg run ms | avg total work ms | frames with detections |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Baseline RGA cvt+resize | 0 | 130 | 61 | 5.050 | 48.591 | 49.622 | 148.048 | 50 |
| RGA letterbox | 1 | 130 | 61 | 6.271 | 43.295 | 47.787 | 137.241 | 50 |

Interpretation:

- The experimental path is functionally valid: output video, detection CSV, ROI JSONL, and alarm CSV were generated.
- The detection count matched the baseline on the tested video, so the new path did not visibly break model input geometry.
- `prepare_ms` increased slightly because the path creates and fills a full letterbox canvas before RGA processing.
- `input_set_or_update_ms`, `rknn_run_ms`, render time, and total work time were lower in this short run, but this should be treated as a small-sample result rather than a final universal conclusion.

Conclusion:

Keep `RK_YOLO_RGA_LETTERBOX=0` as the stable default. Keep `RK_YOLO_RGA_LETTERBOX=1` as a validated optional RGA preprocessing experiment for thesis comparison and further board-side profiling.
