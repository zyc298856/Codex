# 2026-04-29 INT8 / RGA / zero-copy board validation

## Purpose

This validation records the first RK3588 board-side comparison of the stable FP RKNN model, the newly prepared INT8 RKNN model, RGA preprocessing switches, and zero-copy input switch. The test is intentionally non-invasive: the known working FP model and executable are kept unchanged, and all experimental switches are enabled only through environment variables.

## Board and inputs

- Board: RK3588 Ubuntu, host `focal`, architecture `aarch64`
- Project directory on board: `/home/ubuntu/eclipse-workspace/eclipse-workspace`
- Executable: `/home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_video/build/rk_yolo_video`
- FP model: `/home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn`
- INT8 model: `/home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.int8.v220.rknn`
- Videos:
  - `/home/ubuntu/public_videos/anti_uav_fig2.mp4`
  - `/home/ubuntu/public_videos/anti_uav_fig1.mp4`

## Commands

The board-side one-click script was uploaded and executed:

```bash
tools/int8_rga/run_int8_rga_experiments.sh \
  --video /home/ubuntu/public_videos/anti_uav_fig1.mp4 \
  --fp-model /home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn \
  --int8-model /home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.int8.v220.rknn \
  --binary /home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_video/build/rk_yolo_video \
  --out-dir /home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_video/artifacts/int8_rga_20260429_1940_fig1_full \
  --cases all
```

The script enables profiling through `RK_YOLO_PROFILE=1` and compares:

- FP + OpenCV baseline
- FP + RGA resize / RGA color-convert-resize / RGA letterbox
- FP + zero-copy input
- INT8 + OpenCV baseline
- INT8 + RGA color-convert-resize / RGA letterbox
- INT8 + zero-copy input

## Local result directories

- `Thesis Project/eval_runs/int8_rga/rk3588_board_20260429_1932_full`
- `Thesis Project/eval_runs/int8_rga/rk3588_board_20260429_1940_fig1_full`
- `Thesis Project/eval_runs/int8_rga/rk3588_board_20260429_1945_fig1_low_score`

Each result directory contains output videos, detection CSV files, ROI JSONL files, alarm event CSV files, logs, and `profile_summary.csv`.

## Key results

### Video `anti_uav_fig2.mp4`

This video produced no detections under the current threshold, so it is used as a performance and stability sample.

| Case | Frames | Detections | Input mode | Mean RKNN run ms | Mean total work ms |
|---|---:|---:|---|---:|---:|
| FP OpenCV baseline | 160 | 0 | rknn_inputs_set | 48.342 | 140.894 |
| FP zero-copy | 160 | 0 | zero_copy | 77.609 | 135.160 |
| INT8 OpenCV baseline | 160 | 0 | rknn_inputs_set | 35.212 | 104.012 |
| INT8 zero-copy | 160 | 0 | zero_copy | 35.524 | 102.964 |

### Video `anti_uav_fig1.mp4`

This video is more useful for functional comparison because FP produces detections.

| Case | Frames | Detections | Input mode | Mean RKNN run ms | Mean total work ms |
|---|---:|---:|---|---:|---:|
| FP OpenCV baseline | 130 | 30 | rknn_inputs_set | 50.333 | 147.318 |
| FP RGA resize | 130 | 30 | rknn_inputs_set | 49.629 | 144.311 |
| FP zero-copy | 130 | 30 | zero_copy | 90.222 | 134.802 |
| INT8 OpenCV baseline | 130 | 0 | rknn_inputs_set | 29.445 | 68.419 |
| INT8 zero-copy | 130 | 0 | zero_copy | 29.206 | 67.749 |

### INT8 low-threshold check

To check whether INT8 only reduced confidence scores, the INT8 cases were repeated on `anti_uav_fig1.mp4` with `--score 0.05`.

| Case | Frames | Detections | Mean RKNN run ms | Mean total work ms |
|---|---:|---:|---:|---:|
| INT8 OpenCV baseline, score 0.05 | 130 | 0 | 29.458 | 68.223 |
| INT8 zero-copy, score 0.05 | 130 | 0 | 30.425 | 70.325 |

The low-threshold result still produced zero detections, which indicates that the current INT8 model is not yet a reliable replacement for the FP model. The likely issue is quantization calibration or output distribution mismatch rather than only a high confidence threshold.

