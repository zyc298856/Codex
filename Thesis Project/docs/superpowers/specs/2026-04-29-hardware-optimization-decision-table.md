# 2026-04-29 Hardware Optimization Decision Table

## Purpose

This document consolidates the RK3588 hardware-optimization experiments into one thesis-ready
decision table. It is intended to support Chapter 5, weekly reports, and final defense discussion.

The key principle is conservative: an optimization is not treated as the default only because it
uses hardware acceleration. It must also improve the complete real-time pipeline or provide clear
experimental value.

## Current Stable Baseline

Recommended stable live-viewing configuration:

```text
model: best.end2end_false.op12.rk3588.fp.v220.rknn
program: rk_yolo_live_rtsp
resolution: 640 x 480
score threshold: 0.35
nms threshold: 0.45
detect_every_n: 2
track mode: motion
box smoothing: on
dynamic ROI: on
software alarm overlay: on when demonstration needs alarm feedback
```

Recommended fixed-video validation entry:

```text
program: rk_yolo_video
model: best.end2end_false.op12.rk3588.fp.v220.rknn
purpose: reproducible offline comparison, CSV/ROI/alarm sidecar output, screenshot/video evidence
```

## Optimization Summary Table

| Optimization | Runtime switch | Code path | Validated status | Key result | Default recommendation | Thesis role |
| --- | --- | --- | --- | --- | --- | --- |
| NPU multi-context inference | `RK_YOLO_INFER_WORKERS=2` with `detect_every_n=1` | `rk_yolo_live_rtsp` inference workers | Validated | Every-frame dual-context improved NPU FPS from about 8.59 to 10.64 and reduced latency from about 268.89 ms to 173.58 ms compared with single-context every-frame inference | Not default for viewing; use for hardware parallelism experiment | Main NPU multi-thread/multi-context evidence |
| Detection interval policy | command arg `detect_every_n=2` | `rk_yolo_live_rtsp` scheduler | Validated | `N=2` kept output around 10.36 FPS and reduced visible box jitter compared with sparse `N=3` mode | Recommended live-viewing default | Practical real-time strategy |
| Box smoothing | `RK_YOLO_BOX_SMOOTH=1`, `RK_YOLO_BOX_SMOOTH_ALPHA=0.35` | display result smoothing | Validated | User-observed left-right box oscillation was reduced without changing model output | Recommended live-viewing default | Visual stability improvement |
| Motion tracking | `RK_YOLO_TRACK_MODE=motion` | inter-frame result reuse | Validated | Provides lightweight reuse between keyframe detections and avoids heavier optical-flow cost | Recommended live-viewing default | Lightweight tracking component |
| Dynamic ROI | `RK_YOLO_DYNAMIC_ROI=1` | inference crop strategy | Validated in live path | Helps focus keyframe inference around previous target area while periodically refreshing full frame | Recommended with periodic full-frame refresh | Small-target focused scheduling strategy |
| Stage profiling | `RK_YOLO_PROFILE=1` | `rk_yolo_video` and shared detector timing | Validated | Emits `profile_csv` rows for prepare, input upload/update, RKNN run, output get, decode, render, total work | Off for demo, on for experiments | Experimental method and bottleneck analysis |
| RKNN zero-copy input | `RK_YOLO_ZERO_COPY_INPUT=1` | `rknn_create_mem` + `rknn_set_io_mem` | Validated | Input update dropped from about 31.50 ms to 0.35 ms, but `rknn_run` rose from about 48.30 ms to 80.04 ms; total work did not improve | Keep off | Shows implemented but not beneficial under current FP model |
| RGA model-input resize | `RK_YOLO_PREPROCESS=rga` | shared detector preprocessing | Validated | RGA resize path works and falls back safely | Not default | First RGA preprocessing step |
| RGA color-convert + resize | `RK_YOLO_PREPROCESS=rga_cvt_resize` | shared detector preprocessing | Validated | Full-frame stress RTSP test improved stream/NPU FPS from about 6.99 to 8.00 and reduced work time from about 145.23 ms to 132.65 ms | Optional; useful in stress tests | Strongest RGA preprocessing result |
| RGA live frame resize | `RK_YOLO_RGA_FRAME_RESIZE=1` | live capture frame normalization | Validated | On 1920 x 1080 input resized to 640 x 480, total average work time fell from about 108.90 ms to 93.42 ms | Optional when source resolution is higher than output | RGA pipeline-level acceleration evidence |
| RGA NV12 publishing | `RK_YOLO_RGA_PUBLISH_NV12=1` | BGR display frame to NV12 before `mpph264enc` | Validated | Functionally valid, but overall average latency increased in the tested pipeline | Keep off | Negative/neutral result proving measured selection |
| RGA letterbox preprocessing | `RK_YOLO_RGA_LETTERBOX=1` | shared detector letterbox input canvas | Validated | Detection count matched baseline; short fixed-video test total work fell from about 148.05 ms to 137.24 ms, but prepare time increased slightly | Optional; not yet default | Latest RGA input-path experiment |
| Software alarm overlay | `RK_YOLO_ALARM_OVERLAY=1` | video overlay and alarm CSV | Validated | Provides visible red `UAV ALERT` / green `NORMAL` feedback without external GPIO hardware | Recommended for demonstration | Replaces unavailable relay/buzzer hardware |

## Recommended Interpretation

The project now has two complementary conclusions.

System-level real-time recommendation:

```text
detect_every_n=2
track_mode=motion
box_smooth=on
dynamic_roi=on
single RKNN context for live viewing
software alarm overlay enabled for demos
```

This configuration is best for visual monitoring because it balances latency, output smoothness,
and implementation stability.

Hardware-optimization recommendation:

```text
Use dual-context NPU inference to demonstrate RK3588 NPU parallelism.
Use RGA preprocessing and frame-resize experiments to demonstrate hardware-assisted image processing.
Keep zero-copy input and NV12 publish as validated optional experiments, not default paths.
```

This separation avoids overclaiming. Multi-context, RGA, and zero-copy are all implemented and
measured, but only the options that improve the complete pipeline should be recommended for normal
operation.

## Paper-Friendly Conclusion

The RK3588 optimization process shows that hardware acceleration must be evaluated at the system
level rather than at the operator level. Dual NPU context improves every-frame inference throughput,
but the lowest-latency live viewing mode still comes from a detection-interval policy with
lightweight tracking and box smoothing. RGA reduces preprocessing or frame-resize overhead in
selected scenarios, especially under full-frame stress or high-resolution input, while NV12 publish
and RKNN zero-copy input demonstrate that moving work into hardware does not automatically improve
end-to-end latency. Therefore, the final system uses a stable FP RKNN model with policy-based
scheduling as the demonstration baseline, and reports RGA, zero-copy, and multi-context paths as
controlled hardware-optimization experiments.

## Source Validation Records

- `2026-04-25-rtsp-detect-every-n-smoothing-ablation.md`
- `2026-04-25-rtsp-multi-context-performance-ablation.md`
- `2026-04-28-rk-yolo-profile-zero-copy-validation.md`
- `2026-04-28-live-rtsp-rga-preprocess-validation.md`
- `2026-04-28-live-rtsp-rga-frame-resize-validation.md`
- `2026-04-28-live-rtsp-rga-nv12-publish-validation.md`
- `2026-04-29-rga-letterbox-preprocess-validation.md`
