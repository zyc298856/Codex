# Project History Master Index

## Purpose

This note is the top-level index for the graduation-design project history. It is meant to answer one practical question:

> If we later need to write the thesis, weekly logs, advisor summaries, or recover project context after a pause, where should we start and which files matter most?

It does not replace the detailed notes. Instead, it links the major design documents, experiment summaries, model artifacts, and result outputs into one traceable timeline.

## Current Status in One Paragraph

The project has already moved beyond migration planning and basic bring-up. A working RK3588 real-time detection pipeline exists, first-round NPU multithreading experiments have produced throughput-latency conclusions, a drone-specific model has been trained and converted, and the recovered drone-specific RKNN model now runs stably in the board-side offline validator on a longer public UAV video. The highest-value remaining step is to integrate the recovered drone-specific model into the live RTSP path and continue the system toward a more complete thesis-level closed-loop implementation.

## Timeline and Main Milestones

| Stage | Main milestone | Primary record |
|---|---|---|
| 2026-03-30 | Built the first RK3588-side offline YOLO video validation design | [2026-03-30-rk3588-yolov10-video-design.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-03-30-rk3588-yolov10-video-design.md) |
| 2026-04-14 | Created the single-class drone dataset scaffold and import plan | [2026-04-14-drone-dataset-design.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-14-drone-dataset-design.md) |
| 2026-04-20 | Locked the first training bootstrap plan | [2026-04-20-drone-training-bootstrap-design.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-20-drone-training-bootstrap-design.md) |
| 2026-04-21 | Finished first-round live NPU experiments and best-live-config recommendation | [2026-04-21-rk3588-npu-first-live-results-summary.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-21-rk3588-npu-first-live-results-summary.md), [2026-04-21-rk3588-npu-best-live-config-recommendation.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-21-rk3588-npu-best-live-config-recommendation.md) |
| 2026-04-21 | Wrote deployment defaults and model error analysis for the drone-specific model | [2026-04-21-drone-model-deployment-defaults-design.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-21-drone-model-deployment-defaults-design.md), [2026-04-21-drone-model-error-analysis.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-21-drone-model-error-analysis.md) |
| 2026-04-21 to 2026-04-22 | Built the public-video evaluation layer and later extended it to longer DUT-Anti-UAV sequences | [2026-04-21-public-video-eval-bootstrap-summary.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-21-public-video-eval-bootstrap-summary.md) |
| 2026-04-22 | Updated thesis requirement mapping to reflect current completion and remaining gaps | [2026-04-22-thesis-requirements-progress-mapping.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-22-thesis-requirements-progress-mapping.md) |
| 2026-04-22 | Recorded the recovered drone-model board milestone in reusable weekly-log / thesis wording | [2026-04-22-weekly-log-materials-drone-board-recovery.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-22-weekly-log-materials-drone-board-recovery.md) |

## Must-Read Documents by Use Case

### If the goal is to understand the whole project quickly

1. [Thesis Project/README.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/README.md)
2. [2026-04-22-project-history-master-index.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-22-project-history-master-index.md)
3. [2026-04-22-thesis-requirements-progress-mapping.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-22-thesis-requirements-progress-mapping.md)

### If the goal is to write weekly reports or a stage summary

1. [2026-04-22-weekly-log-materials-drone-board-recovery.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-22-weekly-log-materials-drone-board-recovery.md)
2. [2026-04-21-public-video-eval-bootstrap-summary.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-21-public-video-eval-bootstrap-summary.md)
3. [2026-04-22-thesis-requirements-progress-mapping.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-22-thesis-requirements-progress-mapping.md)

### If the goal is to write the thesis experiments chapter

1. [2026-04-21-rk3588-npu-first-live-results-summary.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-21-rk3588-npu-first-live-results-summary.md)
2. [2026-04-21-rk3588-npu-best-live-config-recommendation.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-21-rk3588-npu-best-live-config-recommendation.md)
3. [2026-04-21-public-video-eval-bootstrap-summary.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-21-public-video-eval-bootstrap-summary.md)
4. [2026-04-21-drone-model-error-analysis.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-21-drone-model-error-analysis.md)

### If the goal is to resume model / deployment debugging

1. [2026-04-21-public-video-eval-bootstrap-summary.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-21-public-video-eval-bootstrap-summary.md)
2. [2026-04-21-drone-model-deployment-defaults-design.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-21-drone-model-deployment-defaults-design.md)
3. [2026-04-21-drone-model-error-analysis.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-21-drone-model-error-analysis.md)

## Core Code Entrypoints

### Real-time path

- [rk_yolo_live_rtsp/src/main.cpp](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_live_rtsp/src/main.cpp)
- [rk_yolo_live_rtsp/README.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_live_rtsp/README.md)

### Offline board/video validation path

- [rk_yolo_video/src/main.cpp](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_video/src/main.cpp)
- [rk_yolo_video/src/yolo_rknn.cpp](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_video/src/yolo_rknn.cpp)
- [rk_yolo_video/include/yolo_rknn.h](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_video/include/yolo_rknn.h)
- [rk_yolo_video/README.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_video/README.md)

### Training / evaluation path

- [training/drone_yolov10/train_drone_yolov10.py](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/training/drone_yolov10/train_drone_yolov10.py)
- [training/drone_yolov10/evaluate_public_videos.py](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/training/drone_yolov10/evaluate_public_videos.py)
- [training/drone_yolov10/analyze_drone_predictions.py](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/training/drone_yolov10/analyze_drone_predictions.py)