## Conclusions

1. The board-side INT8 RKNN model can be loaded and executed successfully, so the INT8 deployment path is technically runnable.
2. INT8 inference is significantly faster in `rknn_run` time, around 29-35 ms in these tests, compared with roughly 48-50 ms for FP.
3. The current INT8 model is not yet functionally reliable because it produced zero detections on a video where the FP model detected 30 targets.
4. RGA preprocessing paths are runnable, but in the current implementation the measured total time does not yet show a decisive improvement over the OpenCV baseline.
5. Zero-copy input reduces input update/copy overhead from tens of milliseconds in the FP normal path to sub-millisecond level, but FP zero-copy increases `rknn_run` time in this test. INT8 zero-copy remains fast overall but does not fix INT8 detection loss.
6. The stable recommendation remains: use the FP RKNN model as the formal demonstration and thesis baseline, while documenting INT8/RGA/zero-copy as explored optimization paths with measured limitations.

## Next steps

- Rebuild INT8 with a stronger and more representative calibration list, especially frames containing small drone targets.
- Compare INT8 output tensors against FP output tensors on the same preprocessed frames to locate whether the issue comes from quantization, preprocessing scale, or post-processing assumptions.
- Keep RGA as an optimization path, but do not replace the stable FP/OpenCV path until it shows repeatable end-to-end benefit.

## Follow-up: public-target calibration attempt

After the first board-side comparison, a stronger calibration list was generated from:

- 30 original frames from `anti_uav_fig1.mp4` where the FP model produced detections.
- 200 images sampled from the existing single-class drone train/val dataset.

Generated assets:

- Target-frame directory: `datasets/drone_single_class/calibration/int8_public_targets_20260429/selected_frames`
- Windows calibration list: `training_runs/drone_gpu_50e/calibration/drone_calib_230_with_public_targets_windows.txt`
- WSL no-space calibration list: `training_runs/drone_gpu_50e/calibration/drone_calib_230_with_public_targets_wsl_nospace.txt`
- Rebuilt INT8 model: `training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.int8.public230.v220.rknn`

The first conversion attempt failed because the calibration list was written with a UTF-8 BOM by PowerShell. RKNN Toolkit2 interpreted the first image path as invalid. Rewriting the list as UTF-8 without BOM fixed the issue, and the new INT8 model was exported successfully.

The rebuilt INT8 model was tested on `anti_uav_fig1.mp4` with the same command matrix:

| Case | Frames | Detections | Input mode | Mean RKNN run ms | Mean total work ms |
|---|---:|---:|---|---:|---:|
| INT8 public230 OpenCV baseline | 130 | 0 | rknn_inputs_set | 29.902 | 67.978 |
| INT8 public230 RGA cvt-resize | 130 | 0 | rknn_inputs_set | 30.496 | 74.835 |
| INT8 public230 RGA letterbox | 130 | 0 | rknn_inputs_set | 29.991 | 72.576 |
| INT8 public230 zero-copy | 130 | 0 | zero_copy | 29.860 | 69.149 |

Debugging with `RK_YOLO_DEBUG_LAYOUT=1` showed that the output tensor shape remained `[1, 5, 8400]`, but the fifth channel, which represents the single-class confidence score in the current post-processing logic, was still all zero for the INT8 model. Therefore, the current INT8 failure is not fixed by adding public target frames to calibration. The evidence points to confidence-channel collapse or quantization/post-processing incompatibility rather than a simple threshold issue.

Updated conclusion: the INT8 path is runnable and fast, but it is not yet functionally valid for the thesis demonstration. The FP RKNN model remains the stable model for all formal demonstration and result reporting.

## Follow-up: hybrid INT8 detection-head protection

Because both the original INT8 model and the public-target-calibrated INT8 model collapsed the single-class confidence channel to zero, a hybrid quantization attempt was performed next.

Two conversion paths were added to `tools/int8_rga/convert_onnx_to_rknn.py`:

- `--auto-hybrid`: uses RKNN Toolkit2 automatic hybrid quantization.
- `--custom-hybrid-pair START,END`: manually specifies hybrid quantization subgraph ranges. This form is easier to pass through PowerShell and WSL than a nested Python list string.

### Auto-hybrid result

Model:

- `training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.int8.autohybrid230.v220.rknn`

