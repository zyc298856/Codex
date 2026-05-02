# Thesis Requirements vs Current Progress

## Scope

This note maps the official graduation-design task book and proposal to the current project status. It is intended for weekly reports, mid-term review, advisor updates, and later thesis writing.

Source documents:

- [论文任务书(22123739_朱奕澄).doc](C:/Users/Tony/Downloads/%E8%AE%BA%E6%96%87%E4%BB%BB%E5%8A%A1%E4%B9%A6(22123739_%E6%9C%B1%E5%A5%95%E6%BE%84).doc)
- [论文开题报告(22123739_朱奕澄).doc](C:/Users/Tony/Downloads/%E8%AE%BA%E6%96%87%E5%BC%80%E9%A2%98%E6%8A%A5%E5%91%8A(22123739_%E6%9C%B1%E5%A5%95%E6%BE%84).doc)

## Overall Judgment

The project direction is aligned with the task book and proposal. The current work already covers the main technical backbone of the topic:

- RK3588-side deployment
- `PyTorch -> ONNX -> RKNN` model conversion
- C++ real-time detection system
- multithread / multi-context performance experiments
- public-video fixed-input evaluation

The work is no longer at the "preparation" stage. It has already entered the "system running + experiment conclusions emerging" stage.

The remaining gap is mainly in the final engineering completion items:

- live integration of the recovered drone-specific RKNN model
- GPIO / alarm / control-loop integration
- longer stability testing
- deeper optimization layers such as INT8 quantization and RGA-first preprocessing

## Requirement Mapping

| Requirement from task book / proposal | Current status | Evidence / current output | Gap / next step |
|---|---|---|---|
| Deploy the model onto RK3588 | Mostly completed | RK3588 live RTSP path is already running; offline validator is available; the recovered drone-specific RKNN now runs successfully on the board-side offline path | Promote the recovered drone-specific RKNN into the live RTSP path |
| Build the `PyTorch -> ONNX -> RKNN` conversion path | Completed | [`best.pt`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/training_runs/drone_gpu_50e/weights/best.pt), [`best.onnx`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/training_runs/drone_gpu_50e/weights/best.onnx), [`best.end2end_false.op12.rk3588.fp.v220.rknn`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn) | Need to complete live-path validation for the recovered model |
| Study RK3588 NPU acceleration | Completed at the first experimental level | Live-camera and fixed-input experiments already compare throughput, latency, and resource use; longer board-side public-video inference is now also available | Can be strengthened by more complete ablation and longer-run testing |
| Design a C++ multithread pipeline | Completed | `rk_yolo_live_rtsp` and `rk_yolo_video` are both C++ tools; live pipeline supports capture/infer/publish separation and experimental multi-context inference | Can still be extended toward stronger integration with the legacy encoder path |
| Solve real-time bottlenecks with multithreading / pipelining | Completed at current stage | First-round live results already show clear throughput-latency trade-offs | Still worth adding more scenes and longer tests |
| Evaluate throughput, latency, and stability | Partially completed | First-round live metrics, policy sweep, fixed-input replay metrics, and a longer board-side public-video validation (`video01`, `1050` frames) already exist | Need longer stability testing and more systematic reporting |
| Construct a complete embedded application system | Partially completed | Camera -> detection -> RTSP live pipeline is working on board | Full closure with external-device actuation is not finished |
| External interface for alarm / countermeasure / closed-loop action | Not completed yet | No GPIO / alarm actuation path wired in yet | This is one of the highest-priority remaining engineering items |
| INT8 quantization strategy under accuracy constraints | Partially completed / weakly covered | RKNN conversion has been completed and deployment path exists | Current stable path is still floating-point oriented; no full INT8 accuracy-speed comparison yet |
| RGA hardware preprocessing in the heterogeneous pipeline | Not completed as a mainline path | Current practical path still uses OpenCV preprocessing for bring-up and validation | A later optimization stage should evaluate `RGA -> NPU` preprocessing flow |

## What Is Already Strong

### 1. RK3588 live system is already real

This is not a paper-only design. A working live chain already exists:

- camera input
- on-board RKNN inference
- box drawing
- RTSP push to PC

This directly supports the task-book focus on embedded deployment and engineering integration.

Relevant outputs include:

- [`rk_yolo_live_rtsp`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_live_rtsp)
- [`rk_yolo_video`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_video)

### 2. Multithread / multi-context experiments already support the thesis mainline

The advisor-facing core thesis theme has already started to take shape:

