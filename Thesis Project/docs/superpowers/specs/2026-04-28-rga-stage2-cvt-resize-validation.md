# 2026-04-28 RGA Second-Stage Color-Convert Resize Validation

## Purpose

This validation records the second small-step RGA optimization for `rk_yolo_video`.
The goal was to check whether RGA can handle BGR-to-RGB conversion and resize in one
preprocess path while preserving the known-good OpenCV baseline.

The experiment is intentionally limited to the offline fixed-video tool. The live
RTSP path is not changed by this step.

## Code Change

The default preprocessing path remains OpenCV. A new optional mode was added:

```bash
RK_YOLO_PREPROCESS=rga_cvt_resize
```

Supported modes now include:

- `opencv`: stable default path.
- `rga`: first-stage RGA resize path after OpenCV BGR-to-RGB conversion.
- `rga_cvt_resize`: second-stage RGA path using BGR input and RGB resized output.

If RGA is unavailable or the RGA operation fails, the program prints a warning and
falls back to the stable OpenCV preprocessing path.

## Board Environment

- Board: RK3588
- Tool: `/home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_video/build/rk_yolo_video`
- Model: `/home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn`
- Video: `/home/ubuntu/public_videos/anti_uav_fig1.mp4`
- Run directory: `/home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_video/artifacts/rga_stage2_20260428_212733`

## Validation Command Pattern

```bash
RK_YOLO_PROFILE=1 \
RK_YOLO_PREPROCESS=<mode> \
RK_YOLO_ZERO_COPY_INPUT=0 \
RK_YOLO_ALARM_OVERLAY=0 \
./rk_yolo_video \
  /home/ubuntu/public_videos/anti_uav_fig1.mp4 \
  <mode>.mp4 \
  /home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn \
  0.20 0.45 \
  <mode>.csv \
  <mode>.roi.jsonl \
  <mode>.alarm.csv
```

Tested modes:

- `opencv`
- `rga`
- `rga_cvt_resize`

The RK3588 build completed successfully before the run:

```text
[100%] Built target rk_yolo_video
```

## Output Consistency

All three modes produced the same detection count:

| Mode | Frames | Total detections | Detection frames |
| --- | ---: | ---: | ---: |
| OpenCV | 130 | 47 | 43 |
| RGA resize | 130 | 47 | 43 |
| RGA cvt+resize | 130 | 47 | 43 |

CSV comparison result:

| Comparison | Result |
| --- | --- |
| OpenCV vs RGA resize | Identical |
| OpenCV vs RGA cvt+resize | Identical |

This confirms that the second-stage RGA path did not change detection output on
this fixed validation video.

## Timing Result

The table below reports mean stage timing after excluding the first 5 warm-up
frames.

| Mode | prepare_ms | input_set_or_update_ms | rknn_run_ms | outputs_get_ms | decode_nms_ms | render_ms | total_work_ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| OpenCV | 4.79 | 48.20 | 50.07 | 0.79 | 4.13 | 38.49 | 146.50 |
| RGA resize | 5.82 | 47.65 | 50.10 | 0.84 | 4.28 | 39.11 | 147.84 |
| RGA cvt+resize | 5.24 | 47.37 | 49.98 | 0.80 | 4.19 | 38.32 | 145.94 |

## Interpretation

The new `rga_cvt_resize` path is functional and output-identical to the stable
OpenCV path on the tested video. Compared with the first-stage RGA resize mode,
it improves preprocessing and total work time:

- `prepare_ms`: 5.82 ms to 5.24 ms
- `total_work_ms`: 147.84 ms to 145.94 ms

However, the OpenCV baseline still has the lowest preprocessing time in this run:

- OpenCV `prepare_ms`: 4.79 ms
- RGA cvt+resize `prepare_ms`: 5.24 ms

The overall total time difference between OpenCV and `rga_cvt_resize` is small
and not enough to justify changing the stable default path. A likely reason is
that this implementation still uses virtual-address buffers and keeps OpenCV
letterbox padding, so it does not yet represent a full zero-copy RGA/MPP input
pipeline.

## Decision

Keep `RK_YOLO_PREPROCESS=opencv` as the default demonstration and validation path.
Keep `RK_YOLO_PREPROCESS=rga_cvt_resize` as an optional ablation path for the
thesis hardware-preprocess discussion.

Recommended thesis wording:

> The second-stage RGA experiment verified that color conversion and resize can
> be moved into an optional RGA preprocessing path without changing detection
> output. On the tested fixed video, this path improved over the first RGA resize
> attempt, but it did not provide a decisive preprocessing advantage over the
> OpenCV baseline. Therefore, the final system keeps OpenCV as the stable default
> and retains RGA as a validated optional optimization direction.