### Dataset / video import path

- [datasets/drone_single_class/scripts/import_kaggle_drone_object_detection.py](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/datasets/drone_single_class/scripts/import_kaggle_drone_object_detection.py)
- [datasets/drone_single_class/scripts/import_public_uav_videos.py](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/datasets/drone_single_class/scripts/import_public_uav_videos.py)
- [datasets/drone_single_class/scripts/validate_yolo_dataset.py](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/datasets/drone_single_class/scripts/validate_yolo_dataset.py)
- [datasets/drone_single_class/manifests/public_video_eval_manifest.json](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/datasets/drone_single_class/manifests/public_video_eval_manifest.json)

## Core Model Artifacts

### Training outputs

- [best.pt](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/training_runs/drone_gpu_50e/weights/best.pt)
- [best.onnx](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/training_runs/drone_gpu_50e/weights/best.onnx)
- [best.end2end_false.op12.onnx](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.onnx)

### RKNN deployment outputs

- [best.rk3588.fp.rknn](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/training_runs/drone_gpu_50e/weights/best.rk3588.fp.rknn)
  - older drone-specific RKNN path that later proved problematic on board
- [best.end2end_false.op12.rk3588.fp.v220.rknn](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn)
  - recovered board-side drone-specific RKNN path currently used for stable offline validation
- [yolov10n.rknn](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/yolov10n.rknn)
  - earlier generic model used to bring up the RK3588 chain

## Core Experiment and Result Artifacts

### NPU live experiment metrics and figures

- [first_round_live_metrics.csv](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_live_rtsp/artifacts/experiments/first_round_live_metrics.csv)
- [fig_first_round_live_perf_compare.svg](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_live_rtsp/artifacts/experiments/fig_first_round_live_perf_compare.svg)
- [policy_sweep_live_metrics.csv](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_live_rtsp/artifacts/experiments/policy_sweep_live_metrics.csv)
- [fig_policy_sweep_live_compare.svg](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_live_rtsp/artifacts/experiments/fig_policy_sweep_live_compare.svg)

### Live/fixed-input screenshots

- [fig_exp03L_multictx_result.png](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_live_rtsp/artifacts/experiments/fig_exp03L_multictx_result.png)
- [fig_exp04L_policy_result.png](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_live_rtsp/artifacts/experiments/fig_exp04L_policy_result.png)

### Board-side public-video outputs

- [anti_uav_fig1_base_eval.mp4](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos/rk3588_board/anti_uav_fig1_base_eval.mp4)
- [anti_uav_fig1_drone_eval_v3.mp4](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos/rk3588_board/anti_uav_fig1_drone_eval_v3.mp4)
- [anti_uav_fig2_drone_eval_v1.mp4](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos/rk3588_board/anti_uav_fig2_drone_eval_v1.mp4)
- [video01_drone_eval_v1.mp4](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos/rk3588_board/video01_drone_eval_v1.mp4)

Supporting structured outputs:

- corresponding `.csv` and `.roi.jsonl` files in [eval_runs/public_videos/rk3588_board](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos/rk3588_board)

### PC/WSL public-video screening outputs

- [eval_runs/public_videos](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos)
- [eval_runs/public_videos_conf035](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos_conf035)
- [eval_runs/public_videos_dut_screen_conf035](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos_dut_screen_conf035)

## Best Current “Showcase” Assets

If someone asks, “What should I open first to see a meaningful result?”, use these:

1. [video01_drone_eval_v1.mp4](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos/rk3588_board/video01_drone_eval_v1.mp4)
   - best current longer public-video board-side drone result
2. [anti_uav_fig1_drone_eval_v3.mp4](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/eval_runs/public_videos/rk3588_board/anti_uav_fig1_drone_eval_v3.mp4)
   - first recovered board-side drone success case
3. [fig_first_round_live_perf_compare.svg](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/rk_yolo_live_rtsp/artifacts/experiments/fig_first_round_live_perf_compare.svg)
   - best current throughput-latency comparison figure

## What Is Already Well Organized

The following are already organized well enough for later thesis writing:

- the main design and experiment notes
- the first-round NPU experiment summaries
- the public-video evaluation path
- the recovered board-side drone-model milestone
- the requirement-to-progress mapping
- the main board-side videos and structured outputs

## What Is Still Not Fully Consolidated

The project is now in good shape, but not every minor trace has been consolidated into polished notes yet. The main missing “nice to have” layer is:

- a fully unified thesis figure inventory
- a fully unified weekly-log timeline covering every single intermediate failed attempt
- a polished single-table summary that maps every experiment ID to every produced file

These are documentation-quality gaps, not technical-progress gaps. The key technical materials are already preserved.

## Practical Recommendation

For any later writing task, start from this order:

1. [2026-04-22-project-history-master-index.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-22-project-history-master-index.md)
2. [2026-04-22-thesis-requirements-progress-mapping.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-22-thesis-requirements-progress-mapping.md)
3. [2026-04-22-weekly-log-materials-drone-board-recovery.md](C:/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis%20Project/docs/superpowers/specs/2026-04-22-weekly-log-materials-drone-board-recovery.md)
4. then the specific experiment summary or artifact file needed for that section