Board result on `anti_uav_fig1.mp4`:

| Case | Frames | Detections | Input mode | Mean RKNN run ms | Mean total work ms |
|---|---:|---:|---|---:|---:|
| INT8 auto-hybrid OpenCV baseline | 130 | 0 | rknn_inputs_set | 30.161 | 69.763 |
| INT8 auto-hybrid RGA cvt-resize | 130 | 0 | rknn_inputs_set | 30.575 | 72.864 |
| INT8 auto-hybrid RGA letterbox | 130 | 0 | rknn_inputs_set | 30.745 | 75.661 |
| INT8 auto-hybrid zero-copy | 130 | 0 | zero_copy | 30.866 | 70.999 |

Auto-hybrid did not restore detections. RKNN logs still reported `output0` as INT8, so the output confidence channel remained unreliable.

### Manual hybrid-head result

Model:

- `training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.int8.hybrid_head230.v220.rknn`

Manual hybrid ranges:

- `/model.16/cv2/act/Mul_output_0` to `output0`
- `/model.19/cv2/act/Mul_output_0` to `output0`
- `/model.22/cv2/act/Mul_output_0` to `output0`

RKNN conversion logs showed that `output0` changed from INT8 to float16. This is important because it protects the YOLO detection head and output confidence from full INT8 quantization.

Board result on `anti_uav_fig1.mp4`:

| Case | Frames | Detections | Input mode | Mean RKNN run ms | Mean total work ms |
|---|---:|---:|---|---:|---:|
| INT8 hybrid-head OpenCV baseline | 130 | 23 | rknn_inputs_set | 33.054 | 77.461 |
| INT8 hybrid-head RGA cvt-resize | 130 | 23 | rknn_inputs_set | 33.364 | 80.379 |
| INT8 hybrid-head RGA letterbox | 130 | 23 | rknn_inputs_set | 33.182 | 81.073 |
| INT8 hybrid-head zero-copy | 130 | 23 | zero_copy | 33.319 | 75.629 |

This is the first INT8-family model that restored detections on the public UAV validation video. It is still less sensitive than the FP model, which produced 30 detections on the same clip, but it is now functionally runnable rather than only performance-runnable.

Updated conclusion: the stable demonstration model remains FP RKNN, but the INT8 optimization path now has a validated hybrid quantization result. The most defensible thesis wording is that full INT8 caused confidence-channel collapse, while manual hybrid quantization of the detection head restored partial detection capability and preserved a lower `rknn_run` time than FP.

## Follow-up: RGA staged pipeline optimization

To move the implementation closer to the task-book pipeline of `video capture -> RGA preprocessing -> NPU inference -> post-processing`, the fixed-video program was extended with an optional staged pipeline.

New runtime controls:

- `RK_YOLO_PIPELINE=1`: enables bounded-queue video pipeline mode.
- `RK_YOLO_PIPELINE_STAGED=1`: splits preprocessing and inference into separate worker stages.
- `RK_YOLO_PIPELINE_QUEUE=<N>`: controls queue depth, default `4`.
- `RK_YOLO_PREPROCESS=rga_cvt_resize`: enables the RGA color-conversion and resize preprocessing path when librga is available.

Implementation notes:

- The original serial path remains the default and is unchanged for stable demonstration.
- `YoloRknnDetector::PrepareFrame` performs frame preprocessing and records `prepare_ms`.
- `YoloRknnDetector::InferPrepared` consumes the prepared input tensor and performs RKNN input update, NPU run, output fetch, decode and output release.
- With `RK_YOLO_PIPELINE=1 RK_YOLO_PIPELINE_STAGED=1`, the runtime structure becomes capture thread -> preprocess thread -> inference/postprocess thread -> render/output thread.

Board-side baseline before the staged split was compiled and tested on `anti_uav_fig1.mp4`:

| Case | Frames | Detections | Pipeline | Preprocess | Mean total work ms |
|---|---:|---:|---|---|---:|
| FP serial OpenCV | 130 | 30 | off | OpenCV | 99.34 |
| FP pipeline RGA cvt-resize | 130 | 30 | on | RGA cvt-resize | 111.49 |

