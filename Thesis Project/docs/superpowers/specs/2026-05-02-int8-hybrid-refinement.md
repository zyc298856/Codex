# 2026-05-02 INT8 hybrid refinement on RK3588

## Purpose

This record follows the task-book INT8 optimization line. Earlier full-INT8 RKNN models were runnable on RK3588 but produced zero detections on the fixed public UAV clip. The goal of this follow-up was to test whether a narrower hybrid quantization range could preserve detections while keeping the model closer to an INT8 deployment than the previous broad detection-head hybrid.

## Models

Input ONNX:

- `training_runs/drone_gpu_50e/weights/best.end2end_false.op12.onnx`

Calibration list:

- `training_runs/drone_gpu_50e/calibration/drone_calib_500_with_public_targets_wsl_nospace.txt`

New RKNN candidates:

- `best.end2end_false.op12.rk3588.int8.hybrid_head500.v220.rknn`
- `best.end2end_false.op12.rk3588.int8.hybrid_tail500.v220.rknn`

The `hybrid_head500` model uses the same manually protected detection-head ranges as the previous `hybrid_head230`, but with the stronger 500-image calibration list:

- `/model.16/cv2/act/Mul_output_0` to `output0`
- `/model.19/cv2/act/Mul_output_0` to `output0`
- `/model.22/cv2/act/Mul_output_0` to `output0`

The `hybrid_tail500` model protects only the optimized detect-tail tensors:

- `/model.23/Concat_output_0` to `output0`
- `/model.23/Concat_1_output_0-rs` to `output0`

An initial tail-only attempt using the original ONNX tensor name `/model.23/Concat_1_output_0` failed because RKNN Toolkit2 rewrote this tensor during graph optimization. The corrected optimized name `/model.23/Concat_1_output_0-rs` built successfully.

## Board validation

Board:

- RK3588 Ubuntu board at `192.168.2.156`

Fixed video:

- `/home/ubuntu/public_videos/anti_uav_fig1.mp4`

Board output directory:

- `/home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_video/artifacts/int8_hybrid_refine_20260502_fig1`

Local summary copy:

- `eval_runs/int8_rga/rk3588_board_20260502_int8_hybrid_refine_fig1/taskbook_int8_sweep_summary.csv`

The board-side command reused `rk_yolo_video/scripts/run_taskbook_int8_sweep.sh` and swept confidence thresholds `0.05`, `0.10`, `0.20`, and `0.35`.

## Results

| Model | Conf | Frames with detections | Total detections | Mean RKNN run ms | Mean total work ms |
|---|---:|---:|---:|---:|---:|
| FP baseline | 0.05 | 102 | 220 | 49.792 | 146.878 |
| Full INT8 | 0.05 | 0 | 0 | 30.424 | 73.265 |
| Hybrid head230 | 0.05 | 98 | 191 | 33.394 | 79.120 |
| Hybrid head500 | 0.05 | 96 | 189 | 31.854 | 73.561 |
| Hybrid tail500 | 0.05 | 94 | 185 | 30.791 | 76.657 |
| FP baseline | 0.35 | 30 | 30 | 49.570 | 146.263 |
| Full INT8 | 0.35 | 0 | 0 | 30.309 | 72.281 |
| Hybrid head230 | 0.35 | 23 | 23 | 31.803 | 73.311 |
| Hybrid head500 | 0.35 | 23 | 24 | 32.850 | 76.109 |
| Hybrid tail500 | 0.35 | 23 | 24 | 30.218 | 73.197 |

## Interpretation

Full INT8 is still not a usable replacement for the FP RKNN model because it produced zero detections at all tested thresholds. This confirms the earlier finding that the failure is caused by confidence/output-head quantization collapse rather than only by a high score threshold.

The new `hybrid_head500` model did not materially improve detection stability over the existing `hybrid_head230` model. Its detection count is similar, and its timing is also in the same range.

The new `hybrid_tail500` model is the most useful result in this follow-up. It protects a much narrower tail section than `hybrid_head230`, has a smaller file size close to the full-INT8 model, and still restores detections. At the default-like threshold `0.35`, it produced `23` frames with detections and `24` total detections, while mean RKNN run time was `30.218 ms`. This is the closest current compromise to the task-book INT8 goal: most of the model remains quantized, the output tensor remains usable, and the board-side detector still produces boxes.

