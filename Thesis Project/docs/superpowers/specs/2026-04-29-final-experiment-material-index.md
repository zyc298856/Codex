# 2026-04-29 Final Experiment Material Index

## Purpose

This document maps the thesis experiments and implementation claims to their code entry points, validation records, and result artifacts. It is intended for thesis revision, defense slides, weekly logs, and advisor Q&A.

Important reading rule:

- `paper/full_thesis_latest_merged.docx` is the current formal thesis draft.
- `docs/superpowers/specs/` contains project history, experiment records, and thesis-support notes.
- Some older screenshots, draft DOCX files, raw videos, and temporary QA renders remain local-only and should not be treated as the clean GitHub-facing baseline.

## Current Stable Thesis Position

The current thesis should be described as an RK3588 embedded deployment and system-optimization project, not as a new detection-network proposal.

Stable claims:

- A single-class YOLOv10 drone detector was trained, exported, converted, and deployed through RKNN.
- The system provides fixed-video validation and real-time RTSP detection on RK3588.
- NPU multi-context inference, detection interval control, dynamic ROI, lightweight tracking, box smoothing, stage profiling, RGA experiments, zero-copy input, and software alarm overlay were implemented as switchable engineering paths.
- The recommended live demonstration path remains conservative: FP RKNN, stable preprocessing, detection interval, motion tracking, box smoothing, dynamic ROI, and optional software alarm overlay.

Do not overclaim:

- INT8 quantization is not yet a completed precision-speed closed loop.
- Full RGA hardware preprocessing with MPP/physically continuous memory/default RTSP path is not yet the stable main path.
- GPIO relay/buzzer closed-loop hardware was replaced by software alarm overlay in this phase.

## Thesis Section to Evidence Map

| Thesis section | Main claim | Code entry point | Primary evidence | Status |
| --- | --- | --- | --- | --- |
| 2.3 RK3588 platform | RK3588 provides CPU/GPU/NPU/video codec resources; INT8 TOPS is hardware potential, not current main model path | N/A | Thesis references and platform description | Written in thesis |
| 3.3 Model migration and detection module | YOLOv10 output formats and single-class `[1,5,8400]` layout are handled by post-processing | `rk_yolo_video/src/yolo_rknn.cpp`, `rk_yolo_video/include/yolo_rknn.h` | `2026-04-21-drone-model-error-analysis.md`, `2026-04-21-drone-model-deployment-defaults-design.md` | Implemented |
| 3.4 Real-time video pipeline | Capture, inference, render, RTSP, and logging are decoupled by queues/threads | `rk_yolo_live_rtsp/src/main.cpp` | RTSP validation records and Chapter 5 tests | Implemented |
| 3.5 Dynamic ROI and lightweight tracking | Detection interval, ROI, motion prediction, optical flow, and box smoothing reduce jitter/latency | `rk_yolo_live_rtsp/src/main.cpp` | `2026-04-25-rtsp-detect-every-n-smoothing-ablation.md` | Implemented |
| 3.6 Multi-context NPU design | Multiple independent RKNN contexts can improve every-frame inference throughput | `rk_yolo_live_rtsp/src/main.cpp` | `2026-04-25-rtsp-multi-context-performance-ablation.md`, `2026-04-29-hardware-optimization-decision-table.md` | Implemented |
| 4.3 RKNN inference module | Load/Infer/Release, profiling, zero-copy, RGA preprocessing, and output decoding are encapsulated | `rk_yolo_video/src/yolo_rknn.cpp`, `rk_yolo_video/include/yolo_rknn.h` | `2026-04-29-thesis-code-consistency-check.md` | Implemented |
| 4.4 Fixed-video detection | Board-side repeatable input path outputs boxed video, CSV, ROI JSONL, and alarm CSV | `rk_yolo_video/src/main.cpp` | Public-video and fixed-video validation outputs | Implemented |
| 4.5 Real-time RTSP detection | RK3588 can publish boxed live video through RTSP | `rk_yolo_live_rtsp/src/main.cpp` | `2026-04-25-drone-rtsp-integration-smoke-test.md`, `2026-04-28-live-rtsp-final-validation.md` | Implemented |
| 4.8 Logging and profiling | Runtime metrics and stage timing are available for bottleneck analysis | `rk_yolo_video/src/main.cpp`, `rk_yolo_video/src/yolo_rknn.cpp`, `rk_yolo_live_rtsp/src/main.cpp` | `2026-04-25-rknn-stage-profile-bottleneck.md`, `2026-04-28-rk-yolo-profile-zero-copy-validation.md` | Implemented |
| 5.2 Training results | The trained drone model has basic single-class detection capability | `training/drone_yolov10/train_drone_yolov10.py` | training run outputs under `training_runs/drone_gpu_50e/` | Implemented |
| 5.3 Public-video fixed input | The recovered drone RKNN can detect drones in public UAV videos on RK3588 | `rk_yolo_video` | board-side `eval_runs/public_videos/rk3588_board/` outputs | Implemented |
| 5.4 RTSP first-round comparison | Multi-context raises throughput; policy-optimized path improves practical latency | `rk_yolo_live_rtsp` | `first_round_live_metrics.csv`, first-round summary notes | Implemented |
| 5.5 Detection interval and smoothing | `detect_every_n`, motion tracking, and box smoothing improve visual stability | `rk_yolo_live_rtsp/src/main.cpp` | `2026-04-25-rtsp-detect-every-n-smoothing-ablation.md` | Implemented |
| 5.6 Multi-context experiment | Dual context is the clearest NPU parallelism evidence | `rk_yolo_live_rtsp/src/main.cpp` | `2026-04-25-rtsp-multi-context-performance-ablation.md` | Implemented |
| 5.7 Stage bottleneck analysis | Input update and RKNN execution dominate, not drawing or post-processing | `RK_YOLO_PROFILE=1` paths | `2026-04-25-rknn-stage-profile-bottleneck.md` | Implemented |
| 5.8 Zero-copy and RGA input paths | Zero-copy and RGA paths are validated optional experiments, not stable defaults | `rk_yolo_video/src/yolo_rknn.cpp`, `rk_yolo_live_rtsp/src/main.cpp` | `2026-04-28-*RGA*`, `2026-04-29-rga-letterbox-preprocess-validation.md` | Implemented as experiments |
| 5.9 Recommended configuration | Final recommendation separates live-viewing default from hardware-optimization evidence | `rk_yolo_live_rtsp`, `rk_yolo_video` | `2026-04-29-hardware-optimization-decision-table.md` | Written in thesis |
| 6.2 Outlook | INT8, full RGA closed loop, GPIO hardware response, and real flight tests remain future work | N/A | `2026-04-29-thesis-code-consistency-check.md` | Wording aligned |