The RGA path preserved detection correctness in this video, but the first end-to-end timing comparison did not yet show a speed gain. This is useful evidence for the thesis: RGA integration is functionally available, while performance benefit depends on whether preprocessing is truly decoupled from NPU inference and whether the video writer/render path becomes the bottleneck.

The staged interface and optional staged pipeline were then compiled on RK3588 successfully. The board build detected librga and linked successfully.

Board result directory:

- Board: `rk_yolo_video/artifacts/rga_staged_20260429_fig1`
- Local copy: `eval_runs/int8_rga/rk3588_board_20260429_rga_staged_fig1`

Board validation on `anti_uav_fig1.mp4`:

| Case | Frames | Detections | Alarm events | Mean prepare ms | Mean input/update ms | Mean RKNN run ms | Mean render ms | Mean profile total ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| FP serial OpenCV | 130 | 30 | 14 | 4.859 | 46.461 | 50.003 | 39.678 | 145.827 |
| FP pipeline OpenCV | 130 | 30 | 14 | 7.781 | 72.529 | 49.814 | 38.823 | 174.285 |
| FP pipeline RGA cvt-resize | 130 | 30 | 14 | 5.634 | 49.682 | 47.199 | 39.713 | 146.778 |
| FP staged pipeline RGA cvt-resize | 130 | 30 | 14 | 3.975 | 81.831 | 50.427 | 41.360 | 183.303 |
| FP staged pipeline RGA cvt-resize + zero-copy | 130 | 30 | 14 | 3.559 | 0.735 | 124.550 | 38.022 | 172.175 |

Interpretation:

- Functional goal passed: the explicit staged path `capture -> preprocess -> inference/postprocess -> render` runs on RK3588 and preserves the same detections and alarm events as the stable serial baseline.
- RGA preprocessing itself reduced the measured `prepare_ms` in staged mode, which supports the claim that preprocessing can be offloaded or isolated.
- End-to-end speed did not improve in this test. In staged RGA without zero-copy, `rknn_inputs_set` became the dominant cost. In staged RGA with zero-copy, input update cost dropped, but `rknn_run` increased substantially.
- The safest thesis conclusion is that RGA staged pipeline integration has been implemented and validated functionally, while the current measured bottleneck shifts to input synchronization and RKNN runtime scheduling. Therefore, FP serial/OpenCV or non-staged RGA remains the safer demonstration path, and staged RGA is best reported as an optimization exploration rather than the final recommended runtime configuration.

## Follow-up: strict task-book RGA validation mode

To make the implementation closer to the task-book requirement, a strict RGA validation mode was added:

- `RK_YOLO_REQUIRE_RGA=1`

When this flag is enabled, the program no longer silently falls back to OpenCV if RGA is requested but unavailable or if the selected RGA operation fails. This makes the experiment more defensible because a successful run proves that the RGA path was actually used.

A board-side reproducibility script was also added:

- `rk_yolo_video/scripts/run_taskbook_pipeline_eval.sh`

The script runs the strict task-book pipeline:

`video capture -> strict RGA preprocessing -> NPU inference -> post-processing/render`

Strict board validation command:

```bash
bash scripts/run_taskbook_pipeline_eval.sh \
  /home/ubuntu/public_videos/anti_uav_fig1.mp4 \
  /home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn \
  /home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_video/artifacts/taskbook_strict_rga_20260429_fig1
```

Important runtime evidence from the log:

- `preprocess=rga_cvt_resize`
- `rga_required=on`
- `pipeline=on`
- `staged_pipeline=on`
- `rga_api version 1.10.1_[10]`

Strict board validation result:

| Case | Frames | Detections | Alarm events | Mean prepare ms | Mean input/update ms | Mean RKNN run ms | Mean render ms | Mean profile total ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| taskbook strict RGA staged | 130 | 30 | 14 | 4.727 | 82.021 | 50.894 | 42.618 | 186.207 |

Local evidence directory:

- `eval_runs/int8_rga/rk3588_board_20260429_taskbook_strict_rga_fig1`

Updated conclusion: the task-book RGA pipeline is now not only implemented as an optional path, but also has a strict validation mode and reproducible board-side script. For final demonstration, the stable FP path remains preferred; for task-book compliance and thesis evidence, the strict RGA staged run should be cited as the hardware preprocessing validation experiment.

## Follow-up: strict full-RGA preprocessing validation