## Current recommendation

For formal demonstration, keep the FP RKNN model as the main stable model because it has the strongest detection consistency.

For the INT8 optimization section, report the following hierarchy:

1. Full INT8: technically runnable and faster, but detection-invalid on the fixed UAV clip.
2. Hybrid head230/head500: detection-restoring compromise, but protects a broader region.
3. Hybrid tail500: best current INT8-oriented compromise because it narrows the FP16 protected section while preserving useful detections.

This result is more aligned with the task-book INT8 optimization requirement than the earlier broad hybrid result, but it should still be described as hybrid INT8 rather than full INT8.

## Follow-up: minimal score-tail protection

After `hybrid_tail500` was validated, two narrower hybrid candidates were built to locate the current safe boundary:

- `best.end2end_false.op12.rk3588.int8.hybrid_score500.v220.rknn`
- `best.end2end_false.op12.rk3588.int8.hybrid_sigmoid500.v220.rknn`

`hybrid_score500` protects only:

- `/model.23/Concat_1_output_0-rs` to `output0`

`hybrid_sigmoid500` protects only:

- `/model.23/Sigmoid_output_0_rs` to `output0`

Both models are close in size to the full INT8 model:

- `hybrid_score500`: about `4.38 MB`
- `hybrid_sigmoid500`: about `4.38 MB`
- `hybrid_tail500`: about `4.36 MB`
- `hybrid_head500`: about `5.18 MB`

Board result directory:

- `/home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_video/artifacts/int8_hybrid_minimal_20260502_fig1`

Local summary copy:

- `eval_runs/int8_rga/rk3588_board_20260502_int8_hybrid_minimal_fig1/taskbook_int8_sweep_summary.csv`

Key results:

| Model | Conf | Frames with detections | Total detections | Mean RKNN run ms | Mean total work ms |
|---|---:|---:|---:|---:|---:|
| FP baseline | 0.35 | 30 | 30 | 49.929 | 147.794 |
| Full INT8 | 0.35 | 0 | 0 | 29.422 | 69.543 |
| Hybrid tail500 | 0.35 | 23 | 24 | 29.069 | 68.933 |
| Hybrid score500 | 0.35 | 23 | 31 | 31.230 | 75.040 |
| Hybrid sigmoid500 | 0.35 | 23 | 31 | 30.637 | 74.541 |

At a low threshold of `0.05`, both `score500` and `sigmoid500` produced `94` frames with detections and `235` total detections. This means the final confidence path is sufficient to restore output scores, while most of the detection head and backbone can remain INT8.

## Strict RGA validation with minimal hybrid INT8

To check the complete task-book-style pipeline, `hybrid_sigmoid500` was also tested with strict RGA preprocessing:

```bash
RK_YOLO_PROFILE=1 RK_YOLO_ALARM_OVERLAY=1 \
RK_YOLO_PREPROCESS=rga_cvt_resize RK_YOLO_RGA_LETTERBOX=1 RK_YOLO_REQUIRE_RGA=1 \
./build/rk_yolo_video anti_uav_fig1.mp4 out.mp4 \
  best.end2end_false.op12.rk3588.int8.hybrid_sigmoid500.v220.rknn \
  0.35 0.45 detections.csv roi.jsonl alarm.csv
```

Runtime evidence:

- `preprocess=rga_cvt_resize`
- `rga_letterbox=on`
- `rga_required=on`
- `rga_api version 1.10.1_[10]`
- `frames=130`
- `frames_with_detections=23`
- `total_detections=31`
- `avg_infer_ms=40.19`

Local log copy:

- `eval_runs/int8_rga/rk3588_board_20260502_sigmoid500_strict_rga_fig1/sigmoid500_strict_rga_conf035.log`

Updated conclusion: `hybrid_sigmoid500` is now the closest validated result to the task-book INT8 requirement. It keeps nearly the whole model in INT8, protects only the final confidence/output tail, and still runs together with strict RGA preprocessing on RK3588. The stable demonstration path should still use FP RKNN, but the thesis can now present `hybrid_sigmoid500 + strict RGA` as the strongest optimization evidence chain.
