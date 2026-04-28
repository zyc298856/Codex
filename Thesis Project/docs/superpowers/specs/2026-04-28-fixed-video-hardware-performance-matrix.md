# 2026-04-28 Fixed-Video Hardware Performance Matrix

## Purpose

This experiment compares the main RK3588 hardware-optimization paths using the
same model and the same public UAV video. It is intended to support the thesis
discussion about NPU multi-thread inference, zero-copy input, RGA preprocessing,
and real-time scheduling policy.

The experiment is split into two parts:

- Offline fixed-video inference with `rk_yolo_video`
- RTSP fixed-video replay with `rk_yolo_live_rtsp`

The existing stable demo stream on port `8554` was not stopped or modified.

## Common Inputs

- Board: RK3588
- Video: `/home/ubuntu/public_videos/anti_uav_fig1.mp4`
- Model: `/home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn`
- Score threshold: `0.20`
- NMS threshold: `0.45`

## Part 1: Offline Fixed-Video Matrix

Run directory:

```text
/home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_video/artifacts/hardware_matrix_20260428_214012
```

Command pattern:

```bash
RK_YOLO_PROFILE=1 \
RK_YOLO_PREPROCESS=<mode> \
RK_YOLO_ZERO_COPY_INPUT=<0_or_1> \
RK_YOLO_ALARM_OVERLAY=0 \
./rk_yolo_video input.mp4 output.mp4 model.rknn 0.20 0.45 output.csv output.roi.jsonl output.alarm.csv
```

### Output Consistency

All offline modes produced identical detection CSV output.

| Mode | Frames | Detections | Detection frames | Equal to OpenCV CSV |
| --- | ---: | ---: | ---: | --- |
| OpenCV baseline | 130 | 61 | 50 | Baseline |
| RGA cvt+resize | 130 | 61 | 50 | Yes |
| Zero-copy input | 130 | 61 | 50 | Yes |

### Stage Timing

Mean timing was computed after excluding early warm-up frames.

| Mode | prepare_ms | input_set_or_update_ms | rknn_run_ms | outputs_get_ms | decode_nms_ms | render_ms | total_work_ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| OpenCV baseline | 4.80 | 48.34 | 49.87 | 0.79 | 4.14 | 38.62 | 146.60 |
| RGA cvt+resize | 5.14 | 48.41 | 49.90 | 0.81 | 4.23 | 38.66 | 147.17 |
| Zero-copy input | 4.40 | 0.45 | 92.62 | 0.72 | 3.76 | 35.08 | 137.07 |

### Offline Interpretation

The RGA color-convert plus resize path preserved detection output but did not
improve total offline processing time over the OpenCV baseline in this run. This
confirms that the current RGA implementation is useful as an optional hardware
preprocessing ablation, but it should not replace the stable default path yet.

The zero-copy input path also preserved detection output. It nearly eliminated
the explicit `rknn_inputs_set` cost, but `rknn_run` became longer. Even with this
shift, the measured `total_work_ms` was lower than the OpenCV baseline in this
fixed-video run. This makes zero-copy a useful experimental optimization path,
but it still requires live-pipeline validation before becoming the default.

## Part 2: RTSP Fixed-Video Replay Matrix

Run directory:

```text
/home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_live_rtsp/artifacts/hardware_matrix_20260428_214201
```

Each case replayed the same public UAV video through the RTSP pipeline. A local
`ffmpeg` client pulled the stream during each run, so the reported values reflect
actual RTSP publishing rather than an idle server.

### RTSP Configuration

| Case | detect_every_n | Workers | Smoothing | Dynamic ROI | Tracking | Purpose |
| --- | ---: | ---: | --- | --- | --- | --- |
| `baseline_n1` | 1 | 1 | Off | Off | Motion | Single-context full-frame baseline |
| `multictx_w2_n1` | 1 | 2 | Off | Off | Motion | Multi-context NPU throughput test |
| `policy_n2` | 2 | 1 | On | On | Motion | Balanced real-time policy |
| `policy_n3` | 3 | 1 | On | On | Motion | Low-latency real-time policy |

### RTSP Results

| Case | avg_stream_fps | avg_npu_fps | avg_roi_fps | avg_work_ms | avg_end_to_end_ms | dropped_capture | dropped_publish |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `baseline_n1` | 6.80 | 6.80 | 0.00 | 141.30 | 254.88 | 364 | 0 |
| `multictx_w2_n1` | 13.71 | 13.71 | 0.00 | 124.91 | 215.25 | 0 | 0 |
| `policy_n2` | 12.16 | 6.75 | 1.86 | 128.32 | 188.97 | 69 | 0 |
| `policy_n3` | 13.40 | 4.45 | 1.12 | 87.81 | 92.16 | 0 | 0 |

Additional counters:

| Case | captured | inferred | published | NPU runs | Reused frames | ROI crop runs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `baseline_n1` | 735 | 369 | 369 | 369 | 0 | 0 |
| `multictx_w2_n1` | 736 | 734 | 734 | 734 | 0 | 0 |
| `policy_n2` | 727 | 656 | 656 | 363 | 294 | 108 |
| `policy_n3` | 711 | 711 | 711 | 237 | 474 | 66 |

All four RTSP runs were successfully pulled by `ffmpeg`.

## Conclusion

The single-context full-frame baseline is not suitable as the final real-time
configuration. It only reached about `6.80 FPS` and dropped many capture frames.

The multi-context case with two NPU workers is the strongest evidence for the
teacher-requested NPU multi-thread direction. Under full-frame detection
(`detect_every_n=1`), it increased RTSP throughput from `6.80 FPS` to
`13.71 FPS` and removed capture drops. This result should be used as the main
technical evidence for the multi-context NPU parallel-inference section.

For real-time viewing, the scheduling policy remains important. The `policy_n3`
configuration produced the lowest average end-to-end latency (`92.16 ms`) and no
capture or publish drops, while using only about `4.45` NPU runs per second. The
`policy_n2` configuration performs more frequent detection and may be safer when
the target moves quickly or when detection freshness is more important than
latency.

Recommended wording for the thesis:

> The experiment shows that multi-context NPU inference can significantly improve
> full-frame detection throughput on RK3588. However, for real-time viewing, the
> best practical configuration is not necessarily full-frame detection on every
> frame. A combined strategy using periodic NPU detection, lightweight tracking,
> dynamic ROI, and temporal smoothing provides a better balance between latency,
> continuity, and hardware load.

Recommended current system choices:

- Use `RK_YOLO_INFER_WORKERS=2` with `detect_every_n=1` when demonstrating NPU
  multi-thread throughput.
- Use `detect_every_n=3` with dynamic ROI, motion tracking, smoothing, and alarm
  overlay when demonstrating low-latency real-time viewing.
- Use `detect_every_n=2` if target freshness is prioritized over the lowest
  latency.
- Keep OpenCV preprocessing as the default stable path.
- Keep RGA and zero-copy as validated optional optimization experiments.