To further align with the task-book wording about RGA hardware preprocessing, the strict validation path was upgraded from `RGA cvt-resize` to a fuller RGA preprocessing path:

`video capture -> RGA color conversion + resize -> RGA letterbox canvas construction -> NPU inference -> post-processing/render`

Runtime switches:

- `RK_YOLO_PREPROCESS=rga_cvt_resize`
- `RK_YOLO_RGA_LETTERBOX=1`
- `RK_YOLO_REQUIRE_RGA=1`
- `RK_YOLO_PIPELINE=1`
- `RK_YOLO_PIPELINE_STAGED=1`

Important runtime evidence from the board log:

- `preprocess=rga_cvt_resize`
- `rga_letterbox=on`
- `rga_required=on`
- `pipeline=on`
- `staged_pipeline=on`
- `rga_api version 1.10.1_[10]`

Strict full-RGA validation result on `anti_uav_fig1.mp4`:

| Case | Frames | Detections | Alarm events | Mean prepare ms | Mean input/update ms | Mean RKNN run ms | Mean render ms | Mean profile total ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| taskbook full-RGA staged | 130 | 30 | 14 | 9.002 | 81.936 | 51.328 | 41.889 | 190.133 |

Local evidence directory:

- `eval_runs/int8_rga/rk3588_board_20260429_taskbook_full_rga_fig1`

The task-book reproducibility script was updated so its primary case now uses strict full-RGA mode:

- `rk_yolo_video/scripts/run_taskbook_pipeline_eval.sh`

Updated conclusion: the RGA line now satisfies the implementation and validation requirement more strongly than the earlier partial RGA path. It proves that RGA participates in color conversion, resizing, and letterbox input construction, with no OpenCV fallback allowed in the strict run. The measured performance is not the best among all configurations, but the functional and reproducibility evidence is sufficient to support the thesis claim that an RGA hardware-preprocessing pipeline was implemented and validated on RK3588.

## Claude audit follow-up: S-2 + S-3

After the RGA preprocessing audit, two follow-up items were completed:

- **S-2 thesis wording update**: section 5.8 of `paper/full_thesis_latest_merged.docx` now explicitly records the `taskbook full-RGA strict` validation case, including the key switches `RK_YOLO_PIPELINE=1`, `RK_YOLO_PIPELINE_STAGED=1`, `RK_YOLO_PREPROCESS=rga_cvt_resize`, `RK_YOLO_RGA_LETTERBOX=1`, and `RK_YOLO_REQUIRE_RGA=1`. The wording also clarifies that this case is used as task-book compliance and hardware-preprocessing evidence, not as the final fastest runtime recommendation.
- **S-3 runtime log evidence**: `PrepareLetterboxWithRga()` now prints a one-time success message when the RGA letterbox path is actually used: `RGA letterbox used: OpenCV cvtColor/resize/copyMakeBorder skipped`.

Board-side S-3 validation result:

- Build status: passed with `librga found`.
- Runtime switches: `preprocess=rga_cvt_resize`, `rga_letterbox=on`, `rga_required=on`, `pipeline=on`, `staged_pipeline=on`.
- New evidence line: `RGA letterbox used: OpenCV cvtColor/resize/copyMakeBorder skipped`.
- Fixed-video result: `frames=130`, `frames_with_detections=30`, `total_detections=30`, `alarm_events=14`, `avg_infer_ms=132.92`.

Local S-3 evidence files:

- `eval_runs/int8_rga/rk3588_board_20260429_taskbook_full_rga_fig1/full_rga_s3.log`
- `eval_runs/int8_rga/rk3588_board_20260429_taskbook_full_rga_fig1/full_rga_s3_detections.csv`
- `eval_runs/int8_rga/rk3588_board_20260429_taskbook_full_rga_fig1/full_rga_s3_alarm.csv`
- `eval_runs/int8_rga/rk3588_board_20260429_taskbook_full_rga_fig1/full_rga_s3_roi.jsonl`

## Follow-up: task-book INT8 validation

To make the INT8 line closer to the task-book requirement, a reproducible board-side INT8 comparison script was added:

- `rk_yolo_video/scripts/run_taskbook_int8_eval.sh`

The script compares three fixed-video cases under the same input video, score threshold, NMS threshold, and profiling mode:

