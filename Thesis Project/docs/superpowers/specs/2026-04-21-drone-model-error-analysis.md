# Drone Model Error Analysis

Date: 2026-04-21

Scope:
- model: `training_runs/drone_gpu_50e/weights/best.pt`
- dataset split: `datasets/drone_single_class/test`
- runtime: WSL CPU analysis

## Summary

The first single-class drone detector is usable, but its default confidence threshold matters a lot.

At `conf=0.25`, the model leans toward high recall and produces too many duplicate or low-confidence false positives.
At `conf=0.35`, the balance is noticeably better and should be the first deployment candidate for RK3588 validation.

## Threshold Sweep

| Confidence | TP | FP | FN | Precision | Recall | F1 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 0.25 | 286 | 146 | 34 | 0.6620 | 0.8938 | 0.7606 |
| 0.30 | 276 | 118 | 44 | 0.7005 | 0.8625 | 0.7731 |
| 0.35 | 269 | 75 | 51 | 0.7820 | 0.8406 | 0.8102 |
| 0.45 | 239 | 41 | 81 | 0.8536 | 0.7469 | 0.7967 |

Recommendation:
- start board-side validation at `conf=0.35`
- if the live stream still shows too many duplicate boxes, try `0.40` next
- if recall becomes too weak for small distant drones, fall back to `0.30`

## What The Errors Look Like

The main error modes are:

1. Duplicate boxes on the same drone
- several hard examples contain two or three predictions around one object
- this inflates false positives even when localization is already good

2. Sparse false positives on negative frames
- some negative test frames still produce medium-confidence predictions
- this is the biggest reason precision drops at `conf=0.25`

3. Misses on small secondary drones
- in multi-object frames, the model often finds the clearer drone but misses a smaller or weaker one

## Key Observations

- test images analyzed: `401`
- ground-truth boxes: `320`
- matched box IoU stays healthy at roughly `0.79`
- this means localization quality is not the main problem
- the bigger issue is objectness/confidence calibration on small targets and duplicate detections

## Useful Artifacts

- baseline analysis:
  - `training_runs/analysis/drone_gpu_50e_test_analysis_cpu/summary.json`
  - `training_runs/analysis/drone_gpu_50e_test_analysis_cpu/per_image.csv`
- confidence sweep:
  - `training_runs/analysis/drone_gpu_50e_test_analysis_conf030/summary.json`
  - `training_runs/analysis/drone_gpu_50e_test_analysis_conf035/summary.json`
  - `training_runs/analysis/drone_gpu_50e_test_analysis_conf045/summary.json`

## Next Steps

1. Use `conf=0.35` as the first RK3588 deployment default for the new drone model.
2. When the board is available again, compare live results at `0.30`, `0.35`, and `0.40`.
3. Add more hard negative samples and multi-drone scenes to the training set.
4. If duplicate boxes remain strong on board, inspect NMS behavior during RKNN post-processing before retraining again.