- baseline single-worker inference
- dual-context throughput-focused mode
- policy-optimized low-latency mode

These experiments already support the main claim that RK3588 NPU parallelism is a throughput-latency trade-off problem rather than a pure "more threads is better" problem.

Relevant materials:

- [`2026-04-21-rk3588-npu-first-live-results-summary.md`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-21-rk3588-npu-first-live-results-summary.md)
- [`first_round_live_metrics.csv`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_live_rtsp/artifacts/experiments/first_round_live_metrics.csv)
- [`fig_first_round_live_perf_compare.svg`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_live_rtsp/artifacts/experiments/fig_first_round_live_perf_compare.svg)
- [`policy_sweep_live_metrics.csv`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_live_rtsp/artifacts/experiments/policy_sweep_live_metrics.csv)
- [`fig_policy_sweep_live_compare.svg`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_live_rtsp/artifacts/experiments/fig_policy_sweep_live_compare.svg)

### 3. A drone-specific model path already exists

The work is not limited to running a generic detector. A drone-specific model training and export chain already exists:

- dataset preparation
- GPU training
- ONNX export
- RKNN conversion

This is a strong thesis asset because it links algorithm-side work to deployment-side work, and the recovered board-side offline path is now already working on longer public UAV video.

### 4. Public-video testing has been added as a practical substitute

Since real-flight scenes are currently hard to obtain, the project now has a repeatable public-video evaluation layer. This is academically reasonable and helps separate:

- system-level evaluation
- task-level evaluation

Relevant materials:

- [`public_video_eval_manifest.json`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/datasets/drone_single_class/manifests/public_video_eval_manifest.json)
- [`evaluate_public_videos.py`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/training/drone_yolov10/evaluate_public_videos.py)
- [`2026-04-21-public-video-eval-bootstrap-summary.md`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-21-public-video-eval-bootstrap-summary.md)

## Main Remaining Gaps

### 1. Drone-specific RKNN live-path integration still remains

The original board-side runtime blocker has been resolved for the offline validator, but the recovered drone-specific model has not yet been promoted into the live RTSP path.

Status:

- the old generic `yolov10n.rknn` works in the board-side offline validator
- the recovered drone-specific model [`best.end2end_false.op12.rk3588.fp.v220.rknn`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn) now also works in the board-side offline validator
- the recovered model has already produced stable results on [`video01_drone_eval_v1.mp4`](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos/rk3588_board/video01_drone_eval_v1.mp4)

Meaning:

- the board-side drone-specific path is no longer blocked at `rknn_run()`
- the next engineering step is now live integration rather than runtime rescue
- this shifts the remaining gap from "the model cannot run on board" to "the model has not yet been integrated into the real-time path"

This should remain the highest-priority technical integration item when the board is available.

### 2. GPIO / closed-loop action is still missing

The task book and proposal both clearly mention a closed-loop system:

- detect UAV
- trigger alarm / gimbal / countermeasure / external response

This part is still not implemented. From the thesis-completion perspective, this is a more important unfinished item than some deeper optimization layers.

### 3. INT8 and RGA are still optimization-stage topics

These two items are not useless; they are simply not yet the strongest missing core path.

- `INT8 quantization`:
  the project has RKNN conversion, but not yet a complete INT8-vs-FP comparison with accuracy/speed discussion
- `RGA preprocessing`:
  the current mainline is still OpenCV-based for stability and bring-up efficiency

They remain good enhancement topics, but should not outrank live integration of the recovered drone model and the missing closed-loop control path.

## Recommended Priority Order

To align the thesis more tightly with the official documents, the remaining work should be prioritized in this order:

1. Integrate the recovered drone-specific RKNN model into the live RTSP path and compare it with the current generic live model.
2. Add a minimal GPIO / alarm / output trigger path to satisfy the closed-loop requirement.
3. Add longer stability testing and cleaner resource/performance reporting.
4. If time remains, extend into INT8 quantization comparison.
5. If time remains after that, evaluate RGA-based preprocessing as a deeper heterogeneous optimization path.

## Suggested Summary Sentence for Reports

At the current stage, the project has already completed the main embedded deployment backbone of the topic, including RK3588-side model conversion, C++ real-time system bring-up, first-round NPU multithreading experiments, and stable board-side offline validation of the recovered drone-specific model on a longer public UAV sequence. The remaining work is concentrated on the final engineering completion items: live integration of the recovered drone-specific model, external-device closed-loop integration, and deeper optimization topics such as INT8 quantization and RGA-based preprocessing.