1. `fp_baseline`: stable FP RKNN model.
2. `int8_full`: fully quantized INT8 RKNN model.
3. `int8_hybrid_head230`: manual hybrid INT8 RKNN model that keeps the sensitive output-head range in higher precision.

Board-side command used for validation:

```bash
cd /home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_video
scripts/run_taskbook_int8_eval.sh \
  /home/ubuntu/public_videos/anti_uav_fig1.mp4 \
  /home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn \
  /home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.int8.v220.rknn \
  /home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.int8.hybrid_head230.v220.rknn \
  /home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_video/artifacts/taskbook_int8_script_20260429_fig1
```

Fixed-video validation result on `anti_uav_fig1.mp4`:

| Case | Frames | Frames with detections | Total detections | Alarm events | Avg infer ms | Mean input/update ms | Mean RKNN run ms | Mean total work ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| FP baseline | 130 | 30 | 30 | 14 | 103.82 | 43.921 | 49.001 | 139.969 |
| Full INT8 | 130 | 0 | 0 | 0 | 38.93 | 0.440 | 29.922 | 68.740 |
| Hybrid INT8 head230 | 130 | 23 | 23 | 16 | 43.29 | 0.442 | 33.194 | 77.250 |

Interpretation:

- Full INT8 shows the expected runtime benefit, but the detection confidence collapses on this fixed video, producing zero detections. It should not be promoted as the stable deployment path.
- Manual hybrid INT8 restores most of the FP detection behavior in this short validation clip: 23 detection frames versus 30 for FP, while keeping a much lower average inference time than FP.
- The current best thesis wording is that INT8 quantization conversion, calibration, and board-side validation have been completed, and that manual hybrid quantization is an effective mitigation for the observed full-INT8 accuracy collapse. However, FP RKNN remains the final stable demonstration path because it preserves the best detection consistency.

Local INT8 evidence directory:

- `eval_runs/int8_rga/rk3588_board_20260429_taskbook_int8_fig1`

Key evidence files:

- `taskbook_int8_summary.csv`
- `fp_baseline.log`
- `int8_full.log`
- `int8_hybrid_head230.log`
- corresponding detection, ROI, and alarm CSV/JSONL files for all three cases

## Offline follow-up: stronger INT8 calibration and threshold sweep preparation

When the board is not available, the INT8 line can still be improved without touching the stable FP/RGA runtime path. Two offline improvements were prepared:

1. Calibration-list generation now supports pinned images through `--pinned-image-dir`. This ensures that difficult public-video drone frames are always included in the calibration set rather than being lost during random sampling.
2. A board-side threshold sweep script was added: `rk_yolo_video/scripts/run_taskbook_int8_sweep.sh`.

New calibration lists prepared locally:

| Calibration list | Images | Purpose |
|---|---:|---|
| `drone_calib_500_with_public_targets_windows.txt` | 500 | Windows-path medium calibration set with pinned public target frames |
| `drone_calib_500_with_public_targets_wsl_nospace.txt` | 500 | WSL `/tmp/thesis_project` medium calibration set for RKNN Toolkit2 |
| `drone_calib_1000_with_public_targets_windows.txt` | 1000 | Windows-path larger calibration set with pinned public target frames |
| `drone_calib_1000_with_public_targets_wsl_nospace.txt` | 1000 | WSL `/tmp/thesis_project` larger calibration set for RKNN Toolkit2 |

The first 30 entries in these lists are the selected public-video drone target frames from:

- `datasets/drone_single_class/calibration/int8_public_targets_20260429/selected_frames`

The next board-side validation should compare:

- Existing FP baseline
- Existing full INT8
- Existing hybrid INT8
- Newly converted INT8 model from the 500-image pinned calibration set
- Newly converted INT8 model from the 1000-image pinned calibration set

The threshold sweep should use:

- `RK_YOLO_INT8_SWEEP_THRESHOLDS="0.05 0.10 0.20 0.35"`
- `RK_YOLO_INT8_SWEEP_NMS=0.45`

Expected interpretation:

- If full INT8 remains at zero detections even at low confidence thresholds, the issue is likely deeper quantization sensitivity in the output head or activation distribution.
- If full INT8 recovers detections at lower thresholds, the main issue is confidence-scale shift after quantization, and the thesis can report threshold retuning as part of INT8 deployment.
- If larger pinned calibration lists improve full INT8 consistency, the task-book INT8 requirement is better supported by showing a concrete calibration-quality improvement path.