## Core Code Entry Points

| Purpose | File or directory | Notes |
| --- | --- | --- |
| Fixed-video RK3588 validation | `rk_yolo_video/src/main.cpp` | Video file in, boxed video/CSV/ROI/alarm output |
| Shared RKNN detector | `rk_yolo_video/src/yolo_rknn.cpp` | Load, input preparation, RGA, zero-copy, profiling, decode |
| Detector API | `rk_yolo_video/include/yolo_rknn.h` | Stable `Load/Infer/Release` interface |
| Real-time RTSP demo | `rk_yolo_live_rtsp/src/main.cpp` | Capture, scheduling, tracking, smoothing, alarm, RTSP publish |
| Training entry | `training/drone_yolov10/train_drone_yolov10.py` | Single-class drone model training |
| Public-video evaluation | `training/drone_yolov10/evaluate_public_videos.py` | PC/WSL screening and output generation |
| Dataset import/validation | `datasets/drone_single_class/scripts/` | Dataset and public-video organization scripts |

## Runtime Switch Index

| Switch | Program | Purpose | Thesis role |
| --- | --- | --- | --- |
| `RK_YOLO_PROFILE=1` | `rk_yolo_video`, `rk_yolo_live_rtsp` | Print stage timing/profile rows | Stage bottleneck analysis |
| `RK_YOLO_ZERO_COPY_INPUT=1` | shared RKNN detector | Use `rknn_create_mem` and `rknn_set_io_mem` input path | Optional hardware-input experiment |
| `RK_YOLO_PREPROCESS=rga` | shared RKNN detector | RGA resize experiment | RGA preliminary path |
| `RK_YOLO_PREPROCESS=rga_cvt_resize` | shared RKNN detector | RGA color conversion plus resize | Strongest RGA preprocessing evidence |
| `RK_YOLO_RGA_LETTERBOX=1` | shared RKNN detector | RGA writes letterbox input canvas | Latest RGA input-path experiment |
| `RK_YOLO_RGA_FRAME_RESIZE=1` | `rk_yolo_live_rtsp` | RGA frame normalization before pipeline | High-resolution input optimization |
| `RK_YOLO_RGA_PUBLISH_NV12=1` | `rk_yolo_live_rtsp` | RGA BGR-to-NV12 publish path | Valid but not default |
| `RK_YOLO_INFER_WORKERS=2` | `rk_yolo_live_rtsp` | Dual RKNN context inference workers | Main NPU parallelism evidence |
| `RK_YOLO_TRACK_MODE=motion` | `rk_yolo_live_rtsp` | Linear motion prediction between detection frames | Recommended live demo default |
| `RK_YOLO_TRACK_MODE=optflow` | `rk_yolo_live_rtsp` | Lucas-Kanade sparse optical flow tracking | Alternative tracking mode |
| `RK_YOLO_BOX_SMOOTH=1` | `rk_yolo_live_rtsp` | Smooth displayed boxes | Reduces visible jitter |
| `RK_YOLO_DYNAMIC_ROI=1` | `rk_yolo_live_rtsp` | Enable dynamic ROI strategy | Low-latency strategy component |
| `RK_YOLO_ALARM_OVERLAY=1` | both tools | Draw `UAV ALERT` / `NORMAL` banner | Software replacement for hardware alarm |

