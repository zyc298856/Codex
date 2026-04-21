# Drone Model Deployment Defaults Design

Date: 2026-04-21

## Goal

Define the safest first deployment path for the newly trained single-class `drone` model when the RK3588 board is available again.

This design does not change any current runtime defaults in code.
It only standardizes:

- which tool should validate the new model first
- which threshold pair should be used first
- what to look for during board-side verification
- how to fall back if live results differ from the offline analysis

## Recommendation

Use `rk_yolo_video` as the first board-side validation entrypoint for the new drone model.

Reasoning:

- it is already the least risky validation path in this project
- it isolates model behavior from live USB capture, tracking, RTSP, and camera-side variability
- it makes it easier to compare board output against the existing WSL error analysis

Only after `rk_yolo_video` looks sane should the model be moved into `rk_yolo_live_rtsp`.

## First Deployment Defaults

Recommended starting values for the new drone model:

- model: `training_runs/drone_gpu_50e/weights/best.rk3588.fp.rknn`
- entrypoint: `rk_yolo_video`
- score threshold: `0.35`
- NMS threshold: `0.45`
- input size: keep the current `640`

Why `0.35`:

- `0.25` keeps recall high but produces too many low-confidence or duplicate false positives
- `0.35` gives the best F1 result in the current offline sweep
- `0.45` is cleaner still, but it starts dropping too many valid drone detections

Current offline reference:

| Confidence | Precision | Recall | F1 |
| --- | ---: | ---: | ---: |
| 0.25 | 0.6620 | 0.8938 | 0.7606 |
| 0.30 | 0.7005 | 0.8625 | 0.7731 |
| 0.35 | 0.7820 | 0.8406 | 0.8102 |
| 0.45 | 0.8536 | 0.7469 | 0.7967 |

## Validation Order

Recommended order:

1. Run `rk_yolo_video` with the new drone RKNN model on a known video.
2. Check whether detections are visually plausible.
3. Check whether duplicate boxes remain a major issue.
4. Compare output CSV against expected confidence behavior from offline analysis.
5. Only then wire the same model into `rk_yolo_live_rtsp`.

This keeps the known-good validation ladder:

- offline video first
- live RTSP second
- main encoder integration later

## Board-Side Acceptance Checklist

The first board-side run should answer these questions:

1. Does the new RKNN model load and run without runtime instability?
2. Do obvious drone targets get boxes at `score=0.35`, `nms=0.45`?
3. Are there many duplicate boxes around a single drone?
4. Do negative or empty frames still produce medium-confidence drone detections?
5. Does board output qualitatively match the PC-side error analysis trend?

If the answer to 1 is no:
- suspect RKNN conversion/runtime compatibility before touching thresholds

If the answer to 2 is no:
- suspect threshold or post-processing mismatch before retraining

If the answer to 3 is yes:
- inspect NMS and decoded RKNN outputs before blaming the dataset

If the answer to 4 is yes:
- raise `score` toward `0.40`

## Fallback Rules

Start here:

- `score=0.35`
- `nms=0.45`

Then adjust in this order:

1. If recall is too weak for small distant drones, lower score to `0.30`.
2. If false positives are still distracting, raise score to `0.40`.
3. If duplicates remain strong, inspect post-processing and NMS before any retraining.
4. If board behavior is very different from WSL offline analysis, verify RKNN output layout and decode logic before changing the model.

## Non-Goals

This design intentionally does not:

- change the current default thresholds in code
- replace the existing general-purpose COCO model in the live demo
- merge the new drone model into the encoder path yet
- assume the board-side RKNN result will exactly match PyTorch output

## Artifacts

Related files:

- `training_runs/drone_gpu_50e/weights/best.pt`
- `training_runs/drone_gpu_50e/weights/best.onnx`
- `training_runs/drone_gpu_50e/weights/best.rk3588.fp.rknn`
- `training_runs/analysis/drone_gpu_50e_test_analysis_cpu/summary.json`
- `training_runs/analysis/drone_gpu_50e_test_analysis_conf030/summary.json`
- `training_runs/analysis/drone_gpu_50e_test_analysis_conf035/summary.json`
- `training_runs/analysis/drone_gpu_50e_test_analysis_conf045/summary.json`
- `docs/superpowers/specs/2026-04-21-drone-model-error-analysis.md`