## 2026-05-01 board follow-up: INT8 threshold sweep

Board network note:

- Location: home
- SSH address used successfully: `ubuntu@192.168.2.156`

The prepared sweep script was synchronized to the board and executed on the fixed public UAV clip:

- Video: `/home/ubuntu/public_videos/anti_uav_fig1.mp4`
- Script: `rk_yolo_video/scripts/run_taskbook_int8_sweep.sh`
- Thresholds: `0.05`, `0.10`, `0.20`, `0.35`
- NMS threshold: `0.45`
- Preprocess path: `opencv`

### Existing INT8 variants

Board output directory:

- `/home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_video/artifacts/taskbook_int8_sweep_20260501_fig1`

Local evidence directory:

- `eval_runs/int8_rga/taskbook_int8_sweep_20260501_fig1`

Key result summary:

| Model | Conf | Frames with detections | Total detections | Avg infer ms |
|---|---:|---:|---:|---:|
| FP baseline | 0.05 | 102 | 220 | 109.01 |
| INT8 full | 0.05 | 0 | 0 | 39.43 |
| INT8 hybrid head230 | 0.05 | 98 | 191 | 43.24 |
| INT8 public230 | 0.05 | 0 | 0 | 39.01 |
| INT8 autohybrid230 | 0.05 | 0 | 0 | 36.67 |
| FP baseline | 0.35 | 30 | 30 | 109.23 |
| INT8 full | 0.35 | 0 | 0 | 40.35 |
| INT8 hybrid head230 | 0.35 | 23 | 23 | 43.85 |
| INT8 public230 | 0.35 | 0 | 0 | 38.15 |
| INT8 autohybrid230 | 0.35 | 0 | 0 | 37.82 |

Conclusion from this sweep:

- Lowering the confidence threshold to `0.05` does not recover the fully quantized INT8 model.
- `public230` and Toolkit auto-hybrid also remain at zero detections on this fixed clip.
- Manual hybrid quantization of the sensitive output-head range remains the only INT8 variant that preserves useful detection behavior.

### New pinned-calibration INT8 variants

Two new full-INT8 models were converted in the WSL RKNN Toolkit2 environment using the pinned public-target calibration lists:

- `best.end2end_false.op12.rk3588.int8.calib500.v220.rknn`
- `best.end2end_false.op12.rk3588.int8.calib1000.v220.rknn`

Board output directory:

- `/home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_video/artifacts/taskbook_int8_sweep_calib500_1000_20260501_fig1`

Local evidence directory:

- `eval_runs/int8_rga/taskbook_int8_sweep_calib500_1000_20260501_fig1`

Key result summary:

| Model | Conf | Frames with detections | Total detections | Avg infer ms |
|---|---:|---:|---:|---:|
| FP baseline | 0.05 | 102 | 220 | 109.17 |
| INT8 full | 0.05 | 0 | 0 | 35.09 |
| INT8 hybrid head230 | 0.05 | 98 | 191 | 42.66 |
| INT8 calib500 | 0.05 | 0 | 0 | 39.31 |
| INT8 calib1000 | 0.05 | 0 | 0 | 38.33 |
| FP baseline | 0.35 | 30 | 30 | 111.63 |
| INT8 full | 0.35 | 0 | 0 | 38.26 |
| INT8 hybrid head230 | 0.35 | 23 | 23 | 43.03 |
| INT8 calib500 | 0.35 | 0 | 0 | 40.04 |
| INT8 calib1000 | 0.35 | 0 | 0 | 39.17 |

Updated conclusion:

- Enlarging the calibration set to 500 or 1000 images and pinning public UAV target frames does not recover full-INT8 detection on this fixed clip.
- This makes the failure mode clearer: the model is not merely affected by insufficient calibration samples or an over-high confidence threshold. The sensitive YOLO output head and activation distribution likely require partial high-precision retention.
- For the thesis and task-book compliance, the strongest defensible statement is: INT8 conversion, calibration-set improvement, multi-threshold board validation, and hybrid quantization mitigation were all completed. Full INT8 gives lower inference latency but loses detection consistency; manual hybrid INT8 gives a practical speed/accuracy compromise, while FP RKNN remains the final stable demonstration path.