## Key Validation Records

### Model and deployment

- `2026-04-20-drone-training-bootstrap-design.md`
- `2026-04-21-drone-model-deployment-defaults-design.md`
- `2026-04-21-drone-model-error-analysis.md`
- `2026-04-21-public-video-eval-bootstrap-summary.md`

### Live RTSP and NPU scheduling

- `2026-04-21-rk3588-npu-first-live-results-summary.md` *(local material if not yet tracked)*
- `2026-04-21-rk3588-npu-best-live-config-recommendation.md` *(local material if not yet tracked)*
- `2026-04-25-rtsp-detect-every-n-smoothing-ablation.md` *(local material if not yet tracked)*
- `2026-04-25-rtsp-multi-context-performance-ablation.md` *(local material if not yet tracked)*
- `2026-04-28-rtsp-10min-stability-validation.md`
- `2026-04-28-live-rtsp-final-validation.md` *(local material if not yet tracked)*

### Profiling, zero-copy, and RGA

- `2026-04-25-rknn-stage-profile-bottleneck.md` *(local material if not yet tracked)*
- `2026-04-25-rknn-zero-copy-input-experiment.md` *(local material if not yet tracked)*
- `2026-04-28-rk-yolo-profile-zero-copy-validation.md` *(local material if not yet tracked)*
- `2026-04-28-rga-preprocess-validation.md`
- `2026-04-28-rga-stage2-cvt-resize-validation.md`
- `2026-04-28-live-rtsp-rga-preprocess-validation.md`
- `2026-04-28-live-rtsp-rga-frame-resize-validation.md`
- `2026-04-28-live-rtsp-rga-nv12-publish-validation.md`
- `2026-04-29-rga-letterbox-preprocess-validation.md`
- `2026-04-29-hardware-optimization-decision-table.md`

### Thesis alignment

- `2026-04-22-project-history-master-index.md` *(local material if not yet tracked)*
- `2026-04-22-thesis-requirements-progress-mapping.md` *(local material if not yet tracked)*
- `2026-04-29-thesis-code-consistency-check.md`

## Result Artifact Index

| Result type | Location | Use |
| --- | --- | --- |
| Formal thesis draft | `paper/full_thesis_latest_merged.docx` | Current thesis file |
| Training outputs | `training_runs/drone_gpu_50e/weights/` | `best.pt`, ONNX, RKNN model artifacts |
| Fixed-video public UAV outputs | `eval_runs/public_videos/rk3588_board/` | Boxed videos, CSV, ROI JSONL, alarm CSV |
| RTSP experiment metrics | `rk_yolo_live_rtsp/artifacts/experiments/` | CSV and figure assets for Chapter 5 |
| Public-video manifest | `datasets/drone_single_class/manifests/public_video_eval_manifest.json` | Source tracking for public-video validation |
| Thesis figures and drafts | `docs/thesis_drafting/` | Local-only drafting and visual-QA materials |

## Best Evidence to Show First

If time is limited, use these in this order:

1. `paper/full_thesis_latest_merged.docx`
2. `docs/superpowers/specs/2026-04-29-hardware-optimization-decision-table.md`
3. `docs/superpowers/specs/2026-04-29-thesis-code-consistency-check.md`
4. `rk_yolo_video/src/yolo_rknn.cpp`
5. `rk_yolo_live_rtsp/src/main.cpp`
6. `eval_runs/public_videos/rk3588_board/` boxed video outputs

## Defense Q&A Pointers

If asked why INT8 is not the main result:

- Answer that the current stable model path is FP RKNN.
- INT8 requires calibration data, precision comparison, and board-side speed validation.
- The thesis truthfully keeps INT8 as future work.

If asked why RGA is not the default:

- Answer that RGA paths have been implemented and tested as optional experiments.
- Some RGA paths improved specific stages or stress scenarios, but not every path improved end-to-end latency.
- Therefore the default remains the most stable live-viewing path, while RGA is reported as hardware optimization evidence.

If asked whether the project satisfies the NPU multithreading direction:

- Answer yes: dual RKNN context workers were implemented and measured.
- The strongest evidence is the every-frame dual-context experiment.
- The thesis separates this hardware-throughput result from the practical low-latency demonstration configuration.

If asked how GPIO alarm was handled:

- Answer that no relay/buzzer hardware was available.
- The project implements software alarm overlay and alarm CSV logging as a visible, repeatable substitute.
- GPIO hardware response is left as future closed-loop expansion.

## Next Recommended Documentation Step

The next useful document would be a defense-slide outline that maps each slide to this evidence index. That will prevent the final PPT from drifting away from the actual code and experiments.
